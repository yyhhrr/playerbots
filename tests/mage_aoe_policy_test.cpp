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
    Check(IsMageBlizzardCasterReady(caster), "a caster can channel without a tank, threat margin or distance requirement");

    for (const char* action : {"arcane explosion", "leveling arcane explosion", "flamestrike", "blizzard"})
        Check(IsMageAoeDamageAction(action), std::string(action) + " participates in persistent AOE output");
    for (const char* action : {"frostbolt", "fireball", "cone of cold", "healing potion"})
        Check(!IsMageAoeDamageAction(action), std::string(action) + " retains ordinary threat rules");
    for (const char* action : {"flee", "runaway", "flee with pet", "invisibility", "lesser invisibility", "ice block", "shoot", "evocation", "blink"})
        Check(IsMageAoeDisengageAction(action), std::string(action) + " cannot interrupt an available AOE opportunity");
    for (const char* action : {"healing potion", "mana potion", "mana gem", "mana shield", "ice barrier", "frost nova", "counterspell", "follow", "select new target"})
        Check(!IsMageAoeDisengageAction(action), std::string(action) + " remains available during AOE");
    std::cout << checks << " mage AOE policy checks passed\n";
    return EXIT_SUCCESS;
}
