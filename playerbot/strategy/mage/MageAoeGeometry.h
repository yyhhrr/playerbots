#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <vector>

namespace ai
{
namespace MageAoeGeometry
{
    struct Point
    {
        float x;
        float y;
        float z;
    };

    inline bool IsFinite(const Point& point)
    {
        return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    }

    inline bool IsWithinRadius(const Point& center, const Point& target, float radius)
    {
        if (!IsFinite(center) || !IsFinite(target) || !std::isfinite(radius) || radius < 0.0f)
            return false;

        const double dx = double(center.x) - target.x;
        const double dy = double(center.y) - target.y;
        const double dz = double(center.z) - target.z;
        const double radiusSquared = double(radius) * radius;
        // Intersection points are converted back to floats. Allow their rounding error,
        // without expanding the spell radius by a creature's combat reach.
        const double tolerance = radiusSquared * 0.000001;
        return dx * dx + dy * dy + dz * dz <= radiusSquared + tolerance;
    }

    namespace Detail
    {
        template<class AddPoint>
        inline void AddCircleIntersections(double firstX, double firstY, double firstRadius,
            double secondX, double secondY, double secondRadius, double z, AddPoint& addPoint)
        {
            const double dx = secondX - firstX;
            const double dy = secondY - firstY;
            const double distanceSquared = dx * dx + dy * dy;
            if (distanceSquared == 0.0)
                return;

            const double distance = std::sqrt(distanceSquared);
            const double tolerance = std::max(firstRadius, secondRadius) * 0.0000001;
            if (distance > firstRadius + secondRadius + tolerance ||
                distance < std::abs(firstRadius - secondRadius) - tolerance)
                return;

            const double along = (firstRadius * firstRadius - secondRadius * secondRadius +
                distanceSquared) / (2.0 * distance);
            const double heightSquared = firstRadius * firstRadius - along * along;
            if (heightSquared < -tolerance * std::max(1.0, firstRadius))
                return;

            const double height = std::sqrt(std::max(0.0, heightSquared));
            const double middleX = firstX + dx * along / distance;
            const double middleY = firstY + dy * along / distance;
            const double offsetX = -dy * height / distance;
            const double offsetY = dx * height / distance;
            addPoint(middleX + offsetX, middleY + offsetY, z);
            addPoint(middleX - offsetX, middleY - offsetY, z);
        }
    }

    // Candidates are deterministic, finite and in casting range before terrain
    // adjustment. Callers must snap to ground, check range/LOS and recount afterward.
    // Pair circles are exact on level terrain; their midpoint height also provides
    // useful candidates on slopes without counting packs on other floors.
    inline std::vector<Point> BuildCandidates(const std::vector<Point>& targets,
        const Point& caster, float radius, float maxRange)
    {
        std::vector<Point> candidates;
        if (!IsFinite(caster) || !std::isfinite(radius) || radius < 0.0f ||
            !std::isfinite(maxRange) || maxRange < 0.0f)
            return candidates;

        const auto lessPoint = [](const Point& left, const Point& right)
        {
            return std::tie(left.x, left.y, left.z) < std::tie(right.x, right.y, right.z);
        };
        std::vector<Point> validTargets;
        for (const Point& target : targets)
        {
            if (IsFinite(target))
                validTargets.push_back(target);
        }
        std::sort(validTargets.begin(), validTargets.end(), lessPoint);
        validTargets.erase(std::unique(validTargets.begin(), validTargets.end(),
            [](const Point& left, const Point& right)
            {
                return left.x == right.x && left.y == right.y && left.z == right.z;
            }), validTargets.end());
        if (validTargets.empty())
            return candidates;

        // Quantized keys remove the many repeated centers produced by dense packs.
        // Double keys avoid integer conversion overflow for malformed coordinates.
        std::set<std::tuple<double, double, double>> seen;
        const auto addPoint = [&](double x, double y, double z)
        {
            const double maximum = std::numeric_limits<float>::max();
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
                std::abs(x) > maximum || std::abs(y) > maximum || std::abs(z) > maximum)
                return;

            const Point point{float(x), float(y), float(z)};
            if (!IsWithinRadius(caster, point, maxRange))
                return;

            const auto key = std::make_tuple(std::round(x * 10000.0),
                std::round(y * 10000.0), std::round(z * 10000.0));
            if (seen.insert(key).second)
                candidates.push_back(point);
        };

        addPoint(caster.x, caster.y, caster.z);
        for (const Point& target : validTargets)
        {
            addPoint(target.x, target.y, target.z);

            const double height = double(target.z) - caster.z;
            const double rangeSquaredAtHeight = double(maxRange) * maxRange - height * height;
            if (rangeSquaredAtHeight < 0.0)
                continue;

            const double rangeAtHeight = std::sqrt(rangeSquaredAtHeight);
            const double dx = double(target.x) - caster.x;
            const double dy = double(target.y) - caster.y;
            const double horizontalDistance = std::hypot(dx, dy);
            if (horizontalDistance > 0.0)
            {
                const double fraction = std::min(1.0, rangeAtHeight / horizontalDistance);
                addPoint(caster.x + dx * fraction, caster.y + dy * fraction, target.z);
            }
            Detail::AddCircleIntersections(caster.x, caster.y, rangeAtHeight,
                target.x, target.y, radius, target.z, addPoint);
        }

        for (size_t first = 0; first < validTargets.size(); ++first)
        {
            for (size_t second = first + 1; second < validTargets.size(); ++second)
            {
                const Point& left = validTargets[first];
                const Point& right = validTargets[second];
                const double dx = double(left.x) - right.x;
                const double dy = double(left.y) - right.y;
                const double dz = double(left.z) - right.z;
                if (dx * dx + dy * dy + dz * dz > 4.0 * double(radius) * radius)
                    continue;

                const double z = (double(left.z) + right.z) * 0.5;
                addPoint((double(left.x) + right.x) * 0.5,
                    (double(left.y) + right.y) * 0.5, z);
                const double radiusSquaredAtHeight = double(radius) * radius - dz * dz * 0.25;
                Detail::AddCircleIntersections(left.x, left.y,
                    std::sqrt(std::max(0.0, radiusSquaredAtHeight)), right.x, right.y,
                    std::sqrt(std::max(0.0, radiusSquaredAtHeight)), z, addPoint);
            }
        }

        std::sort(candidates.begin(), candidates.end(), lessPoint);
        return candidates;
    }
}
}
