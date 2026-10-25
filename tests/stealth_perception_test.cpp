// Execute production perception and target-validation entry points against a
// controllable native visibility result. Detection mathematics stay in the core.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
using uint8 = std::uint8_t;
struct ObjectGuid
{
    uint32 value = 0;
    bool operator==(const ObjectGuid&) const = default;
    bool operator<(const ObjectGuid& other) const { return value < other.value; }
    explicit operator bool() const { return value != 0; }
};
enum AuraType { SPELL_AURA_SPIRIT_OF_REDEMPTION, SPELL_AURA_MOD_STEALTH,
    SPELL_AURA_MOD_INVISIBILITY, SPELL_AURA_MOD_CHARM, SPELL_AURA_MOD_POSSESS, SPELL_AURA_MOD_STALKED };
enum Visibility { VISIBILITY_ON, VISIBILITY_OFF, VISIBILITY_GROUP_STEALTH,
    VISIBILITY_GROUP_INVISIBILITY, VISIBILITY_RESPAWN };
enum UnitFields { UNIT_FIELD_FLAGS };
constexpr uint32 UNIT_FLAG_NOT_ATTACKABLE_1 = 1, UNIT_FLAG_UNTARGETABLE = 2, UNIT_FLAG_UNINTERACTIBLE = 4,
    UNIT_FLAG_PLAYER_CONTROLLED = 8;
constexpr float MAX_PLAYER_STEALTH_DETECT_RANGE = 45.0f;
enum DistanceCalculation { DIST_CALC_NONE };
enum class BotState { BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT };
struct Map { uint32 id = 489; };
struct WorldObject
{
    virtual ~WorldObject() = default;
    Map* map = nullptr;
    bool inWorld = true;
    bool front = true;
    float distance = 8.0f, x = 0, y = 0, z = 0;
    bool IsInWorld() const { return inWorld; }
    Map* GetMap() const { return map; }
    uint32 GetMapId() const { return map ? map->id : 0; }
    virtual bool IsUnit() const { return false; }
    bool isInFrontInMap(const WorldObject* target, float range) const { return front && target->distance <= range; }
    void GetPosition(float& ox, float& oy, float& oz) const { ox = x; oy = y; oz = z; }
};
struct Unit;
struct Player;
struct Pet;
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL };
enum SpellState { SPELL_STATE_CREATED, SPELL_STATE_TARGETING, SPELL_STATE_CASTING, SPELL_STATE_DELAYED,
    SPELL_STATE_TRAVELING, SPELL_STATE_LANDING, SPELL_STATE_CHANNELING, SPELL_STATE_FINISHED };
enum SpellCastResult { SPELL_CAST_OK, SPELL_FAILED_BAD_TARGETS };
struct Item {};
struct SpellTargets
{
    Unit* unit = nullptr;
    Unit* getUnitTarget() const { return unit; }
};
struct Spell
{
    SpellState state = SPELL_STATE_CASTING;
    SpellTargets m_targets;
    bool CanBeInterrupted() const { return state <= SPELL_STATE_DELAYED || state == SPELL_STATE_CHANNELING; }
};
enum MovementGeneratorType { IDLE_MOTION_TYPE, POINT_MOTION_TYPE, CHASE_MOTION_TYPE, FOLLOW_MOTION_TYPE };
struct MovementGenerator
{
    Unit* target = nullptr;
    Unit* GetCurrentTarget() const { return target; }
};
struct MotionMaster
{
    MovementGeneratorType type = IDLE_MOTION_TYPE;
    MovementGenerator current;
    unsigned follows = 0;
    MovementGeneratorType GetCurrentMovementGeneratorType() const { return type; }
    MovementGenerator* GetCurrent() { return &current; }
    void MoveFollow(Player*, float, float) { ++follows; type = FOLLOW_MOTION_TYPE; current.target = nullptr; }
};
constexpr float PET_FOLLOW_DIST = 1.0f, PET_FOLLOW_ANGLE = 0.0f;
struct ThreatManager
{
    float oldThreat = 0;
    float getThreat(const Unit*) const { return oldThreat; }
};
struct VisibilityData
{
    float stealthDistance = 10.0f;
    mutable const Unit* lastObserver = nullptr;
    mutable bool lastPlayerControlled = false;
    float GetStealthVisibilityDistance(const Unit* observer, bool controlled) const
    {
        lastObserver = observer; lastPlayerControlled = controlled; return stealthDistance;
    }
};
struct Unit : WorldObject
{
    ObjectGuid guid{2}, masterGuid;
    bool alive = true, friendly = false, attackable = true, detected = true;
    bool reachable = true, arathiAllowed = true, warsongAllowed = true, dungeonAllowed = true;
    bool damageImmune = false, breakableCC = false, unbreakableCC = false;
    uint32 flags = 0;
    Visibility visibility = VISIBILITY_ON;
    std::array<bool, 6> auras{};
    bool stunned = false, revealed = false, los = true;
    ObjectGuid markCaster;
    Unit* victim = nullptr;
    ThreatManager threat;
    MotionMaster motion;
    std::array<Spell*, 3> spells{};
    std::map<const Unit*, bool> perceptionByObserver;
    std::vector<Unit*> attackers;
    VisibilityData visibilityData;
    unsigned attackStops = 0, interrupts = 0;
    mutable unsigned detectionCalls = 0;
    mutable const Unit* lastObserver = nullptr;
    mutable const WorldObject* lastViewPoint = nullptr;
    mutable bool lastDetect = false, last3d = false;
    bool IsUnit() const override { return true; }
    bool IsAlive() const { return alive; }
    bool IsDead() const { return !alive; }
    virtual bool IsPlayer() const { return false; }
    Unit* GetVictim() const { return victim; }
    ThreatManager& getThreatManager() { return threat; }
    bool HasFlag(UnitFields, uint32 flag) const { return (flags & flag) != 0; }
    bool HasAuraType(AuraType aura) const { return auras[aura]; }
    bool HasAuraTypeWithCaster(AuraType aura, ObjectGuid caster) const { return auras[aura] && markCaster == caster; }
    bool IsFogOfWarVisibleStealth(const Unit*) const { return revealed; }
    bool IsStunned() const { return stunned; }
    bool CanAttack(const Unit* target) const { return target->attackable; }
    const std::vector<Unit*>& getAttackers() const { return attackers; }
    const VisibilityData& GetVisibilityData() const { return visibilityData; }
    float GetDistance(const WorldObject*, bool, DistanceCalculation) const { return distance * distance; }
    bool IsWithinLOS(float, float, float) const { return los; }
    Visibility GetVisibility() const { return visibility; }
    ObjectGuid GetObjectGuid() const { return guid; }
    ObjectGuid GetMasterGuid() const { return masterGuid; }
    std::string GetName() const { return "target"; }
    MotionMaster* GetMotionMaster() { return &motion; }
    Spell* GetCurrentSpell(CurrentSpellTypes type) const { return spells[type]; }
    void AttackStop()
    {
        ++attackStops;
        if (victim) std::erase(victim->attackers, this);
        victim = nullptr;
    }
    void InterruptSpell(CurrentSpellTypes type)
    {
        ++interrupts;
        if (spells[type]) spells[type]->state = SPELL_STATE_FINISHED;
        spells[type] = nullptr;
    }
    bool IsVisibleForOrDetect(const Unit* observer, const WorldObject* viewPoint, bool detect,
        bool = false, bool is3d = true, bool = false) const
    {
        ++detectionCalls;
        lastObserver = observer; lastViewPoint = viewPoint; lastDetect = detect; last3d = is3d;
        // Match the native historical-combat shortcut, which the module must
        // not let its own old attacks sustain after stealth has become hidden.
        if (visibility == VISIBILITY_GROUP_STEALTH && !attackers.empty()) return true;
        const auto found = perceptionByObserver.find(observer);
        return found == perceptionByObserver.end() ? detected : found->second;
    }
};
struct Creature : Unit
{
    float attackDistance = 20.0f;
    float GetAttackDistance(const Unit*) const { return attackDistance; }
};
struct Pet : Creature {};
struct Camera
{
    WorldObject body;
    WorldObject* GetBody() { return &body; }
};
struct PlayerbotAI;
struct Player : Unit
{
    bool combat = false, teleporting = false, gm = false;
    Camera camera;
    PlayerbotAI* ai = nullptr;
    ObjectGuid selected{2};
    Unit* selectedTarget = nullptr;
    Pet* pet = nullptr;
    bool IsPlayer() const override { return true; }
    bool IsInCombat() const { return combat; }
    bool IsInMap(const WorldObject* other) const { return other && other->map == map; }
    Camera& GetCamera() { return camera; }
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
    bool IsGameMaster() const { return gm; }
    bool IsWithinDistInMap(const Unit* target, float range) const { return target->map == map && target->distance <= range; }
    bool IsInGroup(const Unit*) const { return false; }
    ObjectGuid GetSelectionGuid() const { return selected; }
    void SetSelectionGuid(ObjectGuid next) { selected = next; }
    void SetTarget(Unit* target) { selectedTarget = target; }
    Pet* GetPet() const { return pet; }
    bool IsBeingTeleported() const { return teleporting; }
};
template<class T> struct Value
{
    T data{};
    T& Get() { return data; }
    void Set(const T& next) { data = next; }
    unsigned resets = 0;
    void Reset() { data = {}; ++resets; }
};
struct Context
{
    std::map<std::string, Value<ObjectGuid>> guids;
    Value<std::list<ObjectGuid>> oldAttackers;
    Value<std::list<ObjectGuid>> attackers, enemies;
    Value<bool> hasEnemies;
    template<class T> auto GetValue(const std::string& name)
    {
        if constexpr (std::is_same_v<T, ObjectGuid>) return &guids[name];
        else if constexpr (std::is_same_v<T, bool>) return &hasEnemies;
        else return name == "attackers" ? &attackers : name == "enemy player targets" ? &enemies : &oldAttackers;
    }
};
struct PlayerbotAI
{
    Player* bot = nullptr;
    Context context;
    std::map<uint32, Unit*> units;
    bool canMove = true, freeAttack = true;
    unsigned stops = 0, wakeups = 0, internalDelay = 8000, castStages = 0;
    Unit* GetUnit(ObjectGuid guid) const { return units.contains(guid.value) ? units.at(guid.value) : nullptr; }
    Player* GetBot() const { return bot; }
    bool IsInVehicle() const { return false; }
    bool CanMove() const { return canMove; }
    bool IsSafe(const WorldObject* object) const { return object && object->IsInWorld() && object->GetMap() == bot->GetMap(); }
    void StopMoving() { ++stops; bot->motion.type = IDLE_MOTION_TYPE; bot->motion.current.target = nullptr; }
    Context* GetAiObjectContext() { return &context; }
    void ResetAIInternalUpdateDelay() { ++wakeups; internalDelay = 0; }
    void OuterUpdate();
    bool CanCastSpell(uint32, Unit*, uint8, bool, Item*, bool, bool, bool, SpellCastResult*);
    bool CastSpell(uint32, Unit*, Item*, bool, uint32*);
    bool CastPetSpell(uint32, Unit*);
    bool TellPlayerNoFacing(Player*, const std::string&) { return true; }
};
struct ServerFacade
{
    bool UnitIsDead(Unit* target) const { return !target->alive; }
    bool IsFriendlyTo(const Unit* first, const Unit* second) const { return first->friendly || second->friendly; }
    bool IsHostileTo(const Unit* observer, const Unit* target) const { return !IsFriendlyTo(observer, target); }
    float GetDistance2d(const Unit*, const Unit* target) const { return target->distance; }
} sServerFacade;
struct PlayerbotAIConfig { float sightDistance = 60.0f; } sPlayerbotAIConfig;

namespace ai
{
    namespace perception
    {
        bool CanUseTarget(PlayerbotAI*, Unit*);
        bool ReleaseLostTargets(PlayerbotAI*);
    }
    class PossibleTargetsValue
    {
    public:
        static bool IsFriendly(Unit*, Player*);
        static bool IsAttackable(Unit*, Player*);
        static bool CanPerceive(Unit*, Player*);
        static bool CanPerceive(Unit*, Unit*, const WorldObject*);
        static bool IsValid(Unit*, Player*, bool = false);
    };
    struct CanFreeMoveValue
    {
        static bool CanFreeAttack(PlayerbotAI* ai, Unit*) { return !ai || ai->freeAttack; }
    };
    namespace dungeon
    {
        bool CanTarget(PlayerbotAI*, const WorldObject* target)
        {
            const Unit* unit = dynamic_cast<const Unit*>(target);
            return !unit || unit->dungeonAllowed;
        }
        bool IsActive(PlayerbotAI*) { return false; }
    }
    class PossibleAttackTargetsValue
    {
    public:
        static bool IsValid(Unit* target, Player* player)
        {
            return PossibleTargetsValue::IsValid(target, player) && target->attackable;
        }
        static bool IsPossibleTarget(Unit*, Player*, float, bool = false);
        static bool IsImmuneToDamage(Unit* target, Player*) { return target->damageImmune; }
        static bool HasIgnoreCCRti(Unit*, Player*) { return false; }
        static bool HasBreakableCC(Unit* target, Player*) { return target->breakableCC; }
        static bool HasUnBreakableCC(Unit* target, Player*) { return target->unbreakableCC; }
        static bool IsCcTarget(Unit*, Player*) { return false; }
        static bool IsTapped(Unit*, Player*) { return true; }
    };
    class EnemyPlayersValue
    {
    public:
        static bool IsValid(Unit*, Player*);
        static bool IsReachable(Unit* target, Player*) { return target->reachable; }
        static bool IsAllowedArathiDefenderTarget(Unit* target, Player*) { return target->arathiAllowed; }
        static bool IsAllowedWarsongTarget(Unit* target, Player*) { return target->warsongAllowed; }
    };
    class InvalidTargetValue
    {
    public:
        PlayerbotAI* ai;
        Player* bot;
        Context* context;
        std::string qualifier = "current target";
        bool Calculate();
    };
    class AttackAction
    {
    public:
        PlayerbotAI* ai;
        Player* bot;
        bool verbose = false;
        unsigned petOrders = 0;
        bool IsTargetValid(Player*, Unit*);
        bool PetAttack(Player*, Unit*);
    };
    class MovementAction
    {
    public:
        PlayerbotAI* ai;
        Player* bot;
        unsigned chaseOrders = 0;
        bool ChaseTo(WorldObject*, float = 0, float = 0);
    };
}
using namespace ai;
#define AI_VALUE(type, name) context->GetValue<type>(name)->Get()
#include "stealth_possible_targets.inc"
#include "stealth_invalid_target.inc"
#include "stealth_enemy_player.inc"
#include "stealth_possible_attack.inc"
#include "stealth_attack_target.inc"
#include "stealth_pet_attack_guard.inc"
#include "stealth_chase_guard.inc"
#undef AI_VALUE
#ifdef STEALTH_HAS_CAN_PERCEIVE
#include "stealth_cleanup.inc"
void PlayerbotAI::OuterUpdate()
{
#include "stealth_outer_cleanup.inc"
}
#include "stealth_can_cast_guard.inc"
#include "stealth_cast_guard.inc"
#include "stealth_pet_cast_guard.inc"
#endif

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool passed, const std::string& reason)
    {
        ++checks;
        if (!passed) { ++failures; std::cerr << "FAILED: " << reason << '\n'; }
    }
    struct Fixture
    {
        Map map;
        Player bot, enemy;
        PlayerbotAI ai;
        InvalidTargetValue invalid{&ai, &bot, &ai.context};
        AttackAction attack{&ai, &bot};
        MovementAction move{&ai, &bot};
        Fixture()
        {
            bot.map = enemy.map = &map;
            bot.guid = {1}; enemy.guid = {2};
            enemy.flags |= UNIT_FLAG_PLAYER_CONTROLLED;
            bot.ai = &ai; ai.bot = &bot; ai.units[2] = &enemy;
            ai.context.guids["current target"].data = {2};
        }
        void Hide(bool invisibility = false)
        {
            enemy.detected = false;
            enemy.visibility = invisibility ? VISIBILITY_GROUP_INVISIBILITY : VISIBILITY_GROUP_STEALTH;
            enemy.auras[invisibility ? SPELL_AURA_MOD_INVISIBILITY : SPELL_AURA_MOD_STEALTH] = true;
        }
    };
}

int main()
{
    for (bool invisibility : {false, true})
    {
        for (bool combat : {false, true})
        {
            for (bool ignoreLos : {false, true})
            {
                Fixture f; f.Hide(invisibility); f.bot.combat = combat;
                Check(!PossibleTargetsValue::IsValid(&f.enemy, &f.bot, ignoreLos),
                    "combat and ignore-LOS cannot grant knowledge of an undetected hidden target");
            }
        }
        {
            Fixture f; f.Hide(invisibility); f.enemy.victim = &f.bot;
            Check(!PossibleTargetsValue::IsValid(&f.enemy, &f.bot),
                "an old victim link cannot bypass current hidden-target detection");
            f.enemy.victim = nullptr; f.enemy.threat.oldThreat = 100;
            Check(!PossibleTargetsValue::IsValid(&f.enemy, &f.bot),
                "old threat cannot bypass current hidden-target detection");
        }
        {
            Fixture f; f.Hide(invisibility); f.bot.combat = true;
            f.ai.context.oldAttackers.data = {{2}};
            Check(f.invalid.Calculate(), "a cached attackers entry cannot keep an undetected current target valid");
            f.ai.context.guids["duel target"].data = {2};
            Check(f.invalid.Calculate(), "the duel-target exemption cannot reveal a hidden duel opponent");
            Check(!f.attack.IsTargetValid(&f.bot, &f.enemy),
                "explicit attack and RTI target validation reject an undetected hidden target before pet orders");
            Check(!f.attack.PetAttack(&f.bot, &f.enemy) && f.attack.petOrders == 0,
                "a direct pet attack order cannot bypass the owner's current perception");
            Check(!f.move.ChaseTo(&f.enemy) && f.move.chaseOrders == 0 && f.ai.stops == 1,
                "pursuit cannot submit a movement order toward an undetected hidden target");
            Check(!EnemyPlayersValue::IsValid(&f.enemy, &f.bot),
                "a cached enemy-player candidate must pass live hidden-target detection");
            Check(!PossibleAttackTargetsValue::IsPossibleTarget(&f.enemy, &f.bot, 60, true),
                "cached possible-attack candidates cannot bypass perception when crowd control is ignored");
        }
        {
            Fixture f; f.Hide(invisibility); f.enemy.detected = true; f.bot.combat = true;
            Check(PossibleTargetsValue::IsValid(&f.enemy, &f.bot, true),
                "native successful detection still permits a stealthed or invisible opponent");
            Check(!f.invalid.Calculate(), "a natively detected hidden current target stays valid");
            Check(f.attack.IsTargetValid(&f.bot, &f.enemy), "a natively detected hidden target can be explicitly attacked");
            Check(f.attack.PetAttack(&f.bot, &f.enemy) && f.attack.petOrders == 1,
                "pet attack orders remain available when the owner genuinely detects the target");
            Check(f.move.ChaseTo(&f.enemy) && f.move.chaseOrders == 1,
                "a natively detected hidden target can be pursued");
            Check(PossibleAttackTargetsValue::IsPossibleTarget(&f.enemy, &f.bot, 60, true),
                "native successful detection allows the actual possible-attack filter");
        }
    }
    {
        Fixture f;
        Check(PossibleTargetsValue::IsValid(&f.enemy, &f.bot), "ordinary visible hostile targets remain valid");
        Check(f.enemy.lastObserver == &f.bot && f.enemy.lastViewPoint == f.bot.camera.GetBody() && f.enemy.lastDetect,
            "perception uses the observer's native camera and enables real detection in the correct direction");
        f.enemy.friendly = true;
        Check(!PossibleTargetsValue::IsValid(&f.enemy, &f.bot), "friendly units do not become attack targets");
        f.enemy.friendly = false; f.enemy.alive = false;
        Check(!PossibleTargetsValue::IsValid(&f.enemy, &f.bot), "dead targets stay invalid");
        Check(!PossibleTargetsValue::IsValid(nullptr, &f.bot), "missing targets stay invalid");
    }
#ifdef STEALTH_HAS_CAN_PERCEIVE
    {
        Fixture f;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "the shared helper delegates ordinary perception to the core");
        Check(f.enemy.lastObserver == &f.bot && f.enemy.lastViewPoint == f.bot.camera.GetBody() &&
            f.enemy.lastDetect && f.enemy.last3d, "shared perception uses fresh native detection with its normal 3D default");
        f.enemy.alive = false;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "perception does not incorrectly erase a visible corpse");
        f.enemy.friendly = true; f.enemy.attackable = false;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "perception remains usable for friendly and nonattackable spell targets");
        f.enemy.inWorld = false;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "despawned targets cannot be perceived through saved pointers");
        f.enemy.inWorld = true; f.bot.inWorld = false;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "an out-of-world observer cannot perceive targets");
        f.bot.inWorld = true;
        Map otherInstance; f.enemy.map = &otherInstance;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "matching map IDs cannot reveal a unit in a different instance");
        Check(!PossibleTargetsValue::CanPerceive(nullptr, &f.bot) &&
            !PossibleTargetsValue::CanPerceive(&f.enemy, nullptr), "missing perception operands are safely rejected");
    }
#endif
    {
        Fixture f; f.Hide(true); f.enemy.friendly = true;
        Check(f.move.ChaseTo(&f.enemy) && f.move.chaseOrders == 1 && f.ai.stops == 0,
            "the hostile pursuit guard does not block movement toward a friendly unit");
        WorldObject objective; objective.map = &f.map;
        Check(f.move.ChaseTo(&objective) && f.move.chaseOrders == 2,
            "unit perception does not block flag or other gameobject movement");
        f.ai.canMove = false;
        Check(!f.move.ChaseTo(&f.enemy) && f.move.chaseOrders == 2,
            "perception changes do not bypass normal movement restrictions");
    }
#ifdef STEALTH_HAS_CAN_PERCEIVE
    {
        Fixture f; f.Hide(); f.enemy.attackers = {&f.bot}; f.enemy.distance = 30;
        Check(f.enemy.IsVisibleForOrDetect(&f.bot, f.bot.camera.GetBody(), true),
            "fixture reproduces the core's historical attacker-link visibility shortcut");
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "an old autoattack link cannot reveal stealth beyond the core-computed detection distance");
        f.enemy.distance = 5; f.bot.camera.body.front = false;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "historical combat cannot reveal a stealthed opponent behind the actual camera viewpoint");
        f.bot.camera.body.front = true; f.enemy.los = false;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "the historical-combat path still requires the native stealth line of sight");
        f.enemy.los = true; f.bot.stunned = true;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "a stunned observer cannot maintain a stale stealth detection link");
        f.bot.stunned = false; f.enemy.visibilityData.stealthDistance = 0;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "zero native stealth visibility distance is not replaced with an invented detection radius");
        f.enemy.visibilityData.stealthDistance = 10; f.enemy.distance = 10;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "a frontal opponent at the actual core detection boundary remains detectable");
        Check(f.enemy.visibilityData.lastObserver == &f.bot && f.enemy.visibilityData.lastPlayerControlled,
            "the core stealth-distance calculator receives the real observer and target control flag");
        f.enemy.distance = 10.1f;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "detection compares native squared distance against the squared core range");
        f.enemy.visibilityData.stealthDistance = 100; f.enemy.distance = 46;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot),
            "a high calculated range cannot bypass the core's player detection maximum");
    }
    {
        Fixture f; f.Hide(); f.enemy.attackers = {&f.bot}; f.enemy.distance = 100;
        f.bot.camera.body.front = false;
        f.bot.gm = true;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "legitimate native GM visibility remains available");
        f.bot.gm = false; f.enemy.revealed = true;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "core fog-of-war stealth reveals remain authoritative");
        f.enemy.revealed = false; f.enemy.auras[SPELL_AURA_MOD_STALKED] = true; f.enemy.markCaster = f.bot.guid;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "the observing player's own legitimate tracking mark still reveals stealth");
        f.enemy.markCaster = {99};
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "another player's tracking mark does not grant this bot detection");
        f.enemy.markCaster = f.bot.guid; f.enemy.attackable = false;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "tracking-mark visibility retains the native attackability prerequisite");
        f.enemy.masterGuid = f.bot.guid;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "native ownership permits seeing an owned stealth unit");
        f.enemy.attackers.clear(); f.bot.gm = true;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &f.bot), "module exceptions never override a negative native perception result");
    }
    {
        Fixture f; f.Hide(); Pet pet; pet.guid = {7}; pet.map = &f.map; pet.victim = &f.enemy;
        f.bot.pet = &pet; f.enemy.attackers = {&pet}; f.enemy.distance = 30;
        Check(f.enemy.IsVisibleForOrDetect(&pet, &pet, true) &&
            !PossibleTargetsValue::CanPerceive(&f.enemy, &pet, &pet),
            "a pet's own historical attack link cannot grant long-range stealth detection");
        Check(perception::ReleaseLostTargets(&f.ai) && pet.attackStops == 1 && pet.motion.follows == 1 &&
            f.enemy.attackers.empty(), "pet cleanup breaks the self-sustaining stale attack link and resumes follow");
    }
    {
        Fixture f; f.Hide(); Pet pet; pet.guid = {7}; pet.map = &f.map; pet.victim = &f.enemy;
        f.bot.pet = &pet; f.enemy.attackers = {&pet}; f.enemy.distance = 30;
        f.enemy.auras[SPELL_AURA_MOD_STALKED] = true; f.enemy.markCaster = f.bot.guid;
        Check(perception::ReleaseLostTargets(&f.ai) && pet.attackStops == 1 &&
            f.ai.context.guids["current target"].data == ObjectGuid{2},
            "the owner's tracking mark does not incorrectly give the pet independent detection");
    }
    {
        Fixture f; f.Hide(); Pet pet; pet.guid = {7}; pet.map = &f.map;
        f.enemy.attackers = {&pet}; f.enemy.distance = 8;
        Check(PossibleTargetsValue::CanPerceive(&f.enemy, &pet, &pet),
            "a pet still detects a nearby frontal stealth target within its own native range");
        pet.front = false;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &pet, &pet), "pet detection checks the pet's facing rather than its owner's camera");
        pet.front = true; f.enemy.visibilityData.stealthDistance = 30; f.enemy.distance = 21;
        Check(!PossibleTargetsValue::CanPerceive(&f.enemy, &pet, &pet), "pet detection retains its native creature attack-distance maximum");
    }
    {
        Fixture f; f.Hide();
        for (const char* name : {"current target", "attack target", "pull target", "old target"})
            f.ai.context.guids[name].data = {2};
        f.bot.victim = &f.enemy; f.bot.motion.type = CHASE_MOTION_TYPE;
        f.bot.motion.current.target = &f.enemy; f.bot.selectedTarget = &f.enemy;
        f.ai.context.attackers.data = f.ai.context.oldAttackers.data = f.ai.context.enemies.data = {{2}};
        Check(perception::ReleaseLostTargets(&f.ai), "loss of perception releases the live combat lifecycle");
        for (const char* name : {"current target", "attack target", "pull target", "old target"})
            Check(!f.ai.context.guids[name].data, "hidden targets are removed from current, attack, pull and old-target memory");
        Check(f.bot.attackStops == 1 && !f.bot.victim && f.ai.stops == 1 &&
            f.bot.motion.type == IDLE_MOTION_TYPE, "autoattack and the native chase stop together when the target disappears");
        Check(!f.bot.selected && !f.bot.selectedTarget, "lost target selection no longer points to a hidden unit");
        Check(f.ai.context.attackers.resets && f.ai.context.oldAttackers.resets &&
            f.ai.context.enemies.resets && f.ai.context.hasEnemies.resets,
            "attacker and enemy-player caches are invalidated before the next decision");
        Check(!perception::ReleaseLostTargets(&f.ai), "an already released target does not repeatedly interrupt later decisions");
    }
    {
        Fixture f; f.Hide(); f.ai.OuterUpdate();
        Check(f.ai.wakeups == 1 && f.ai.internalDelay == 0,
            "the real outer update handles target loss while the internal engine is sleeping");
    }
    {
        Fixture f; f.ai.OuterUpdate();
        Check(f.ai.wakeups == 0 && f.ai.internalDelay == 8000 && f.ai.context.guids["current target"].data == ObjectGuid{2},
            "a visible target does not reset ordinary internal action timing");
        f.Hide(); f.bot.teleporting = true;
        Check(!perception::ReleaseLostTargets(&f.ai) && f.ai.context.guids["current target"].data == ObjectGuid{2},
            "teleport transitions do not mutate live target state");
        f.bot.teleporting = false; f.bot.alive = false;
        Check(!perception::ReleaseLostTargets(&f.ai), "dead bot updates do not run combat perception cleanup");
    }
    {
        Fixture f; f.Hide(); f.ai.context.guids["current target"].data = {};
        f.bot.motion.type = CHASE_MOTION_TYPE; f.bot.motion.current.target = &f.enemy;
        Check(perception::ReleaseLostTargets(&f.ai) && f.ai.stops == 1,
            "an orphan native chase is stopped even after AI target memory was already cleared");
    }
    for (CurrentSpellTypes slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
    {
        for (SpellState state : {SPELL_STATE_CREATED, SPELL_STATE_TARGETING, SPELL_STATE_CASTING, SPELL_STATE_DELAYED,
            SPELL_STATE_TRAVELING, SPELL_STATE_LANDING, SPELL_STATE_CHANNELING, SPELL_STATE_FINISHED})
        {
            Fixture f; f.Hide(); f.ai.context.guids["current target"].data = {};
            Spell spell; spell.state = state; spell.m_targets.unit = &f.enemy; f.bot.spells[slot] = &spell;
            const bool interruptible = spell.CanBeInterrupted();
            Check(perception::ReleaseLostTargets(&f.ai) == interruptible,
                "only pending or channeled unit-targeted spells cause hidden-target cleanup");
            Check(f.bot.interrupts == unsigned(interruptible) &&
                (interruptible ? f.bot.spells[slot] == nullptr : f.bot.spells[slot] == &spell),
                "native spell-state semantics preserve traveling projectiles and landing effects");
        }
    }
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        Fixture f; f.Hide(); f.ai.context.guids["current target"].data = {};
        Spell spell; f.bot.spells[CURRENT_GENERIC_SPELL] = &spell;
        if (mode == 0) spell.m_targets.unit = nullptr;
        if (mode == 1) spell.m_targets.unit = &f.bot;
        if (mode == 2) { f.enemy.friendly = true; spell.m_targets.unit = &f.enemy; }
        Check(!perception::ReleaseLostTargets(&f.ai) && f.bot.interrupts == 0,
            "ground, self and friendly casts are not canceled by the hostile-target guard");
    }
    {
        Fixture f; f.Hide(); Pet pet; pet.map = &f.map; pet.victim = &f.enemy;
        pet.motion.type = CHASE_MOTION_TYPE; pet.motion.current.target = &f.enemy; f.bot.pet = &pet;
        Check(perception::ReleaseLostTargets(&f.ai) && pet.attackStops == 1 && pet.motion.follows == 1,
            "a pet that cannot perceive its old victim stops attacking and returns to its owner");
    }
    {
        Fixture f; f.Hide(); Pet pet; pet.map = &f.map; pet.victim = &f.enemy; f.bot.pet = &pet;
        f.enemy.perceptionByObserver[&pet] = true;
        Check(perception::ReleaseLostTargets(&f.ai) && pet.attackStops == 0 && pet.victim == &f.enemy,
            "legitimate native pet detection is independent of the owner's detection");
    }
    {
        Fixture f; Pet pet; pet.map = &f.map; pet.victim = &f.enemy; f.bot.pet = &pet;
        f.enemy.perceptionByObserver[&pet] = false;
        Check(perception::ReleaseLostTargets(&f.ai) && pet.attackStops == 1 &&
            f.ai.context.guids["current target"].data == ObjectGuid{2},
            "a hidden-to-pet target does not erase the owner's independently visible target");
    }
    {
        Fixture f; f.Hide(); SpellCastResult result = SPELL_CAST_OK;
        Check(!f.ai.CanCastSpell(1, &f.enemy, 0, false, nullptr, false, false, false, &result) &&
            result == SPELL_FAILED_BAD_TARGETS && f.ai.castStages == 0,
            "new unit-targeted casts are rejected before spell lookup or facing changes");
        Check(!f.ai.CastSpell(1, &f.enemy, nullptr, false, nullptr) && !f.ai.CastPetSpell(1, &f.enemy) &&
            f.ai.castStages == 0, "normal and pet spell execution cannot bypass the shared perception gate");
        f.enemy.detected = true;
        Check(f.ai.CanCastSpell(1, &f.enemy, 0, false, nullptr, false, false, false, nullptr) &&
            f.ai.CastSpell(1, &f.enemy, nullptr, false, nullptr) && f.ai.CastPetSpell(1, &f.enemy) &&
            f.ai.castStages == 3, "native successful detection preserves all three cast entry paths");
        Check(perception::CanUseTarget(&f.ai, nullptr) && perception::CanUseTarget(&f.ai, &f.bot),
            "unit-target gating keeps null ground targets and self casts available");
        f.enemy.detected = false; f.enemy.friendly = true;
        Check(perception::CanUseTarget(&f.ai, &f.enemy), "friendly spells preserve their ordinary independent targeting rules");
    }
#endif
    std::cout << checks << " stealth perception checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
