
#include "playerbot/playerbot.h"
#include "MageActions.h"
#include "MageAoe.h"
#include "MageAoePolicy.h"

#include "Combat/ThreatManager.h"
#include "Groups/Group.h"

using namespace ai;

bool CastMageGroundAoeAction::SelectAoeTarget(MageAoeTarget& target)
{
    return FindMageAoeTarget(ai, GetSpellID(), target, avoidExistingFlamestrike) && IsAoeReady(target);
}

Unit* CastMageGroundAoeAction::GetTarget()
{
    MageAoeTarget target;
    if (!FindMageAoeTarget(ai, GetSpellID(), target, avoidExistingFlamestrike))
        return nullptr;

    // Generic spell usefulness checks still need a hostile target, but ground
    // spells must not depend on an unrelated or out-of-range current target.
    return target.targets.empty() ? nullptr : ai->GetUnit(target.targets.front());
}

bool CastMageGroundAoeAction::isUseful()
{
    MageAoeTarget target;
    return SelectAoeTarget(target) && CastSpellAction::isUseful();
}

bool CastMageGroundAoeAction::isPossible()
{
    MageAoeTarget target;
    return SelectAoeTarget(target) &&
        ai->CanCastSpell(GetSpellID(), target.position.coord_x, target.position.coord_y, target.position.coord_z, 0);
}

bool CastMageGroundAoeAction::Execute(Event& event)
{
    MageAoeTarget target;
    if (!SelectAoeTarget(target) || !CastSpellAction::isUseful() ||
        !ai->CanCastSpell(GetSpellID(), target.position.coord_x, target.position.coord_y, target.position.coord_z, 0))
        return false;

    // Recompute when dequeued: the pack may have moved, died, or already be
    // covered by our Flamestrike since the trigger was evaluated.
    uint32 spellDuration = sPlayerbotAIConfig.globalCoolDown;
    if (!ai->CastSpell(GetSpellID(), target.position.coord_x, target.position.coord_y, target.position.coord_z,
        nullptr, false, &spellDuration))
        return false;

    context->GetValue<time_t>("last spell cast time", GetSpellName())->Set(time(0));
    if (ai->HasCheat(BotCheatMask::attackspeed))
        spellDuration = 1;

    SetDuration(spellDuration);
    return true;
}

bool CastBlizzardAction::IsAoeReady(const MageAoeTarget& target)
{
    MageBlizzardCasterState caster;
    caster.uncontrolled = bot->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);
    caster.jumping = ai->IsJumping();
    caster.falling = bot->IsFalling();

    // Do not start a channel with an immediate attacker, including one outside
    // the selected pack. Ordinary movement can be stopped by the ground-cast
    // path; jumping and uncontrolled movement cannot.
    for (Unit* attacker : bot->getAttackers())
    {
        if (attacker && attacker->IsInWorld() && sServerFacade.IsAlive(attacker) &&
            (attacker->CanReachWithMeleeAttack(bot) || sServerFacade.GetDistance2d(bot, attacker) < 10.0f))
            caster.immediateAttacker = true;
    }

    if (!IsMageBlizzardCasterReady(caster))
        return false;

    std::set<Player*> tanks;
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* member = group->GetFirstMember(); member; member = member->next())
        {
            Player* player = member->getSource();
            if (player && player != bot && player->IsInWorld() && player->GetMap() == bot->GetMap() &&
                sServerFacade.IsAlive(player) && ai->IsSafe(player) && ai->IsTank(player))
                tanks.insert(player);
        }
    }

    for (ObjectGuid guid : target.targets)
    {
        Unit* enemy = ai->GetUnit(guid);
        if (!enemy || !enemy->IsInWorld() || !sServerFacade.IsAlive(enemy))
            return false;

        MageBlizzardTargetState state;
        state.hasGroupTank = !tanks.empty();
        state.distance = sServerFacade.GetDistance2d(bot, enemy);
        state.immediateAttacker = enemy->GetVictim() == bot &&
            (enemy->CanReachWithMeleeAttack(bot) || state.distance < 10.0f);

        if (state.hasGroupTank)
        {
            Unit* victim = enemy->GetVictim();
            state.victimIsGroupTank = victim && victim->GetObjectGuid().IsPlayer() &&
                tanks.find(static_cast<Player*>(victim)) != tanks.end();
            if (state.victimIsGroupTank)
                state.tankThreat = sServerFacade.GetThreatManager(enemy).getThreat(victim);
            state.mageThreat = sServerFacade.GetThreatManager(enemy).getThreat(bot);
        }
        else
        {
            state.rooted = enemy->hasUnitState(UNIT_STAT_ROOT) || enemy->HasAuraType(SPELL_AURA_MOD_ROOT);
            state.meleeReach = enemy->GetCombatReach() + bot->GetCombatReach();
            state.runSpeed = enemy->GetSpeed(MOVE_RUN);
        }

        if (!IsMageBlizzardTargetReady(state))
            return false;
    }

    return true;
}
