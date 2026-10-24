#pragma once

#include <cstddef>
#include <string_view>

namespace ai
{
    constexpr std::size_t MageAoeMinimumTargets = 3;
    constexpr float MageArcaneExplosionRadius = 10.0f;

    inline bool HasMageAoeTargetCount(std::size_t count)
    {
        return count >= MageAoeMinimumTargets;
    }

    inline bool IsMageLevelingAoeAllowed(unsigned level, std::size_t nearbyCount)
    {
        return level < 60 && HasMageAoeTargetCount(nearbyCount);
    }

    inline bool IsMageAoeDamageAction(std::string_view name)
    {
        return name == "arcane explosion" || name == "leveling arcane explosion" ||
            name == "flamestrike" || name == "blizzard";
    }

    inline bool IsMageAoeDisengageAction(std::string_view name)
    {
        return name == "flee" || name == "runaway" || name == "flee with pet" ||
            name == "invisibility" || name == "lesser invisibility" || name == "ice block" ||
            name == "shoot" || name == "evocation" || name == "blink";
    }

    inline bool IsMageFlamestrikeCovered(std::size_t covered, std::size_t total)
    {
        // At least 75% of this pack must already be covered. This formulation
        // rounds the requirement up without overflowing large target counts.
        return total != 0 && covered >= total - total / 4;
    }

    struct MageBlizzardCasterState
    {
        bool uncontrolled = false;
        bool jumping = false;
        bool falling = false;
    };

    inline bool IsMageBlizzardCasterReady(const MageBlizzardCasterState& state)
    {
        // Ordinary walking is intentionally allowed: the ground-cast path
        // stops movement, then retries if necessary for an unmastered bot.
        return !state.uncontrolled && !state.jumping && !state.falling;
    }
}
