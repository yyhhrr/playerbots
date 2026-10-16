#include "playerbot/strategy/mage/MageAoeGeometry.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using ai::MageAoeGeometry::BuildCandidates;
using ai::MageAoeGeometry::IsWithinRadius;
using ai::MageAoeGeometry::Point;

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

    size_t Count(const std::vector<Point>& targets, const Point& center, float radius)
    {
        return size_t(std::count_if(targets.begin(), targets.end(), [&](const Point& target)
        {
            return IsWithinRadius(center, target, radius);
        }));
    }

    size_t BestCount(const std::vector<Point>& targets, const Point& caster,
        float radius, float range)
    {
        size_t best = 0;
        for (const Point& center : BuildCandidates(targets, caster, radius, range))
            best = std::max(best, Count(targets, center, radius));
        return best;
    }

    bool SameCandidates(const std::vector<Point>& left, const std::vector<Point>& right)
    {
        if (left.size() != right.size())
            return false;
        for (size_t i = 0; i < left.size(); ++i)
        {
            if (left[i].x != right[i].x || left[i].y != right[i].y || left[i].z != right[i].z)
                return false;
        }
        return true;
    }
}

int main()
{
    const Point caster{0.0f, 0.0f, 0.0f};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    std::vector<Point> smallPack{{20.0f, 0.0f, 0.0f}, {21.0f, 0.0f, 0.0f}};
    Check(BestCount(smallPack, caster, 10.0f, 30.0f) == 2,
        "two enemies remain below the three-enemy remote threshold");
    smallPack.push_back({20.0f, 1.0f, 0.0f});
    Check(BestCount(smallPack, caster, 10.0f, 30.0f) == 3,
        "three enemies inside one spell radius satisfy the remote threshold");

    std::vector<Point> pack{{20.0f, 0.0f, 0.0f}, {21.0f, 0.0f, 0.0f},
        {20.0f, 1.0f, 0.0f}, {21.0f, 1.0f, 0.0f}};
    Check(BestCount(pack, caster, 10.0f, 30.0f) == 4,
        "four enemies are counted exactly");
    pack.push_back({20.5f, 0.5f, 0.0f});
    Check(BestCount(pack, caster, 10.0f, 30.0f) == 5,
        "five enemies are counted exactly");

    const std::vector<Point> split{{15.0f, -16.0f, 0.0f}, {16.0f, -16.0f, 0.0f},
        {15.0f, -17.0f, 0.0f}, {15.0f, 16.0f, 0.0f}, {16.0f, 16.0f, 0.0f},
        {15.0f, 17.0f, 0.0f}};
    Check(BestCount(split, caster, 8.0f, 40.0f) == 3,
        "separate packs are not combined into a fictitious six-enemy AOE");

    // A regular pentagon fits in one radius-5 circle, but centering on any
    // monster hits only itself and its two neighbours. Its diagonals also
    // have midpoints too far from the opposite vertex, requiring circle centers.
    std::vector<Point> ring;
    for (int i = 0; i < 5; ++i)
    {
        const double angle = i * 2.0 * 3.14159265358979323846 / 5.0;
        ring.push_back({float(20.0 + 4.9 * std::cos(angle)),
            float(4.9 * std::sin(angle)), 0.0f});
    }
    size_t monsterCenteredBest = 0;
    size_t midpointBest = 0;
    for (const Point& target : ring)
        monsterCenteredBest = std::max(monsterCenteredBest, Count(ring, target, 5.0f));
    for (size_t first = 0; first < ring.size(); ++first)
        for (size_t second = first + 1; second < ring.size(); ++second)
            midpointBest = std::max(midpointBest, Count(ring,
                {(ring[first].x + ring[second].x) / 2.0f,
                    (ring[first].y + ring[second].y) / 2.0f, 0.0f}, 5.0f));
    Check(monsterCenteredBest < 5 && midpointBest < 5,
        "intersection fixture cannot be solved by targeting a monster or pair midpoint");
    Check(BestCount(ring, caster, 5.0f, 30.0f) == 5,
        "pair circle intersections find the common ground AOE center");

    const std::vector<Point> edge{{33.0f, 0.0f, 0.0f}, {33.0f, 1.0f, 0.0f},
        {33.0f, -1.0f, 0.0f}, {34.0f, 0.0f, 0.0f}, {32.0f, 0.0f, 0.0f}};
    Check(BestCount(edge, caster, 5.0f, 30.0f) == 5,
        "cast-range boundary centers reach a pack whose monsters are all out of cast range");
    for (const Point& point : BuildCandidates(edge, caster, 5.0f, 30.0f))
        Check(IsWithinRadius(caster, point, 30.0f), "all returned centers are in casting range");

    // The best center lies where a target's AOE disk meets the casting boundary,
    // not at its nearest radial projection. A target-centered search misses it.
    const std::vector<Point> boundaryIntersection{{32.0f, 4.0f, 0.0f},
        {28.0f, 8.5f, 0.0f}, {28.5f, 8.5f, 0.0f},
        {29.0f, 8.0f, 0.0f}, {29.5f, 8.0f, 0.0f}};
    Check(BestCount(boundaryIntersection, caster, 5.0f, 30.0f) == 5,
        "off-axis pack remains targetable near the casting boundary");

    const std::vector<Point> floors{{20.0f, 0.0f, 0.0f}, {20.0f, 1.0f, 0.0f},
        {21.0f, 0.0f, 0.0f}, {20.0f, 0.0f, 25.0f}, {20.0f, 1.0f, 25.0f}};
    Check(BestCount(floors, caster, 10.0f, 40.0f) == 3,
        "enemies on vertically separated floors do not count as one pack");
    Check(!IsWithinRadius(caster, {0.0f, 0.0f, 10.1f}, 10.0f),
        "height contributes to radius checks");
    Check(IsWithinRadius(caster, {0.0f, 0.0f, 10.0f}, 10.0f),
        "a target exactly on the sphere boundary is included");

    const std::vector<Point> axis{{0.0f, 10.0f, 0.0f}, {0.0f, 11.0f, 0.0f},
        {0.0f, 12.0f, 0.0f}, {0.0f, 13.0f, 0.0f}, {0.0f, 14.0f, 0.0f}};
    Check(BestCount(axis, caster, 5.0f, 30.0f) == 5,
        "a valid zero X coordinate is not interpreted as an unset target");
    Check(BuildCandidates({}, caster, 10.0f, 30.0f).empty(), "empty input has no candidates");
    Check(BuildCandidates({{nan, 0.0f, 0.0f}}, caster, 10.0f, 30.0f).empty(),
        "non-finite targets are rejected");
    Check(BuildCandidates(pack, {0.0f, infinity, 0.0f}, 10.0f, 30.0f).empty(),
        "non-finite caster coordinates are rejected");
    Check(BuildCandidates(pack, caster, -1.0f, 30.0f).empty(), "negative radius is rejected");
    Check(BuildCandidates(pack, caster, 10.0f, -1.0f).empty(), "negative casting range is rejected");
    Check(BuildCandidates(pack, caster, infinity, 30.0f).empty(), "infinite radius is rejected");
    Check(BuildCandidates(pack, caster, 10.0f, nan).empty(), "NaN casting range is rejected");
    Check(!IsWithinRadius(caster, {nan, 0.0f, 0.0f}, 10.0f),
        "non-finite radius-test coordinates are rejected");
    Check(!IsWithinRadius(caster, caster, nan), "NaN radius checks fail closed");

    const std::vector<Point> duplicates(5, {10.0f, 10.0f, 0.0f});
    Check(BestCount(duplicates, caster, 0.0f, 30.0f) == 5,
        "coincident enemies and a zero radius do not divide by zero");
    const auto zeroRange = BuildCandidates(pack, caster, 10.0f, 0.0f);
    Check(zeroRange.size() == 1 && IsWithinRadius(caster, zeroRange.front(), 0.0f),
        "zero casting range only permits the caster position");

    const auto originalCandidates = BuildCandidates(split, caster, 8.0f, 40.0f);
    auto reversed = split;
    std::reverse(reversed.begin(), reversed.end());
    Check(SameCandidates(originalCandidates, BuildCandidates(reversed, caster, 8.0f, 40.0f)),
        "candidate ordering and equal-score choices are stable under target input permutation");
    auto invalidMixed = split;
    invalidMixed.push_back({nan, 1.0f, 2.0f});
    Check(SameCandidates(originalCandidates, BuildCandidates(invalidMixed, caster, 8.0f, 40.0f)),
        "invalid targets do not perturb stable valid candidates");

    std::cout << "Mage AOE geometry: " << checks << " checks passed\n";
    return EXIT_SUCCESS;
}
