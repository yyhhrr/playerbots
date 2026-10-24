
#include "playerbot/playerbot.h"
#include "MageMultipliers.h"
#include "MageActions.h"
#include "MageAoePolicy.h"

using namespace ai;

float MageAoeCommitMultiplier::GetValue(Action* action)
{
    if (!action || bot->getClass() != CLASS_MAGE || !IsMageAoeDisengageAction(action->getName()))
        return 1.0f;

    const std::string name = action->getName();
    // Flee also handles harmful ground effects; keep that movement available.
    if ((name == "flee" || name == "runaway") && AI_VALUE2(bool, "has area debuff", "self target"))
        return 1.0f;

    // A spell already being cast is not "useful" to start again. Keep its cast
    // or channel committed instead of letting an escape action interrupt it.
    for (CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        Spell* current = bot->GetCurrentSpell(slot);
        if (!current || current->getState() == SPELL_STATE_FINISHED)
            continue;
        const uint32 first = sSpellMgr.GetFirstSpellInChain(current->m_spellInfo->Id);
        if (first != 10 && first != 2120) // Blizzard and Flamestrike.
            continue;
        float x, y, z;
        current->m_targets.getDestination(x, y, z);
        return IsMageAoeAreaSafe(ai, current->m_spellInfo->Id, x, y, z) ? 0.0f : 1.0f;
    }

    // Only hold position while a real, affordable and control-safe AOE is ready.
    // If no spell can be cast, normal escape and mana recovery remain available.
    for (const char* spell : { "leveling arcane explosion", "arcane explosion", "flamestrike", "blizzard" })
    {
        const std::string_view spellName = spell;
        // Only consider spells that this specialization actually schedules.
        if (spellName == "arcane explosion" && strategyName.find("arcane") == std::string::npos)
            continue;
        if (spellName == "arcane explosion")
        {
            Trigger* trigger = context->GetTrigger("mage arcane explosion aoe");
            if (!trigger || !trigger->IsActive())
                continue;
        }
        if (spellName == "flamestrike" && strategyName.find("frost") != std::string::npos)
        {
            Trigger* trigger = context->GetTrigger("frost spells locked");
            if (!trigger || !trigger->IsActive())
                continue;
        }
        Action* aoe = context->GetAction(spell);
        if (aoe && aoe->isUseful())
        {
            if (aoe->isPossible())
                return 0.0f;

            // A global cooldown between explosions is not a reason to retreat.
            CastSpellAction* cast = dynamic_cast<CastSpellAction*>(aoe);
            const SpellEntry* info = cast ? sServerFacade.LookupSpellInfo(cast->GetSpellID()) : nullptr;
            if (info && bot->HasGCD(info) &&
                !bot->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL) &&
                bot->GetPower(POWER_MANA) >= Spell::CalculatePowerCost(info, bot))
                return 0.0f;
        }
    }
    return 1.0f;
}
