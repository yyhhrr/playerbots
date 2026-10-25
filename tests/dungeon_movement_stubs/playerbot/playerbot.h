#pragma once
#include "Common.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct ObjectGuid
{
    uint64 value = 0;
    uint64 GetRawValue() const { return value; }
    uint32 GetCounter() const { return uint32(value); }
    bool operator==(const ObjectGuid&) const = default;
    bool operator<(const ObjectGuid& other) const { return value < other.value; }
    explicit operator bool() const { return value != 0; }
};

namespace G3D
{
    struct Vector3
    {
        float x = 0, y = 0, z = 0;
        Vector3() = default;
        Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    };
}
namespace Movement
{
    using Vector3 = G3D::Vector3;
    using PointsArray = std::vector<Vector3>;
#ifdef DUNGEON_MOTION_TEST
    struct MoveSpline
    {
        bool finalized = true;
        uint32 id = 0, remaining = 1000;
        Vector3 live{0, 0, 20};
        bool Finalized() const { return finalized; }
        uint32 GetId() const { return id; }
        uint32 Duration() const { return 1000; }
        float GetElapsedValue() const { return 1.0f - float(remaining) / Duration(); }
    };
#endif
}
using Movement::PointsArray;
using Movement::Vector3;
constexpr float M_PI_F = 3.14159265358979323846f;

#ifdef DUNGEON_MOTION_TEST
enum MovementGeneratorType { IDLE_MOTION_TYPE, PATH_MOTION_TYPE, CHASE_MOTION_TYPE };
enum ForcedMovement { FORCED_MOVEMENT_WALK, FORCED_MOVEMENT_RUN };
struct MotionMaster
{
    Movement::MoveSpline* spline;
    std::function<Vector3()> currentPosition;
    unsigned clears = 0, launches = 0;
    MovementGeneratorType type = IDLE_MOTION_TYPE;
    ForcedMovement speed = FORCED_MOVEMENT_RUN;
    bool failLaunch = false;
    PointsArray points, nativePoints;
    MovementGeneratorType GetCurrentMovementGeneratorType() const { return type; }
    void Clear(bool, bool) { ++clears; type = IDLE_MOTION_TYPE; }
    void MovePath(std::vector<G3D::Vector3>& route, ForcedMovement movement, bool, bool = false)
    {
        ++launches;
        points = route;
        nativePoints = {currentPosition()};
        nativePoints.insert(nativePoints.end(), route.begin(), route.end());
        speed = movement;
        spline->live = currentPosition();
        spline->finalized = failLaunch;
        spline->remaining = 1000;
        ++spline->id;
        type = PATH_MOTION_TYPE;
    }
};
namespace WorldTimer
{
    inline uint32 now = 1000;
    inline uint32 getMSTime() { return now; }
    inline uint32 getMSTimeDiff(uint32 from, uint32 to) { return to - from; }
}
#endif

struct Map
{
    uint32 id = 90, instance = 1;
    bool dungeon = true;
    bool lineOfSight = true;
    std::function<float(float, float, float)> ground = [](float, float, float) { return 20.0f; };
    bool IsDungeon() const { return dungeon; }
    uint32 GetId() const { return id; }
    uint32 GetInstanceId() const { return instance; }
    float GetHeight(float x, float y, float z, bool = false) const { return ground(x, y, z); }
    bool IsInLineOfSight(float, float, float, float, float, float, bool = false) const { return lineOfSight; }
};

struct WorldObject
{
    virtual ~WorldObject() = default;
    ObjectGuid guid{1};
    Map* map = nullptr;
    float x = 0.0f, y = 0.0f, z = 20.0f;
    float reach = 1.5f, bounding = 0.4f, orientation = 0.0f;
    bool inWorld = true;
    uint32 GetMapId() const { return map ? map->id : 0; }
    uint32 GetInstanceId() const { return map ? map->instance : 0; }
    Map* GetMap() const { return map; }
    bool IsInWorld() const { return inWorld; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    float GetOrientation() const { return orientation; }
    float GetCombatReach() const { return reach; }
    float GetObjectBoundingRadius() const { return bounding; }
    float GetCollisionHeight() const { return 2.0f; }
    ObjectGuid GetObjectGuid() const { return guid; }
    uint32 GetGUIDLow() const { return uint32(guid.value); }
    void GetPosition(float& ox, float& oy, float& oz) const { ox = x; oy = y; oz = z; }
    bool IsInMap(const WorldObject* other) const { return other && other->map == map; }
    bool IsWithinDistInMap(const WorldObject* other, float distance) const
    {
        if (!IsInMap(other)) return false;
        const float dx = x - other->x, dy = y - other->y, dz = z - other->z;
        return dx * dx + dy * dy + dz * dz <= distance * distance;
    }
    virtual bool IsUnit() const { return false; }
    virtual bool IsPlayer() const { return false; }
};

struct Unit : WorldObject
{
    bool alive = true, flying = false, freeFlying = false, swimming = false, inWater = false;
    bool falling = false, teleporting = false, combat = false, visible = true;
    void* transport = nullptr;
    Unit* victim = nullptr;
    bool IsUnit() const override { return true; }
    bool IsAlive() const { return alive; }
    bool IsDead() const { return !alive; }
    bool IsFlying() const { return flying; }
    bool IsFreeFlying() const { return freeFlying; }
    bool IsSwimming() const { return swimming; }
    bool IsInWater() const { return inWater; }
    bool IsFalling() const { return falling; }
    bool IsBeingTeleported() const { return teleporting; }
    bool IsInCombat() const { return combat; }
    bool IsTaxiFlying() const { return false; }
    void* GetTransport() const { return transport; }
    Unit* GetVictim() const { return victim; }
    Unit* GetTarget() const { return victim; }
    bool CanReachWithMeleeAttack(const Unit* other) const { return IsWithinDistInMap(other, 5.0f); }
    bool IsWithinLOSInMap(const Unit* other) const { return IsInMap(other) && map->lineOfSight && other->visible; }
    bool IsWithinLOS(float, float, float, bool = false) const { return map->lineOfSight && visible; }
    float GetCombinedCombatReach(const Unit*, bool = false) const { return 5.0f; }
    void UpdateAllowedPositionZ(float px, float py, float& pz) const { pz = map->GetHeight(px, py, pz + 0.5f); }
};
struct PlayerbotAI;
enum class BotState { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT, BOT_STATE_DEAD, BOT_STATE_REACTION, BOT_STATE_ALL };
#ifdef DUNGEON_MOTION_TEST
namespace ai { class LastMovement; }
struct DungeonTestAiContext
{
    struct MovementValue
    {
        ai::LastMovement* data = nullptr;
        ai::LastMovement& Get() { return *data; }
    } movement;
    template<class T> MovementValue* GetValue(const char*) { return &movement; }
};
#endif
struct Player : Unit
{
    bool battleGround = false, gm = false;
    PlayerbotAI* ai = nullptr;
#ifdef DUNGEON_MOTION_TEST
    Movement::MoveSpline spline;
    Movement::MoveSpline* movespline = &spline;
    MotionMaster motion{&spline};
    unsigned synchronizations = 0;
    Player() { motion.currentPosition = [this] { return Vector3{x, y, z}; }; }
    MotionMaster* GetMotionMaster() { return &motion; }
    void UpdateSplinePosition(bool)
    {
        ++synchronizations;
        x = spline.live.x; y = spline.live.y; z = spline.live.z;
    }
#endif
    bool IsPlayer() const override { return true; }
    bool InBattleGround() const { return battleGround; }
    bool IsGameMaster() const { return gm; }
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
};
struct PlayerbotAI
{
    Player* bot = nullptr;
    Player* master = nullptr;
    BotState state = BotState::BOT_STATE_NON_COMBAT;
    mutable unsigned stateReads = 0;
    bool dungeonCombatStrategy = false, dungeonNonCombatStrategy = false;
    Player* GetBot() const { return bot; }
    Player* GetMaster() const { return master; }
    Player* GetGroupMaster() const { return master; }
    bool HasActivePlayerMaster() const { return master != nullptr; }
    bool IsSafe(const WorldObject* object) const { return object && object->IsInWorld(); }
    Unit* GetUnit(ObjectGuid guid) const { return master && master->guid == guid ? master : nullptr; }
    BotState GetState() const { ++stateReads; return state; }
    bool IsStateActive(BotState requested) const { return requested == state; }
    bool HasStrategy(const std::string& name, BotState requested) const
    {
        if (name != "dungeon safe") return false;
        return requested == BotState::BOT_STATE_COMBAT ? dungeonCombatStrategy :
            requested == BotState::BOT_STATE_NON_COMBAT && dungeonNonCombatStrategy;
    }
    bool IsRanged(const Player*) const { return false; }
    float GetRange(const char*) const { return 30.0f; }
#ifdef DUNGEON_MOTION_TEST
    DungeonTestAiContext context;
    DungeonTestAiContext* GetAiObjectContext() { return &context; }
    bool canMove = true;
    unsigned stops = 0;
    bool CanMove() const { return canMove; }
    void StopMoving()
    {
        ++stops;
        bot->spline.finalized = true;
        bot->motion.type = IDLE_MOTION_TYPE;
    }
#endif
};
struct ServerFacade
{
    Unit* GetChaseTarget(const Player*) const { return nullptr; }
    float GetChaseAngle(const Player*) const { return 0.0f; }
    float GetChaseOffset(const Player*) const { return 0.0f; }
    bool IsAlive(const Unit* unit) const { return unit->IsAlive(); }
    float GetDistance2d(const WorldObject* first, const WorldObject* second) const
    {
        return std::hypot(first->x - second->x, first->y - second->y);
    }
};
inline ServerFacade sServerFacade;
struct PlayerbotAIConfig
{
    float sightDistance = 60.0f, targetPosRecalcDistance = 1.0f;
};
inline PlayerbotAIConfig sPlayerbotAIConfig;
namespace MaNGOS
{
    inline bool IsValidMapCoord(float x, float y, float z, float = 0.0f)
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
            std::abs(x) < 17066.7f && std::abs(y) < 17066.7f && std::abs(z) < 17066.7f;
    }
}
