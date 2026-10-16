#include "playerbot/playerbot.h"
#include "MageAoe.h"
#include "MageAoeGeometry.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

using namespace ai;

namespace
{
    using Point = MageAoeGeometry::Point;

    Point GetPoint(const WorldObject* object)
    {
        return { object->GetPositionX(), object->GetPositionY(), object->GetPositionZ() };
    }

    float GetHitRadius(Player* bot, Unit* target, float radius)
    {
        // Match Spell's PUSH_DEST_CENTER and DynamicObjectUpdater: use raw 3D
        // centres, adding only the core's allowed NPC combat-reach extension.
        if (bot->IsControlledByPlayer() && !target->IsControlledByPlayer())
            radius += target->GetCombatReach();
        return radius;
    }

    bool CanHit(Player* bot, const Point& center, Unit* target, float radius, bool ignoreLos)
    {
        return MageAoeGeometry::IsWithinRadius(center, GetPoint(target), GetHitRadius(bot, target, radius)) &&
            (ignoreLos || bot->GetMap()->IsInLineOfSight(center.x, center.y, center.z + 1.0f,
                target->GetPositionX(), target->GetPositionY(),
                target->GetPositionZ() + target->GetCollisionHeight(), false));
    }

    float GetDamageRadius(Player* bot, const SpellEntry* spell)
    {
        float radius = std::numeric_limits<float>::max();
        for (uint32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; ++i)
        {
            if (spell->Effect[i] != SPELL_EFFECT_SCHOOL_DAMAGE &&
                spell->Effect[i] != SPELL_EFFECT_PERSISTENT_AREA_AURA)
                continue;

            float effectRadius = GetSpellRadius(sSpellRadiusStore.LookupEntry(spell->EffectRadiusIndex[i]));
            if (Player* owner = bot->GetSpellModOwner())
                owner->ApplySpellMod(spell->Id, SPELLMOD_RADIUS, effectRadius, false);

            // If effects have different footprints, require the counted group
            // to fit in all damaging parts of the spell.
            if (effectRadius > 0.0f)
                radius = std::min(radius, effectRadius);
        }

        return radius == std::numeric_limits<float>::max() ? 0.0f : radius;
    }

    class OwnFlamestrikeCheck
    {
    public:
        OwnFlamestrikeCheck(Player* bot, float range) : bot(bot), range(range) {}
        WorldObject const& GetFocusObject() const { return *bot; }
        bool operator()(DynamicObject* object) const
        {
            return object && object->GetCasterGuid() == bot->GetObjectGuid() &&
                object->GetDuration() > 0 && object->GetRadius() > 0.0f &&
                sSpellMgr.GetFirstSpellInChain(object->GetSpellId()) == 2120 &&
                bot->IsWithinDistInMap(object, range);
        }

    private:
        Player* bot;
        float range;
    };

    bool AlreadyCovered(Player* bot, const std::vector<Unit*>& targets,
        const std::list<DynamicObject*>& existing)
    {
        size_t covered = 0;
        for (Unit* target : targets)
        {
            for (DynamicObject* object : existing)
            {
                if (CanHit(bot, GetPoint(object), target, object->GetRadius(), false))
                {
                    ++covered;
                    break;
                }
            }
        }

        // Permit a new pack/area, but don't shift the centre slightly just to
        // keep recasting on the same burning group. Other casters do not count.
        return IsMageFlamestrikeCovered(covered, targets.size());
    }
}

bool ai::FindMageAoeTarget(PlayerbotAI* ai, uint32 spellId, MageAoeTarget& result,
    bool avoidExistingFlamestrike)
{
    result = MageAoeTarget();
    Player* bot = ai ? ai->GetBot() : nullptr;
    const SpellEntry* spell = sServerFacade.LookupSpellInfo(spellId);
    if (!bot || bot->getClass() != CLASS_MAGE || !bot->IsAlive() ||
        !bot->IsInWorld() || bot->IsBeingTeleported() || !spell)
        return false;

    float radius = GetDamageRadius(bot, spell);
    float maxRange = GetSpellMaxRange(sSpellRangeStore.LookupEntry(spell->rangeIndex));
    if (Player* owner = bot->GetSpellModOwner())
        owner->ApplySpellMod(spell->Id, SPELLMOD_RANGE, maxRange, false);
    if (radius <= 0.0f || maxRange <= 0.0f)
        return false;

    const bool ignoreLos = spell->HasAttribute(SPELL_ATTR_EX2_IGNORE_LINE_OF_SIGHT);
    AiObjectContext* context = ai->GetAiObjectContext();
    std::vector<Unit*> targets;
    std::vector<Point> points;
    std::set<ObjectGuid> seen;
    float maxHitRadius = radius;
    // Include frozen combat targets: generic 'possible attack targets' can
    // discard Frost Nova roots even though Blizzard is meant to hit them.
    const std::list<ObjectGuid> attackers = context->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (const ObjectGuid& guid : attackers)
    {
        if (!seen.insert(guid).second)
            continue;
        Unit* target = ai->GetUnit(guid);
        if (!target || !ai->IsSafe(target) || !target->IsAlive() ||
            target->HasBreakableByDamageCrowdControlAura() ||
            target->IsAOEImmune() || target->IsTaxiFlying() ||
            (target->IsCreature() && static_cast<Creature*>(target)->IsTotem()) ||
            !bot->CanAttackSpell(target, spell, true) ||
            !PossibleAttackTargetsValue::IsValid(target, bot, maxRange + radius, true) ||
            target->IsImmuneToDamage(GetSpellSchoolMask(spell)))
            continue;

        bool immune = false;
        for (uint32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; ++i)
        {
            if ((spell->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE ||
                spell->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                (target->IsImmuneToSpell(spell, false, uint8(1 << i), bot) ||
                 target->IsImmuneToSpellEffect(spell, SpellEffectIndex(i), false)))
                immune = true;
        }
        if (immune)
            continue;

        targets.push_back(target);
        points.push_back(GetPoint(target));
        maxHitRadius = std::max(maxHitRadius, GetHitRadius(bot, target, radius));
    }

    if (targets.size() < MAGE_GROUND_AOE_MIN_TARGETS)
        return false;

    std::vector<Unit*> protectedTargets;
    const std::list<ObjectGuid> nearby = context->GetValue<std::list<ObjectGuid>>("possible targets no los")->Get();
    for (const ObjectGuid& guid : nearby)
    {
        Unit* target = ai->GetUnit(guid);
        if (target && ai->IsSafe(target) && target->IsAlive() && target->HasBreakableByDamageCrowdControlAura())
            protectedTargets.push_back(target);
    }

    std::list<DynamicObject*> existing;
    if (avoidExistingFlamestrike)
    {
        const float searchRange = maxRange + radius * 2.0f;
        OwnFlamestrikeCheck check(bot, searchRange);
        MaNGOS::DynamicObjectListSearcher<OwnFlamestrikeCheck> searcher(existing, check);
        Cell::VisitAllObjects((const WorldObject*)bot, searcher, searchRange);
    }

    const Point caster = GetPoint(bot);
    std::vector<Point> centers = MageAoeGeometry::BuildCandidates(points, caster, radius, maxRange);
    if (maxHitRadius > radius)
    {
        const std::vector<Point> reachCenters = MageAoeGeometry::BuildCandidates(points, caster, maxHitRadius, maxRange);
        centers.insert(centers.end(), reachCenters.begin(), reachCenters.end());
    }
    float bestDistance = std::numeric_limits<float>::max();
    for (Point center : centers)
    {
        bot->UpdateAllowedPositionZ(center.x, center.y, center.z);
        if (!MageAoeGeometry::IsWithinRadius(caster, center, maxRange) ||
            (!ignoreLos && !bot->IsWithinLOS(center.x, center.y, center.z + 1.0f)))
            continue;

        std::vector<Unit*> covered;
        for (Unit* target : targets)
        {
            if (MageAoeGeometry::IsWithinRadius(center, GetPoint(target), GetHitRadius(bot, target, radius)))
                covered.push_back(target);
        }
        if (covered.size() < MAGE_GROUND_AOE_MIN_TARGETS || covered.size() < result.targets.size())
            continue;

        covered.erase(std::remove_if(covered.begin(), covered.end(), [&](Unit* target)
        {
            return !CanHit(bot, center, target, radius, ignoreLos);
        }), covered.end());
        if (covered.size() < MAGE_GROUND_AOE_MIN_TARGETS || covered.size() < result.targets.size())
            continue;

        const bool breaksControl = std::any_of(protectedTargets.begin(), protectedTargets.end(), [&](Unit* target)
        {
            return CanHit(bot, center, target, radius, ignoreLos);
        });
        if (breaksControl || (avoidExistingFlamestrike && AlreadyCovered(bot, covered, existing)))
            continue;

        const float dx = center.x - caster.x;
        const float dy = center.y - caster.y;
        const float dz = center.z - caster.z;
        const float distance = dx * dx + dy * dy + dz * dz;
        if (covered.size() == result.targets.size() && distance >= bestDistance)
            continue;

        result.position = WorldLocation(bot->GetMapId(), center.x, center.y, center.z, 0.0f);
        result.radius = radius;
        result.targets.clear();
        for (Unit* target : covered)
            result.targets.push_back(target->GetObjectGuid());
        bestDistance = distance;
    }

    return result.targets.size() >= MAGE_GROUND_AOE_MIN_TARGETS;
}
