#include "playerbot/playerbot.h"
#include "DungeonMovement.h"
#include "MotionGenerators/PathFinder.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <chrono>
#include <thread>

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

    struct Fixture
    {
        Map map;
        Player bot, master;
        Unit target;
        PlayerbotAI ai;
        Fixture()
        {
            static uint64 nextGuid = 100;
            bot.guid.value = ++nextGuid;
            master.guid.value = ++nextGuid;
            target.guid.value = ++nextGuid;
            bot.map = master.map = target.map = &map;
            target.x = 10.0f;
            ai.bot = &bot;
            ai.master = &master;
            ai.dungeonCombatStrategy = ai.dungeonNonCombatStrategy = true;
            bot.ai = &ai;
            dungeonTestPath = {};
            dungeonTestWater = {};
        }
        ai::WorldPosition Destination(float x = 10.0f, float y = 0.0f, float z = 20.0f) const
        {
            return {map.id, x, y, z};
        }
    };
}

int main()
{
    using namespace ai::dungeon;
    {
        Map map;
        Player bot;
        PlayerbotAI ai;
        bot.map = &map;
        ai.bot = &bot;
        Check(!ai.HasStrategy("dungeon safe", BotState::BOT_STATE_COMBAT) &&
            !ai.HasStrategy("dungeon safe", BotState::BOT_STATE_NON_COMBAT) && !IsActive(&ai),
            "new bots have no dungeon-safety strategy until a player manually adds it");
    }
    {
        Fixture f;
        f.target.x = 2; f.target.z = 0;
        f.ai.dungeonCombatStrategy = false;
        Check(IsActive(&f.ai) && !CanTarget(&f.ai, &f.target),
            "removing the combat strategy leaves non-combat platform protection active");
        f.ai.state = BotState::BOT_STATE_COMBAT;
        Check(!IsActive(&f.ai) && CanTarget(&f.ai, &f.target),
            "removing the combat strategy disables combat checks independently of the non-combat engine");
        Check(!f.bot.IsInCombat(), "the current AI engine selects the strategy even before native combat flags catch up");
        f.ai.dungeonCombatStrategy = true;
        f.ai.dungeonNonCombatStrategy = false;
        Check(IsActive(&f.ai) && !CanTarget(&f.ai, &f.target),
            "re-adding the combat strategy restores its protection while the non-combat strategy remains disabled");
        f.ai.state = BotState::BOT_STATE_NON_COMBAT;
        Check(!IsActive(&f.ai) && CanTarget(&f.ai, &f.target),
            "removing the non-combat strategy disables only non-combat protection");
        f.ai.dungeonNonCombatStrategy = true;
        Check(IsActive(&f.ai) && !CanTarget(&f.ai, &f.target),
            "re-adding the non-combat strategy immediately restores its floor check");
    }
    {
        Fixture f;
        f.ai.dungeonCombatStrategy = f.ai.dungeonNonCombatStrategy = false;
        f.target.x = 2; f.target.z = 0;
        const auto lower = f.Destination(2, 0, 0);
        Check(!IsActive(&f.ai), "removing both strategies disables the shared dungeon movement layer");
        Check(CanMoveTo(&f.ai, lower) && CanTarget(&f.ai, &f.target) && CanLoot(&f.ai, &f.target),
            "disabled safety delegates movement, target selection and loot decisions to their original callers");
        Check(IsSafeLocalPoint(&f.ai, f.Destination(), lower) && ValidatePath(&f.ai, {}, MovePurpose::Local),
            "disabled safety does not impose local-point or hazard-path validation");
        Check(CanUseWaterRoute(&f.ai, lower), "disabled safety does not impose its water-entry check");
        std::vector<ai::WorldPosition> path{lower};
        Check(!BuildSafePath(&f.ai, lower, MovePurpose::Follow, path) && path.empty(),
            "disabled safety never fabricates a checked route or returns a stale path");
        ai::WorldPosition approach = lower;
        Check(!FindMeleeApproach(&f.ai, &f.target, lower, approach) && !approach,
            "disabled safety leaves melee approach selection to the original chase implementation");
        Check(dungeonTestPath.calls == 0, "disabled safety avoids all native pathfinder queries");
        f.ai.dungeonNonCombatStrategy = true;
        Check(!CanTarget(&f.ai, &f.target) && dungeonTestPath.calls > 0,
            "re-enabling the strategy rebuilds the safety proof instead of trusting disabled-mode permission");
        f.ai.state = BotState::BOT_STATE_DEAD;
        Check(!IsActive(&f.ai), "a strategy enabled only for co and nc is inactive in the dead engine");
        f.ai.state = BotState::BOT_STATE_REACTION;
        Check(!IsActive(&f.ai), "a strategy enabled only for co and nc is inactive in the reaction engine");
    }
    {
        Fixture f;
        Check(IsActive(&f.ai), "grounded dungeon bots enable shared platform protection");
        Check(CanMoveTo(&f.ai, f.Destination()), "a flat same-platform point remains reachable");
        Check(CanTarget(&f.ai, &f.target), "a same-platform combat target remains valid");
        Check(CanLoot(&f.ai, &f.target), "same-platform loot remains reachable");
    }
    {
        Fixture f;
        f.map.dungeon = false;
        Check(!IsActive(&f.ai), "outdoor movement is outside the dungeon policy");
        Check(CanMoveTo(&f.ai, f.Destination(2, 0, 0)), "outdoor movement retains its existing behavior");
        f.map.dungeon = true; f.bot.battleGround = true;
        Check(!IsActive(&f.ai), "battleground movement does not inherit dungeon restrictions");
        f.bot.battleGround = false; f.bot.transport = &f;
        Check(!IsActive(&f.ai), "a moving transport retains its specialized movement handling");
        f.bot.transport = nullptr; f.bot.flying = true;
        Check(!IsActive(&f.ai), "flight does not use grounded platform rules");
        f.bot.flying = false; f.bot.inWater = true;
        Check(!IsActive(&f.ai), "water movement retains its existing handling");
        f.bot.inWater = false; f.bot.swimming = true;
        Check(!IsActive(&f.ai), "swimming does not require a walkable floor");
        std::vector<ai::WorldPosition> path{f.Destination()};
        Check(!BuildSafePath(&f.ai, f.Destination(), MovePurpose::Follow, path) && path.empty(),
            "inactive protection does not return a fabricated or stale safe path");
        Check(!CanTarget(&f.ai, nullptr) && !CanLoot(&f.ai, nullptr), "inactive scope still rejects missing world targets");
    }
    for (unsigned flags : {unsigned(PATHFIND_NOPATH), unsigned(PATHFIND_INCOMPLETE), unsigned(PATHFIND_SHORT),
        unsigned(PATHFIND_NORMAL | PATHFIND_NOT_USING_PATH), unsigned(PATHFIND_NORMAL | PATHFIND_SHORTCUT)})
    {
        Fixture f;
        dungeonTestPath.type = PathType(flags);
        Check(!CanMoveTo(&f.ai, f.Destination()), "only a complete real navigation path proves dungeon movement safe");
    }
    {
        Fixture f;
        dungeonTestPath.snapped = true;
        dungeonTestPath.snappedEnd = {10, 0, 0};
        Check(!CanMoveTo(&f.ai, f.Destination()), "navmesh snapping the endpoint onto the lower floor is rejected");
    }
    {
        Fixture f;
        dungeonTestPath.actualEndOverride = true;
        dungeonTestPath.actualEnd = {10, 0, 0};
        Check(!CanMoveTo(&f.ai, f.Destination()), "native actual-end metadata must agree even when the returned path ends at the requested height");
    }
    {
        Fixture f;
        Check(!CanMoveTo(&f.ai, f.Destination(2, 0, 0)), "a local destination on the floor below is not a shortcut");
        f.target.x = 2; f.target.z = 0;
        Check(!CanTarget(&f.ai, &f.target), "a lower-platform enemy cannot become an autonomous target");
        Check(!CanLoot(&f.ai, &f.target), "a lower-platform corpse cannot pull the bot off the route");
        f.target.flying = true;
        Check(!CanTarget(&f.ai, &f.target), "a lower-floor flying enemy cannot bypass platform safety");
        f.target.flying = false; f.target.inWater = true;
        Check(!CanTarget(&f.ai, &f.target), "a lower-floor swimming enemy cannot bypass platform safety");
        f.target.inWater = false; f.target.transport = &f;
        Check(!CanTarget(&f.ai, &f.target), "a lower-floor transported enemy does not bypass the caller's ground route policy");
    }
    {
        Fixture f;
        f.map.ground = [](float x, float, float) { return x > 3 && x < 7 ? 0.0f : 20.0f; };
        Check(!CanMoveTo(&f.ai, f.Destination()), "an unsupported gap between supported endpoints is rejected");
    }
    {
        Fixture f;
        f.map.ground = [](float x, float y, float) { return x > 9.8f && y < -0.1f ? 0.0f : 20.0f; };
        Check(!CanMoveTo(&f.ai, f.Destination()), "endpoint foot clearance rejects a position hanging over a platform edge");
        Check(CanMoveTo(&f.ai, f.Destination(), MovePurpose::Target),
            "a supported enemy center does not require the bot to fit around the enemy's whole footprint");
        Check(CanTarget(&f.ai, &f.target), "an enemy beside the platform edge remains a valid combat target");
        Check(!CanLoot(&f.ai, &f.target), "target-center permission does not permit standing at an unsafe loot position");
    }
    {
        Fixture f;
        dungeonTestPath.type = PATHFIND_NOPATH;
        Check(!CanMoveTo(&f.ai, f.Destination(), MovePurpose::Target),
            "target-center reachability still requires a complete native navigation route");
    }
    {
        Fixture f;
        f.map.ground = [](float x, float, float) { return x > 3 && x < 7 ? 0.0f : 20.0f; };
        Check(!CanMoveTo(&f.ai, f.Destination(), MovePurpose::Target),
            "target-center reachability cannot bypass an unsupported middle of the route");
    }
    {
        Fixture f;
        f.bot.z = f.target.z = 0;
        f.map.ground = [](float, float, float queryZ) { return queryZ > 10 ? 20.0f : 0.0f; };
        Check(!CanMoveTo(&f.ai, f.Destination(10, 0, 0), MovePurpose::Target),
            "target-center reachability remains anchored to the master's floor");
    }
    {
        Fixture f;
        f.map.ground = [](float x, float, float) { return x > 10.1f ? 0.0f : 20.0f; };
        ai::WorldPosition approach;
        const auto unsafeBack = f.Destination(12);
        Check(!CanMoveTo(&f.ai, unsafeBack), "the preferred backstab point is beyond the platform edge");
        Check(FindMeleeApproach(&f.ai, &f.target, unsafeBack, approach),
            "melee pursuit falls back to a safe front or side contact point when the preferred stance is unsafe");
        Check(CanMoveTo(&f.ai, approach), "the fallback is proven safe for the bot's actual footprint");
        Check(approach.distance(ai::WorldPosition(&f.target)) <= 5.0f,
            "the fallback reaches melee range rather than returning the bot's old position");
    }
    {
        Fixture f;
        const auto preferred = f.Destination(8);
        ai::WorldPosition approach;
        Check(FindMeleeApproach(&f.ai, &f.target, preferred, approach), "a safe preferred melee stance remains usable");
        Check(approach.distance(preferred) < 0.01f, "safe preferred positioning is retained before fallback candidates");
    }
    {
        Fixture f;
        f.map.ground = [](float, float, float) { return 0.0f; };
        ai::WorldPosition approach = f.Destination();
        Check(!FindMeleeApproach(&f.ai, &f.target, f.Destination(12), approach),
            "melee pursuit stays blocked when no contact point has a supported safe route");
        Check(!approach, "a rejected approach clears the old movement destination");
    }
    {
        Fixture f;
        dungeonTestPath.path = {{0, 0, 20}, {0, 25, 20}, {10, 25, 20}, {10, 0, 20}};
        Check(CanMoveTo(&f.ai, f.Destination()),
            "the native runtime accepts a supported same-floor U-shaped corridor for local movement");
    }
    {
        Fixture f;
        dungeonTestPath.path = {{0, 0, 20}, {0, 30, 20}, {40, 30, 0}, {6, 0, 0}};
        f.map.ground = [](float, float, float queryZ) { return queryZ - 0.5f; };
        Check(!CanMoveTo(&f.ai, f.Destination(6, 0, 0), MovePurpose::Target),
            "a close lower-floor enemy cannot use a long stairway detour as a local pursuit route");
        std::vector<ai::WorldPosition> path;
        Check(BuildSafePath(&f.ai, f.Destination(6, 0, 0), MovePurpose::Follow, path),
            "the master can still deliberately lead the bot along that supported staircase");
    }
    {
        Fixture f;
        f.map.ground = [](float x, float, float) { return 20.0f + x * 0.5f; };
        f.master.x = 20; f.master.z = 30;
        std::vector<ai::WorldPosition> path;
        Check(BuildSafePath(&f.ai, f.Destination(20, 0, 30), MovePurpose::Follow, path),
            "following a master up a continuous staircase remains possible");
        Check(!path.empty(), "following receives a real approved staircase path");
        f.target.x = 12; f.target.z = 26; f.master.x = 12; f.master.z = 26;
        Check(CanTarget(&f.ai, &f.target), "combat on a continuous staircase beside the master remains possible");
    }
    {
        Fixture f;
        f.map.ground = [](float x, float, float) { return 20.0f - x * 0.5f; };
        f.master.x = 20; f.master.z = 10;
        std::vector<ai::WorldPosition> path;
        Check(BuildSafePath(&f.ai, f.Destination(20, 0, 10), MovePurpose::Follow, path),
            "following a master down a continuous ramp remains possible");
    }
    {
        Fixture f;
        Map other = f.map; other.instance = 2;
        f.target.map = &other;
        Check(!CanTarget(&f.ai, &f.target), "same map IDs cannot connect targets from different dungeon instances");
        Check(!CanLoot(&f.ai, &f.target), "loot from another instance cannot be selected");
        other.instance = 1; other.id = 1;
        Check(!CanTarget(&f.ai, &f.target), "different maps cannot become dungeon combat targets");
    }
    {
        Fixture f;
        f.bot.z = 0; f.target.z = 0; f.target.x = 2;
        f.map.ground = [](float, float, float) { return 0.0f; };
        Check(!CanTarget(&f.ai, &f.target), "a bot already below its master cannot autonomously acquire more lower-floor enemies");
        Check(!CanLoot(&f.ai, &f.target), "a bot below its master cannot extend the detour by looting there");
        std::vector<ai::WorldPosition> path;
        Check(BuildSafePath(&f.ai, f.Destination(2, 0, 0), MovePurpose::Follow, path),
            "follow route validation remains distinct from master-anchored local activity");
    }
    {
        Fixture f;
        Check(CanMoveTo(&f.ai, f.Destination()), "the initial safe movement is approved");
        const unsigned calls = dungeonTestPath.calls;
        Check(CanMoveTo(&f.ai, f.Destination()), "an unchanged safe query remains approved");
        Check(dungeonTestPath.calls == calls, "an unchanged repeated query uses the short safety cache");
        f.bot.x = 3.0f;
        f.map.ground = [](float, float, float) { return 0.0f; };
        Check(!CanMoveTo(&f.ai, f.Destination()), "moving the bot invalidates a cached route before a different floor is used");
        Check(dungeonTestPath.calls > calls, "a changed start point rebuilds the navigation proof");
    }
    {
        Fixture f;
        Check(CanMoveTo(&f.ai, f.Destination()), "a local path seeds the cache");
        unsigned calls = dungeonTestPath.calls;
        std::vector<ai::WorldPosition> path;
        Check(BuildSafePath(&f.ai, f.Destination(), MovePurpose::Follow, path), "the same point can also be followed");
        Check(dungeonTestPath.calls > calls, "cache entries distinguish follow from local safety modes");
        calls = dungeonTestPath.calls;
        f.map.instance = 2;
        Check(CanMoveTo(&f.ai, f.Destination()) && dungeonTestPath.calls > calls,
            "changing dungeon instance invalidates the cached navigation proof");
    }
    {
        Fixture f;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(!CanMoveTo(&f.ai, f.Destination(nan)), "non-finite points are rejected before cache-key conversion");
        Check(!CanMoveTo(&f.ai, {1, 10, 0, 20}), "a route cannot silently cross map IDs");
        Check(!CanMoveTo(&f.ai, f.Destination(1.0e12f)), "invalid map coordinates are rejected before cache-key conversion");
        Check(dungeonTestPath.calls == 0, "malformed positions do not invoke the native pathfinder");
    }
    {
        Fixture f;
        Check(CanMoveTo(&f.ai, f.Destination()), "a same-floor master initially permits local movement");
        f.master.z = 40;
        f.map.ground = [](float, float, float queryZ) { return queryZ > 30 ? 40.0f : 20.0f; };
        Check(!CanMoveTo(&f.ai, f.Destination()), "a changed master floor invalidates the local authorization despite an unchanged bot path");
    }
    {
        Fixture f;
        dungeonTestPath.type = PATHFIND_NOPATH;
        Check(!CanMoveTo(&f.ai, f.Destination()), "an unavailable path seeds a short negative cache entry");
        const unsigned calls = dungeonTestPath.calls;
        dungeonTestPath.type = PATHFIND_NORMAL;
        Check(!CanMoveTo(&f.ai, f.Destination()) && dungeonTestPath.calls == calls,
            "repeated unavailable routes do not immediately spam the pathfinder");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        Check(CanMoveTo(&f.ai, f.Destination()) && dungeonTestPath.calls > calls,
            "a route that becomes available is retried after the short cache expires");
    }
    {
        Fixture f;
        dungeonTestPath.type = PATHFIND_NOPATH;
        f.target.x = 2; f.target.victim = &f.bot;
        Check(CanTarget(&f.ai, &f.target), "nearby attackers can still be defended against while navigation is unavailable");
        f.target.z = 0;
        Check(!CanTarget(&f.ai, &f.target), "an attacker far below cannot bypass safety through a self-defense exception");
    }
    {
        Fixture f;
        const std::vector<ai::WorldPosition> detour{{f.map.id, 0, 0, 20}, {f.map.id, 5, 2, 20}, {f.map.id, 10, 0, 20}};
        Check(ValidatePath(&f.ai, detour, MovePurpose::Local), "a supported same-floor hazard detour is accepted");
        f.map.lineOfSight = false;
        Check(!ValidatePath(&f.ai, detour, MovePurpose::Local), "hazard avoidance cannot cut a route through a wall");
        f.map.lineOfSight = true;
        f.map.ground = [](float x, float, float) { return x > 3 && x < 7 ? 0.0f : 20.0f; };
        Check(!ValidatePath(&f.ai, detour, MovePurpose::Local), "hazard avoidance cannot introduce a mid-route platform gap");
        f.map.ground = [](float, float, float) { return 20.0f; };
        auto changed = detour;
        changed[1].mapid = 1;
        Check(!ValidatePath(&f.ai, changed, MovePurpose::Local), "a hazard detour cannot jump map IDs");
        Check(!ValidatePath(&f.ai, {}, MovePurpose::Local), "an empty hazard detour cannot be executed");
        changed = detour; changed.front().coord_x = 20;
        Check(!ValidatePath(&f.ai, changed, MovePurpose::Local), "a detour must start beside the current bot position");
    }
    {
        Fixture f;
        Check(!CanUseWaterRoute(&f.ai, f.Destination()), "ordinary floor destinations cannot trigger the swimming exception");
        dungeonTestWater.water = true;
        Check(CanUseWaterRoute(&f.ai, f.Destination(10, 0, 19)), "entering water from the same-height shoreline remains supported");
        dungeonTestWater.level = 22;
        Check(CanUseWaterRoute(&f.ai, f.Destination(10, 0, 21)), "small shoreline height differences remain supported");
        dungeonTestWater.level = 0;
        Check(!CanUseWaterRoute(&f.ai, f.Destination(10, 0, 0)), "a deep lower-floor pool cannot bypass falling protection");
        dungeonTestWater.level = std::numeric_limits<float>::quiet_NaN();
        Check(!CanUseWaterRoute(&f.ai, f.Destination()), "unknown water height cannot justify a swimming exception");
        dungeonTestWater.level = 20;
        Check(!CanUseWaterRoute(&f.ai, {1, 10, 0, 20}), "water movement cannot cross map IDs");
    }
    {
        Fixture f;
        f.target.flying = true; f.target.z = 30;
        Check(!CanTarget(&f.ai, &f.target), "an unengaged flying enemy cannot trigger the airborne-boss exception");
        f.target.combat = true;
        Check(!CanTarget(&f.ai, &f.target), "an airborne enemy fighting others does not by itself authorize this bot to engage");
        f.bot.combat = true;
        Check(CanTarget(&f.ai, &f.target), "an engaged airborne boss above a reachable same-floor projection remains targetable");
        Check(!CanMoveTo(&f.ai, ai::WorldPosition(&f.target)), "airborne-boss targetability does not authorize walking into empty space");
        f.target.visible = false;
        Check(!CanTarget(&f.ai, &f.target), "the airborne exception requires a visible line of sight");
        f.target.visible = true; f.target.z = 0;
        Check(!CanTarget(&f.ai, &f.target), "a lower-platform bat is below the current floor projection and is rejected");
    }
    {
        Fixture f;
        f.target.flying = true; f.target.combat = f.bot.combat = true; f.target.z = 45;
        f.map.ground = [](float x, float, float) { return x > 5 ? 40.0f : 20.0f; };
        Check(!CanTarget(&f.ai, &f.target), "an airborne enemy above a separate upper platform has no safe same-floor projection");
    }
    {
        Fixture f;
        f.target.flying = true; f.target.combat = f.bot.combat = true; f.target.z = 30;
        f.map.ground = [](float, float, float) { return 0.0f; };
        Check(!CanTarget(&f.ai, &f.target), "an airborne enemy above a lower-floor projection still cannot lure bots down");
    }
    std::cout << checks << " dungeon runtime checks passed\n";
    return EXIT_SUCCESS;
}
