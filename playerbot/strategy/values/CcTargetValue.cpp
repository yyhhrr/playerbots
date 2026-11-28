
#include "playerbot/playerbot.h"
#include "CcTargetValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/values/RtiTargetValue.h"

#include <algorithm>

using namespace ai;

namespace
{
    bool IsAttackingCcTarget(Unit* attacker, Unit* target)
    {
        if (!attacker)
            return false;
        if (attacker->GetVictim() == target)
            return true;

        for (CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL })
        {
            Spell* cast = attacker->GetCurrentSpell(slot);
            if (cast && cast->getState() != SPELL_STATE_FINISHED &&
                cast->m_targets.getUnitTargetGuid() == target->GetObjectGuid() && !IsPositiveSpell(cast->m_spellInfo))
                return true;
        }

        return false;
    }
}

class FindTargetForCcStrategy : public FindTargetStrategy
{
public:
    FindTargetForCcStrategy(PlayerbotAI* ai, const std::string& spell) : FindTargetStrategy(ai),
        spell(spell), maxDistance(sPlayerbotAIConfig.sightDistance),
        dungeonPolymorph(spell == "polymorph" && ai->GetBot()->IsInWorld() && ai->GetBot()->GetMap()->IsDungeon()) {}

public:
    virtual void CheckAttacker(Unit* creature, ThreatManager* threatManager)
    {
        Player* bot = ai->GetBot();

        AiObjectContext* context = ai->GetAiObjectContext();

        Unit* markedTarget = ai->GetUnit(AI_VALUE(ObjectGuid, "rti cc target"));
        if (markedTarget == creature)
        {
            result = creature;
            return;
        }

        if (AI_VALUE(ObjectGuid,"current target") == creature->GetObjectGuid())
            return;

        if (AI_VALUE(ObjectGuid,"rti target") == creature->GetObjectGuid())
            return;

        uint8 health = creature->GetHealthPercent();
        if (health < sPlayerbotAIConfig.mediumHealth)
            return;

        float minDistance = ai->GetRange("spell");
        Group* group = bot->GetGroup();
        if (!group)
            return;

        if (dungeonPolymorph)
        {
            if (markedTarget || !creature->IsCreature() || creature->HasBreakableByDamageCrowdControlAura())
                return;
        }
        else if (AI_VALUE(uint8,"aoe count") > 2)
        {
            WorldLocation aoe = AI_VALUE(WorldLocation,"aoe position");
            if (sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.GetDistance2d(creature, aoe.coord_x, aoe.coord_y), sPlayerbotAIConfig.aoeRadius))
                return;
        }

        if (creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) && !(spell == "fear" || spell == "banish"))
            return;

        if (!ai->CanCastSpell(spell, creature, true, nullptr, false, true))
            return;

        // Dungeon mages can pick an unmarked add when the chosen icon is unused.
        if (!ai->HasStrategy("cc", BotState::BOT_STATE_COMBAT) ||
            (!dungeonPolymorph && RtiTargetValue::GetRtiIndex(AI_VALUE(std::string, "rti cc")) != -1))
            return;

        bool selectedByParty = false;
        for (const Group::MemberSlot& slot : group->GetMemberSlots())
        {
            Player* member = sObjectMgr.GetPlayer(slot.guid);
            if (!member || member == bot || !sServerFacade.IsAlive(member) || bot->GetMapId() != member->GetMapId() ||
                (dungeonPolymorph && bot->GetInstanceId() != member->GetInstanceId()))
                continue;

            PlayerbotAI* memberAI = member->GetPlayerbotAI();
            AiObjectContext* memberContext = memberAI ? memberAI->GetAiObjectContext() : nullptr;
            const bool selected = memberAI ? memberContext &&
                memberContext->GetValue<ObjectGuid>("current target")->Get() == creature->GetObjectGuid() :
                member->GetSelectionGuid() == creature->GetObjectGuid();
            if (dungeonPolymorph)
            {
                // A selection alone can be a tab target. Actual attacks would break the sheep.
                if (IsAttackingCcTarget(member, creature) || IsAttackingCcTarget(member->GetPet(), creature))
                    return;
                selectedByParty |= selected;
            }
            else if (selected)
                return;

            if (ai->IsTank(member))
                minDistance = std::min(minDistance, sServerFacade.GetDistance2d(member, creature));
        }

        if (dungeonPolymorph && result && selectedByParty && !resultSelectedByParty)
            return;

        if ((!result && !creature->IsPlayer()) || minDistance > maxDistance ||
            (dungeonPolymorph && resultSelectedByParty && !selectedByParty))
        {
            result = creature;
            maxDistance = minDistance;
            resultSelectedByParty = selectedByParty;
        }
    }

private:
    std::string spell;
    float maxDistance;
    const bool dungeonPolymorph;
    bool resultSelectedByParty = false;
};

ObjectGuid CcTargetValue::Calculate()
{
    std::list<ObjectGuid> possible = AI_VALUE(std::list<ObjectGuid>,"possible targets no los");

    for (std::list<ObjectGuid>::iterator i = possible.begin(); i != possible.end(); ++i)
    {
        ObjectGuid guid = *i;
        Unit* add = ai->GetUnit(guid);
        if (!add)
            continue;

        if (!ai->IsSafe(add))
            continue;

        if (ai->HasMyAura(qualifier, add))
            return ObjectGuid();

        if (qualifier == "polymorph")
        {
            if (ai->HasMyAura("polymorph: pig", add))
                return ObjectGuid();
            if (ai->HasMyAura("polymorph: turtle", add))
                return ObjectGuid();
        }
    }

    FindTargetForCcStrategy strategy(ai, qualifier);
    Unit* target = FindTarget(&strategy);
    return target ? target->GetObjectGuid() : ObjectGuid();
}
