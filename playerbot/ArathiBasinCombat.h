#pragma once

#include <cstdint>

namespace ai
{
    constexpr float ArathiDefenderRadius = 40.0f;
    constexpr float ArathiSelfDefenseRadius = 10.0f;

    // AB event states: neutral = 0, alliance/horde contested = 1/2,
    // alliance/horde occupied = 3/4. Team indices are alliance = 0, horde = 1.
    inline bool CanAssaultArathiBanner(unsigned node, unsigned state, unsigned attackerTeam)
    {
        return node < 5 && state <= 4 && attackerTeam < 2 &&
            (state == 0 || attackerTeam == state % 2);
    }

    inline bool IsArathiDefenderTargetAllowed(bool defending, bool hasObjective,
        float distanceFromObjective, float distanceFromDefender, bool directlyAttackingDefender,
        bool playerControlled = true)
    {
        if (!defending || !hasObjective || !playerControlled)
            return true;

        // A nearby direct attacker can still be fought while moving back to the flag.
        // Threat history or an owner's distant pet must not turn into a pursuit exemption.
        return distanceFromObjective <= ArathiDefenderRadius ||
            (directlyAttackingDefender && distanceFromDefender <= ArathiSelfDefenseRadius);
    }

    struct ArathiCombatCandidate
    {
        bool capturingBanner = false;
        float distance = 0.0f;
        std::uint32_t health = 0;
    };

    inline bool IsBetterArathiCombatTarget(const ArathiCombatCandidate& candidate,
        const ArathiCombatCandidate& current, bool melee)
    {
        if (candidate.capturingBanner != current.capturingBanner)
            return candidate.capturingBanner;

        // Reach the nearest active flag capture first, independent of class role.
        if (candidate.capturingBanner || melee)
            return candidate.distance < current.distance;

        return candidate.health < current.health;
    }
}
