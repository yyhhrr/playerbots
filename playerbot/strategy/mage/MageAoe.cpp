#include "playerbot/playerbot.h"
#include "MageAoe.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"

#include <set>

using namespace ai;

Unit* ai::FindMagePveRetreatTarget(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->getClass() != CLASS_MAGE || !bot->IsAlive() || !bot->IsInWorld() ||
        bot->IsBeingTeleported() || ai->IsRealPlayer() || ai->IsInPvp() || !ai->CanMove() ||
        !sPlayerbotAIConfig.fleeingEnabled ||
        ai->HasStrategy("stay", BotState::BOT_STATE_COMBAT) || ai->GetRange("flee") <= ATTACK_DISTANCE)
        return nullptr;

    AiObjectContext* context = ai->GetAiObjectContext();
    if (!context)
        return nullptr;

    Unit* closest = nullptr;
    float closestDistanceSquared = 12.0f * 12.0f;
    std::set<ObjectGuid> seen;
    auto consider = [&](ObjectGuid guid)
    {
        if (!guid || !seen.insert(guid).second)
            return;
        Unit* target = ai->GetUnit(guid);
        if (!target || target == bot || !target->IsCreature() || !target->IsAlive() ||
            !target->IsInWorld() || !ai->IsSafe(target) || target->GetMapId() != bot->GetMapId() ||
            target->GetInstanceId() != bot->GetInstanceId() ||
            !sServerFacade.IsHostileTo(bot, target) || target->HasBreakableByDamageCrowdControlAura() ||
            !PossibleTargetsValue::CanPerceive(target, bot) || !bot->IsWithinLOSInMap(target))
            return;

        const float dx = target->GetPositionX() - bot->GetPositionX();
        const float dy = target->GetPositionY() - bot->GetPositionY();
        const float dz = target->GetPositionZ() - bot->GetPositionZ();
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!(distanceSquared < closestDistanceSquared))
            return;
        closestDistanceSquared = distanceSquared;
        closest = target;
    };

    // Follow/guard formation and the generic 65% run-speed veto must not prevent
    // creating space, especially after Frost Nova. Roots are deliberately allowed.
    consider(context->GetValue<ObjectGuid>("closest attacker targeting me")->Get());
    consider(context->GetValue<ObjectGuid>("current target")->Get());
    for (ObjectGuid guid : context->GetValue<std::list<ObjectGuid>>("attackers targeting me")->Get())
        consider(guid);
    return closest;
}

bool ai::FindMageAoeTarget(PlayerbotAI* ai, uint32 spellId, MageAoeTarget& result,
    bool avoidExistingFlamestrike)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->getClass() != CLASS_MAGE)
    {
        result = MageAoeTarget();
        return false;
    }
    return FindGroundAoeTarget(ai, spellId, result, MAGE_GROUND_AOE_MIN_TARGETS, avoidExistingFlamestrike);
}

bool ai::IsMageAoeAreaSafe(PlayerbotAI* ai, uint32 spellId, float x, float y, float z)
{
    return IsSpellAoeAreaSafe(ai, spellId, x, y, z);
}

Unit* ai::FindMageArcaneExplosionTarget(PlayerbotAI* ai, bool levelingOnly)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || bot->getClass() != CLASS_MAGE || !bot->IsAlive() || !bot->IsInWorld() ||
        bot->IsBeingTeleported() || (levelingOnly && bot->GetLevel() >= 60))
        return nullptr;

    const uint32 spellId = ai->GetAiObjectContext()->GetValue<uint32>("spell id", "arcane explosion")->Get();
    return FindCloseAoeTarget(ai, spellId, MageArcaneExplosionRadius, MageAoeMinimumTargets);
}
