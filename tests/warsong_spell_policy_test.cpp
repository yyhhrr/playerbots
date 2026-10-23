#include "playerbot/WarsongSpellPolicy.h"

#include <cstdlib>
#include <iostream>
#include <map>

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const char* description)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << description << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace ai::warsong;
    std::map<std::uint32_t, FlagSpellInfo> spells;
    auto lookup = [&spells](std::uint32_t id)
    {
        auto found = spells.find(id);
        return found == spells.end() ? FlagSpellInfo{} : found->second;
    };
    auto safe = [&lookup](const FlagSpellInfo& spell, bool caster, bool target)
    {
        return IsCarrierSpellSafe(spell, caster, target, lookup);
    };
    auto aura = [](std::uint32_t id, FlagSpellAura kind, unsigned recipients = CasterRecipient)
    {
        FlagSpellInfo result;
        result.id = id;
        result.effects[0] = { FlagSpellEffect::Aura, kind, recipients, true, 0 };
        return result;
    };

    for (FlagSpellAura kind : { FlagSpellAura::Stealth, FlagSpellAura::Invisibility,
        FlagSpellAura::FeignDeath, FlagSpellAura::Mount })
    {
        FlagSpellInfo spell = aura(1, kind);
        Check(!safe(spell, true, true), "self concealment mount and feign death are forbidden to a carrier");
        Check(!safe(spell, true, false), "self spells still affect a carrier with another selected target");
        Check(safe(spell, false, true), "a self spell on the escort does not conceal the targeted carrier");
        Check(safe(spell, false, false), "noncarriers retain concealment mount and feign death");

        spell.effects[0].recipients = TargetRecipient;
        Check(!safe(spell, false, true), "effects explicitly applied to a carrier are forbidden");
        Check(safe(spell, true, false), "carriers may apply an effect to a noncarrier ally");
    }

    FlagSpellInfo immunity = aura(1022, FlagSpellAura::SchoolImmunity, TargetRecipient);
    immunity.immunityPurgesEffects = true;
    Check(!safe(immunity, false, true), "an escort cannot cast flag-removing protection on a carrier");
    Check(!safe(immunity, true, true), "self-targeted protection is forbidden to the carrier");
    Check(safe(immunity, true, false), "a carrier can still protect a noncarrier ally");
    immunity.immunityPurgesEffects = false;
    Check(safe(immunity, true, true), "school immunity without the core purge attribute is not blanket banned");

    immunity = aura(90000, FlagSpellAura::EffectImmunity, TargetRecipient);
    Check(!safe(immunity, false, true), "positive effect immunity removes flag auras in the core");
    immunity.effects[0].positive = false;
    Check(safe(immunity, false, true), "negative effect immunity does not enter the positive immunity removal branch");

    for (std::uint32_t id : { 768U, 5487U, 783U, 1850U, 1953U, 22812U, 871U, 1044U, 17U })
    {
        FlagSpellInfo spell = aura(id, FlagSpellAura::Other);
        Check(safe(spell, true, true), "forms sprint blink barkskin shield wall freedom and absorbs stay allowed");
    }

    FlagSpellInfo teleport;
    teleport.id = 23442;
    teleport.effects[0].effect = FlagSpellEffect::Teleport;
    teleport.effects[0].recipients = CasterRecipient;
    Check(!safe(teleport, true, false), "item and spell map teleport effects are forbidden to the carrier");
    Check(safe(teleport, false, false), "noncarriers retain teleport spells");

    FlagSpellInfo intervention;
    intervention.id = 19752;
    intervention.effects[2] = { FlagSpellEffect::InstantKill, FlagSpellAura::Other, CasterRecipient, false, 0 };
    Check(!safe(intervention, true, false), "a carrier cannot use Divine Intervention and kill itself");
    Check(safe(intervention, false, false), "noncarriers retain Divine Intervention");
    intervention.effects[2].recipients = TargetRecipient;
    Check(safe(intervention, false, true), "instant-kill attacks on an enemy carrier are not mistaken for suicide");
    intervention.effects[2].recipients = CasterRecipient | TargetRecipient;
    Check(safe(intervention, false, true), "an escort is not forbidden to attack the enemy carrier by a combined kill target mask");

    FlagSpellInfo vanish;
    vanish.id = 1856;
    vanish.effects[0] = { FlagSpellEffect::Trigger, FlagSpellAura::Other, CasterRecipient, true, 11327 };
    spells[11327] = aura(11327, FlagSpellAura::Stealth);
    Check(!safe(vanish, true, false), "vanish is rejected through its triggered stealth spell");
    Check(safe(vanish, false, true), "an escort can vanish without dropping the actual carrier's flag");

    FlagSpellInfo potion;
    potion.id = 90001;
    potion.effects[0] = { FlagSpellEffect::Trigger, FlagSpellAura::Other, CasterRecipient, true, 90002 };
    spells[90002] = aura(90002, FlagSpellAura::SchoolImmunity, TargetRecipient);
    spells[90002].immunityPurgesEffects = true;
    Check(!safe(potion, true, false), "a self item trigger forwards the caster as child recipient");

    potion.effects[0].recipients = TargetRecipient;
    Check(!safe(potion, false, true), "a targeted item trigger forwards a human or bot carrier recipient");
    Check(safe(potion, true, false), "targeted immunity on another ally is not confused with caster immunity");

    FlagSpellInfo cyclic;
    cyclic.id = 90003;
    cyclic.effects[0] = { FlagSpellEffect::Trigger, FlagSpellAura::Other, CasterRecipient, true, 90004 };
    spells[90004] = cyclic;
    spells[90004].id = 90004;
    spells[90004].effects[0].triggeredSpell = 90003;
    spells[90003] = cyclic;
    Check(safe(cyclic, true, true), "cyclic trigger records terminate without suppressing unrelated safe spells");
    spells[90004].effects[1] = { FlagSpellEffect::Aura, FlagSpellAura::Mount, CasterRecipient, true, 0 };
    Check(!safe(cyclic, true, true), "cycle detection still checks later hazardous effects");

    FlagSpellInfo multi = aura(90005, FlagSpellAura::Other);
    multi.effects[2] = { FlagSpellEffect::Aura, FlagSpellAura::Invisibility, CasterRecipient, true, 0 };
    Check(!safe(multi, true, false), "all three effects are checked rather than only the first");
    multi.effects[2].effect = FlagSpellEffect::Other;
    Check(safe(multi, true, false), "unused aura fields do not falsely classify a non-aura effect");

    for (std::uint32_t id = 91000; id < 91018; ++id)
    {
        FlagSpellInfo link;
        link.id = id;
        link.effects[0] = { FlagSpellEffect::Trigger, FlagSpellAura::Other, CasterRecipient, true, id + 1 };
        spells[id] = link;
    }
    Check(!safe(spells[91000], true, true), "abnormally deep trigger trees cannot bypass carrier protection");
    Check(safe(spells[91000], false, false), "noncarriers do not pay for deep trigger checks");

    std::cout << "Warsong carrier spell policy: " << checks << " checks passed\n";
}
