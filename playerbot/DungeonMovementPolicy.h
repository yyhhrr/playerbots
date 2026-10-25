#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace ai::dungeon::policy
{
    struct Point { float x, y, z; };
    enum class Mode { Local, Follow };

    inline bool IsFinite(const Point& p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    inline float HorizontalDistance(const Point& a, const Point& b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    inline float Distance(const Point& a, const Point& b)
    {
        return std::hypot(HorizontalDistance(a, b), a.z - b.z);
    }

    inline bool IsGroundSampleSafe(float expectedZ, float actualZ)
    {
        return std::isfinite(expectedZ) && std::isfinite(actualZ) &&
            actualZ > -100000.0f && std::abs(expectedZ - actualZ) <= 2.5f;
    }

    inline bool HasSafeClearance(const std::vector<float>& heights, float centerZ)
    {
        if (heights.empty())
            return false;
        for (float height : heights)
            if (!IsGroundSampleSafe(centerZ, height))
                return false;
        return true;
    }

    inline bool IsSafeRoute(const std::vector<Point>& path, const Point& start,
        const Point& destination, bool complete, Mode mode)
    {
        if (!complete || path.size() < 2 || !IsFinite(start) || !IsFinite(destination))
            return false;
        for (const Point& point : path)
            if (!IsFinite(point))
                return false;

        // A nearest polygon on the floor below is not the requested destination.
        if (HorizontalDistance(path.front(), start) > 1.5f || std::abs(path.front().z - start.z) > 2.0f ||
            HorizontalDistance(path.back(), destination) > 1.0f || std::abs(path.back().z - destination.z) > 1.75f)
            return false;

        float length = 0.0f;
        float routeLow = start.z, routeHigh = start.z;
        const float low = std::min(start.z, destination.z), high = std::max(start.z, destination.z);
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            const float horizontal = HorizontalDistance(path[i - 1], path[i]);
            const float vertical = std::abs(path[i - 1].z - path[i].z);
            // Stairs may rise or descend continuously; vertical links and jumps may not.
            if (vertical > 1.5f + horizontal * 1.2f)
                return false;
            length += Distance(path[i - 1], path[i]);
            routeLow = std::min(routeLow, path[i].z);
            routeHigh = std::max(routeHigh, path[i].z);
            if (mode == Mode::Local && (path[i].z < low - 3.0f || path[i].z > high + 6.0f))
                return false;
        }

        if (mode == Mode::Local)
        {
            const float direct = Distance(start, destination);
            // A flat corridor may turn around a wall. The detour limit protects
            // against changing floors, not normal same-floor corners.
            if (routeHigh - routeLow > 4.0f && length > std::max(20.0f, direct * 2.0f + 8.0f))
                return false;
            // Stacked platforms are close in space but require a stairway detour.
            // A direct, continuous ramp is still accepted, regardless of total rise.
            if (std::abs(start.z - destination.z) > 4.0f &&
                HorizontalDistance(start, destination) < std::abs(start.z - destination.z) * 1.5f &&
                length > direct * 1.25f + 3.0f)
                return false;
        }
        return true;
    }
}
