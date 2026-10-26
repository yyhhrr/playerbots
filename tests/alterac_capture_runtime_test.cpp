#include "playerbot/AlteracCapturePolicy.h"
#include "playerbot/AlteracCapture.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
using int32 = std::int32_t;
constexpr float INTERACTION_DISTANCE = 5;
constexpr unsigned GO_STATE_READY = 0;
enum { BG_AV_GO_BANNER_ALLIANCE = 100, BG_AV_GO_BANNER_ALLIANCE_CONT, BG_AV_GO_BANNER_HORDE,
    BG_AV_GO_BANNER_HORDE_CONT, BG_AV_GO_GY_BANNER_ALLIANCE, BG_AV_GO_GY_BANNER_ALLIANCE_CONT,
    BG_AV_GO_GY_BANNER_HORDE, BG_AV_GO_GY_BANNER_HORDE_CONT, BG_AV_GO_GY_BANNER_SNOWFALL };
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL };
enum SpellState { SPELL_STATE_CASTING, SPELL_STATE_FINISHED };
enum AuraType { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_ROOT,
    SPELL_AURA_MOUNTED, SPELL_AURA_MOD_SHAPESHIFT };
enum SpellCastResult { SPELL_CAST_OK, SPELL_FAILED_BAD_TARGETS };
struct ObjectGuid
{
    std::uint64_t value = 0;
    bool operator==(const ObjectGuid&) const = default;
    explicit operator bool() const { return value != 0; }
    std::uint64_t GetRawValue() const { return value; }
};
struct Unit;
struct Player;
struct PlayerbotAI;
struct GameObject;
struct Map;
struct SpellEntry { uint32 Id = 21651; };
struct SpellTargets
{
    Unit* unit = nullptr;
    ObjectGuid flag;
    Unit* getUnitTarget() const { return unit; }
    ObjectGuid getGOTargetGuid() const { return flag; }
    void setGOTarget(GameObject* object);
};
struct Spell
{
    const SpellEntry* m_spellInfo = nullptr;
    SpellTargets m_targets;
    SpellState state = SPELL_STATE_CASTING;
    Player* caster = nullptr;
    static inline SpellCastResult nextResult = SPELL_CAST_OK;
    static inline unsigned starts = 0;
    static inline std::vector<Spell*> events;
    Spell() = default;
    Spell(Player* player, const SpellEntry* entry, bool) : m_spellInfo(entry), caster(player) { events.push_back(this); }
    SpellState getState() const { return state; }
    SpellCastResult SpellStart(SpellTargets* targets);
};
struct Unit
{
    Map* map = nullptr;
    ObjectGuid guid{1};
    float x = 0, y = 0, z = 0;
    bool alive = true, inWorld = true, hostile = true, visible = true, los = true;
    std::array<bool, 6> auras{};
    Unit* victim = nullptr;
    std::array<Spell*, 3> spells{};
    bool IsInWorld() const { return inWorld; }
    bool IsAlive() const { return alive; }
    Map* GetMap() const { return map; }
    Unit* GetVictim() const { return victim; }
    ObjectGuid GetObjectGuid() const { return guid; }
    bool HasAuraType(AuraType type) const { return auras[type]; }
    Spell* GetCurrentSpell(CurrentSpellTypes type) const { return spells[type]; }
};
struct GameObject
{
    Map* map = nullptr;
    ObjectGuid guid{900};
    unsigned entry = BG_AV_GO_BANNER_HORDE, state = GO_STATE_READY;
    bool spawned = true, inUse = false, interactable = true, nodeEligible = true, los = true;
    float x = 2, y = 0, z = 0;
    ObjectGuid GetObjectGuid() const { return guid; }
    unsigned GetEntry() const { return entry; }
    bool IsInUse() const { return inUse; }
    unsigned GetGoState() const { return state; }
};
void SpellTargets::setGOTarget(GameObject* object) { flag = object->guid; }
struct PlayerRef { Player* player; Player* getSource() const { return player; } };
struct Map
{
    uint32 id = 30;
    bool active = true;
    std::vector<PlayerRef> players;
    std::vector<Unit*> units;
    const std::vector<PlayerRef>& GetPlayers() const { return players; }
};
struct Player : Unit
{
    PlayerbotAI* ai = nullptr;
    uint32 health = 1000;
    unsigned team = 0, interrupts = 0, removedMounts = 0, removedForms = 0;
    bool teleporting = false, useAllowed = true, mounted = false, mountForm = false;
    std::vector<Unit*> attackers;
    ObjectGuid selected;
    Unit* selectedTarget = nullptr;
    uint32 GetHealth() const { return health; }
    unsigned GetTeam() const { return team; }
    PlayerbotAI* GetPlayerbotAI() { return ai; }
    bool IsBeingTeleported() const { return teleporting; }
    bool CanUseBattleGroundObject() const { return useAllowed; }
    bool CanInteract(const GameObject* flag) const { return flag->interactable; }
    bool CanAttack(const Unit* enemy) const { return enemy->hostile; }
    float GetDistance(const Unit* enemy) const
    {
        return std::sqrt((x - enemy->x) * (x - enemy->x) + (y - enemy->y) * (y - enemy->y) + (z - enemy->z) * (z - enemy->z));
    }
    float GetDistance(const GameObject* flag) const
    {
        return std::sqrt((x - flag->x) * (x - flag->x) + (y - flag->y) * (y - flag->y) + (z - flag->z) * (z - flag->z));
    }
    bool IsWithinDistInMap(const GameObject* flag, float range) const { return map == flag->map && GetDistance(flag) <= range; }
    bool IsNonMeleeSpellCasted(bool) const
    {
        return (spells[0] && spells[0]->state != SPELL_STATE_FINISHED) ||
            (spells[1] && spells[1]->state != SPELL_STATE_FINISHED);
    }
    bool IsMounted() const { return mounted; }
    bool IsInDisallowedMountForm() const { return mountForm; }
    void RemoveSpellsCausingAura(AuraType type)
    {
        if (type == SPELL_AURA_MOUNTED) { mounted = false; ++removedMounts; }
        if (type == SPELL_AURA_MOD_SHAPESHIFT) { mountForm = false; ++removedForms; }
    }
    const std::vector<Unit*>& getAttackers() const { return attackers; }
    void InterruptSpell(CurrentSpellTypes slot)
    {
        ++interrupts;
        if (spells[slot]) spells[slot]->state = SPELL_STATE_FINISHED;
        spells[slot] = nullptr;
    }
    void SetSelectionGuid(ObjectGuid value) { selected = value; }
    void SetTarget(Unit* value) { selectedTarget = value; }
};
SpellCastResult Spell::SpellStart(SpellTargets*)
{
    ++starts;
    if (nextResult == SPELL_CAST_OK) caster->spells[CURRENT_GENERIC_SPELL] = this;
    else state = SPELL_STATE_FINISHED;
    return nextResult;
}
struct LastSpellCast
{
    uint32 id = 0;
    ObjectGuid target;
    std::time_t when = 0;
    void Set(uint32 spell, ObjectGuid guid, std::time_t timestamp) { id = spell; target = guid; when = timestamp; }
    void Reset() { id = 0; target = {}; when = 0; }
};
template<class T> struct Value
{
    T data{};
    unsigned resets = 0;
    T& Get() { return data; }
    void Set(const T& value) { data = value; }
    void Reset() { data = {}; ++resets; }
};
struct Context
{
    std::map<std::string, Value<int32>> integers;
    std::map<std::string, Value<ObjectGuid>> guids;
    std::map<std::string, Value<bool>> booleans;
    std::map<std::string, Value<std::list<ObjectGuid>>> lists;
    Value<LastSpellCast> lastCast;
    template<class T> auto GetValue(const std::string& name, const std::string& qualifier = "")
    {
        const std::string key = qualifier.empty() ? name : qualifier;
        if constexpr (std::is_same_v<T, int32>) return &integers[key];
        else if constexpr (std::is_same_v<T, ObjectGuid>) return &guids[key];
        else if constexpr (std::is_same_v<T, bool>) return &booleans[key];
        else if constexpr (std::is_same_v<T, LastSpellCast&>) return &lastCast;
        else return &lists[key];
    }
};
struct PlayerbotAI
{
    Player* bot;
    Context context;
    bool canMove = true, real = false;
    unsigned stops = 0;
    std::vector<GameObject*> objects;
    Player* GetBot() const { return bot; }
    Context* GetAiObjectContext() { return &context; }
    bool CanMove() const { return canMove; }
    bool IsRealPlayer() const { return real; }
    GameObject* GetGameObject(ObjectGuid guid)
    {
        for (GameObject* object : objects) if (object->guid == guid) return object;
        return nullptr;
    }
    void StopMoving() { ++stops; }
};
namespace WorldTimer { uint32 now = 10000; uint32 getMSTime() { return now; } }
struct ServerFacade
{
    SpellEntry capture;
    bool spellExists = true;
    bool IsWithinLOSInMap(const Player*, const Unit* enemy) const { return enemy->los; }
    bool IsWithinLOSInMap(const Player*, const GameObject* flag) const { return flag->los; }
    bool isSpawned(const GameObject* flag) const { return flag->spawned; }
    const SpellEntry* LookupSpellInfo(uint32) const { return spellExists ? &capture : nullptr; }
} sServerFacade;
namespace MaNGOS
{
    struct AnyUnitInObjectRangeCheck { AnyUnitInObjectRangeCheck(Player*, float) {} };
    template<class Check> struct UnitListSearcher
    {
        std::list<Unit*>& result;
        UnitListSearcher(std::list<Unit*>& units, Check&) : result(units) {}
    };
}
namespace Cell
{
    template<class Searcher> void VisitAllObjects(Player* player, Searcher& searcher, float)
    {
        for (Unit* unit : player->map->units) searcher.result.push_back(unit);
    }
}
namespace ai
{
    struct PossibleTargetsValue { static bool CanPerceive(Unit* unit, Player*) { return unit->visible; } };
}
namespace ai::alterac
{
    bool IsActive(Player* player) { return player && player->map && player->map->active && player->map->id == 30 && player->inWorld; }
    bool CanUseNodeFlag(PlayerbotAI* ai, GameObject* flag) { return ai && flag->nodeEligible && ai->GetBot()->map == flag->map; }
}
#include "alterac_capture_source.inc"

namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool condition, const char* description)
    {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAILED: " << description << '\n'; }
    }
    struct Fixture
    {
        Map map;
        Player bot;
        GameObject flag;
        Unit enemy;
        PlayerbotAI ai{&bot};
        Fixture()
        {
            WorldTimer::now = 10000; Spell::starts = 0; Spell::nextResult = SPELL_CAST_OK;
            sServerFacade.spellExists = true;
            bot.ai = &ai; bot.map = flag.map = enemy.map = &map; bot.guid = {10};
            enemy.guid = {20}; enemy.x = 10;
            map.players = {{&bot}}; ai.objects = {&flag};
            ai.context.lists["closest game objects static los"].data = {flag.guid};
        }
        void Threat() { enemy.victim = &bot; map.units.push_back(&enemy); }
        void Finish()
        {
            if (bot.spells[CURRENT_GENERIC_SPELL]) bot.spells[CURRENT_GENERIC_SPELL]->state = SPELL_STATE_FINISHED;
            bot.spells[CURRENT_GENERIC_SPELL] = nullptr;
        }
    };
}

int main()
{
    using namespace ai::alterac;
    {
        Fixture f; f.bot.mounted = f.bot.mountForm = true;
        Check(TryCapture(&f.ai) && IsCapturing(&f.bot) && Spell::starts == 1, "a safe assigned flag starts one native capture cast");
        Check(f.ai.context.lastCast.data.id == CaptureSpellId && f.ai.context.lastCast.data.target == f.flag.guid &&
            f.ai.context.integers["alterac capture health"].data == 1000,
            "a successful start records the flag cast and pre-damage health");
        Check(f.bot.removedMounts == 1 && f.bot.removedForms == 1 && f.ai.stops == 1,
            "the opener dismounts, leaves a forbidden mount form and stands still once");
        Check(!TryCapture(&f.ai) && Spell::starts == 1, "an already active opener never restarts the same flag cast");
        Check(!UpdateCaptureResponse(&f.ai), "an unthreatened active capture yields without inventing a combat response");
        WorldTimer::now += 8000; f.Finish();
        Check(UpdateCaptureResponse(&f.ai) && !f.ai.context.lastCast.data.id &&
            !f.ai.context.integers["alterac capture health"].data && !f.ai.context.booleans["alterac capture retry"].data,
            "a completed capture clears the record and wakes tactical processing without a false failure hold");
    }
    for (unsigned threatType = 0; threatType < 4; ++threatType)
    {
        Fixture f; Check(TryCapture(&f.ai), "capture starts before an unexpected live threat");
        Spell ranged; SpellEntry damage{123}; ranged.m_spellInfo = &damage; ranged.m_targets.unit = &f.bot;
        if (threatType == 0) f.Threat();
        if (threatType == 1) { f.enemy.spells[CURRENT_GENERIC_SPELL] = &ranged; f.map.units = {&f.enemy}; }
        if (threatType == 2) { f.enemy.spells[CURRENT_AUTOREPEAT_SPELL] = &ranged; f.map.units = {&f.enemy}; }
        if (threatType == 3) { f.enemy.victim = &f.bot; f.bot.attackers = {&f.enemy}; }
        Check(UpdateCaptureResponse(&f.ai) && f.bot.interrupts == 1 && !IsCapturing(&f.bot),
            "a guard, ranged cast, auto-shot or live attacker cancels the threatened opener immediately");
        Check(f.bot.selected == f.enemy.guid && f.bot.selectedTarget == &f.enemy &&
            f.ai.context.guids["current target"].data == f.enemy.guid &&
            f.ai.context.guids["attack target"].data == f.enemy.guid,
            "the response makes the actual visible attacker available to combat");
        f.map.units.clear(); f.bot.attackers.clear(); f.enemy.spells = {}; f.enemy.victim = nullptr;
        Check(!TryCapture(&f.ai), "a cancelled opener does not immediately return to the flag");
        WorldTimer::now += CaptureRetryDelayMs - 1;
        Check(!TryCapture(&f.ai), "capture remains held for the complete retry interval");
        ++WorldTimer::now; Check(TryCapture(&f.ai), "a safe opener becomes eligible when the retry delay expires");
    }
    for (unsigned filtered = 0; filtered < 8; ++filtered)
    {
        Fixture f; f.Threat();
        if (filtered == 0) f.enemy.visible = false;
        if (filtered == 1) f.enemy.los = false;
        if (filtered == 2) f.enemy.hostile = false;
        if (filtered == 3) f.enemy.alive = false;
        if (filtered == 4) f.enemy.inWorld = false;
        if (filtered == 5) f.enemy.x = 41;
        if (filtered == 6) f.enemy.auras[SPELL_AURA_MOD_STUN] = true;
        if (filtered == 7) f.enemy.auras[SPELL_AURA_MOD_FEAR] = true;
        Check(!GetImmediateCaptureThreat(&f.ai), "hidden, blocked, friendly, dead, stale, distant and disabled units are not live capture threats");
    }
    {
        Fixture f; f.Threat(); f.enemy.auras[SPELL_AURA_MOD_ROOT] = true;
        Check(GetImmediateCaptureThreat(&f.ai) == &f.enemy, "a rooted attacker can still threaten an opener");
        Check(!TryCapture(&f.ai) && Spell::starts == 0, "a known live attacker prevents a new unsafe opening");
    }
    {
        Fixture f; Check(TryCapture(&f.ai), "capture starts before an instant untracked hit");
        f.bot.health -= 100; f.Finish();
        Check(UpdateCaptureResponse(&f.ai) && f.bot.interrupts == 0 && !f.bot.selected &&
            f.ai.context.booleans["alterac capture retry"].data,
            "damage that already ended the native cast still wakes the bot without inventing an attacker");
    }
    {
        Fixture f; Check(TryCapture(&f.ai), "capture starts before a non-damaging interruption");
        WorldTimer::now += 500; f.Finish();
        Check(UpdateCaptureResponse(&f.ai) && !TryCapture(&f.ai), "a premature native interruption wakes tactical processing and arms the retry hold");
    }
    {
        Fixture f; Spell::nextResult = SPELL_FAILED_BAD_TARGETS;
        Check(!TryCapture(&f.ai) && Spell::starts == 1 && !IsCapturing(&f.bot) &&
            !f.ai.context.lastCast.data.id && !f.ai.context.integers["alterac capture health"].data,
            "SpellStart failure is not reported as a successful opening and leaves no capture record");
        Check(!TryCapture(&f.ai) && Spell::starts == 1, "a failed native cast is not retried every AI update");
    }
    for (unsigned invalid = 0; invalid < 13; ++invalid)
    {
        Fixture f;
        if (invalid == 0) f.map.id = 529;
        if (invalid == 1) f.map.active = false;
        if (invalid == 2) f.bot.alive = false;
        if (invalid == 3) f.ai.real = true;
        if (invalid == 4) f.ai.canMove = false;
        if (invalid == 5) f.bot.useAllowed = false;
        if (invalid == 6) f.flag.nodeEligible = false;
        if (invalid == 7) f.flag.spawned = false;
        if (invalid == 8) f.flag.inUse = true;
        if (invalid == 9) f.flag.state = 1;
        if (invalid == 10) f.flag.x = 10;
        if (invalid == 11) f.flag.los = false;
        if (invalid == 12) f.flag.entry = 999;
        Check(!TryCapture(&f.ai) && Spell::starts == 0, "inactive, controlled, invalid-node and unusable-object cases cannot start a capture");
    }
    {
        Fixture f; Player other; PlayerbotAI otherAI{&other};
        other.ai = &otherAI; other.map = &f.map; other.guid = {5}; other.x = 1;
        f.map.players.push_back({&other});
        Check(!TryCapture(&f.ai), "a closer eligible teammate is the single elected opener");
        other.x = -3;
        Check(TryCapture(&f.ai), "the nearest eligible opener proceeds while other bots cover");
    }
    {
        Fixture f; Player human; Spell opening; SpellEntry entry;
        human.map = &f.map; human.guid = {77}; human.spells[CURRENT_GENERIC_SPELL] = &opening;
        opening.m_spellInfo = &entry; opening.m_targets.flag = f.flag.guid;
        f.map.players.push_back({&human});
        Check(!TryCapture(&f.ai), "a human already opening this flag receives cover instead of a second opener");
        opening.m_targets.flag = {901};
        Check(TryCapture(&f.ai), "an unrelated flag cast does not block this node's elected opener");
    }
    {
        Fixture f; Spell buff; SpellEntry entry{123}; buff.m_spellInfo = &entry;
        f.bot.spells[CURRENT_GENERIC_SPELL] = &buff; f.Threat();
        Check(!UpdateCaptureResponse(&f.ai) && f.bot.interrupts == 0, "capture monitoring never interrupts an unrelated spell without a capture record");
    }
    {
        Fixture f; WorldTimer::now = 0; f.Threat();
        Check(!TryCapture(&f.ai), "a threat at millisecond zero still arms the capture retry hold");
        f.map.units.clear(); f.enemy.victim = nullptr;
        WorldTimer::now = CaptureRetryDelayMs - 1; Check(!TryCapture(&f.ai), "zero timestamp cannot bypass the armed retry interval");
        ++WorldTimer::now; Check(TryCapture(&f.ai), "zero-timestamp retry becomes eligible on time");
    }
    {
        Fixture f; WorldTimer::now = 0xfffffff0u; f.Threat(); Check(!TryCapture(&f.ai), "capture threat can be recorded before timer wrap");
        f.map.units.clear(); f.enemy.victim = nullptr; WorldTimer::now += CaptureRetryDelayMs;
        Check(TryCapture(&f.ai), "capture retry expires correctly across the millisecond counter wrap");
    }
    for (Spell* spell : Spell::events) delete spell;
    std::cout << checks << " Alterac capture runtime checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
