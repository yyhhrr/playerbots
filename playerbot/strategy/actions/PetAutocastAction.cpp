#include "playerbot/playerbot.h"
#include "PetAutocastAction.h"

#include <string>

using namespace ai;

namespace
{
    bool IsDisabledAutocastSpell(uint32 firstRank)
    {
        // Cower, Prowl, Spell Lock and Devour Magic.
        return firstRank == 1742 || firstRank == 24450 || firstRank == 19244 || firstRank == 19505;
    }

    Value<std::string>* KnownAutocastSpells(PlayerbotAI* ai, const Pet* pet)
    {
        const CharmInfo* charm = pet->GetCharmInfo();
        if (!charm || !charm->GetPetNumber())
            return nullptr;

        return ai->GetAiObjectContext()->GetValue<std::string>(
            "manual saved string", "pet autocast spells " + std::to_string(charm->GetPetNumber()));
    }

    bool RememberSpell(std::string& knownSpells, uint32 firstRank)
    {
        const std::string key = std::to_string(firstRank) + '|';
        if (knownSpells.find('|' + key) != std::string::npos)
            return false;

        knownSpells += key;
        return true;
    }
}

bool PetAutocastAction::HasNewSpells(PlayerbotAI* ai, const Pet* pet)
{
    if (!pet || !pet->IsAlive())
        return false;

    auto* known = KnownAutocastSpells(ai, pet);
    if (!known)
        return false;

    const std::string knownSpells = known->Get();
    if (knownSpells.empty())
        return true;

    for (const auto& [spellId, spell] : pet->m_spells)
    {
        if (spell.state == PETSPELL_REMOVED || !IsAutocastable(spellId))
            continue;

        if (knownSpells.find('|' + std::to_string(sSpellMgr.GetFirstSpellInChain(spellId)) + '|') == std::string::npos)
            return true;
    }

    return false;
}

bool PetAutocastAction::ApplyDefaults(PlayerbotAI* ai, Pet* pet, uint32 manualSpell)
{
    if (!pet)
        return false;

    auto* known = KnownAutocastSpells(ai, pet);
    if (!known)
        return false;

    std::string knownSpells = known->Get();
    const bool firstObservation = knownSpells.empty();
    if (firstObservation)
    {
        knownSpells = "|";
        // Existing pets already have autocast settings in the core's spell store.
        // A removed lower rank also protects the live rank's inherited setting.
        for (const auto& [spellId, spell] : pet->m_spells)
            if (spell.state != PETSPELL_NEW && IsAutocastable(spellId))
                RememberSpell(knownSpells, sSpellMgr.GetFirstSpellInChain(spellId));
    }

    if (manualSpell)
        RememberSpell(knownSpells, sSpellMgr.GetFirstSpellInChain(manualSpell));

    bool applied = false;
    for (const auto& [spellId, spell] : pet->m_spells)
    {
        if (spell.state == PETSPELL_REMOVED || !IsAutocastable(spellId))
            continue;

        const uint32 firstRank = sSpellMgr.GetFirstSpellInChain(spellId);
        if (RememberSpell(knownSpells, firstRank))
        {
            pet->ToggleAutocast(spellId, !IsDisabledAutocastSpell(firstRank));
            applied = true;
        }
    }

    known->Set(knownSpells);
    return applied;
}

bool PetAutocastAction::isUseful()
{
    return HasNewSpells(ai, bot->GetPet());
}

bool PetAutocastAction::Execute(Event& event)
{
    return ApplyDefaults(ai, bot->GetPet());
}
