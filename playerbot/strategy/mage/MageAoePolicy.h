#pragma once

#include <cstddef>

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
        bool immediateAttacker = false;
    };

    inline bool IsMageBlizzardCasterReady(const MageBlizzardCasterState& state)
    {
        // Ordinary walking is intentionally allowed: the ground-cast path
        // stops movement, then retries if necessary for an unmastered bot.
        return !state.uncontrolled && !state.jumping && !state.falling && !state.immediateAttacker;
    }

    struct MageBlizzardTargetState
    {
        bool hasGroupTank = false;
        bool victimIsGroupTank = false;
        bool rooted = false;
        bool immediateAttacker = false;
        float mageThreat = 0.0f;
        float tankThreat = 0.0f;
        float distance = 0.0f;
        float meleeReach = 0.0f;
        float runSpeed = 0.0f;
    };

    inline bool IsMageBlizzardTargetReady(const MageBlizzardTargetState& state)
    {
        if (state.immediateAttacker)
            return false;

        if (state.hasGroupTank)
        {
            // Match the existing AOE threat margin, but require this creature
            // to be attacking a tank rather than trusting party composition.
            return state.victimIsGroupTank && state.tankThreat > 0.0f &&
                state.mageThreat < state.tankThreat * 0.5f;
        }

        // Reserve room for several ticks before melee contact. Roots hold the
        // enemy; current run speed naturally accounts for movement slows.
        const float safeDistance = state.meleeReach + state.runSpeed * 3.0f;
        return state.distance > 10.0f && (state.rooted || state.distance >= safeDistance);
    }
}
