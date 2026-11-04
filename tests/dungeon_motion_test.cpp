#include "playerbot/playerbot.h"
#include "DungeonMovement.h"
#include "MotionGenerators/PathFinder.h"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <list>
#include <string>
#include <type_traits>

namespace ai
{
    using TravelPath = std::vector<WorldPosition>;
    using HazardPosition = std::pair<WorldPosition, float>;
    struct Event {};
#include "dungeon_last_movement.inc"

    class DungeonSafeStrategy
    {
    public:
        explicit DungeonSafeStrategy(PlayerbotAI* ai) : ai(ai) {}
        void OnStrategyAdded(BotState);
        void OnStrategyRemoved(BotState);
    private:
        PlayerbotAI* ai;
        void ResetMovement(BotState);
    };

    class MovementAction
    {
    public:
        PlayerbotAI* ai;
        Player* bot;
        LastMovement state;
        std::list<HazardPosition> hazards;
        std::vector<WorldPosition> hazardPath;
        explicit MovementAction(PlayerbotAI* ai) : ai(ai), bot(ai->GetBot()) {}
        bool MoveAlongDungeonPath(const WorldPosition&, dungeon::MovePurpose, bool = false);
        bool GeneratePathAvoidingHazards(std::vector<WorldPosition>& path)
        {
            if (hazardPath.empty()) return false;
            path = hazardPath;
            return true;
        }
        template<class T> T ReadValue(const char*)
        {
            if constexpr (std::is_same_v<std::remove_reference_t<T>, LastMovement>) return state;
            else return hazards;
        }
    };
}

using namespace ai;
#include "dungeon_strategy_hooks.inc"
#define AI_VALUE(type, name) ReadValue<type>(name)
#include "dungeon_motion_source.inc"
#undef AI_VALUE

namespace ai
{
    struct Formation
    {
        WorldPosition location;
        float GetAngle() const { return 0; }
        float GetOffset() const { return 0; }
        WorldPosition GetLocation() const { return location; }
        static bool IsNullLocation(const WorldPosition& location) { return !location; }
    };
    using WorldLocation = WorldPosition;

    class FollowAction : public MovementAction
    {
    public:
        explicit FollowAction(PlayerbotAI* ai) : MovementAction(ai), context(ai->GetAiObjectContext()) {}
        static Player* GetDungeonCatchUpTarget(PlayerbotAI* ai);
        bool Execute(Event& event);
        unsigned nativeFollows = 0;

    private:
        AiObjectContext* context;
        bool Follow(Unit* target, float, float)
        {
            if (!target)
                return false;
            if (dungeon::IsActive(ai))
                return MoveAlongDungeonPath(WorldPosition(target), dungeon::MovePurpose::Follow);
            if (bot->motion.type == FOLLOW_MOTION_TYPE)
                return false;
            ++nativeFollows;
            bot->motion.type = FOLLOW_MOTION_TYPE;
            bot->spline.finalized = false;
            return true;
        }
    };
#define AI_VALUE(type, name) context->GetValue<type>(name)->Get()
#include "dungeon_follow_recovery.inc"
#undef AI_VALUE

    struct NotNearMasterTrigger
    {
        PlayerbotAI* ai;
        Player* bot;
        int checkInterval;
        Formation formation;
        NotNearMasterTrigger(PlayerbotAI* ai, std::string, int interval)
            : ai(ai), bot(ai->GetBot()), checkInterval(interval) {}
        virtual bool IsActive() { return true; }
        template<class T> T ReadValue(const char*)
        {
            if constexpr (std::is_same_v<T, ObjectGuid>) return ai->master ? ai->master->guid : ObjectGuid{};
            else if constexpr (std::is_same_v<T, Formation*>) return &formation;
            else return WorldPosition(ai->master);
        }
    };
#define AI_VALUE(type, name) ReadValue<type>(name)
#include "dungeon_follow_trigger.inc"
#undef AI_VALUE
}

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
        Group group;
        Player bot, master;
        PlayerbotAI ai;
        LootObjectStack loot;
        Formation formation;
        FollowAction action;
        Fixture() : action(&ai)
        {
            static uint64 guid = 10000;
            bot.guid.value = ++guid; master.guid.value = ++guid;
            bot.map = master.map = &map;
            bot.group = master.group = &group;
            ai.bot = &bot; ai.master = &master; bot.ai = &ai;
            ai.dungeonCombatStrategy = ai.dungeonNonCombatStrategy = true;
            ai.context.movement.data = &action.state;
            ai.context.followTarget.data = master.guid;
            ai.context.loot.data = &loot;
            ai.context.formation.data = &formation;
            formation.location = Goal();
            action.bot = &bot;
            dungeonTestPath = {};
            dungeonTestWater = {};
            WorldTimer::now = 1000;
        }
        WorldPosition Goal(float x = 30, float y = 0, float z = 20) const { return {map.id, x, y, z}; }
        bool Move(const WorldPosition& goal, bool walking = false,
            dungeon::MovePurpose purpose = dungeon::MovePurpose::Follow)
        {
            return action.MoveAlongDungeonPath(goal, purpose, walking);
        }
    };
}

int main()
{
    {
        Fixture f;
        DungeonSafeStrategy strategy(&f.ai);
        f.ai.state = BotState::BOT_STATE_ALL;
        strategy.OnStrategyAdded(BotState::BOT_STATE_NON_COMBAT);
        Check(f.ai.stateReads == 0 && f.ai.stops == 0,
            "adding the default strategy before initial AI-state setup does not read that uninitialized state");
        strategy.OnStrategyRemoved(BotState::BOT_STATE_NON_COMBAT);
        Check(f.ai.stateReads == 0 && f.ai.stops == 0,
            "a strategy hook without an owned route does not inspect the current AI engine or stop other motion");
    }
    {
        Fixture f;
        DungeonSafeStrategy strategy(&f.ai);
        Check(f.Move(f.Goal()), "a checked non-combat route begins before strategy removal");
        f.action.state.lastFollow = &f.master;
        f.action.state.lastFlee = 456;
        f.action.state.lastAreaTrigger = 789;
        f.action.state.lastPath = {f.Goal(7)};
        f.action.state.dungeonHazards = {{f.Goal(12), 2.0f}};
        strategy.OnStrategyRemoved(BotState::BOT_STATE_NON_COMBAT);
        Check(f.ai.stops == 1 && f.bot.spline.Finalized(),
            "removing the current strategy promptly stops its own active checked path");
        Check(!f.action.state.dungeonGoal && f.action.state.dungeonSplineId == 0 &&
            f.action.state.dungeonRepathTime == 0 && f.action.state.dungeonPurpose == 0 &&
            !f.action.state.dungeonWalking && f.action.state.dungeonHazards.empty(),
            "removing the strategy invalidates every dungeon spline-reuse field");
        Check(f.action.state.lastFollow == &f.master && f.action.state.lastFlee == 456 &&
            f.action.state.lastAreaTrigger == 789 && f.action.state.lastPath.size() == 1 &&
            f.action.state.lastPath.front() == f.Goal(7),
            "strategy removal preserves unrelated follow, flee, area-trigger and travel state");
    }
    {
        Fixture f;
        DungeonSafeStrategy strategy(&f.ai);
        Check(f.Move(f.Goal()), "a checked route begins before adding the current strategy again");
        strategy.OnStrategyAdded(BotState::BOT_STATE_NON_COMBAT);
        Check(f.ai.stops == 1 && f.action.state.dungeonSplineId == 0,
            "adding the active strategy invalidates an old checked route before the next movement decision");
    }
    {
        Fixture f;
        DungeonSafeStrategy strategy(&f.ai);
        Check(f.Move(f.Goal()), "a non-combat checked route begins before the other engine is edited");
        const uint32 spline = f.action.state.dungeonSplineId;
        strategy.OnStrategyRemoved(BotState::BOT_STATE_COMBAT);
        Check(f.ai.stops == 0 && !f.bot.spline.Finalized() && f.action.state.dungeonSplineId == spline,
            "removing the other engine's strategy does not interrupt the current engine's movement");
        strategy.OnStrategyAdded(BotState::BOT_STATE_COMBAT);
        Check(f.ai.stops == 0 && f.action.state.dungeonSplineId == spline,
            "adding the other engine's strategy does not discard the current route identity");
        f.ai.state = BotState::BOT_STATE_COMBAT;
        strategy.OnStrategyRemoved(BotState::BOT_STATE_COMBAT);
        Check(f.ai.stops == 1 && f.action.state.dungeonSplineId == 0,
            "editing combat protection after switching engines stops the owned route in combat");
    }
    {
        Fixture f;
        DungeonSafeStrategy strategy(&f.ai);
        Check(f.Move(f.Goal()), "a route seeds tracking before another action replaces its spline");
        ++f.bot.spline.id;
        strategy.OnStrategyRemoved(BotState::BOT_STATE_NON_COMBAT);
        Check(f.ai.stops == 0 && !f.bot.spline.Finalized() && f.action.state.dungeonSplineId == 0,
            "a same-type path with a different spline ID survives strategy removal while stale tracking is cleared");
    }
    {
        Fixture f;
        DungeonSafeStrategy strategy(&f.ai);
        Check(f.Move(f.Goal()), "a route seeds tracking before switching to native chase");
        f.bot.motion.type = CHASE_MOTION_TYPE;
        strategy.OnStrategyRemoved(BotState::BOT_STATE_NON_COMBAT);
        Check(f.ai.stops == 0 && f.bot.motion.type == CHASE_MOTION_TYPE && f.action.state.dungeonSplineId == 0,
            "strategy removal cannot interrupt a native chase generator even if an old spline ID matches");
    }
    {
        Fixture f;
        DungeonSafeStrategy strategy(&f.ai);
        Check(f.Move(f.Goal()), "a route seeds tracking before normal spline completion");
        f.bot.spline.finalized = true;
        strategy.OnStrategyRemoved(BotState::BOT_STATE_NON_COMBAT);
        Check(f.ai.stops == 0 && f.action.state.dungeonSplineId == 0,
            "strategy removal clears completed route tracking without issuing a redundant movement stop");
    }
    {
        Fixture f;
        f.bot.x = 3;
        f.bot.spline.finalized = false; f.bot.spline.id = 9;
        f.bot.spline.live = {7, 0, 20};
        Check(f.Move(f.Goal()), "a valid route starts while replacing unrelated existing motion");
        Check(f.bot.synchronizations == 1 && f.bot.x == 7,
            "the route starts from the live spline position rather than the stale map-update position");
        Check(f.bot.motion.points.front().x > 7 && f.bot.motion.nativePoints.front().x == 7,
            "the native generator adds the current source once and receives only forward waypoints");
        Check(f.bot.motion.points.back().x == 30,
            "the entire approved route is submitted instead of truncating movement to eight yards");
        for (const auto& point : f.bot.motion.points)
            Check(point.x > 7, "replacement motion cannot turn back toward a stale start point");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal()), "the first follow request launches a complete checked spline");
        const unsigned calls = dungeonTestPath.calls;
        f.bot.spline.live = {2, 0, 20};
        WorldTimer::now += 100;
        Check(f.Move(f.Goal()), "an ordinary follow refresh keeps moving");
        Check(f.bot.motion.clears == 1 && f.bot.motion.launches == 1,
            "refreshing the same valid active route does not repeatedly clear and restart motion");
        Check(f.bot.x == 2 && dungeonTestPath.calls > calls,
            "spline reuse still proves route safety from the updated live position");
        WorldTimer::now += 1000;
        Check(f.Move(f.Goal()) && f.bot.motion.launches == 1,
            "a stationary goal keeps the existing spline even after the replan interval expires");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal(10)), "a moving-target route is initially submitted");
        WorldTimer::now += 100;
        Check(f.Move(f.Goal(14)) && f.bot.motion.launches == 1,
            "frequent moving-target updates do not interrupt a safe spline before the replan interval");
        WorldTimer::now += 500;
        Check(f.Move(f.Goal(14)) && f.bot.motion.launches == 2 && f.bot.motion.points.back().x == 14,
            "a meaningful moving-goal change is submitted after the bounded replan interval");
        f.bot.spline.remaining = 100;
        WorldTimer::now += 50;
        Check(f.Move(f.Goal(15)) && f.bot.motion.launches == 3,
            "a nearly finished spline can promptly extend to the moving goal");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal(10)), "the direction-change scenario begins moving");
        WorldTimer::now += 600;
        Check(f.Move(f.Goal(0, 10)) && f.bot.motion.launches == 2,
            "pursuit replans when a moving target turns around a substantial corner");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal()), "a tracked spline begins");
        ++f.bot.spline.id;
        WorldTimer::now += 100;
        Check(f.Move(f.Goal()) && f.bot.motion.launches == 2,
            "a spline launched by another movement action is not mistaken for the checked dungeon route");
        f.bot.motion.type = CHASE_MOTION_TYPE;
        Check(f.Move(f.Goal()) && f.bot.motion.launches == 3,
            "the checked spline ID cannot authorize a different native movement generator");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal()), "a run route begins");
        Check(f.Move(f.Goal(), true) && f.bot.motion.launches == 2 &&
            f.bot.motion.speed == FORCED_MOVEMENT_WALK, "a requested walk/run change replaces the old route immediately");
        Check(f.Move(f.Goal(), true, dungeon::MovePurpose::Local) && f.bot.motion.launches == 3,
            "changing from follow to local movement cannot reuse a differently authorized route");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal()), "a hazard-sensitive route begins");
        f.action.hazards.push_back({f.Goal(8), 2.0f});
        Check(f.Move(f.Goal()) && f.bot.motion.launches == 2,
            "changing active hazards invalidates reuse without waiting for the moving-target throttle");
        f.action.hazardPath = {{f.map.id, 0, 0, 20}, {f.map.id, 4, 0, 0}, f.Goal()};
        Check(!f.Move(f.Goal()) && f.ai.stops == 1 && f.bot.motion.launches == 2,
            "a newly proposed hazard detour with an unsupported drop stops instead of replacing a safe spline");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal()), "an initially safe route begins");
        f.bot.spline.live = {2, 0, 20};
        dungeonTestPath.type = PATHFIND_NOPATH;
        Check(!f.Move(f.Goal()) && f.ai.stops == 1 && f.bot.motion.launches == 1,
            "an active spline is stopped when its refreshed native route is no longer valid");
        f.ai.canMove = false;
        Check(!f.Move(f.Goal()) && f.ai.stops == 2,
            "a bot that cannot move never keeps a previously validated active spline");
    }
    {
        Fixture f;
        dungeonTestPath.path = {{0, 0, 20}, {0, 0, 20}, {12, 0, 20}};
        Check(f.Move(f.Goal(12)), "duplicate native source points do not prevent movement");
        Check(f.bot.motion.points.size() == 1 && f.bot.motion.points.front().x == 12,
            "duplicate path sources are removed while the real forward destination remains");
    }
    {
        Fixture f;
        std::vector<WorldPosition> path;
        Check(dungeon::BuildSafePath(&f.ai, f.Goal(), dungeon::MovePurpose::Follow, path),
            "a route is cached before the next small spline advance");
        const unsigned calls = dungeonTestPath.calls;
        f.bot.x = 0.2f;
        Check(f.Move(f.Goal()) && dungeonTestPath.calls == calls,
            "the safety cache can serve a route whose source precedes the live position by a fraction of a yard");
        Check(f.bot.motion.points.front().x > 0.2f,
            "a cached route's older source is never sent as a backward waypoint");
    }
    {
        Fixture f;
        f.bot.motion.failLaunch = true;
        Check(!f.Move(f.Goal()) && f.ai.stops == 1 && f.action.state.dungeonSplineId == 0,
            "a failed native spline launch is not recorded as successful movement");
    }
    {
        Fixture f;
        Check(!f.Move(f.Goal(0)) && f.ai.stops == 1 && f.bot.motion.launches == 0,
            "a route with no actual forward movement does not restart a zero-length spline");
    }
    {
        Fixture f;
        Check(f.Move(f.Goal(), true), "a route seeds real LastMovement tracking state");
        f.action.state.lastFlee = 987;
        f.action.state.dungeonHazards.push_back({f.Goal(8), 2.0f});
        LastMovement copied(f.action.state);
        Check(copied.dungeonGoal == f.action.state.dungeonGoal &&
            copied.dungeonSplineId == f.action.state.dungeonSplineId &&
            copied.dungeonRepathTime == f.action.state.dungeonRepathTime &&
            copied.dungeonPurpose == f.action.state.dungeonPurpose && copied.dungeonWalking &&
            copied.dungeonHazards == f.action.state.dungeonHazards && copied.lastFlee == 987,
            "copying production LastMovement retains the complete dungeon-route identity");
        copied.clear();
        Check(!copied.dungeonGoal && copied.dungeonSplineId == 0 && copied.dungeonRepathTime == 0 &&
            copied.dungeonPurpose == 0 && !copied.dungeonWalking && copied.dungeonHazards.empty(),
            "clearing production LastMovement discards all spline-reuse authorization");
    }
    {
        Fixture f;
        UpdateFollowTrigger trigger(&f.ai);
        Check(trigger.IsActive() && trigger.checkInterval == 1,
            "the production follow trigger checks grounded dungeon movement on every AI update");
        f.map.dungeon = false;
        Check(trigger.IsActive() && trigger.checkInterval == 3,
            "the production follow trigger retains its ordinary interval outside dungeons");
        f.map.dungeon = true;
        Check(trigger.IsActive() && trigger.checkInterval == 1,
            "returning to a dungeon restores responsive follow checks");
        f.ai.dungeonNonCombatStrategy = false;
        Check(trigger.IsActive() && trigger.checkInterval == 3,
            "removing the non-combat strategy restores the ordinary follow-check interval in a dungeon");
        f.ai.dungeonNonCombatStrategy = true;
        Check(trigger.IsActive() && trigger.checkInterval == 1,
            "re-adding the non-combat strategy restores responsive dungeon follow checks");
        f.ai.state = BotState::BOT_STATE_COMBAT;
        f.ai.dungeonCombatStrategy = false;
        trigger.IsActive();
        Check(trigger.checkInterval == 3,
            "an enabled non-combat strategy does not force responsive follow checks in the disabled combat engine");
        f.ai.dungeonCombatStrategy = true;
        trigger.IsActive();
        Check(trigger.checkInterval == 1, "re-adding the combat strategy restores responsive follow checks in combat");
    }
    {
        Fixture f;
        f.master.x = 30;
        f.loot.holdingFollow = true;
        Check(!FollowAction::GetDungeonCatchUpTarget(&f.ai), "a progressing loot task keeps its existing hold");
        f.loot.holdingFollow = false;
        Check(FollowAction::GetDungeonCatchUpTarget(&f.ai) == &f.master,
            "a stopped follower can catch up once the active loot hold ends");
        f.bot.spline.finalized = false;
        Check(!FollowAction::GetDungeonCatchUpTarget(&f.ai), "an active spline does not trigger recovery");
        f.bot.spline.finalized = true;
        f.master.x = 8;
        Check(!FollowAction::GetDungeonCatchUpTarget(&f.ai), "ordinary formation spacing does not trigger recovery");
        f.master.x = 30;
        f.ai.followRange = 28;
        Check(!FollowAction::GetDungeonCatchUpTarget(&f.ai), "the recovery threshold respects a larger configured follow range");
    }
    {
        Fixture f;
        f.master.x = 30;
        auto suppressed = [&] { return !FollowAction::GetDungeonCatchUpTarget(&f.ai); };
        f.bot.combat = true;
        Check(suppressed(), "recovery does not pull a fighting bot away from combat");
        f.bot.combat = false;
        f.ai.state = BotState::BOT_STATE_COMBAT;
        Check(suppressed(), "recovery waits for the non-combat engine after the native combat flag clears");
        f.ai.state = BotState::BOT_STATE_NON_COMBAT;
        f.master.combat = true;
        Check(suppressed(), "recovery does not take over when the leader is already fighting");
        f.master.combat = false;
        f.bot.casting = true;
        Check(suppressed(), "recovery does not interrupt a cast");
        f.bot.casting = false;
        f.bot.teleporting = true;
        Check(suppressed(), "recovery does not move a teleporting follower");
        f.bot.teleporting = false;
        f.master.teleporting = true;
        Check(suppressed(), "recovery does not chase a teleporting leader");
        f.master.teleporting = false;
        Map otherInstance = f.map;
        ++otherInstance.instance;
        f.master.map = &otherInstance;
        Check(suppressed(), "the same map ID in another dungeon instance is not a valid catch-up target");
        f.master.map = &f.map;
        f.ai.stayStrategy = true;
        Check(suppressed(), "explicit stay suppresses catch-up");
        f.ai.stayStrategy = false;
        f.ai.guardStrategy = true;
        Check(suppressed(), "explicit guard suppresses catch-up");
        f.ai.guardStrategy = false;
        f.ai.passiveStrategy = true;
        Check(suppressed(), "explicit passive suppresses catch-up");
        f.ai.passiveStrategy = false;
        f.ai.followStrategy = false;
        Check(suppressed(), "catch-up does not turn on follow for a bot that has it disabled");
        f.ai.followStrategy = true;
        f.map.dungeon = false;
        Check(suppressed(), "ordinary outdoor follow never enters the dungeon recovery path");
        f.map.dungeon = true;
        PlayerbotAI leaderAI;
        f.master.ai = &leaderAI;
        Check(suppressed(), "a bot leader cannot trigger the player-led catch-up");
        leaderAI.realPlayer = true;
        Check(FollowAction::GetDungeonCatchUpTarget(&f.ai) == &f.master,
            "a human leader with an AI context is still a real player");
    }
    {
        Fixture f;
        f.master.x = 30;
        f.formation.location = {};
        Event event;
        Check(f.action.Execute(event) && f.bot.motion.launches == 1 && f.action.nativeFollows == 0,
            "protected catch-up uses a checked path to the leader even if no formation point is available");
        f.bot.spline.finalized = true;
        f.master.x = 35;
        dungeonTestPath.type = PATHFIND_NOPATH;
        Check(!f.action.Execute(event) && f.bot.motion.launches == 1 && f.action.nativeFollows == 0,
            "a failed safety check cannot fall back to unchecked native follow");
    }
    {
        Fixture f;
        f.master.x = 30;
        f.ai.dungeonNonCombatStrategy = false;
        f.bot.motion.type = FOLLOW_MOTION_TYPE;
        Event event;
        Check(f.action.Execute(event) && f.bot.motion.clears == 1 && f.action.nativeFollows == 1,
            "a finalized native follow is replaced and resubmitted after the leader moves on");
        Check(!f.action.Execute(event) && f.bot.motion.clears == 1 && f.action.nativeFollows == 1,
            "the restarted native follow does not get cleared again while its spline runs");
    }
    std::cout << checks << " dungeon spline checks passed\n";
    return EXIT_SUCCESS;
}
