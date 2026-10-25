#include "playerbot/ArathiCapturePolicy.h"
#include "playerbot/ArathiBasinCombat.h"

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
using int32 = std::int32_t;
struct ObjectGuid
{
    std::uint64_t value = 0;
    bool operator==(const ObjectGuid&) const = default;
};
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL };
enum SpellState { SPELL_STATE_CASTING, SPELL_STATE_FINISHED };
enum AuraType { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_ROOT };
struct Unit;
struct SpellEntry { uint32 Id = 21651; };
struct SpellTargets
{
    Unit* unit = nullptr;
    Unit* getUnitTarget() const { return unit; }
};
struct Spell
{
    SpellEntry* m_spellInfo;
    SpellState state = SPELL_STATE_CASTING;
    SpellTargets m_targets;
    SpellState getState() const { return state; }
};
struct Player;
struct PlayerRef { Player* player; Player* getSource() const { return player; } };
struct Map
{
    uint32 id = 529;
    std::vector<PlayerRef> players;
    const std::vector<PlayerRef>& GetPlayers() const { return players; }
};
struct Camera { void* GetBody() const { return nullptr; } };
struct Unit
{
    Map* map = nullptr;
    ObjectGuid guid{1};
    float x = 0, y = 0;
    bool alive = true, inWorld = true, hostile = true, visible = true, los = true;
    std::array<bool, 4> auras{};
    Unit* victim = nullptr;
    std::array<Spell*, 3> spells{};
    bool IsInWorld() const { return inWorld; }
    bool IsAlive() const { return alive; }
    Map* GetMap() const { return map; }
    uint32 GetMapId() const { return map ? map->id : 0; }
    Unit* GetVictim() const { return victim; }
    ObjectGuid GetObjectGuid() const { return guid; }
    bool HasAuraType(AuraType type) const { return auras[type]; }
    Spell* GetCurrentSpell(CurrentSpellTypes type) const { return spells[type]; }
    bool IsVisibleForOrDetect(const Player*, void*, bool) const { return visible; }
};
struct MotionMaster
{
    unsigned follows = 0;
    void MoveFollow(Player*, float, float) { ++follows; }
};
struct Pet : Unit
{
    MotionMaster motion;
    unsigned stops = 0;
    void AttackStop() { ++stops; }
    MotionMaster* GetMotionMaster() { return &motion; }
};
struct Player : Unit
{
    uint32 health = 1000;
    unsigned team = 0, interrupts = 0, stops = 0, selectionChanges = 0, targetChanges = 0;
    bool arathi = true;
    Pet* pet = nullptr;
    std::vector<Unit*> attackers;
    ObjectGuid selected;
    Unit* target = nullptr;
    Camera camera;
    uint32 GetHealth() const { return health; }
    unsigned GetTeam() const { return team; }
    Camera& GetCamera() { return camera; }
    bool CanAttack(const Unit* enemy) const { return enemy->hostile; }
    float GetDistance(const Unit* enemy) const { return std::hypot(x - enemy->x, y - enemy->y); }
    const std::vector<Unit*>& getAttackers() const { return attackers; }
    Pet* GetPet() const { return pet; }
    void InterruptSpell(CurrentSpellTypes type)
    {
        ++interrupts;
        if (spells[type]) spells[type]->state = SPELL_STATE_FINISHED;
        spells[type] = nullptr;
    }
    void SetSelectionGuid(ObjectGuid guid) { selected = guid; ++selectionChanges; }
    void SetTarget(Unit* unit) { target = unit; ++targetChanges; }
    void AttackStop() { ++stops; }
};
namespace ai
{
    struct PositionEntry
    {
        float x = 0, y = 0, z = 0;
        uint32 mapId = 529;
        bool valueSet = false;
        bool isSet() const { return valueSet; }
    };
    using PositionMap = std::map<std::string, PositionEntry>;
}
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
    std::map<std::string, Value<int32>> integers;
    std::map<std::string, Value<ObjectGuid>> guids;
    Value<std::list<ObjectGuid>> enemies;
    Value<bool> hasEnemies, defender;
    Value<ai::PositionMap> positions;
    template<class T> auto GetValue(const std::string& name, const std::string& qualifier = "")
    {
        if constexpr (std::is_same_v<T, int32>) return &integers[qualifier.empty() ? name : qualifier];
        else if constexpr (std::is_same_v<T, ObjectGuid>) return &guids[name];
        else if constexpr (std::is_same_v<T, std::list<ObjectGuid>>) return &enemies;
        else if constexpr (std::is_same_v<T, bool>) return qualifier == "arathi defender" ? &defender : &hasEnemies;
        else if constexpr (std::is_same_v<T, ai::PositionMap&>) return &positions;
    }
};
struct PlayerbotAI
{
    Player* bot;
    Context context;
    bool canMove = true;
    unsigned wakeups = 0;
    uint32 internalDelay = 8000;
    Player* GetBot() const { return bot; }
    Context* GetAiObjectContext() { return &context; }
    bool CanMove() const { return canMove; }
    void ResetAIInternalUpdateDelay() { internalDelay = 0; ++wakeups; }
    void OuterUpdate();
};
namespace WorldTimer
{
    uint32 now = 10000;
    uint32 getMSTime() { return now; }
}
struct ServerFacade
{
    bool IsWithinLOSInMap(const Player*, const Unit* enemy) const { return enemy->los; }
    float GetDistance2d(const Unit* bot, const Unit* enemy) const { return std::hypot(bot->x - enemy->x, bot->y - enemy->y); }
    float GetDistance2d(const Unit* bot, float x, float y) const { return std::hypot(bot->x - x, bot->y - y); }
} sServerFacade;

namespace ai::arathi
{
    constexpr uint32 CaptureSpell = 21651;
    bool IsArathi(Player* player) { return player && player->arathi && player->IsInWorld(); }
    Unit* GetImmediateCaptureThreat(PlayerbotAI* ai);
#include "arathi_capture_delay.inc"
#include "arathi_capture_response.inc"
}

using namespace ai;
void PlayerbotAI::OuterUpdate()
{
#include "arathi_outer_response.inc"
}

constexpr float PET_FOLLOW_DIST = 1.0f, PET_FOLLOW_ANGLE = 0.0f;
struct BGTactics
{
    PlayerbotAI* ai;
    Player* bot;
    Context* context;
    bool routeAvailable = true;
    unsigned moves = 0;
    explicit BGTactics(PlayerbotAI* ai) : ai(ai), bot(ai->bot), context(&ai->context) {}
    bool MoveTo(uint32, float, float, float, bool, bool, bool, bool) { ++moves; return routeAvailable; }
    bool returnToArathiObjective();
};
#include "arathi_return_objective.inc"

namespace
{
    unsigned checks = 0;
    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition) { std::cerr << "FAILED: " << description << '\n'; std::exit(EXIT_FAILURE); }
    }
    struct Fixture
    {
        Map map;
        Player bot, enemy;
        Pet pet;
        PlayerbotAI ai{&bot};
        SpellEntry captureInfo{21651}, attackInfo{116}, healInfo{2061};
        Spell capture{&captureInfo}, attack{&attackInfo}, heal{&healInfo};
        BGTactics tactics{&ai};
        Fixture()
        {
            bot.map = enemy.map = pet.map = &map;
            bot.guid.value = 1; enemy.guid.value = 2; pet.guid.value = 3;
            enemy.x = 15; enemy.team = 1;
            map.players = {{&bot}, {&enemy}};
            WorldTimer::now = 10000;
            ai.context.defender.data = true;
            ai.context.positions.data["bg objective"] = {100, 0, 0, 529, true};
        }
        void BeginCapture()
        {
            capture.state = SPELL_STATE_CASTING;
            bot.spells[CURRENT_GENERIC_SPELL] = &capture;
            arathi::RecordCaptureStart(&ai);
        }
        void EnemyAttacks() { enemy.victim = &bot; }
    };
}

int main()
{
    using namespace ai::arathi;
    {
        Fixture f;
        f.BeginCapture();
        Check(!UpdateCaptureResponse(&f.ai) && IsCapturing(&f.bot), "safe capture continues without being interrupted just because enemies exist");
        Check(f.bot.interrupts == 0, "safe capture does not cancel its banner spell");
        f.EnemyAttacks();
        Check(UpdateCaptureResponse(&f.ai) && !IsCapturing(&f.bot), "a live attacker immediately cancels the actual banner spell");
        Check(f.bot.interrupts == 1, "the banner cast is interrupted exactly once");
        Check(f.ai.context.guids["current target"].data == f.enemy.guid && f.ai.context.guids["attack target"].data == f.enemy.guid,
            "the threat becomes the actual current and attack target for retaliation");
        Check(f.bot.selected == f.enemy.guid && f.bot.target == &f.enemy,
            "selection and native target agree with retaliation so invalid-target checks cannot clear it");
        Check(f.ai.context.enemies.resets == 1 && f.ai.context.hasEnemies.resets == 1,
            "stale cached enemy lists are invalidated before the combat engine resumes");
        Check(!CanAttemptCapture(&f.ai), "the attacked opener cannot immediately reopen the flag");
        WorldTimer::now += 3000;
        Check(!CanAttemptCapture(&f.ai), "continuing pressure refreshes capture backoff instead of reopening every cooldown");
        f.enemy.victim = nullptr;
        WorldTimer::now += 2499;
        Check(!CanAttemptCapture(&f.ai), "capture does not resume immediately when the attacker stops");
        ++WorldTimer::now;
        Check(CanAttemptCapture(&f.ai), "capture resumes once live pressure and the short retry hold have ended");
    }
    {
        Fixture f;
        f.BeginCapture(); f.attack.m_targets.unit = &f.bot;
        f.enemy.spells[CURRENT_GENERIC_SPELL] = &f.attack;
        Check(f.bot.attackers.empty() && GetImmediateCaptureThreat(&f.ai) == &f.enemy,
            "a ranged cast is detected from live players before an autoattack link exists");
        f.ai.OuterUpdate();
        Check(f.ai.wakeups == 1 && f.ai.internalDelay == 0 && !IsCapturing(&f.bot),
            "the real outer AI update wakes a capture-sleeping engine to retaliate");
    }
    {
        Fixture f;
        f.BeginCapture();
        f.ai.OuterUpdate();
        Check(f.ai.wakeups == 0 && f.ai.internalDelay == 8000 && IsCapturing(&f.bot),
            "safe capture retains its ordinary cast delay instead of repeatedly restarting");
    }
    {
        Fixture f;
        f.BeginCapture();
        f.bot.health = 900;
        Check(UpdateCaptureResponse(&f.ai), "an instant hit with no current cast or victim link still wakes the opener");
        Check(f.ai.context.guids["current target"].data == ObjectGuid{},
            "damage without an identifiable attacker does not fabricate a combat target");
        Check(!CanAttemptCapture(&f.ai), "instant damage also prevents a repeated capture loop");
    }
    {
        Fixture f;
        f.BeginCapture();
        f.bot.health = 1100;
        Check(!UpdateCaptureResponse(&f.ai), "healing during capture does not falsely trigger retaliation");
        f.bot.health = 1050;
        Check(UpdateCaptureResponse(&f.ai), "a hit after healing is measured from the updated health snapshot");
    }
    {
        Fixture f;
        f.BeginCapture();
        f.bot.spells[CURRENT_GENERIC_SPELL] = nullptr;
        f.bot.health = 900;
        Check(UpdateCaptureResponse(&f.ai), "a native damage interrupt does not hide the hit before the outer update can respond");
    }
    {
        Fixture f;
        f.EnemyAttacks();
        f.bot.spells[CURRENT_GENERIC_SPELL] = &f.heal;
        Check(!UpdateCaptureResponse(&f.ai) && f.bot.interrupts == 0,
            "a normal healing or damage cast is not mistaken for flag opening");
    }
    {
        Fixture f;
        f.BeginCapture();
        f.capture.state = SPELL_STATE_FINISHED;
        Check(!UpdateCaptureResponse(&f.ai) && f.ai.context.integers["arathi capture health"].data == 0,
            "completed capture clears its health snapshot instead of treating later combat as capture damage");
        f.bot.health = 900;
        Check(!UpdateCaptureResponse(&f.ai), "damage after an already completed capture cannot create a stale capture response");
    }
    {
        Fixture f;
        f.BeginCapture();
        f.bot.alive = false;
        Check(!UpdateCaptureResponse(&f.ai) && f.ai.context.integers["arathi capture health"].data == 0,
            "death clears the capture snapshot instead of leaking it into the next resurrection");
    }
    {
        Fixture f;
        f.EnemyAttacks(); f.enemy.auras[SPELL_AURA_MOD_ROOT] = true;
        Check(GetImmediateCaptureThreat(&f.ai) == &f.enemy,
            "a rooted caster remains a threat and cannot justify standing under ranged fire");
        f.enemy.auras[SPELL_AURA_MOD_STUN] = true;
        Check(!GetImmediateCaptureThreat(&f.ai), "a stunned opponent does not interrupt a safe capture");
        f.enemy.auras[SPELL_AURA_MOD_STUN] = false; f.enemy.los = false;
        Check(!GetImmediateCaptureThreat(&f.ai), "a nonvisible line of fire cannot create an impossible retaliation target");
        f.enemy.los = true; f.enemy.x = 50;
        Check(!GetImmediateCaptureThreat(&f.ai), "a stale distant attacker cannot hold the opener forever");
    }
    {
        Fixture f;
        f.enemy.pet = &f.pet; f.pet.x = 6; f.pet.victim = &f.bot;
        Check(GetImmediateCaptureThreat(&f.ai) == &f.pet, "a nearby enemy pet is read live even without an attacker-list entry");
    }
    {
        Fixture f;
        f.ai.canMove = false;
        Check(!f.tactics.returnToArathiObjective() && f.bot.stops == 0 && f.tactics.moves == 0,
            "a rooted defender cannot cancel retaliation to start an impossible return route");
    }
    {
        Fixture f;
        f.EnemyAttacks(); f.enemy.x = 6;
        Check(!f.tactics.returnToArathiObjective() && f.bot.stops == 0 && f.tactics.moves == 0,
            "a nearby attacker is fought before returning to the assigned flag");
    }
    {
        Fixture f;
        f.tactics.routeAvailable = false;
        Check(!f.tactics.returnToArathiObjective() && f.bot.stops == 0 && f.tactics.moves == 1,
            "a failed return path leaves the existing attack running");
    }
    {
        Fixture f;
        f.bot.pet = &f.pet;
        Check(f.tactics.returnToArathiObjective() && f.bot.stops == 1 && f.tactics.moves == 1,
            "a genuinely started safe return can release the distant fight");
        Check(f.pet.stops == 1 && f.pet.motion.follows == 1, "the pet regroups only after its owner actually starts returning");
    }
    std::cout << checks << " Arathi response runtime checks passed\n";
    return EXIT_SUCCESS;
}
