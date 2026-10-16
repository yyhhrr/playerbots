#include "playerbot/strategy/mage/MageAoePolicy.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const std::string& description)
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
    using namespace ai;

    Check(!HasMageAoeTargetCount(0), "no targets cannot start AOE");
    Check(!HasMageAoeTargetCount(2), "two targets remain below the AOE threshold");
    Check(HasMageAoeTargetCount(3), "three targets enable AOE");
    Check(HasMageAoeTargetCount(4), "additional targets continue to enable AOE");
    Check(!IsMageLevelingAoeAllowed(59, 2), "a leveling mage with two nearby foes cannot start the new explosion");
    Check(IsMageLevelingAoeAllowed(59, 3), "a level fifty-nine mage with three nearby foes can start the explosion");
    Check(!IsMageLevelingAoeAllowed(60, 3), "level sixty is excluded from the leveling explosion rule");
    Check(!IsMageLevelingAoeAllowed(61, 3), "levels above sixty remain excluded");
    Check(!IsMageLevelingAoeAllowed(60, 20), "more nearby foes cannot bypass the level cutoff");

    Check(!IsMageFlamestrikeCovered(0, 0), "an empty area is not an existing pack");
    Check(!IsMageFlamestrikeCovered(0, 3), "own far-away or another mage's area does not cover this pack");
    Check(!IsMageFlamestrikeCovered(2, 3), "two of three covered targets permit a useful new Flamestrike");
    Check(IsMageFlamestrikeCovered(3, 3), "covering the same three targets prevents repeat Flamestrike");
    Check(IsMageFlamestrikeCovered(3, 4), "exactly seventy-five percent existing coverage prevents repeat");
    Check(!IsMageFlamestrikeCovered(3, 5), "partial coverage below seventy-five percent permits a new area");
    Check(IsMageFlamestrikeCovered(4, 5), "fractional coverage requirement rounds up");
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    Check(IsMageFlamestrikeCovered(maximum, maximum), "coverage comparison does not overflow");

    MageBlizzardCasterState caster;
    Check(IsMageBlizzardCasterReady(caster), "a safe caster can stop ordinary walking and start immediately");
    caster.uncontrolled = true;
    Check(!IsMageBlizzardCasterReady(caster), "loss of control prevents a channel");
    caster = {};
    caster.jumping = true;
    Check(!IsMageBlizzardCasterReady(caster), "jumping prevents a channel");
    caster = {};
    caster.falling = true;
    Check(!IsMageBlizzardCasterReady(caster), "falling prevents a channel");
    caster = {};
    caster.immediateAttacker = true;
    Check(!IsMageBlizzardCasterReady(caster), "an immediate attacker outside the selected pack blocks a channel");

    MageBlizzardTargetState tanked;
    tanked.hasGroupTank = true;
    tanked.victimIsGroupTank = true;
    tanked.tankThreat = 100.0f;
    tanked.mageThreat = 49.0f;
    Check(IsMageBlizzardTargetReady(tanked), "forty-nine percent tank threat allows Blizzard without a combat timer");
    tanked.mageThreat = 50.0f;
    Check(!IsMageBlizzardTargetReady(tanked), "fifty percent tank threat blocks Blizzard");
    tanked.mageThreat = 0.0f;
    tanked.tankThreat = 0.0f;
    Check(!IsMageBlizzardTargetReady(tanked), "zero-threat tank has not established control");
    tanked.tankThreat = 100.0f;
    tanked.victimIsGroupTank = false;
    Check(!IsMageBlizzardTargetReady(tanked), "missing or non-tank victim cannot satisfy party tank safety");
    tanked.victimIsGroupTank = true;
    tanked.immediateAttacker = true;
    Check(!IsMageBlizzardTargetReady(tanked), "an immediate attacker blocks even with sufficient tank threat");

    MageBlizzardTargetState solo;
    solo.distance = 15.0f;
    solo.meleeReach = 3.0f;
    solo.runSpeed = 7.0f;
    solo.rooted = true;
    Check(IsMageBlizzardTargetReady(solo), "a safely distant rooted pack allows solo Blizzard");
    solo.distance = 10.0f;
    Check(!IsMageBlizzardTargetReady(solo), "a root does not permit a channel inside the immediate danger margin");
    solo.distance = 9.9f;
    Check(!IsMageBlizzardTargetReady(solo), "a nearby rooted pack remains unsafe");
    solo.rooted = false;
    solo.distance = 23.9f;
    Check(!IsMageBlizzardTargetReady(solo), "ordinary running speed needs sufficient time before contact");
    solo.distance = 24.0f;
    Check(IsMageBlizzardTargetReady(solo), "exact speed-based safe separation permits useful channel ticks");
    solo.distance = 15.0f;
    Check(!IsMageBlizzardTargetReady(solo), "an unslowed nearby pack does not permit a solo channel");
    solo.runSpeed = 3.5f;
    Check(IsMageBlizzardTargetReady(solo), "a slowed pack needs less separation than a running pack");
    solo.immediateAttacker = true;
    Check(!IsMageBlizzardTargetReady(solo), "a close attacker overrides the slower approach speed");

    std::cout << checks << " mage AOE policy checks passed\n";
    return EXIT_SUCCESS;
}
