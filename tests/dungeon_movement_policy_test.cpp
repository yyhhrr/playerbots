#include "playerbot/DungeonMovementPolicy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    unsigned checks = 0;
    void Check(bool passed, const std::string& reason)
    {
        ++checks;
        if (!passed)
        {
            std::cerr << "FAILED: " << reason << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace ai::dungeon::policy;
    const Point start{0.0f, 0.0f, 20.0f}, end{10.0f, 0.0f, 20.0f};
    const std::vector<Point> flat{start, {5.0f, 0.0f, 20.0f}, end};
    Check(IsSafeRoute(flat, start, end, true, Mode::Local), "a complete same-platform local route is accepted");
    Check(IsSafeRoute(flat, start, end, true, Mode::Follow), "a complete same-platform follow route is accepted");
    const std::vector<Point> corner{start, {0, 25, 20}, {10, 25, 20}, end};
    Check(IsSafeRoute(corner, start, end, true, Mode::Local),
        "same-floor pursuit can follow a long U-shaped corridor around a wall");
    Check(!IsSafeRoute(flat, start, end, false, Mode::Local), "an incomplete route is not accepted as local reachability");
    Check(!IsSafeRoute(flat, start, end, false, Mode::Follow), "following cannot invent the missing end of a partial route");
    Check(!IsSafeRoute({}, start, end, true, Mode::Local), "an empty route cannot prove platform safety");
    Check(!IsSafeRoute({start}, start, end, true, Mode::Local), "one path point cannot reach a different destination");
    Check(!IsSafeRoute({start, {10.0f, 0.0f, 0.0f}}, start, end, true, Mode::Follow),
        "a path snapped to the floor below does not match the intended destination");
    Check(!IsSafeRoute({start, {8.0f, 0.0f, 20.0f}}, start, end, true, Mode::Local),
        "a route ending short does not prove the requested endpoint reachable");

    const Point lower{2.0f, 0.0f, 0.0f};
    Check(!IsSafeRoute({start, lower}, start, lower, true, Mode::Local), "a direct platform drop is rejected");
    Check(!IsSafeRoute({start, lower}, start, lower, true, Mode::Follow), "following does not excuse an unsupported vertical drop");
    const Point upper{2.0f, 0.0f, 40.0f};
    Check(!IsSafeRoute({start, upper}, start, upper, true, Mode::Follow), "a vertical shortcut up to an overlapping platform is rejected");

    std::vector<Point> stairs;
    for (unsigned i = 0; i <= 10; ++i)
        stairs.push_back({float(i * 2), 0.0f, 20.0f + float(i)});
    Check(IsSafeRoute(stairs, stairs.front(), stairs.back(), true, Mode::Follow),
        "a continuously navigable staircase can climb more than a flat height cutoff");
    std::reverse(stairs.begin(), stairs.end());
    Check(IsSafeRoute(stairs, stairs.front(), stairs.back(), true, Mode::Follow),
        "a continuous staircase remains usable when descending");

    const std::vector<Point> ramp{{0, 0, 20}, {4, 0, 21}, {8, 0, 22}, {12, 0, 23}, {16, 0, 24}};
    Check(IsSafeRoute(ramp, ramp.front(), ramp.back(), true, Mode::Follow), "a smooth ramp is accepted");
    const std::vector<Point> cliff{{0, 0, 20}, {4, 0, 20}, {4.5f, 0, 9}, {8, 0, 9}};
    Check(!IsSafeRoute(cliff, cliff.front(), cliff.back(), true, Mode::Follow),
        "a sudden drop embedded in an otherwise normal route is rejected");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!IsSafeRoute({start, {nan, 0, 20}, end}, start, end, true, Mode::Follow), "non-finite route coordinates cannot be navigated");
    Check(!IsSafeRoute(flat, start, {nan, 0, 20}, true, Mode::Local), "a non-finite requested endpoint is rejected");

    std::vector<Point> stairDetour{start};
    const auto addSegment = [&](Point destination)
    {
        const Point from = stairDetour.back();
        const unsigned steps = std::max(1u, unsigned(std::ceil(Distance(from, destination) / 2.0f)));
        for (unsigned i = 1; i <= steps; ++i)
        {
            const float fraction = float(i) / steps;
            stairDetour.push_back({from.x + (destination.x - from.x) * fraction,
                from.y + (destination.y - from.y) * fraction, from.z + (destination.z - from.z) * fraction});
        }
    };
    addSegment({0, 30, 20});
    addSegment({40, 30, 0});
    addSegment({6, 0, 0});
    Check(!IsSafeRoute(stairDetour, stairDetour.front(), stairDetour.back(), true, Mode::Local),
        "autonomous targeting cannot detour around a nearby overlapping lower platform");
    Check(IsSafeRoute(stairDetour, stairDetour.front(), stairDetour.back(), true, Mode::Follow),
        "a master can deliberately lead through the same continuous stairway route");
    addSegment({40, -30, 20});
    addSegment({6, 0, 20});
    Check(!IsSafeRoute(stairDetour, stairDetour.front(), stairDetour.back(), true, Mode::Local),
        "a same-height local goal cannot excuse descending to another floor and returning");

    Check(IsGroundSampleSafe(20.0f, 20.0f), "ground at the route height supports movement");
    Check(IsGroundSampleSafe(20.0f, 20.2f), "small stair and mesh-height variation is allowed");
    Check(!IsGroundSampleSafe(20.0f, 0.0f), "ground far below a route exposes an unsupported platform edge");
    Check(!IsGroundSampleSafe(20.0f, nan), "an unknown ground sample cannot prove safety");
    Check(!IsGroundSampleSafe(nan, 20.0f), "an invalid expected ground height is rejected");
    Check(!IsGroundSampleSafe(20.0f, -200000.0f), "the invalid-height sentinel is rejected");

    Check(HasSafeClearance({20.0f, 20.1f, 19.9f, 20.0f}, 20.0f), "a supported footprint has edge clearance");
    Check(!HasSafeClearance({20.0f, 20.0f, 0.0f, 20.0f}, 20.0f), "a footprint extending over a lower floor is rejected");
    Check(!HasSafeClearance({20.0f, nan, 20.0f, 20.0f}, 20.0f), "unknown terrain under part of the footprint is rejected");
    Check(!HasSafeClearance({}, 20.0f), "missing footprint samples do not prove edge clearance");

    std::cout << checks << " dungeon movement policy checks passed\n";
    return EXIT_SUCCESS;
}
