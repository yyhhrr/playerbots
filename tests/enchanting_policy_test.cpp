#include "playerbot/EnchantingPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    unsigned checks = 0;
    void Check(bool condition, const std::string& reason)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << reason << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace ai::enchanting;

    Check(MatchesMask(0, 13), "zero mask means unrestricted inventory type");
    Check(MatchesMask(1u << 7, 7), "a matching subclass is accepted");
    Check(!MatchesMask(1u << 7, 8), "an excluded subclass is rejected");
    Check(!MatchesMask(1u, 32), "out-of-mask values cannot cause an invalid shift");
    Check(MatchesItemRequirements(2, 1u << 7, 0, 2, 7, 13),
        "weapon enchants with an unrestricted inventory mask accept a sword");
    Check(!MatchesItemRequirements(2, 1u << 7, 0, 4, 7, 13),
        "a weapon enchant cannot accept armor even when subclass bits match");
    Check(!MatchesItemRequirements(2, 1u << 7, 0, 2, 8, 13),
        "a zero inventory mask does not bypass the weapon subclass mask");
    Check(MatchesItemRequirements(4, 0, 1u << 10, 4, 1, 10),
        "unrestricted armor subclasses still honor an exact glove inventory type");
    Check(!MatchesItemRequirements(4, 0, 1u << 10, 4, 1, 11),
        "glove enchants cannot attach to a ring");
    Check(MatchesItemRequirements(-1, 1u << 2, 0, 2, 7, 13),
        "unrestricted item class follows the core semantics for subclass masks");

    for (unsigned quality : {0u, 1u})
    {
        Check(!IsEnchantmentAllowed(33, 60, quality, 13, 1),
            "grey and white equipment reject level-appropriate permanent enchants");
        Check(!IsEnchantmentAllowed(60, 60, quality, 13, 300),
            "maximum level does not permit white or grey equipment");
    }
    for (unsigned quality : {2u, 3u, 4u, 5u})
        Check(IsEnchantmentAllowed(33, 60, quality, 13, 165),
            "green and better equipment can receive an appropriate enchant");
    Check(!IsEnchantmentAllowed(0, 60, 2, 13, 1), "invalid zero character level is rejected");

    for (unsigned level : {1u, 10u, 19u, 33u, 59u})
    {
        Check(IsEnchantmentAllowed(level, 60, 2, 13, level * 5),
            "the exact character-level-times-five skill boundary is accepted");
        Check(!IsEnchantmentAllowed(level, 60, 2, 13, level * 5 + 1),
            "a skill requirement one point above the level boundary is rejected");
        Check(!IsEnchantmentAllowed(level, 60, 2, 13, UnknownRequiredSkill),
            "unknown profession requirements fail closed below level sixty");
    }
    Check(!IsEnchantmentAllowed(59, 60, 2, 13, 300), "level fifty-nine cannot receive a 300-skill enchant");
    Check(IsEnchantmentAllowed(60, 60, 2, 13, 300), "level sixty can receive a 300-skill enchant");
    Check(IsEnchantmentAllowed(60, 60, 2, 13, UnknownRequiredSkill),
        "the explicit below-sixty skill ceiling does not apply at level sixty");

    for (unsigned inventoryType : {1u, 3u, 7u})
    {
        Check(IsMaxLevelOnlySlot(inventoryType), "head, shoulder and leg inventory types are recognized");
        Check(!IsEnchantmentAllowed(59, 60, 2, inventoryType, 1),
            "head, shoulder and leg targets stay blocked even for low-skill spells before maximum level");
        Check(!IsEnchantmentAllowed(59, 60, 2, inventoryType, 0),
            "item-based head, shoulder and leg enhancements require maximum level");
        Check(IsEnchantmentAllowed(60, 60, 2, inventoryType, 0),
            "item-based head, shoulder and leg enhancements are allowed at maximum level");
        Check(!IsEnchantmentAllowed(60, 80, 2, inventoryType, 0),
            "item enhancements respect the expansion maximum rather than hardcoding level sixty");
        Check(IsEnchantmentAllowed(80, 80, 2, inventoryType, 0),
            "item enhancements become available at a higher expansion maximum");
    }
    Check(!IsMaxLevelOnlySlot(5), "chest is not a maximum-level-only slot");
    Check(IsEnchantmentAllowed(59, 60, 2, 5, 295), "ordinary chest enchants remain available below maximum level");
    Check(!IsEnchantmentAllowed(59, 60, 2, 5, 0), "an item-based enhancement does not bypass the maximum-level rule through another slot");

    std::cout << "Enchanting policy: " << checks << " checks passed\n";
    return EXIT_SUCCESS;
}
