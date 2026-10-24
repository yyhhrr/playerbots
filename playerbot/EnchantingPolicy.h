#pragma once

#include <cstdint>
#include <limits>

namespace ai::enchanting
{
    constexpr std::uint32_t UnknownRequiredSkill = std::numeric_limits<std::uint32_t>::max();

    inline bool MatchesMask(std::uint32_t mask, unsigned value)
    {
        return mask == 0 || (value < 32 && (mask & (std::uint32_t(1) << value)) != 0);
    }

    inline bool MatchesItemRequirements(int requiredClass, std::uint32_t subclassMask,
        std::uint32_t inventoryMask, unsigned itemClass, unsigned itemSubclass, unsigned inventoryType)
    {
        return (requiredClass == -1 ||
            (unsigned(requiredClass) == itemClass && MatchesMask(subclassMask, itemSubclass))) &&
            MatchesMask(inventoryMask, inventoryType);
    }

    inline bool IsMaxLevelOnlySlot(unsigned inventoryType)
    {
        // ItemPrototype inventory types: head, shoulder and legs.
        return inventoryType == 1 || inventoryType == 3 || inventoryType == 7;
    }

    inline bool IsEnchantmentAllowed(unsigned level, unsigned maximumLevel, unsigned quality,
        unsigned inventoryType, std::uint32_t requiredSkill)
    {
        // Grey/white items never receive automated permanent enchants.
        if (quality < 2 || level == 0)
            return false;
        if ((IsMaxLevelOnlySlot(inventoryType) || requiredSkill == 0) && level < maximumLevel)
            return false;
        // A zero requirement identifies an item enhancement rather than a
        // profession recipe. Missing profession requirements fail closed below 60.
        return level >= 60 || (requiredSkill != UnknownRequiredSkill && requiredSkill <= level * 5);
    }
}
