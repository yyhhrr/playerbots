#include "playerbot/WarsongCombatProgressPolicy.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
constexpr unsigned STATUS_IN_PROGRESS = 1;
constexpr unsigned CLASS_MAGE = 8, CLASS_WARLOCK = 9, CLASS_PRIEST = 5, CLASS_SHAMAN = 7,
    CLASS_DRUID = 11, CLASS_HUNTER = 3, CLASS_WARRIOR = 1;
constexpr float PET_FOLLOW_DIST = 1, PET_FOLLOW_ANGLE = 0;
enum DistanceCalculation { DIST_CALC_NONE, DIST_CALC_COMBAT_REACH };
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL };
enum SpellState { SPELL_STATE_CREATED, SPELL_STATE_TARGETING, SPELL_STATE_CASTING, SPELL_STATE_DELAYED,
    SPELL_STATE_TRAVELING, SPELL_STATE_LANDING, SPELL_STATE_CHANNELING, SPELL_STATE_FINISHED };
enum MovementGeneratorType { IDLE_MOTION_TYPE, POINT_MOTION_TYPE, CHASE_MOTION_TYPE, FOLLOW_MOTION_TYPE };
struct ObjectGuid
{
    std::uint64_t value = 0;
    bool operator==(const ObjectGuid&) const = default;
    explicit operator bool() const { return value != 0; }
    std::uint64_t GetRawValue() const { return value; }
};
struct Point { float x = 0, y = 0, z = 0; };
struct MoveSpline
{
    bool initialized = false, finalized = true, cyclic = false;
    uint32 id = 1;
    uint32 duration = 10000;
    float elapsed = 0;
    Point destination;
    bool Initialized() const { return initialized; }
    bool Finalized() const { return finalized; }
    bool isCyclic() const { return cyclic; }
    uint32 GetId() const { return id; }
    float GetElapsedValue() const { return elapsed; }
    uint32 Duration() const { return duration; }
    Point FinalDestination() const { return destination; }
};
struct Unit;
struct Player;
struct SpellTargets { Unit* unit = nullptr; Unit* getUnitTarget() const { return unit; } };
struct SpellEntry { const char* SpellName[1] = {"test attack"}; };
struct Spell
{
    SpellState state = SPELL_STATE_CASTING;
    SpellEntry entry;
    SpellEntry* m_spellInfo = &entry;
    SpellTargets m_targets;
    SpellState getState() const { return state; }
    bool CanBeInterrupted() const { return state <= SPELL_STATE_DELAYED || state == SPELL_STATE_CHANNELING; }
};
struct MovementGenerator { Unit* target = nullptr; Unit* GetCurrentTarget() const { return target; } };
struct MotionMaster
{
    MovementGeneratorType type = IDLE_MOTION_TYPE;
    MovementGenerator current;
    unsigned follows = 0;
    MovementGeneratorType GetCurrentMovementGeneratorType() const { return type; }
    MovementGenerator* GetCurrent() { return &current; }
    void MoveFollow(Player*, float, float) { ++follows; type = FOLLOW_MOTION_TYPE; current.target = nullptr; }
};
struct Map { uint32 id = 489; };
struct BattleGround { unsigned status = STATUS_IN_PROGRESS; unsigned GetStatus() const { return status; } };
struct Unit
{
    ObjectGuid guid{1};
    Map* map = nullptr;
    bool alive = true, inWorld = true, attackable = true, visible = true, los = true;
    float x = 0, y = 0, z = 0;
    Unit* victim = nullptr;
    unsigned attackStops = 0;
    MotionMaster motion;
    bool IsInWorld() const { return inWorld; }
    bool IsAlive() const { return alive; }
    Map* GetMap() const { return map; }
    ObjectGuid GetObjectGuid() const { return guid; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    Unit* GetVictim() const { return victim; }
    float GetDistance(float px, float py, float pz, DistanceCalculation) const
    {
        return (x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz);
    }
    MotionMaster* GetMotionMaster() { return &motion; }
    void AttackStop() { ++attackStops; victim = nullptr; }
    bool IsWithinDist3d(float px, float py, float pz, float range) const
    {
        return (x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz) <= range * range;
    }
};
struct Pet : Unit {};
struct Player : Unit
{
    uint32 instance = 1;
    unsigned playerClass = CLASS_WARRIOR, interrupts = 0, selectionChanges = 0, targetChanges = 0;
    bool teleporting = false, carrier = false;
    BattleGround* bg = nullptr;
    MoveSpline spline;
    MoveSpline* movespline = &spline;
    Pet* pet = nullptr;
    ObjectGuid selection;
    Unit* selectedTarget = nullptr;
    std::array<Spell*, 3> spells{};
    std::vector<Unit*> attackers;
    bool IsBeingTeleported() const { return teleporting; }
    uint32 GetMapId() const { return map ? map->id : 0; }
    uint32 GetInstanceId() const { return instance; }
    BattleGround* GetBattleGround() const { return bg; }
    bool CanAttack(Unit* target) const { return target && target->attackable; }
    bool IsWithinLOSInMap(Unit* target) const { return target && target->los; }
    float GetDistance(float px, float py, float pz, DistanceCalculation) const
    {
        return (x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz);
    }
    float GetDistance(Unit* target, bool, DistanceCalculation) const
    {
        return std::sqrt(GetDistance(target->x, target->y, target->z, DIST_CALC_NONE));
    }
    bool CanReachWithMeleeAttack(Unit* target) const { return GetDistance(target, true, DIST_CALC_COMBAT_REACH) <= 3; }
    unsigned getClass() const { return playerClass; }
    const std::vector<Unit*>& getAttackers() const { return attackers; }
    Pet* GetPet() const { return pet; }
    ObjectGuid GetSelectionGuid() const { return selection; }
    void SetSelectionGuid(ObjectGuid next) { selection = next; ++selectionChanges; }
    void SetTarget(Unit* next) { selectedTarget = next; ++targetChanges; }
    Spell* GetCurrentSpell(CurrentSpellTypes type) const { return spells[type]; }
    void InterruptSpell(CurrentSpellTypes type) { ++interrupts; spells[type] = nullptr; }
};
template<class T> struct Value
{
    T data{};
    unsigned resets = 0;
    T& Get() { return data; }
    void Set(const T& next) { data = next; }
    void Reset() { data = {}; ++resets; }
};
struct Context
{
    Value<ai::warsong::CombatProgress> progress;
    std::map<std::string, Value<ObjectGuid>> guids;
    std::map<std::string, Value<std::list<ObjectGuid>>> lists;
    std::map<std::string, Value<bool>> booleans;
    std::map<std::string, Value<int>> integers;
    template<class T> auto GetValue(const std::string& name, const std::string& qualifier = "")
    {
        if constexpr (std::is_same_v<T, ai::warsong::CombatProgress&>) return &progress;
        else if constexpr (std::is_same_v<T, ObjectGuid>) return &guids[name];
        else if constexpr (std::is_same_v<T, bool>) return &booleans[name];
        else if constexpr (std::is_same_v<T, int>) return &integers[qualifier];
        else return &lists[name];
    }
};
struct PlayerbotAI
{
    Player* bot;
    Context context;
    bool canMove = true, stay = false, ranged = false, castable = true, realPlayer = false;
    unsigned stops = 0, wakeups = 0, castChecks = 0;
    std::map<std::string, std::pair<float, float>> spellRanges;
    std::vector<Unit*> units;
    Player* GetBot() const { return bot; }
    Context* GetAiObjectContext() { return &context; }
    bool CanMove() const { return canMove; }
    bool HasStrategy(const char*, unsigned) const { return stay; }
    unsigned GetState() const { return 0; }
    bool IsRanged(Player*) const { return ranged; }
    bool IsRealPlayer() const { return realPlayer; }
    Unit* GetUnit(ObjectGuid guid) const
    {
        for (Unit* target : units) if (target->guid == guid) return target;
        return nullptr;
    }
    bool GetSpellRange(const char* name, float* maximum, float* minimum)
    {
        const auto found = spellRanges.find(name);
        if (found == spellRanges.end()) return false;
        *maximum = found->second.first; *minimum = found->second.second;
        return true;
    }
    bool CanCastSpell(const char*, Unit*, unsigned, void*, bool, bool, bool) { ++castChecks; return castable; }
    void StopMoving() { ++stops; bot->motion.type = IDLE_MOTION_TYPE; bot->spline.finalized = true; }
    void ResetAIInternalUpdateDelay() { ++wakeups; }
    void OuterUpdate();
};
namespace WorldTimer { uint32 now = 10000; uint32 getMSTime() { return now; } }
namespace ai
{
    struct PossibleTargetsValue
    {
        static bool CanPerceive(Unit* target, Player*) { return target && target->visible; }
    };
}
namespace ai::warsong { bool IsCarrier(Player* bot) { return bot->carrier; } }
#include "warsong_combat_progress_source.inc"
using namespace ai;
void PlayerbotAI::OuterUpdate()
{
#include "warsong_combat_progress_outer.inc"
}
namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool result, const char* text)
    {
        ++checks;
        if (!result) { ++failures; std::cerr << "FAILED: " << text << '\n'; }
    }
    struct Fixture
    {
        Map map;
        BattleGround bg;
        Player bot;
        Unit enemy;
        PlayerbotAI ai{&bot};
        Fixture()
        {
            WorldTimer::now = 10000;
            bot.map = enemy.map = &map; bot.bg = &bg;
            bot.guid = {10}; enemy.guid = {20}; enemy.x = 12; enemy.z = 24;
            ai.units = {&enemy};
            ai.context.guids["current target"].data = enemy.guid;
        }
        void Tick(uint32 elapsed) { WorldTimer::now += elapsed; ai.OuterUpdate(); }
    };
}

int main()
{
    using namespace ai::warsong;
    {
        Fixture f;
        f.bot.victim = &f.enemy; f.bot.selection = f.enemy.guid; f.bot.selectedTarget = &f.enemy;
        for (const char* name : {"attack target", "pull target", "old target"}) f.ai.context.guids[name].data = f.enemy.guid;
        f.bot.motion.type = CHASE_MOTION_TYPE; f.bot.motion.current.target = &f.enemy;
        Pet pet; pet.victim = &f.enemy; f.bot.pet = &pet;
        f.Tick(0); f.Tick(PursuitStallDelay - 1);
        Check(f.ai.stops == 0 && f.ai.context.guids["current target"].data == f.enemy.guid,
            "an unreachable-looking enemy is given the complete progress timeout");
        f.Tick(251);
        Check(f.ai.wakeups == 1 && f.ai.stops == 1 && !f.bot.victim && !f.bot.selection && !f.bot.selectedTarget,
            "outer update releases a stalled native target and wakes the tactical engine");
        for (const char* name : {"current target", "attack target", "pull target", "old target"})
            Check(!f.ai.context.guids[name].data, "stalled target cannot survive in explicit target values");
        Check(pet.attackStops == 1 && pet.motion.follows == 1,
            "a pet attacking the same unreachable opponent returns to its owner");
        Check(f.ai.context.lists["attackers"].resets == 1 && f.ai.context.lists["possible attack targets"].resets == 1 &&
            f.ai.context.lists["enemy player targets"].resets == 1 && f.ai.context.guids["enemy player target"].resets == 1 &&
            f.ai.context.booleans["has enemy player targets"].resets == 1,
            "all target-list and best-enemy caches are invalidated after releasing a stalled fight");
        Check(IsCombatTargetSuppressed(&f.ai, &f.enemy), "the old enemy is suppressed instead of immediately reselected");
        f.Tick(PursuitRetryDelay - 1);
        Check(IsCombatTargetSuppressed(&f.ai, &f.enemy), "suppression lasts the entire retry interval");
        f.Tick(1);
        Check(!IsCombatTargetSuppressed(&f.ai, &f.enemy), "a target becomes eligible again after the retry interval");
    }
    for (const auto [playerClass, spell] : std::vector<std::pair<unsigned, const char*>>{
        {CLASS_MAGE, "frostbolt"}, {CLASS_WARLOCK, "shadow bolt"}, {CLASS_PRIEST, "smite"},
        {CLASS_SHAMAN, "lightning bolt"}, {CLASS_DRUID, "wrath"}, {CLASS_HUNTER, "auto shot"}})
    {
        Fixture f; f.ai.ranged = true; f.bot.playerClass = playerClass; f.ai.spellRanges[spell] = {35.0f, 0.0f};
        f.Tick(0); f.Tick(20000);
        Check(CanFightFromHere(&f.ai, &f.enemy) && f.ai.stops == 0 && f.ai.castChecks > 0,
            "a ranged class that can attack from its high platform keeps the valid fight");
        f.enemy.los = false;
        f.Tick(250); f.Tick(PursuitStallDelay);
        Check(f.ai.stops == 1, "losing line of sight cannot preserve a stationary high-platform target indefinitely");
    }
    {
        Fixture f; f.ai.ranged = true; f.bot.playerClass = CLASS_HUNTER; f.enemy.x = 2; f.enemy.z = 6;
        f.ai.spellRanges["auto shot"] = {35.0f, 8.0f};
        Check(!CanFightFromHere(&f.ai, &f.enemy), "hunter minimum range is respected instead of using a generic ranged distance");
        f.Tick(0); f.Tick(PursuitStallDelay);
        Check(f.ai.stops == 1, "a hunter inside its dead zone is allowed to recover from a stalled ledge");
    }
    {
        Fixture f; f.ai.ranged = true; f.bot.playerClass = CLASS_MAGE; f.ai.spellRanges["frostbolt"] = {20.0f, 0.0f};
        Check(!CanFightFromHere(&f.ai, &f.enemy), "the actual learned spell maximum range limits a ranged exemption");
        f.ai.spellRanges["frostbolt"] = {35.0f, 0.0f}; f.ai.castable = false;
        Check(!CanFightFromHere(&f.ai, &f.enemy), "an unusable damaging spell cannot keep an unreachable target engaged");
    }
    for (unsigned pause = 0; pause < 3; ++pause)
    {
        Fixture f; f.Tick(0); f.Tick(5000);
        if (pause == 0) f.ai.canMove = false;
        if (pause == 1) f.ai.stay = true;
        if (pause == 2) f.bot.carrier = true;
        f.Tick(10000);
        Check(f.ai.stops == 0 && !f.ai.context.progress.data.tracking,
            "roots, explicit stay and flag-carrier control do not accumulate chase failure");
        f.ai.canMove = true; f.ai.stay = false; f.bot.carrier = false;
        f.Tick(1); f.Tick(PursuitStallDelay - 1);
        Check(f.ai.stops == 0, "resuming movement gives a fresh progress timeout");
        f.Tick(251); Check(f.ai.stops == 1, "a newly stalled pursuit can still recover after the pause");
    }
    for (unsigned inactive = 0; inactive < 6; ++inactive)
    {
        Fixture f; f.Tick(0);
        f.ai.context.progress.data.Suppress(f.enemy.guid.value, WorldTimer::now);
        if (inactive == 0) f.map.id = 529;
        if (inactive == 1) f.bot.alive = false;
        if (inactive == 2) f.bot.inWorld = false;
        if (inactive == 3) f.bot.teleporting = true;
        if (inactive == 4) f.bg.status = 0;
        if (inactive == 5) f.bot.bg = nullptr;
        f.Tick(20000);
        Check(!f.ai.context.progress.data.active && f.ai.context.progress.data.suppressed.empty() && f.ai.stops == 0,
            "leaving active Warsong clears pursuit and suppression without touching other gameplay");
    }
    {
        Fixture f; f.Tick(0); f.ai.context.progress.data.Suppress(f.enemy.guid.value, WorldTimer::now);
        ++f.bot.instance; f.Tick(1);
        Check(!IsCombatTargetSuppressed(&f.ai, &f.enemy), "a new battleground instance cannot inherit an old suppressed target");
    }
    {
        Fixture f; f.Tick(0); f.Tick(5000); f.enemy.guid = {21}; f.ai.context.guids["current target"].data = {21};
        f.Tick(2000); Check(f.ai.stops == 0, "switching target starts an independent progress observation");
    }
    {
        Fixture f; f.Tick(0); f.Tick(5000); f.bot.x = 5; f.Tick(250); f.Tick(5000);
        Check(f.ai.stops == 0, "real distance gained toward the enemy resets the stall timer");
        f.Tick(1000); Check(f.ai.stops == 1, "remaining stationary after real progress eventually recovers");
    }
    {
        Fixture f; f.bot.spline.initialized = true; f.bot.spline.finalized = false;
        f.bot.spline.destination = {f.enemy.x, f.enemy.y, f.enemy.z};
        f.Tick(0); f.Tick(5000); f.bot.x = -3; f.bot.spline.elapsed = 0.25f; f.Tick(250); f.Tick(5000);
        Check(f.ai.stops == 0, "actual progress along a complete same spline can initially lead around a ramp away from the enemy");
    }
    for (unsigned falseProgress = 0; falseProgress < 5; ++falseProgress)
    {
        Fixture f; f.bot.spline.initialized = true; f.bot.spline.finalized = false;
        f.bot.spline.cyclic = falseProgress == 0;
        if (falseProgress == 4) f.bot.spline.duration = 0;
        f.bot.spline.destination = falseProgress == 1 ? Point{-30, 0, 0} : Point{f.enemy.x, f.enemy.y, f.enemy.z};
        f.Tick(0); f.Tick(5000);
        if (falseProgress != 3) f.bot.x = -3;
        if (falseProgress == 2) ++f.bot.spline.id;
        f.bot.spline.elapsed = 0.25f; f.Tick(1000);
        Check(f.ai.stops == 1, "cyclic routes, wrong destinations, restarted splines, stationary clocks and zero-duration paths cannot fake chase progress");
    }
    {
        Fixture f; f.Tick(0); f.Tick(PursuitStallDelay);
        f.enemy.x = f.bot.x + 1; f.enemy.z = f.bot.z;
        Check(!IsCombatTargetSuppressed(&f.ai, &f.enemy) && CanFightFromHere(&f.ai, &f.enemy),
            "a suppressed enemy that comes into real melee range remains available for self-defence");
    }
    {
        Fixture f; f.ai.context.guids["current target"].data = {};
        f.enemy.victim = &f.bot; f.bot.attackers = {&f.enemy};
        f.Tick(0); f.Tick(PursuitStallDelay);
        Check(!IsCombatTargetSuppressed(&f.ai, &f.enemy) && f.ai.stops == 0,
            "an enemy's one-sided attack pointer cannot interrupt the bot's independent tactical movement");
    }
    {
        Fixture f; f.enemy.x = 1; f.enemy.z = 0;
        f.Tick(0); f.Tick(20000);
        Check(f.ai.stops == 0 && CanFightFromHere(&f.ai, &f.enemy), "a valid close fight is preserved for melee classes");
    }
    {
        Fixture f; f.enemy.los = false; f.bot.victim = &f.enemy;
        Spell pending, flying; pending.m_targets.unit = flying.m_targets.unit = &f.enemy; flying.state = SPELL_STATE_TRAVELING;
        f.bot.spells[CURRENT_GENERIC_SPELL] = &pending; f.bot.spells[CURRENT_AUTOREPEAT_SPELL] = &flying;
        f.Tick(0); f.Tick(PursuitStallDelay);
        Check(f.bot.interrupts == 1 && !f.bot.spells[CURRENT_GENERIC_SPELL] && f.bot.spells[CURRENT_AUTOREPEAT_SPELL] == &flying,
            "release interrupts blocked pending targeted casts while preserving already released projectiles");
    }
    {
        Fixture f; WorldTimer::now = 0xfffffff0u; f.Tick(0); f.Tick(PursuitStallDelay);
        Check(f.ai.stops == 1 && IsCombatTargetSuppressed(&f.ai, &f.enemy),
            "stall and retry timing remain valid across the millisecond counter wrap");
        f.Tick(PursuitRetryDelay); Check(!IsCombatTargetSuppressed(&f.ai, &f.enemy), "suppression expires correctly after timer wrap");
    }
    {
        Fixture f; f.ai.realPlayer = true;
        f.ai.context.progress.data.active = true; f.ai.context.progress.data.instance = 1;
        f.ai.context.progress.data.Suppress(f.enemy.guid.value, WorldTimer::now);
        f.Tick(0); f.Tick(20000);
        Check(f.ai.stops == 0 && !f.ai.context.progress.data.active && !IsCombatTargetSuppressed(&f.ai, &f.enemy),
            "a real player using playerbot helpers never inherits automatic pursuit recovery");
    }
    {
        Fixture f; f.Tick(0); f.Tick(5000);
        f.ai.context.integers["warsong graveyard spline"].data = int(f.bot.spline.id);
        f.bot.spline.initialized = true; f.bot.spline.finalized = false; f.bot.motion.type = POINT_MOTION_TYPE;
        f.Tick(20000);
        Check(f.ai.stops == 0 && !f.ai.context.progress.data.tracking,
            "an active owned graveyard descent is not mistaken for a stalled selected enemy");
    }
    {
        Fixture f; Spell attack; attack.m_targets.unit = &f.enemy;
        f.ai.spellRanges["test attack"] = {35.0f, 0.0f}; f.bot.spells[CURRENT_GENERIC_SPELL] = &attack;
        Check(CanFightFromHere(&f.ai, &f.enemy), "a genuine pending attack at legal range preserves its current fight");
        attack.state = SPELL_STATE_FINISHED;
        Check(!CanFightFromHere(&f.ai, &f.enemy), "a finished cached spell cannot permanently excuse a stalled target");
        attack.state = SPELL_STATE_CASTING; f.ai.spellRanges["test attack"] = {20.0f, 0.0f};
        Check(!CanFightFromHere(&f.ai, &f.enemy), "an active spell still must be within its real attack range");
    }
    {
        CombatProgress state;
        state.Suppress(21, 1000); state.Suppress(22, 1250);
        Check(state.IsSuppressed(21, 2000) && state.IsSuppressed(22, 2000),
            "two stalled opponents cannot alternate around a single-target suppression slot");
        state.Prune(9000);
        Check(!state.IsSuppressed(21, 9000) && state.IsSuppressed(22, 9000),
            "each suppressed opponent expires on its independent retry deadline");
        for (unsigned guid = 100; guid < 180; ++guid) state.Suppress(guid, 10000 + guid);
        Check(state.suppressed.size() == 64, "a battleground's temporary failed-target memory remains bounded");
    }
    std::cout << checks << " Warsong combat progress checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
