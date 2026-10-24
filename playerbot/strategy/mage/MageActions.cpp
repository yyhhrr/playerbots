
#include "playerbot/playerbot.h"
#include "MageActions.h"
#include "MageAoe.h"
#include "MageAoePolicy.h"

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

bool CastBlizzardAction::IsAoeReady(const MageAoeTarget&)
{
    MageBlizzardCasterState caster;
    caster.uncontrolled = bot->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL);
    caster.jumping = ai->IsJumping();
    caster.falling = bot->IsFalling();

    // Being hit, holding aggro and the absence of a tank do not stop AOE.
    // Target count, line of sight and crowd control are checked by the selector.
    return IsMageBlizzardCasterReady(caster);
}
