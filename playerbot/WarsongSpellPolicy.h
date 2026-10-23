#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace ai
{
namespace warsong
{
    // Descriptions are populated from the core's spell effects, including item
    // and triggered spells. No translated action names or spell ranks are used.
    enum class FlagSpellEffect { Other, Aura, Trigger, Teleport, InstantKill };
    enum class FlagSpellAura { Other, Stealth, Invisibility, FeignDeath, Mount, EffectImmunity, SchoolImmunity };
    enum FlagSpellRecipient : unsigned { NoRecipient = 0, CasterRecipient = 1, TargetRecipient = 2 };

    struct FlagSpellEffectInfo
    {
        FlagSpellEffect effect = FlagSpellEffect::Other;
        FlagSpellAura aura = FlagSpellAura::Other;
        unsigned recipients = TargetRecipient;
        bool positive = false;
        std::uint32_t triggeredSpell = 0;
    };

    struct FlagSpellInfo
    {
        std::uint32_t id = 0;
        bool immunityPurgesEffects = false;
        std::array<FlagSpellEffectInfo, 3> effects;
    };

    inline bool CanLoseFlag(const FlagSpellInfo& spell, const FlagSpellEffectInfo& effect)
    {
        if (effect.effect == FlagSpellEffect::Teleport)
            return true;
        if (effect.effect == FlagSpellEffect::InstantKill)
            return (effect.recipients & CasterRecipient) != 0;
        if (effect.effect != FlagSpellEffect::Aura)
            return false;

        switch (effect.aura)
        {
            case FlagSpellAura::Stealth:
            case FlagSpellAura::Invisibility:
            case FlagSpellAura::FeignDeath: // Also disallowed as a deliberate carrier policy.
            case FlagSpellAura::Mount:
                return true;
            case FlagSpellAura::EffectImmunity:
                return effect.positive;
            case FlagSpellAura::SchoolImmunity:
                return spell.immunityPurgesEffects;
            default:
                return false;
        }
    }

    template <typename Lookup>
    bool IsCarrierSpellSafe(const FlagSpellInfo& spell, bool casterCarries, bool targetCarries,
        Lookup& lookup, std::vector<std::uint32_t>& path)
    {
        if (!casterCarries && !targetCarries)
            return true;
        if (std::find(path.begin(), path.end(), spell.id) != path.end())
            return true; // This edge was already checked; avoid cyclic trigger records.
        if (path.size() >= 16)
            return false; // Do not risk a carrier on an abnormally deep trigger chain.

        path.push_back(spell.id);
        for (const FlagSpellEffectInfo& effect : spell.effects)
        {
            const bool affectsCarrier = ((effect.recipients & CasterRecipient) && casterCarries) ||
                ((effect.recipients & TargetRecipient) && targetCarries);
            const bool losesCarrierFlag = effect.effect == FlagSpellEffect::InstantKill ?
                (casterCarries && (effect.recipients & CasterRecipient)) : affectsCarrier;
            if (losesCarrierFlag && CanLoseFlag(spell, effect))
            {
                path.pop_back();
                return false;
            }

            if (effect.triggeredSpell &&
                (effect.effect == FlagSpellEffect::Trigger || effect.effect == FlagSpellEffect::Aura))
            {
                // A trigger forwards its selected unit to the child spell; a
                // child's self effect still refers to the original caster.
                const FlagSpellInfo child = lookup(effect.triggeredSpell);
                if (child.id && !IsCarrierSpellSafe(child, casterCarries, affectsCarrier, lookup, path))
                {
                    path.pop_back();
                    return false;
                }
            }
        }
        path.pop_back();
        return true;
    }

    template <typename Lookup>
    bool IsCarrierSpellSafe(const FlagSpellInfo& spell, bool casterCarries, bool targetCarries, Lookup lookup)
    {
        std::vector<std::uint32_t> path;
        return IsCarrierSpellSafe(spell, casterCarries, targetCarries, lookup, path);
    }
}
}
