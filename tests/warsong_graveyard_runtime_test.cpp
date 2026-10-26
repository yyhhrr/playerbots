#include "playerbot/WarsongGraveyardPolicy.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using uint32 = std::uint32_t;
constexpr unsigned BATTLEGROUND_WS = 2, STATUS_IN_PROGRESS = 1, FORCED_MOVEMENT_RUN = 1;
enum MovementGeneratorType { IDLE_MOTION_TYPE, POINT_MOTION_TYPE, CHASE_MOTION_TYPE };
struct Position { float x, y, z, o = 0; };
struct WorldPosition
{
    uint32 map;
    float x, y, z;
    WorldPosition(uint32 id, float px, float py, float pz) : map(id), x(px), y(py), z(pz) {}
    float sqDistance(const WorldPosition& other) const
    {
        return map == other.map ? (x - other.x) * (x - other.x) +
            (y - other.y) * (y - other.y) + (z - other.z) * (z - other.z) : 1e10f;
    }
};
using HazardPosition = std::pair<WorldPosition, float>;
struct Map
{
    float ground = 324;
    mutable unsigned queries = 0;
    float GetHeight(float, float, float) const { ++queries; return ground; }
};
struct BattleGround
{
    unsigned type = BATTLEGROUND_WS, status = STATUS_IN_PROGRESS;
    uint32 GetMapId() const { return 489; }
    unsigned GetTypeId() const { return type; }
    unsigned GetStatus() const { return status; }
};
struct MoveSpline
{
    uint32 id = 1;
    bool finalized = true;
    uint32 GetId() const { return id; }
    bool Finalized() const { return finalized; }
};
struct NativeOrder { uint32 map; float x, y, z; unsigned mode; bool generatePath; };
struct MotionMaster
{
    MoveSpline* spline;
    MovementGeneratorType type = IDLE_MOTION_TYPE;
    unsigned clears = 0;
    bool accept = true;
    std::vector<NativeOrder> orders;
    MovementGeneratorType GetCurrentMovementGeneratorType() const { return type; }
    void Clear() { ++clears; type = IDLE_MOTION_TYPE; spline->finalized = true; }
    void MovePoint(uint32 map, float x, float y, float z, unsigned mode, bool generatePath)
    {
        orders.push_back({map, x, y, z, mode, generatePath});
        if (accept) { ++spline->id; spline->finalized = false; type = POINT_MOTION_TYPE; }
    }
};
struct Player
{
    Position position{};
    uint32 mapId = 489;
    bool alive = true, inWorld = true, teleporting = false, flying = false, water = false;
    bool combat = false, casting = false;
    void* transport = nullptr;
    BattleGround* bg = nullptr;
    Map map;
    MoveSpline spline;
    MoveSpline* movespline = &spline;
    MotionMaster motion{&spline};
    uint32 GetMapId() const { return mapId; }
    float GetPositionX() const { return position.x; }
    float GetPositionY() const { return position.y; }
    float GetPositionZ() const { return position.z; }
    bool IsInWorld() const { return inWorld; }
    bool IsAlive() const { return alive; }
    bool IsBeingTeleported() const { return teleporting; }
    bool IsFlying() const { return flying; }
    bool IsInWater() const { return water; }
    bool IsInCombat() const { return combat; }
    bool IsNonMeleeSpellCasted(bool, bool, bool) const { return casting; }
    void* GetTransport() const { return transport; }
    BattleGround* GetBattleGround() const { return bg; }
    Map* GetMap() { return &map; }
    MotionMaster* GetMotionMaster() { return &motion; }
};
namespace ai
{
    struct PositionEntry { float x = 0, y = 0, z = 0; };
    using PositionMap = std::map<std::string, PositionEntry>;
}
template<class T> struct Value
{
    T data{};
    T& Get() { return data; }
    void Set(const T& value) { data = value; }
};
struct Context
{
    Value<ai::PositionMap> positions;
    std::map<std::string, Value<int>> integers;
    std::map<std::string, Value<bool>> booleans;
    Value<uint32> role;
    Value<std::list<HazardPosition>> hazards;
    template<class T> auto GetValue(const std::string&, const std::string& qualifier = "")
    {
        if constexpr (std::is_same_v<T, ai::PositionMap&>) return &positions;
        else if constexpr (std::is_same_v<T, int>) return &integers[qualifier];
        else if constexpr (std::is_same_v<T, bool>) return &booleans[qualifier];
        else if constexpr (std::is_same_v<T, uint32>) return &role;
        else return &hazards;
    }
};
struct PlayerbotAI
{
    bool canMove = true;
    unsigned interrupts = 0;
    bool CanMove() const { return canMove; }
    void InterruptSpell(bool) { ++interrupts; }
};
struct RouteOrder { uint32 map; float x, y, z; bool noPath; };
struct BGTactics
{
    Player* bot;
    PlayerbotAI* ai;
    Context* context;
    unsigned movementUpdates = 0, idleClears = 0, duration = 0;
    std::vector<RouteOrder> routeOrders;
    bool moveWarsongGraveyardDrop(bool);
    bool wsgPaths();
    bool wsgRoofJump();
    void UpdateMovementState() { ++movementUpdates; }
    void ClearIdleState() { ++idleClears; }
    void SetDuration(uint32 next) { duration = next; }
    bool moveToWarsongPoint(uint32 map, float x, float y, float z,
        bool = false, bool = false, bool noPath = false)
    {
        routeOrders.push_back({map, x, y, z, noPath});
        return true;
    }
};
float frand(float low, float high) { return (low + high) / 2; }
uint32 urand(uint32 low, uint32) { return low; }
using namespace ai;
#include "warsong_movement_source.inc"
#include "warsong_graveyard_drop.inc"

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool result, const char* text)
    {
        ++checks;
        if (!result) { ++failures; std::cerr << "FAILED: " << text << '\n'; }
    }
    bool Near(float a, float b) { return std::abs(a - b) < 0.002f; }
    struct Fixture
    {
        BattleGround bg;
        Player bot;
        PlayerbotAI ai;
        Context values;
        BGTactics action{&bot, &ai, &values};
        explicit Fixture(bool alliance)
        {
            bot.bg = &bg;
            const auto exit = ai::warsong::GetGraveyardExit(alliance);
            bot.position = {exit.edge.x, exit.edge.y, exit.edge.z};
            bot.map.ground = exit.landing.z;
            values.positions.data["bg objective"] = {1227.446f, 1476.235f, 307.484f};
        }
    };
}

int main()
{
    using namespace ai::warsong;
    for (bool alliance : {false, true})
    {
        const auto exit = GetGraveyardExit(alliance);
        Fixture f(alliance);
        f.bot.casting = true;
        Check(f.action.moveWarsongGraveyardDrop(alliance), "checked graveyard drop launches");
        Check(f.bot.motion.orders.size() == 1 && f.action.routeOrders.empty(),
            "drop bypasses ordinary route resolution and clipping");
        const auto order = f.bot.motion.orders.front();
        Check(order.map == 489 && Near(order.x, exit.landing.x) && Near(order.y, exit.landing.y) &&
            Near(order.z, exit.landing.z) && !order.generatePath && order.mode == FORCED_MOVEMENT_RUN,
            "native movement receives the actual lower landing without navmesh generation");
        Check(f.action.duration == 500 && f.values.integers["warsong graveyard spline"].data == int(f.bot.spline.id),
            "only an accepted native point spline is recorded as owned");
        Check(f.ai.interrupts == 1 && f.action.movementUpdates == 1 && f.action.idleClears == 1,
            "drop uses normal movement-state and casting preparation");
        f.bot.position = {exit.landing.x, exit.landing.y, exit.landing.z + 8};
        Check(f.action.moveWarsongGraveyardDrop(alliance) && f.bot.motion.orders.size() == 1 && f.bot.motion.clears == 1,
            "an active owned descent is reused after leaving the upper edge");
    }
    for (bool alliance : {false, true})
        for (unsigned invalid = 0; invalid < 14; ++invalid)
        {
            Fixture f(alliance);
            if (invalid == 0) f.bot.mapId = 529;
            if (invalid == 1) f.bg.type = 3;
            if (invalid == 2) f.bg.status = 0;
            if (invalid == 3) f.bot.bg = nullptr;
            if (invalid == 4) f.bot.alive = false;
            if (invalid == 5) f.bot.inWorld = false;
            if (invalid == 6) f.bot.teleporting = true;
            if (invalid == 7) f.bot.transport = &f.bg;
            if (invalid == 8) f.bot.flying = true;
            if (invalid == 9) f.bot.water = true;
            if (invalid == 10) f.ai.canMove = false;
            if (invalid == 11) f.bot.position.x += 100;
            if (invalid == 12) f.bot.map.ground += 5;
            if (invalid == 13) f.bot.map.ground = std::numeric_limits<float>::quiet_NaN();
            Check(!f.action.moveWarsongGraveyardDrop(alliance) && f.bot.motion.orders.empty(),
                "unsafe world, control, edge or landing conditions cannot issue a direct descent");
        }
    for (bool alliance : {false, true})
    {
        Fixture f(alliance);
        const auto exit = GetGraveyardExit(alliance);
        f.bot.position = alliance ? Position{1415, 1554, 343} : Position{1033, 1386, 340};
        Check(f.action.moveWarsongGraveyardDrop(alliance) && f.action.routeOrders.size() == 1 && f.bot.motion.orders.empty(),
            "a valid resurrection-platform position first approaches the edge instead of attempting a long direct drop");
        const auto order = f.action.routeOrders.front();
        Check(Near(order.x, exit.edge.x) && Near(order.y, exit.edge.y) && Near(order.z, exit.edge.z) && !order.noPath,
            "the approach to the known edge retains ordinary pathfinding");
    }
    for (bool alliance : {false, true})
    {
        Fixture f(alliance);
        const auto exit = GetGraveyardExit(alliance);
        f.values.hazards.data.push_back({WorldPosition(489, exit.landing.x, exit.landing.y, exit.landing.z), 3.0f});
        Check(!f.action.moveWarsongGraveyardDrop(alliance) && f.bot.motion.orders.empty(),
            "known hazards at the landing block a direct descent");
    }
    for (bool alliance : {false, true})
        for (unsigned stale = 0; stale < 3; ++stale)
        {
            Fixture f(alliance);
            f.values.integers["warsong graveyard spline"].data = int(f.bot.spline.id);
            f.bot.spline.finalized = stale == 0;
            f.bot.motion.type = stale == 1 ? CHASE_MOTION_TYPE : POINT_MOTION_TYPE;
            if (stale == 2) ++f.bot.spline.id;
            Check(f.action.moveWarsongGraveyardDrop(alliance) && f.bot.motion.orders.size() == 1,
                "finished, foreign and changed splines are not adopted as an owned descent");
        }
    for (bool alliance : {false, true})
    {
        Fixture f(alliance);
        f.bot.motion.accept = false;
        Check(!f.action.moveWarsongGraveyardDrop(alliance) && !f.values.integers["warsong graveyard spline"].data,
            "a rejected native spline reports failure instead of claiming to descend");
    }
    for (bool alliance : {false, true})
        for (uint32 role = 0; role < 10; ++role)
        {
            Fixture f(alliance);
            f.bot.position = alliance ? Position{1415, 1554, 343} : Position{1033, 1386, 340};
            f.values.role.data = role;
            Check(f.action.wsgPaths() && f.action.routeOrders.size() == 1,
                "every route first approaches the known graveyard edge");
            const auto edgeOrder = f.action.routeOrders.back();
            f.bot.position = {edgeOrder.x, edgeOrder.y, edgeOrder.z};
            f.action.routeOrders.clear();
            Check(f.action.wsgPaths() && f.action.routeOrders.empty() && f.bot.motion.orders.size() == 1,
                "every complete graveyard route reaches the native descent executor");
            const auto landing = f.bot.motion.orders.back();
            Check(!landing.generatePath && landing.z < 330,
                "both factions and all route preferences actually dispatch a lower landing");
        }
    Check(!CanStartGraveyardDrop(false, {1055.182251f, 1396.967529f, 339.361511f}, 335),
        "a landing less than eight yards below cannot masquerade as the fixed descent");
    Check(!CanStartGraveyardDrop(true, {1422.106201f, 1529.851196f, 380}, 322.5f),
        "an unrelated higher platform cannot use the graveyard direct movement");
    std::cout << checks << " Warsong graveyard runtime checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
