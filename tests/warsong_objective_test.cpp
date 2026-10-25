#include "playerbot/WarsongPolicy.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int32 = std::int32_t;
constexpr unsigned ALLIANCE = 0, HORDE = 1, STATUS_IN_PROGRESS = 1;
constexpr unsigned DIST_CALC_NONE = 0;
constexpr float PET_FOLLOW_DIST = 1.0f, PET_FOLLOW_ANGLE = 0.0f;
struct Position { float x, y, z, o = 0; };
struct WorldLocation
{
    uint32 mapid = 489; float coord_x = 0, coord_y = 0, coord_z = 0;
    WorldLocation() = default;
    WorldLocation(uint32 map, float x, float y, float z) : mapid(map), coord_x(x), coord_y(y), coord_z(z) {}
};
struct ObjectGuid
{
    uint64 value = 0;
    uint64 GetRawValue() const { return value; }
    bool operator==(const ObjectGuid&) const = default;
    explicit operator bool() const { return value != 0; }
};
struct BattleGround
{
    unsigned status = STATUS_IN_PROGRESS;
    unsigned GetStatus() const { return status; }
    unsigned GetOtherTeam(unsigned team) const { return team == ALLIANCE ? HORDE : ALLIANCE; }
};
using BattleGroundWS = BattleGround;
constexpr unsigned BG_WS_FLAG_STATE_ON_PLAYER = 1;
struct Player;
struct Camera { void* GetBody() const { return nullptr; } };
struct Unit
{
    bool alive = true, inWorld = true, hostile = true, visible = true, roleAllowed = true;
    void* world = nullptr;
    Unit* victim = nullptr;
    float x = 0, y = 0, z = 0;
    bool IsAlive() const { return alive; }
    bool IsInWorld() const { return inWorld; }
    void* GetMap() const { return world; }
    Unit* GetVictim() const { return victim; }
    float GetPositionX() const { return x; }
    float GetPositionZ() const { return z; }
    bool IsVisibleForOrDetect(const Player*, void*, bool) const { return visible; }
};
enum MovementGeneratorType { IDLE_MOTION_TYPE, POINT_MOTION_TYPE, CHASE_MOTION_TYPE, FOLLOW_MOTION_TYPE };
struct MoveSpline
{
    bool finalized = true;
    uint32 id = 0;
    bool Finalized() const { return finalized; }
    uint32 GetId() const { return id; }
};
struct MotionMaster
{
    unsigned follows = 0;
    MovementGeneratorType type = IDLE_MOTION_TYPE;
    void MoveFollow(Player*, float, float) { ++follows; type = FOLLOW_MOTION_TYPE; }
    MovementGeneratorType GetCurrentMovementGeneratorType() const { return type; }
};
struct Pet : Unit
{
    unsigned stops = 0;
    MotionMaster motion;
    void AttackStop() { ++stops; }
    MotionMaster* GetMotionMaster() { return &motion; }
};
struct Player : Unit
{
    uint64 guid = 10;
    unsigned team = ALLIANCE, attackStops = 0;
    uint32 mapId = 489;
    bool carrier = false, inCombat = false, moving = false;
    BattleGround* bg = nullptr;
    Pet* pet = nullptr;
    Camera camera;
    MoveSpline spline;
    MoveSpline* movespline = &spline;
    MotionMaster motion;
    std::vector<Unit*> attackers;
    uint32 GetMapId() const { return mapId; }
    unsigned GetTeam() const { return team; }
    uint32 GetGUIDLow() const { return uint32(guid); }
    ObjectGuid GetObjectGuid() const { return {guid}; }
    bool IsInCombat() const { return inCombat; }
    bool IsMoving() const { return moving; }
    BattleGround* GetBattleGround() const { return bg; }
    float GetDistance(float gx, float gy, float gz, unsigned) const
    {
        return (x - gx) * (x - gx) + (y - gy) * (y - gy) + (z - gz) * (z - gz);
    }
    void AttackStop() { ++attackStops; }
    Pet* GetPet() const { return pet; }
    MotionMaster* GetMotionMaster() { return &motion; }
    Camera& GetCamera() { return camera; }
    bool CanAttack(const Unit* target) const { return target && target->hostile; }
    const std::vector<Unit*>& getAttackers() const { return attackers; }
    bool IsWithinDistInMap(const Unit* target, float range) const
    {
        return world == target->world && std::sqrt(GetDistance(target->x, target->y, target->z, DIST_CALC_NONE)) <= range;
    }
};
struct PlayerbotAIConfig { float sightDistance = 60.0f; } sPlayerbotAIConfig;
namespace ai
{
    struct PossibleTargetsValue
    {
        static bool CanPerceive(Unit* target, Player* observer)
        {
            return target && observer && target->IsVisibleForOrDetect(observer, observer->GetCamera().GetBody(), true);
        }
    };
    struct PositionEntry
    {
        float x = 0, y = 0, z = 0;
        uint32 mapId = 489;
        bool valueSet = false;
        bool isSet() const { return valueSet; }
        void Set(float nx, float ny, float nz, uint32 map) { x = nx; y = ny; z = nz; mapId = map; valueSet = true; }
        void Reset() { valueSet = false; }
    };
    using PositionMap = std::map<std::string, PositionEntry>;
}
template<class T> struct Value
{
    T data{};
    T& Get() { return data; }
    void Set(const T& next) { data = next; }
};
struct Context
{
    Value<ai::PositionMap> positions;
    std::map<std::string, Value<ObjectGuid>> guids;
    std::map<std::string, Value<int>> integers;
    std::map<std::string, Value<bool>> booleans;
    Value<uint32> role;
    template<class T> auto GetValue(const std::string& name, const std::string& qualifier = "")
    {
        if constexpr (std::is_same_v<T, ai::PositionMap&>) return &positions;
        else if constexpr (std::is_same_v<T, bool>) return &booleans[qualifier];
        else if constexpr (std::is_same_v<T, int>) return &integers[qualifier];
        else if constexpr (std::is_same_v<T, uint32>) return &role;
        else return &guids[name];
    }
};
struct PlayerbotAI
{
    Player* bot = nullptr;
    Context* context = nullptr;
    ai::warsong::Flag own, enemy;
    std::vector<ai::warsong::Member> members;
    ai::warsong::Planner planner;
    ai::warsong::Planner::Assignments assignments;
    uint32 now = 100;
    Unit* currentTarget = nullptr;
    bool canMove = true;
    unsigned stops = 0, refreshes = 0;
    Unit* GetUnit(ObjectGuid guid) const { return guid ? currentTarget : nullptr; }
    Player* GetBot() const { return bot; }
    Context* GetAiObjectContext() const { return context; }
    bool CanMove() const { return canMove; }
    void StopMoving() { ++stops; bot->moving = false; bot->spline.finalized = true; bot->motion.type = IDLE_MOTION_TYPE; }
};
struct EnemyPlayersValue { static bool IsAllowedWarsongTarget(Unit* target, Player*) { return target->roleAllowed; } };
struct BattleBotPath {};
std::vector<BattleBotPath*> vPaths_WS;
std::vector<uint32> vFlagsWS;

using namespace ai;
#include "warsong_objective_positions.inc"

namespace ai::warsong
{
    PlayerbotAI* activeAI = nullptr;
    std::mutex plansMutex;
    bool IsCarrier(Player* bot) { return bot->carrier; }
    BattleGroundWS* ActiveWarsong(Player* bot)
    {
        return bot && bot->alive && bot->bg && bot->bg->status == STATUS_IN_PROGRESS && bot->mapId == 489 ? bot->bg : nullptr;
    }
    unsigned FlagStateValue(BattleGroundWS*, unsigned owner)
    {
        const auto& flag = owner == activeAI->bot->team ? activeAI->own : activeAI->enemy;
        return flag.state == FlagState::Carried ? BG_WS_FLAG_STATE_ON_PLAYER : 0;
    }
    Flag ReadFlag(BattleGroundWS*, unsigned owner, const Point& base, unsigned)
    {
        auto flag = owner == activeAI->bot->team ? activeAI->own : activeAI->enemy;
        if (flag.state == FlagState::Base) { flag.position = base; flag.positionKnown = true; }
        return flag;
    }
    struct TeamPlan { Planner::Assignments assignments; };
    TeamPlan& Refresh(Player*, BattleGroundWS*, const Flag& own, const Flag& enemy)
    {
        static TeamPlan plan;
        ++activeAI->refreshes;
        plan.assignments = activeAI->planner.Update(own, enemy, activeAI->members, activeAI->now);
        activeAI->assignments = plan.assignments;
        return plan;
    }
#include "warsong_task_source.inc"
#ifdef WARSONG_LEGACY_NO_PRESSURE_QUERY
    // Older production code has no pressure-route mode; retain that behavior
    // when running the same fixture against a saved pre-fix source.
    bool IsPressuring(PlayerbotAI*) { return false; }
#endif
#include "warsong_select_objective_source.inc"
}
struct Order { uint32 map; float x, y, z; bool react, noPath, ignoreEnemyTargets; };
class BGTactics
{
public:
    Player* bot;
    PlayerbotAI* ai;
    Context* context;
    std::string name = "warsong objective";
    bool moveResult = true, routeResult = false, waypointResult = false, flagResult = false;
    bool launchMotion = true;
    unsigned routes = 0, waypoints = 0;
    uint32 duration = 0;
    std::vector<Order> orders;
    bool refreshWarsongObjective();
    bool moveToWarsongPoint(uint32, float, float, float, bool = false, bool = false, bool = false);
    bool moveWarsongObjective(bool, bool);
    const std::string& getName() const { return name; }
    bool atFlag(const std::vector<BattleBotPath*>&, const std::vector<uint32>&) { return flagResult; }
    void LaunchMotion()
    {
        if (!launchMotion) return;
        bot->moving = true; bot->spline.finalized = false; ++bot->spline.id; bot->motion.type = POINT_MOTION_TYPE;
    }
    bool moveToObjective() { ++routes; if (routeResult) LaunchMotion(); return routeResult; }
    bool selectObjectiveWp(const std::vector<BattleBotPath*>&) { ++waypoints; if (waypointResult) LaunchMotion(); return waypointResult; }
    void SetDuration(uint32 next) { duration = next; }
    bool MoveTo(uint32 map, float x, float y, float z, bool, bool react, bool noPath, bool ignoreEnemyTargets)
    {
        if (!ai->CanMove()) return false;
        orders.push_back({map, x, y, z, react, noPath, ignoreEnemyTargets});
        if (moveResult) LaunchMotion();
        return moveResult;
    }
};
#include "warsong_objective_actions.inc"

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAILED: " << description << '\n'; }
    }
    struct Fixture
    {
        BattleGround bg;
        Player bot;
        Context context;
        PlayerbotAI ai{&bot, &context};
        BGTactics action{&bot, &ai, &context};
        Fixture()
        {
            bot.bg = &bg; bot.world = &bg; bot.x = 1227; bot.y = 1476; bot.z = 307;
            ai::warsong::activeAI = &ai;
            ai.members = {{bot.guid, {bot.x, bot.y, bot.z}, true, true, false, false},
                {21, {1400, 1460, 330}, true, true, true, false},
                {22, {1400, 1460, 330}, true, true, false, false},
                {77, {1400, 1460, 330}, false, true, false, false}};
        }
        void OurTeamCarries()
        {
            ai.enemy = {ai::warsong::FlagState::Carried, {1400, 1460, 330}, 77, true};
        }
        Unit Enemy() const
        {
            Unit enemy;
            enemy.world = const_cast<BattleGround*>(&bg); enemy.x = bot.x + 5; enemy.y = bot.y; enemy.z = bot.z;
            return enemy;
        }
    };
}

int main()
{
    using namespace ai::warsong;
    for (unsigned team : {ALLIANCE, HORDE})
    {
        Fixture f; f.bot.team = team; f.OurTeamCarries();
        Check(f.action.refreshWarsongObjective(), "an attacking bot receives its live flag-state objective");
        const auto goal = f.context.positions.data.at("bg objective");
        Check(team == ALLIANCE ? goal.x < 1150 : goal.x > 1300,
            "spare offense targets the enemy side instead of the midfield waiting point");
        Check(goal.z < 333, "enemy graveyard pressure remains below the resurrection platform");
        Check(std::hypot(goal.x - 1227.446f, goal.y - 1476.235f) > 100,
            "the former static midfield destination cannot remain the final pressure objective");
        Check(GetTask(&f.ai) == Task::Attack && IsPressuring(&f.ai),
            "only the spare offensive assignment enables the live pressure route");
    }
    for (unsigned team : {ALLIANCE, HORDE})
    {
        std::set<std::pair<float, float>> destinations;
        for (uint64 guid = 1; guid <= 10; ++guid)
        {
            Fixture f; f.bot.team = team; f.bot.guid = guid; f.ai.members.front().guid = guid;
            f.OurTeamCarries(); f.action.refreshWarsongObjective();
            const auto first = f.context.positions.data.at("bg objective");
            destinations.insert({first.x, first.y});
            ++f.ai.now;
            f.action.refreshWarsongObjective();
            const auto second = f.context.positions.data.at("bg objective");
            Check(first.x == second.x && first.y == second.y && first.z == second.z,
                "a pressure group member retains a deterministic ground point across objective refreshes");
            Check(second.z > 300 && second.z < 330,
                "every spread position stays at battlefield ground height rather than the graveyard ledge");
        }
        Check(destinations.size() == 3, "spare offense spreads between the three enemy graveyard approach points");
    }
    {
        Fixture f; f.OurTeamCarries(); f.ai.members[1].guid = f.bot.guid;
        f.ai.members[0].guid = 21;
        f.action.refreshWarsongObjective();
        const auto goal = f.context.positions.data.at("bg objective");
        Check(goal.x == 1400 && goal.y == 1460 && goal.z == 330, "escort retains the live friendly-carrier destination");
        Check(GetTask(&f.ai) == Task::Escort && !IsPressuring(&f.ai), "escorts cannot inherit the pressure movement override");
    }
    {
        Fixture f; f.OurTeamCarries();
        f.ai.own = {FlagState::Carried, {1000, 1400, 325}, 88, true};
        f.action.refreshWarsongObjective();
        const auto goal = f.context.positions.data.at("bg objective");
        Check(goal.x == 1000 && goal.y == 1400, "double-flag recovery is not diverted into graveyard pressure");
        Check(GetTask(&f.ai) == Task::Recover && !IsPressuring(&f.ai), "recoverers do not join the enemy graveyard group");
    }
    {
        Fixture f; f.ai.enemy = {FlagState::Dropped, {1190, 1450, 310}, 0, true};
        f.action.refreshWarsongObjective();
        Check(f.context.positions.data.at("bg objective").x == 1190, "a visible dropped enemy flag still receives immediate pickup attention");
    }
    {
        Fixture f; f.bot.carrier = true; f.ai.own.state = FlagState::Base; f.OurTeamCarries(); f.ai.enemy.carrier = f.bot.guid;
        f.action.refreshWarsongObjective();
        Check(f.context.positions.data.at("bg objective").x == WS_FLAG_POS_ALLIANCE.x,
            "our actual carrier keeps the home delivery point rather than joining the pressure group");
        Check(!IsPressuring(&f.ai), "live carrier identity overrides a stale pressure assignment");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        f.ai.enemy = {FlagState::Dropped, {1190, 1450, 310}, 0, true};
        Check(!IsPressuring(&f.ai), "a dropped enemy flag invalidates the pressure override before the next objective snapshot");
        f.action.refreshWarsongObjective();
        Check(!f.context.booleans["warsong pressure"].data && f.context.positions.data.at("bg objective").x == 1190,
            "drop reassignment clears the saved pressure value and immediately heads for the pickup");
        f.ai.enemy.state = FlagState::Respawning;
        f.action.refreshWarsongObjective();
        Check(!IsPressuring(&f.ai) && f.context.positions.data.at("bg objective").x == WS_FLAG_POS_HORDE.x,
            "flag respawn returns offense to the enemy flag room rather than leaving it in midfield");
        f.bg.status = 0;
        f.context.booleans["warsong pressure"].data = true;
        Check(!f.action.refreshWarsongObjective() && !f.context.booleans["warsong pressure"].data,
            "an ended battleground cannot retain a saved pressure-movement override");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 1,
            "a spare offensive bot leaves midfield for its pressure objective");
        const auto goal = f.context.positions.data.at("bg objective");
        const auto& order = f.action.orders.front();
        Check(order.x == goal.x && order.y == goal.y && order.z == goal.z && order.map == 489,
            "field pressure follows the real ground objective rather than a flag-room waypoint");
        Check(!order.noPath && order.react && !order.ignoreEnemyTargets && f.action.duration == 500,
            "pressure uses normal pathfinding and remains responsive to nearby enemies");
        Check(f.action.routes == 0 && f.action.waypoints == 0, "field pressure never enters a stale base-route fallback");
    }
    {
        Fixture f; f.action.routeResult = true; f.action.refreshWarsongObjective();
        Check(f.action.moveWarsongObjective(false, false) && f.action.routes == 1,
            "an ordinary flag-base objective can start its existing route");
        f.OurTeamCarries(); f.action.refreshWarsongObjective();
        Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 1 && f.action.routes == 1,
            "a flag pickup replaces the active old route with a direct ground pressure order");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        f.action.moveResult = false;
        Check(!f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 1 && f.action.routes == 0,
            "a rejected pressure path reports failure rather than entering the enemy flag room");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        auto goal = f.context.positions.data.at("bg objective");
        f.bot.x = goal.x; f.bot.y = goal.y; f.bot.z = goal.z; f.bot.moving = true;
        Check(!f.action.moveWarsongObjective(false, false) && f.ai.stops == 1 && f.action.orders.empty(),
            "an arrived pressure bot yields to combat instead of restarting zero-length movement");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        f.bot.x = 1510; f.bot.z = 352; f.action.waypointResult = true;
        Check(f.action.moveWarsongObjective(false, false) && f.action.waypoints == 1 &&
            f.action.routes == 0 && f.action.orders.empty() && f.action.duration == 500,
            "an elevated base uses its established exit before moving to the ground pressure point");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.inCombat = true;
        Check(f.action.moveWarsongObjective(false, false) && !f.action.orders.empty(),
            "a lingering combat flag without a live target cannot pin the pressure group in midfield");
    }
    for (unsigned invalid = 0; invalid < 6; ++invalid)
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.inCombat = true;
        Unit enemy = f.Enemy();
        if (invalid == 0) enemy.alive = false;
        if (invalid == 1) enemy.inWorld = false;
        if (invalid == 2) enemy.x += 100;
        if (invalid == 3) enemy.visible = false;
        if (invalid == 4) enemy.hostile = false;
        if (invalid == 5) enemy.roleAllowed = false;
        f.ai.currentTarget = &enemy; f.context.guids["current target"].data = {9};
        Check(f.action.moveWarsongObjective(false, false) && !f.action.orders.empty(),
            "a dead, stale, distant, hidden, friendly or role-invalid target cannot freeze offensive movement");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.inCombat = true;
        Unit enemy = f.Enemy(); f.ai.currentTarget = &enemy; f.context.guids["current target"].data = {9};
        Check(!f.action.moveWarsongObjective(false, false) && f.action.orders.empty(),
            "a live nearby current opponent keeps combat priority over graveyard pressure");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.inCombat = true;
        Unit enemy = f.Enemy(); enemy.victim = &f.bot; f.bot.attackers = {&enemy};
        Check(!f.action.moveWarsongObjective(false, false) && f.action.orders.empty(),
            "a nearby direct attacker is fought even before a current target is selected");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.inCombat = true;
        Unit enemy = f.Enemy(); f.bot.victim = &enemy;
        Check(!f.action.moveWarsongObjective(false, false) && f.action.orders.empty(),
            "a live native attack victim retains combat priority even if the AI target has not caught up");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.inCombat = true;
        Unit enemy = f.Enemy(); f.bot.attackers = {&enemy};
        Check(f.action.moveWarsongObjective(false, false) && !f.action.orders.empty(),
            "stale attacker-list membership without a direct attack does not pin offense in place");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.x = 1510; f.bot.z = 352;
        Check(!f.action.moveWarsongObjective(false, false) && f.action.waypoints == 1 &&
            f.action.routes == 0 && f.action.orders.empty(),
            "an unavailable elevated exit cannot fall back to a direct drop or wrong flag-room route");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        Check(!f.action.moveWarsongObjective(true, false) && f.action.orders.empty(),
            "the high-priority carrier action never consumes an ordinary pressure bot's update");
        Check(!f.action.moveWarsongObjective(false, true) && f.action.orders.empty(),
            "the high-priority escort action never consumes an ordinary pressure bot's update");
    }
    {
        Fixture f; f.OurTeamCarries(); f.ai.members[1].guid = f.bot.guid; f.ai.members[0].guid = 21;
        f.action.refreshWarsongObjective(); f.action.name = "warsong regroup"; f.bot.inCombat = true;
        Check(f.action.moveWarsongObjective(false, true) && !f.action.orders.empty() &&
            f.action.orders.front().ignoreEnemyTargets && f.action.routes == 1,
            "a separated escort keeps its carrier regroup path even while pressure players are advancing");
    }
    {
        Fixture f; Pet pet; f.bot.pet = &pet; f.bot.carrier = true; f.OurTeamCarries(); f.ai.enemy.carrier = f.bot.guid;
        f.action.refreshWarsongObjective(); f.action.name = "warsong carry"; f.action.routeResult = true;
        Check(f.action.moveWarsongObjective(true, false) && f.action.routes == 1 && f.action.orders.empty(),
            "the actual flag carrier continues its delivery route instead of entering ground pressure mode");
        Check(f.bot.attackStops == 1 && pet.stops == 1 && pet.motion.follows == 1,
            "carrier escape still releases an obsolete fight and brings its pet along");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        f.bot.inCombat = true;
        Unit enemy = f.Enemy(); enemy.victim = &f.bot;
        BattleGround other; enemy.world = &other; f.bot.attackers = {&enemy};
        Check(f.action.moveWarsongObjective(false, false), "an attacker from another map cannot hold the new objective hostage");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        Check(f.action.moveWarsongObjective(false, false), "an initial pressure point move is launched");
        const uint32 firstSpline = f.bot.spline.id;
        Check(firstSpline && uint32(f.context.integers["warsong pressure spline"].data) == firstSpline &&
            f.context.positions.data["warsong pressure route"].isSet(),
            "the production objective action records only its newly launched native point spline");
        for (unsigned tick = 0; tick < 4; ++tick)
            Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 1 && f.bot.spline.id == firstSpline,
                "repeated strategic checks reuse the same active spline without clearing and relaunching it");
        f.context.positions.data["bg objective"].x += 20;
        Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 2 && f.bot.spline.id != firstSpline,
            "a changed strategic destination promptly replaces the old pressure spline");
        const uint32 changedGoalSpline = f.bot.spline.id;
        ++f.bot.spline.id;
        Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 3 && f.bot.spline.id > changedGoalSpline + 1,
            "a spline issued by another action is not mistaken for the recorded pressure movement");
        f.bot.motion.type = CHASE_MOTION_TYPE;
        Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 4 && f.bot.motion.type == POINT_MOTION_TYPE,
            "an external chase generator cannot reuse point-route authorization");
        f.bot.spline.finalized = true;
        Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 5,
            "an ended point segment continues toward the strategic goal instead of remaining stuck");
        f.ai.canMove = false;
        Check(!f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 5 &&
            f.context.integers["warsong pressure spline"].data == 0,
            "a bot that cannot move does not claim that its old active route is still usable");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        f.bot.spline.id = 17; f.bot.spline.finalized = false; f.bot.motion.type = POINT_MOTION_TYPE;
        f.action.launchMotion = false;
        Check(f.action.moveWarsongObjective(false, false) && f.context.integers["warsong pressure spline"].data == 0 &&
            !f.context.positions.data["warsong pressure route"].isSet(),
            "success without a newly launched spline cannot adopt another action's active movement");
        Check(f.action.moveWarsongObjective(false, false) && f.action.orders.size() == 2,
            "an empty movement success does not suppress a later genuine dispatch attempt");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective();
        f.bot.spline.id = 17; f.bot.spline.finalized = false; f.bot.motion.type = POINT_MOTION_TYPE;
        f.action.moveResult = false;
        Check(!f.action.moveWarsongObjective(false, false) && f.context.integers["warsong pressure spline"].data == 0 &&
            !f.context.positions.data["warsong pressure route"].isSet(),
            "a failed movement dispatch cannot authorize reuse of an unrelated old spline");
    }
    {
        Fixture f; f.OurTeamCarries(); f.action.refreshWarsongObjective(); f.bot.x = 1510; f.bot.z = 352;
        f.action.waypointResult = true;
        Check(f.action.moveWarsongObjective(false, false) && f.action.waypoints == 1,
            "an elevated pressure player begins its established exit segment");
        Check(f.action.moveWarsongObjective(false, false) && f.action.waypoints == 1,
            "the active elevated exit segment is also reused instead of restarted every update");
        f.bot.spline.finalized = true;
        Check(f.action.moveWarsongObjective(false, false) && f.action.waypoints == 2,
            "completing an elevated exit segment allows the next waypoint to be dispatched");
    }
    for (unsigned team : {ALLIANCE, HORDE})
        for (bool midfield : {false, true})
        {
            Fixture f; f.bot.team = team; f.bot.carrier = true;
            f.OurTeamCarries(); f.ai.enemy.carrier = f.bot.guid;
            f.bot.x = midfield ? 1227.446f : (team == ALLIANCE ? 1125.778f : 1351.759f);
            f.bot.y = midfield ? 1476.235f : 1462.368f;
            f.bot.z = midfield ? 307.484f : 323.674f;
            Check(f.action.refreshWarsongObjective() && !IsPressuring(&f.ai),
                "a carrier leaving either tunnel keeps its delivery assignment");
            f.action.name = "warsong carry";
            Check(f.action.moveWarsongObjective(true, false) && f.action.orders.size() == 1,
                "the actual carrier movement action continues after the mobility form has settled");
            Check(f.action.orders.front().ignoreEnemyTargets &&
                (team == ALLIANCE ? f.action.orders.front().x > f.bot.x : f.action.orders.front().x < f.bot.x),
                "carrier delivery advances homeward and bypasses unrelated combat clipping");
        }
    std::cout << checks << " Warsong objective checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
