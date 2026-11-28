

#include "playerbot/playerbot.h"
#include "WarlockActions.h"
#include "playerbot/strategy/actions/SpellAoeTargeting.h"

using namespace ai;

#ifdef MANGOSBOT_ZERO
namespace
{
    constexpr uint32 RainOfFireFirstRank = 5740;
    constexpr uint32 HellfireFirstRank = 1949;
    constexpr float HellfireCloseRange = 10.0f;

    bool IsWarlockChannelReady(Player* bot, const SpellEntry* spell)
    {
        return spell && bot->IsSpellReady(spell->Id) && !bot->HasGCD(spell) &&
            !bot->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) &&
            !bot->IsNonMeleeSpellCasted(false, false, true);
    }

    uint32 GetHellfireDamageSpell(const SpellEntry* spell)
    {
        if (!spell)
            return 0;

        // The channel applies two auras. The triggered spell deals damage to enemies.
        for (uint32 index = EFFECT_INDEX_0; index <= EFFECT_INDEX_2; ++index)
        {
            if (spell->EffectApplyAuraName[index] == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
                return spell->EffectTriggerSpell[index];
        }
        return 0;
    }
}

bool CastRainOfFireAction::isUseful()
{
    if (GetSpellID() != AI_VALUE2(uint32, "spell id", GetSpellName()))
        SetSpellName(GetSpellName(), "spell id", true);
    return GetSpellID() && !ai->IsJumping() && !bot->IsFalling() &&
        AI_VALUE2(bool, "spell cast useful", GetSpellName());
}

bool CastRainOfFireAction::SelectTarget(GroundAoeTarget& target)
{
    if (!IsWarlockChannelReady(bot, sServerFacade.LookupSpellInfo(GetSpellID())) ||
        !FindGroundAoeTarget(ai, GetSpellID(), target, WarlockAoeMinimumTargets))
        return false;

    const auto& pos = target.position;
    return ai->CanCastSpell(GetSpellID(), pos.coord_x, pos.coord_y, pos.coord_z, 0) &&
        IsSpellAoeAreaSafe(ai, GetSpellID(), pos.coord_x, pos.coord_y, pos.coord_z);
}

bool CastRainOfFireAction::isPossible()
{
    GroundAoeTarget target;
    return SelectTarget(target);
}

bool CastRainOfFireAction::Execute(Event&)
{
    GroundAoeTarget target;
    if (!isUseful() || !SelectTarget(target))
        return false;

    const auto& pos = target.position;
    uint32 duration = sPlayerbotAIConfig.globalCoolDown;
    if (!ai->CastSpell(GetSpellID(), pos.coord_x, pos.coord_y, pos.coord_z, nullptr, false, &duration))
        return false;

    context->GetValue<time_t>("last spell cast time", GetSpellName())->Set(time(nullptr));
    SetDuration(ai->HasCheat(BotCheatMask::attackspeed) ? 1 : duration);
    return true;
}

bool CastHellfireAction::isUseful()
{
    if (GetSpellID() != AI_VALUE2(uint32, "spell id", GetSpellName()))
        SetSpellName(GetSpellName(), "spell id", true);
    if (!GetSpellID() || bot->GetHealthPercent() < sPlayerbotAIConfig.mediumHealth ||
        ai->IsJumping() || bot->IsFalling() || !CastSpellAction::isUseful())
        return false;

    const uint32 damage = GetHellfireDamageSpell(sServerFacade.LookupSpellInfo(GetSpellID()));
    return damage && FindCloseAoeTarget(ai, damage, HellfireCloseRange, WarlockAoeMinimumTargets);
}

bool CastHellfireAction::isPossible()
{
    const SpellEntry* spell = sServerFacade.LookupSpellInfo(GetSpellID());
    if (!IsWarlockChannelReady(bot, spell))
        return false;

    // Hellfire is self-cast and includes a harmful effect on the caster.
    Spell probe(bot, spell, false);
    probe.m_targets.setUnitTarget(bot);
    const SpellCastResult result = probe.CheckCast(true);
    return result == SPELL_CAST_OK || result == SPELL_FAILED_MOVING || result == SPELL_FAILED_NOT_STANDING;
}

bool CastHellfireAction::Execute(Event& event)
{
    return isUseful() && isPossible() && CastSpellAction::Execute(event);
}

bool ai::ShouldStopWarlockAoe(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    Spell* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!channel || channel->getState() == SPELL_STATE_FINISHED)
        return false;

    const SpellEntry* spell = channel->m_spellInfo;
    const uint32 firstRank = sSpellMgr.GetFirstSpellInChain(spell->Id);
    if (firstRank == HellfireFirstRank)
    {
        const uint32 damage = GetHellfireDamageSpell(spell);
        return bot->GetHealthPercent() <= sPlayerbotAIConfig.lowHealth ||
            !IsSpellAoeAreaSafe(ai, damage, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    }
    if (firstRank == RainOfFireFirstRank)
    {
        float x, y, z;
        channel->m_targets.getDestination(x, y, z);
        return !IsSpellAoeAreaSafe(ai, spell->Id, x, y, z);
    }
    return false;
}

bool StopWarlockAoeAction::Execute(Event&)
{
    if (!ShouldStopWarlockAoe(ai))
        return false;

    bot->InterruptSpell(CURRENT_CHANNELED_SPELL);
    return true;
}
#endif
