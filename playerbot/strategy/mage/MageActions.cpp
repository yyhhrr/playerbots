
#include "playerbot/playerbot.h"
#include "MageActions.h"
#include "MageAoe.h"
#include "MageAoePolicy.h"

using namespace ai;

bool CastMageOtArcaneExplosionAction::CanReplaceCurrentCast(bool& replacingGroundAoe) const
{
    replacingGroundAoe = false;
    for (CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        Spell* current = bot->GetCurrentSpell(slot);
        if (!current || current->getState() == SPELL_STATE_FINISHED)
            continue;

        const uint32 first = sSpellMgr.GetFirstSpellInChain(current->m_spellInfo->Id);
        if (first != 10 && first != 2120) // Blizzard and Flamestrike only.
            return false;
        replacingGroundAoe = true;
    }
    return true;
}

bool CastMageOtArcaneExplosionAction::isUseful()
{
    bool replacingGroundAoe;
    return CanReplaceCurrentCast(replacingGroundAoe) && CastArcaneExplosionAction::isUseful();
}

bool CastMageOtArcaneExplosionAction::isPossible()
{
    bool replacingGroundAoe;
    if (!CanReplaceCurrentCast(replacingGroundAoe) || !FindMageArcaneExplosionTarget(ai))
        return false;
    if (!replacingGroundAoe)
        return CastArcaneExplosionAction::isPossible();

    const SpellEntry* info = sServerFacade.LookupSpellInfo(GetSpellID());
    if (!info || !ai->HasSpell(GetSpellID()) || !bot->IsSpellReady(GetSpellID()) || bot->HasGCD(info) ||
        bot->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) ||
        bot->GetPower(POWER_MANA) < Spell::CalculatePowerCost(info, bot))
        return false;

    // Ignore only the concurrent ground cast. Mana, silence, cooldowns and all
    // other native spell checks must pass before a reaction can interrupt it.
    Spell probe(bot, info, TRIGGERED_IGNORE_CURRENT_CASTED_SPELL);
    probe.m_targets.setUnitTarget(bot);
    return probe.CheckCast(true) == SPELL_CAST_OK;
}

bool CastMageOtArcaneExplosionAction::Execute(Event& event)
{
    if (!isUseful() || !isPossible())
        return false;

    bool replacingGroundAoe;
    if (!CanReplaceCurrentCast(replacingGroundAoe))
        return false;
    if (replacingGroundAoe)
        ai->InterruptSpell();

    // Revalidate and cast through the normal spell path after interruption.
    return CastArcaneExplosionAction::Execute(event);
}

bool MagePveRetreatAction::isUseful()
{
    // Let an already-started crowd control cast create space before retreating.
    for (CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        Spell* current = bot->GetCurrentSpell(slot);
        if (current && current->getState() != SPELL_STATE_FINISHED &&
            sSpellMgr.GetFirstSpellInChain(current->m_spellInfo->Id) == 118)
            return false;
    }
    return MovementAction::isUseful() &&
        (!retreatFailed || uint32(WorldTimer::getMSTime() - retreatFailedAt) >= 5000);
}

bool MagePveRetreatAction::Execute(Event& event)
{
    Unit* target = FindMagePveRetreatTarget(ai);
    if (!target)
        return false;
    if (Flee(target))
    {
        retreatFailed = false;
        return true;
    }

    // A missing safe route must leave time to finish ordinary spells before retrying.
    retreatFailed = true;
    retreatFailedAt = WorldTimer::getMSTime();
    return false;
}

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
