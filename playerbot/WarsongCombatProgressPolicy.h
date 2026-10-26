#pragma once

#include <cmath>
#include <cstdint>
#include <map>

namespace ai::warsong
{
    constexpr std::uint32_t PursuitStallDelay = 6000;
    constexpr std::uint32_t PursuitRetryDelay = 8000;

    struct PursuitObservation
    {
        std::uint64_t target = 0;
        float x = 0, y = 0, z = 0, distance = 0;
        std::uint32_t spline = 0;
        float elapsed = 0;
        bool routeToTarget = false;
    };

    struct CombatProgress
    {
        std::uint32_t instance = 0;
        bool active = false, tracking = false, sampled = false;
        PursuitObservation previous;
        float x = 0, y = 0, z = 0, distance = 0;
        std::uint32_t progressed = 0;
        std::uint32_t sampledAt = 0;
        std::map<std::uint64_t, std::uint32_t> suppressed;

        void ResetPursuit()
        {
            tracking = false;
            previous = {};
        }

        bool IsSuppressed(std::uint64_t target, std::uint32_t now) const
        {
            const auto found = suppressed.find(target);
            return target && found != suppressed.end() &&
                std::uint32_t(now - found->second) < PursuitRetryDelay;
        }

        void Prune(std::uint32_t now)
        {
            for (auto it = suppressed.begin(); it != suppressed.end();)
                if (std::uint32_t(now - it->second) >= PursuitRetryDelay)
                    it = suppressed.erase(it);
                else
                    ++it;
        }

        void Suppress(std::uint64_t target, std::uint32_t now)
        {
            Prune(now);
            if (suppressed.size() >= 64 && !suppressed.count(target))
            {
                auto oldest = suppressed.begin();
                for (auto it = suppressed.begin(); it != suppressed.end(); ++it)
                    if (std::uint32_t(now - it->second) > std::uint32_t(now - oldest->second))
                        oldest = it;
                suppressed.erase(oldest);
            }
            suppressed[target] = now;
            ResetPursuit();
        }

        // Movement flags and accepted chase commands are not proof of progress.
        // A route may initially lead away around a ramp, but only a real advance
        // along the same non-cyclic spline ending near the target counts then.
        bool Observe(const PursuitObservation& next, std::uint32_t now)
        {
            const float displacement = (next.x - x) * (next.x - x) +
                (next.y - y) * (next.y - y) + (next.z - z) * (next.z - z);
            const bool closer = next.distance + 1.0f <= distance;
            const bool routeProgress = next.routeToTarget && next.spline &&
                next.spline == previous.spline && next.elapsed > previous.elapsed &&
                displacement >= 1.0f;
            if (!tracking || next.target != previous.target || closer || routeProgress)
            {
                tracking = true;
                x = next.x; y = next.y; z = next.z;
                distance = next.distance;
                progressed = now;
            }
            previous = next;
            return std::uint32_t(now - progressed) >= PursuitStallDelay;
        }
    };
}
