#include "playerbot/WarsongCarrierPolicy.h"

#include <cstdlib>
#include <iostream>

using namespace ai::warsong;

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

    CarrierCandidate Candidate(uint64_t guid, CarrierClass carrierClass, float distance = 20.0f)
    {
        CarrierCandidate result;
        result.guid = guid;
        result.carrierClass = carrierClass;
        result.health = result.maxHealth = 5000;
        result.distance = distance;
        return result;
    }
}

int main()
{
    const auto druid = Candidate(1, CarrierClass::Druid);
    const auto mage = Candidate(2, CarrierClass::FrostMage);
    const auto rogue = Candidate(3, CarrierClass::Rogue);
    const auto warrior = Candidate(4, CarrierClass::Warrior);
    const auto paladin = Candidate(5, CarrierClass::Paladin);
    const auto other = Candidate(6, CarrierClass::Other);
    Check(CarrierScore(druid) > CarrierScore(mage), "healthy druid is preferred to mobile substitutes");
    Check(CarrierScore(mage) > CarrierScore(warrior), "mage is default mobile substitute");
    Check(CarrierScore(rogue) > CarrierScore(paladin), "rogue is default mobile substitute");
    Check(CarrierScore(warrior) > CarrierScore(other), "durable substitute precedes generic class");

    {
        CarrierPlanner planner;
        const auto selection = planner.Update({other, paladin, warrior, rogue, mage, druid}, 100);
        Check(selection.primary == 1 && selection.backup == 2, "selects unique primary and backup");
        Check(!selection.carrier, "selection does not pretend an assigned player already has the flag");
        Check(planner.Update({druid}, 101).backup == 0, "single bot has no duplicate backup");
        const auto empty = planner.Update({}, 102);
        Check(!empty.primary && !empty.backup, "empty roster clears both duties");
    }

    {
        auto unhealthy = druid;
        unhealthy.health = 50;
        Check(CarrierScore(mage) > CarrierScore(unhealthy) + CarrierEmergencyMargin,
            "one percent health druid loses decisively to a healthy substitute");
        auto emptyMana = druid;
        emptyMana.manaPercent = 0;
        Check(CarrierScore(rogue) > CarrierScore(emptyMana), "empty mana druid loses to healthy rogue");
        auto far = druid;
        far.distance = 1000;
        Check(CarrierScore(other) > CarrierScore(far), "extremely far druid does not delay nearby healthy player");
        auto tank = warrior;
        tank.hasHealerSupport = tank.hasShield = true;
        Check(CarrierScore(tank) > CarrierScore(mage), "shield warrior with healing support can be best substitute");
        auto shieldPaladin = paladin;
        shieldPaladin.hasHealerSupport = shieldPaladin.hasShield = true;
        Check(CarrierScore(shieldPaladin) > CarrierScore(rogue), "supported shield paladin is competitive");
        auto healerDruid = druid;
        healerDruid.healer = healerDruid.soleHealer = true;
        Check(CarrierScore(mage) > CarrierScore(healerDruid), "sole healer stays available for escort");
        CarrierPlanner planner;
        Check(planner.Update({healerDruid}, 100).primary == healerDruid.guid,
            "only surviving healer can still pick up when nobody else exists");
    }

    {
        CarrierPlanner planner;
        auto first = mage;
        auto second = rogue;
        first.mobilityReady = true;
        auto initial = planner.Update({first, second}, 100);
        Check(initial.primary == 2 && initial.backup == 3, "initial ready mobility influences election");
        first.mobilityReady = false;
        second.mobilityReady = second.defenseReady = true;
        for (uint32_t now = 2100; now < 100100; now += 2000)
        {
            const auto selection = planner.Update({second, first}, now);
            Check(selection.primary == 2 && selection.backup == 3, "cooldown readiness alone never churns carriers");
        }
        second.distance = 0;
        first.distance = 100;
        Check(planner.Update({second, first}, 100101).primary == 3, "material better substitute replaces after hold");
    }

    {
        CarrierPlanner planner;
        auto first = mage;
        auto second = rogue;
        first.distance = 0;
        second.distance = 150;
        Check(planner.Update({first, second}, 100).primary == 2, "travel distance contributes to initial election");
        first.distance = 130;
        second.distance = 0;
        Check(planner.Update({first, second}, 2100).primary == 2, "normal better score respects twenty five second hold");
        Check(planner.Update({first, second}, 25100).primary == 3, "large travel advantage replaces after hold");
        second.health = 50;
        Check(planner.Update({first, second}, 27100).primary == 2, "critical health overrides hold immediately");
        first.alive = false;
        Check(planner.Update({first, second}, 29100).primary == 3, "death replaces primary immediately");
        second.eligible = false;
        Check(planner.Update({first, second}, 29101).primary == 0, "loss of all eligibility clears primary");
    }

    {
        CarrierPlanner planner;
        auto equalFirst = rogue;
        auto equalSecond = rogue;
        equalFirst.guid = 10;
        equalSecond.guid = 20;
        Check(planner.Update({equalSecond, equalFirst}, 100).primary == 10, "equal scores use deterministic GUID order");
        Check(planner.Update({equalFirst, equalSecond}, 100000).primary == 10, "roster iteration order does not change selection");
        auto missingMobility = druid;
        missingMobility.mobilityReady = missingMobility.defenseReady = false;
        auto noSpell = CarrierPlanner{}.Update({missingMobility}, 100);
        Check(noSpell.primary == 1, "low level or missing optional skill is still eligible");
    }

    {
        CarrierPlanner planner;
        auto human = druid;
        human.guid = 99;
        human.isBot = false;
        Check(!IsCarrierEligible(human), "humans are never assigned prospective carrier duties");
        Check(planner.Update({human, mage}, 100).primary == 2, "nearby human does not displace assigned bot");
        auto carried = planner.Update({human, mage, druid}, 101, 99);
        Check(carried.primary == 99 && carried.carrier == 99 && carried.backup != 99,
            "already carrying human remains authoritative with distinct bot backup");
        Check(planner.Update({}, 102, 99).carrier == 99, "missing snapshot cannot request a handover from active holder");
        Check(!SelectFlagPicker(carried, {{1, 1, true}}, true), "active carrier suppresses any attempted pickup");
        Check(planner.Update({mage, druid}, 103).primary == 1, "flag no longer carried resumes bot selection immediately");
        auto botCarry = planner.Update({druid, mage}, 104, mage.guid);
        Check(botCarry.primary == mage.guid && botCarry.carrier == mage.guid,
            "a lower ranked actual holder is never replaced automatically");
        planner.Clear();
        Check(planner.Update({}, 105).carrier == 0, "clear forgets previous carrier and duties");
    }

    {
        const CarrierSelection selection{1, 2, 0};
        Check(SelectFlagPicker(selection, {{1, 4, true}, {2, 2, true}, {3, 1, true}}, false) == 1,
            "primary at the flag has first click at base");
        Check(SelectFlagPicker(selection, {{1, 4, false}, {2, 2, true}, {3, 1, true}}, false) == 2,
            "controlled primary immediately yields to eligible backup");
        Check(SelectFlagPicker(selection, {{1, 100, true}, {2, 20, true}, {3, 1, true}}, false) == 3,
            "far designated players never block immediate local fallback");
        Check(SelectFlagPicker(selection, {{1, 4, true}, {2, 2, true}, {3, 1, true}}, true) == 3,
            "dropped flag goes to nearest bot without waiting for druid primary");
        Check(SelectFlagPicker(selection, {{1, 1, false}, {2, 2, true}}, true) == 2,
            "nearest controlled bot cannot block emergency relay");
        Check(SelectFlagPicker(selection, {{4, 1, true}, {3, 1, true}}, true) == 3,
            "emergency distance ties are deterministic");
        Check(SelectFlagPicker(selection, {{1, 6, true}}, false) == 0,
            "out of interaction range bot cannot click");
        Check(SelectFlagPicker(selection, {{1, 6, true}}, false, 8) == 1,
            "runtime can supply core interaction radius explicitly");
        Check(SelectFlagPicker(selection, {}, false) == 0, "no local candidates means no picker");
    }

    {
        for (unsigned field = 0; field < 8; ++field)
        {
            auto invalid = druid;
            switch (field)
            {
                case 0: invalid.guid = 0; break;
                case 1: invalid.alive = false; break;
                case 2: invalid.isBot = false; break;
                case 3: invalid.eligible = false; break;
                case 4: invalid.health = 0; break;
                case 5: invalid.maxHealth = 0; break;
                case 6: invalid.distance = -1; break;
                case 7: invalid.distance = std::numeric_limits<float>::quiet_NaN(); break;
            }
            Check(!IsCarrierEligible(invalid), "invalid carrier snapshot is excluded");
            Check(!std::isfinite(CarrierScore(invalid)), "invalid carrier score cannot outrank a valid bot");
            CarrierPlanner planner;
            Check(planner.Update({invalid, mage}, 100).primary == 2, "invalid druid never wins election");
        }
        const CarrierSelection selection{1, 2, 0};
        Check(SelectFlagPicker(selection, {{1, -1, true}, {2, std::numeric_limits<float>::infinity(), true}}, true) == 0,
            "invalid pickup distances are rejected");
        Check(SelectFlagPicker(selection, {{1, 1, true}}, false, -1) == 0, "invalid pickup radius is rejected");
        CarrierPlanner planner;
        auto incumbent = mage;
        auto replacement = rogue;
        incumbent.distance = 0;
        replacement.distance = 150;
        planner.Update({incumbent, replacement}, std::numeric_limits<uint32_t>::max() - 10000);
        incumbent.distance = 130;
        replacement.distance = 0;
        Check(planner.Update({incumbent, replacement}, 16000).primary == 3,
            "hold elapsed time remains correct across millisecond counter rollover");
    }

    std::cout << "Warsong carrier policy: " << checks << " checks passed\n";
    return EXIT_SUCCESS;
}
