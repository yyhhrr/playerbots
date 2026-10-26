#include "playerbot/AlteracValleyPolicy.h"
#include "playerbot/AlteracValleyTactics.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
using uint8 = std::uint8_t;
using Team = unsigned;
constexpr Team ALLIANCE = 0, HORDE = 1;
constexpr unsigned TEAM_INDEX_NEUTRAL = 2, BG_AV_MAX_STATES = 2, POINT_CONTROLLED = 1, POINT_ASSAULTED = 0;
constexpr unsigned BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED = 5, BATTLEGROUND_AV = 1, STATUS_IN_PROGRESS = 1;
constexpr unsigned BG_AV_NODES_FIRSTAID_STATION = 0, BG_AV_NODES_STONEHEART_GRAVE = 2,
    BG_AV_NODES_SNOWFALL_GRAVE = 3, BG_AV_NODES_FROSTWOLF_HUT = 6, BG_AV_NODES_DUNBALDAR_SOUTH = 7,
    BG_AV_NODES_STONEHEART_BUNKER = 10;
constexpr unsigned BG_AV_CAPTAIN_A = 48, BG_AV_CAPTAIN_H = 49, BG_AV_BOSS_A = 61, BG_AV_BOSS_H = 62,
    BG_AV_NODE_CAPTAIN_DEAD_A = 63, BG_AV_NODE_CAPTAIN_DEAD_H = 64,
    BG_AV_MINE_BOSSES_NORTH = 46, BG_AV_MINE_BOSSES_SOUTH = 47;
constexpr unsigned MINUTE = 60, IN_MILLISECONDS = 1000;
constexpr float INTERACTION_DISTANCE = 5;
enum CurrentSpellTypes { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL };
enum SpellState { SPELL_STATE_CASTING, SPELL_STATE_FINISHED };
unsigned GetTeamIndexByTeamId(Team team) { return team; }
struct ObjectGuid
{
    std::uint64_t value = 0;
    bool operator==(const ObjectGuid&) const = default;
    explicit operator bool() const { return value != 0; }
    std::uint64_t GetRawValue() const { return value; }
    bool operator<(const ObjectGuid& other) const { return value < other.value; }
};
struct Map;
struct Player;
struct PlayerbotAI;
struct Unit;
struct WorldLocation
{
    uint32 mapid = 30;
    float coord_x = 0, coord_y = 0, coord_z = 0;
    WorldLocation() = default;
    WorldLocation(uint32 id, float x, float y, float z) : mapid(id), coord_x(x), coord_y(y), coord_z(z) {}
};
struct WorldObject
{
    Map* map = nullptr;
    ObjectGuid guid;
    float x = 0, y = 0, z = 0;
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    ObjectGuid GetObjectGuid() const { return guid; }
    Map* GetMap() const { return map; }
};
struct SpellEntry { uint32 Id = 21651; };
struct SpellTargets { Unit* target = nullptr; Unit* getUnitTarget() const { return target; } };
struct Spell
{
    const SpellEntry* m_spellInfo;
    SpellTargets m_targets;
    SpellState state = SPELL_STATE_CASTING;
    SpellState getState() const { return state; }
};
struct Unit : WorldObject
{
    bool alive = true, inWorld = true, friendly = false, combat = false;
    Unit* victim = nullptr;
    std::array<Spell*, 3> spells{};
    bool IsAlive() const { return alive; }
    bool IsInWorld() const { return inWorld; }
    bool IsInCombat() const { return combat; }
    Unit* GetVictim() const { return victim; }
    Spell* GetCurrentSpell(CurrentSpellTypes type) const { return spells[type]; }
};
struct Creature : Unit {};
struct GameObject : WorldObject { uint32 dbGuid = 1; uint32 GetDbGuid() const { return dbGuid; } };
struct NodeEvent { unsigned event1 = 255, event2 = 255; };
struct MapData
{
    std::map<uint32, NodeEvent> flags;
    NodeEvent GetGameObjectEventIndex(uint32 dbGuid) const
    {
        const auto found = flags.find(dbGuid);
        return found == flags.end() ? NodeEvent{} : found->second;
    }
};
struct PlayerRef { Player* player; Player* getSource() const { return player; } };
struct Map
{
    uint32 id = 30;
    MapData data;
    std::vector<PlayerRef> players;
    std::map<ObjectGuid, Creature*> creatures;
    const auto& GetPlayers() const { return players; }
    const auto& GetMapDataContainer() const { return data; }
    Creature* GetCreature(ObjectGuid guid) const
    {
        auto found = creatures.find(guid); return found == creatures.end() ? nullptr : found->second;
    }
};
struct BattleGround
{
    unsigned status = STATUS_IN_PROGRESS, type = BATTLEGROUND_AV;
    uint32 startTime = 10000;
    std::map<unsigned, unsigned> events;
    std::map<std::pair<unsigned, unsigned>, ObjectGuid> creatureGuids;
    unsigned GetStatus() const { return status; }
    unsigned GetTypeId() const { return type; }
    uint32 GetStartTime() const { return startTime; }
    Team GetOtherTeam(Team team) const { return team == ALLIANCE ? HORDE : ALLIANCE; }
    bool IsActiveEvent(unsigned node, unsigned event) const
    {
        const auto found = events.find(node); return found != events.end() && found->second == event;
    }
    ObjectGuid GetSingleCreatureGuid(unsigned event, unsigned state) const
    {
        const auto found = creatureGuids.find({event, state}); return found == creatureGuids.end() ? ObjectGuid{} : found->second;
    }
};
struct Player : Unit
{
    BattleGround* bg = nullptr;
    PlayerbotAI* ai = nullptr;
    Team team = ALLIANCE;
    uint32 instance = 1;
    bool teleporting = false;
    Team GetTeam() const { return team; }
    uint32 GetMapId() const { return map ? map->id : 0; }
    uint32 GetInstanceId() const { return instance; }
    bool IsBeingTeleported() const { return teleporting; }
    BattleGround* GetBattleGround() const { return bg; }
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
    bool IsWithinDistInMap(const WorldObject* other, float radius) const
    {
        return map == other->map && (x - other->x) * (x - other->x) +
            (y - other->y) * (y - other->y) + (z - other->z) * (z - other->z) <= radius * radius;
    }
};
namespace ai
{
    struct PositionEntry
    {
        uint32 mapId = 30;
        float x = 0, y = 0, z = 0;
        bool valid = false;
        bool isSet() const { return valid; }
        void Set(float px, float py, float pz, uint32 map) { x = px; y = py; z = pz; mapId = map; valid = true; }
        void Reset() { valid = false; }
    };
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
    template<class T> auto GetValue(const std::string&, const std::string& qualifier = "")
    {
        if constexpr (std::is_same_v<T, ai::PositionMap&>) return &positions;
        else if constexpr (std::is_same_v<T, int>) return &integers[qualifier];
        else return &booleans[qualifier];
    }
};
struct PlayerbotAI
{
    Player* bot;
    Context context;
    bool real = false, canMove = true;
    Player* GetBot() const { return bot; }
    Context* GetAiObjectContext() { return &context; }
    bool IsRealPlayer() const { return real; }
    bool CanMove() const { return canMove; }
};
struct ServerFacade { bool IsFriendlyTo(Player*, Unit* unit) const { return unit->friendly; } } sServerFacade;
struct RandomPlayerbotMgr
{
    std::map<std::string, WorldLocation> locations;
    bool GetNamedLocation(const char* name, WorldLocation& location)
    {
        const auto found = locations.find(name);
        if (found == locations.end()) return false;
        location = found->second; return true;
    }
} sRandomPlayerbotMgr;
namespace WorldTimer { uint32 now = 10000; uint32 getMSTime() { return now; } }
using namespace ai;
#include "alterac_tactics_source.inc"

namespace
{
    unsigned checks = 0, failures = 0, nextInstance = 1;
    void Check(bool condition, const char* description)
    {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAILED: " << description << '\n'; }
    }
    struct Fixture
    {
        Map map;
        BattleGround bg;
        std::vector<std::unique_ptr<Player>> players;
        std::vector<std::unique_ptr<PlayerbotAI>> ais;
        std::vector<std::unique_ptr<Creature>> creatures;
        Player* bot;
        PlayerbotAI* ai;
        explicit Fixture(Team team = ALLIANCE, unsigned size = 8)
        {
            using namespace ai::alterac;
            WorldTimer::now = 10000;
            const unsigned instance = nextInstance++;
            sRandomPlayerbotMgr.locations.clear();
            for (unsigned node = 0; node < NodeCount; ++node)
                sRandomPlayerbotMgr.locations[nodeNames[node]] = WorldLocation(30, float(node * 100), 0, 0);
            const unsigned initial[] = {0, 0, 0, 2, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1};
            for (unsigned node = 0; node < NodeCount; ++node) bg.events[node] = initial[node] * 2 + POINT_CONTROLLED;
            for (unsigned i = 0; i < size; ++i)
            {
                auto player = std::make_unique<Player>();
                player->guid = {i + 1}; player->team = team; player->map = &map; player->bg = &bg;
                player->instance = instance; player->x = float(i * 2);
                auto actor = std::make_unique<PlayerbotAI>(player.get()); player->ai = actor.get();
                map.players.push_back({player.get()}); players.push_back(std::move(player)); ais.push_back(std::move(actor));
            }
            bot = players.front().get(); ai = ais.front().get();
        }
        void State(unsigned node, ai::alterac::NodeState state)
        {
            using namespace ai::alterac;
            const unsigned own = bot->team * 2, enemy = (bot->team == ALLIANCE ? HORDE : ALLIANCE) * 2;
            if (state == NodeState::Unknown) bg.events.erase(node);
            if (state == NodeState::Neutral) bg.events[node] = 5;
            if (state == NodeState::FriendlyOwned) bg.events[node] = own + POINT_CONTROLLED;
            if (state == NodeState::FriendlyAssault) bg.events[node] = own + POINT_ASSAULTED;
            if (state == NodeState::EnemyOwned) bg.events[node] = enemy + POINT_CONTROLLED;
            if (state == NodeState::EnemyAssault) bg.events[node] = enemy + POINT_ASSAULTED;
        }
        Creature* AddCreature(unsigned event, unsigned state, float x)
        {
            auto creature = std::make_unique<Creature>(); creature->guid = {100 + creatures.size()}; creature->map = &map; creature->x = x;
            Creature* result = creature.get(); bg.creatureGuids[{event, state}] = result->guid;
            map.creatures[result->guid] = result; creatures.push_back(std::move(creature)); return result;
        }
        bool Has(unsigned node, ai::alterac::Task task)
        {
            const auto& plan = ai::alterac::GetPlan(bot);
            for (const auto& objective : plan.objectives) if (objective.id == node && objective.task == task) return true;
            return false;
        }
        GameObject Flag(unsigned node)
        {
            GameObject flag; flag.map = &map; flag.dbGuid = node + 1; flag.guid = {node + 200};
            flag.x = float(node * 100); map.data.flags[flag.dbGuid] = {node, bg.events[node]}; return flag;
        }
    };
}

int main()
{
    using namespace ai::alterac;
    for (Team team : {ALLIANCE, HORDE})
        for (unsigned node = 0; node < NodeCount; ++node)
        {
            Fixture f(team, 1);
            for (NodeState state : {NodeState::Neutral, NodeState::FriendlyOwned, NodeState::FriendlyAssault,
                NodeState::EnemyOwned, NodeState::EnemyAssault, NodeState::Unknown})
            {
                f.State(node, state); Check(ReadState(&f.bg, team, node) == state, "native node event states map correctly for both factions");
            }
        }
    for (Team team : {ALLIANCE, HORDE})
    {
        Fixture f(team);
        const std::array<unsigned, 4> towers = team == ALLIANCE ? std::array<unsigned, 4>{11, 12, 13, 14} : std::array<unsigned, 4>{7, 8, 9, 10};
        const unsigned terminal = team == ALLIANCE ? 6 : 0;
        f.AddCreature(team == ALLIANCE ? BG_AV_BOSS_H : BG_AV_BOSS_A, 0, 5000);
        for (unsigned node : towers) f.State(node, NodeState::FriendlyAssault);
        f.State(terminal, NodeState::FriendlyOwned);
        Check(!f.Has(BossObjective, Task::Boss), "opening all four towers does not authorize the boss while marshals still remain");
        for (unsigned node : towers) Check(f.Has(node, Task::Guard), "every assaulted enemy tower remains a guard objective until it burns");
        for (unsigned node : towers) f.State(node, NodeState::FriendlyOwned);
        f.State(terminal, NodeState::FriendlyAssault);
        Check(!f.Has(BossObjective, Task::Boss), "a terminal graveyard still being captured is not a ready boss foothold");
        f.State(terminal, NodeState::FriendlyOwned);
        Check(f.Has(BossObjective, Task::Boss), "four completed towers and an owned terminal graveyard enable the live enemy general");
        WorldLocation objective;
        Check(SelectObjective(f.ai, objective) && f.ai->context.integers["alterac task"].data == int(Task::Boss) &&
            objective.coord_x == 5000, "the runtime selection publishes the ready boss rather than an old random node");
        Check(GetAssignedNode(f.ai) == NoNode && !IsDefender(f.ai), "a boss assignment cannot masquerade as a node guard");
    }
    for (Team team : {ALLIANCE, HORDE})
    {
        Fixture f(team);
        const unsigned terminal = team == ALLIANCE ? 6 : 0;
        for (unsigned node = 0; node < NodeCount; ++node) f.State(node, NodeState::FriendlyOwned);
        WorldLocation objective;
        Check(f.Has(BossObjective, Task::Boss) && SelectObjective(f.ai, objective) &&
            objective.coord_x == float(terminal * 100),
            "a ready attack advances to the enemy terminal even while the general's grid is not loaded");
        Creature* boss = f.AddCreature(team == ALLIANCE ? BG_AV_BOSS_H : BG_AV_BOSS_A, 0, 5000);
        WorldTimer::now += 1001;
        Check(SelectObjective(f.ai, objective) && objective.coord_x == 5000,
            "loading the general replaces the approach point with its actual live position");
        boss->alive = false; WorldTimer::now += 1001;
        Check(!f.Has(BossObjective, Task::Boss) && !SelectObjective(f.ai, objective),
            "a known dead general cannot produce a fabricated new boss approach task");
    }
    for (Team team : {ALLIANCE, HORDE})
        for (unsigned node : team == ALLIANCE ? std::vector<unsigned>{0, 1, 2, 7, 8, 9, 10} : std::vector<unsigned>{4, 5, 6, 11, 12, 13, 14})
        {
            Fixture f(team); f.State(node, NodeState::EnemyAssault);
            Check(f.Has(node, Task::Defend), "all key friendly graveyards and towers receive a defence objective when assaulted");
            const auto& plan = GetPlan(f.bot);
            bool urgent = false; for (const auto& objective : plan.objectives) if (objective.id == node) urgent |= objective.urgent;
            Check(urgent, "contested friendly objectives request immediate relief rather than a random role chance");
        }
    for (Team team : {ALLIANCE, HORDE})
    {
        Fixture f(team, 1); const unsigned enemyTower = team == ALLIANCE ? 11 : 7;
        f.State(enemyTower, NodeState::FriendlyAssault);
        Check(f.Has(enemyTower, Task::Guard), "an unfinished enemy tower has a live guarding task");
        f.State(enemyTower, NodeState::FriendlyOwned);
        Check(!f.Has(enemyTower, Task::Guard) && !f.Has(enemyTower, Task::Attack), "a burned enemy tower releases all flag work immediately");
        const unsigned ourTower = team == ALLIANCE ? 7 : 11;
        f.State(ourTower, NodeState::EnemyOwned);
        GameObject burned = f.Flag(ourTower); f.bot->x = burned.x;
        Check(!CanUseNodeFlag(f.ai, &burned), "a destroyed friendly tower cannot be recaptured as an ordinary flag");
    }
    {
        Fixture f(ALLIANCE, 1); f.State(4, NodeState::EnemyAssault); GameObject flag = f.Flag(4); f.bot->x = flag.x;
        Check(CanUseNodeFlag(f.ai, &flag), "a late-arriving bot can save an assaulted graveyard without historical ownership data");
        f.State(4, NodeState::FriendlyAssault); flag = f.Flag(4);
        Check(!CanUseNodeFlag(f.ai, &flag), "a flag already being captured by our team needs cover instead of another click");
        f.State(4, NodeState::FriendlyOwned); flag = f.Flag(4);
        Check(!CanUseNodeFlag(f.ai, &flag), "an owned friendly graveyard cannot trigger a pointless repeated opening");
        f.State(4, NodeState::EnemyAssault); flag = f.Flag(4); f.map.data.flags[flag.dbGuid].event2 = 255;
        Check(!CanUseNodeFlag(f.ai, &flag), "a stale despawned banner event is not eligible for capture");
        f.map.data.flags[flag.dbGuid] = {99, 0};
        Check(!CanUseNodeFlag(f.ai, &flag), "a non-node object cannot pass the Alterac flag event check");
    }
    {
        Fixture f; f.ai->context.booleans["alterac defender"].data = true;
        f.ai->context.positions.data["bg objective"].Set(0, 0, 0, 30);
        Unit enemy; enemy.map = &f.map; enemy.x = 30;
        Check(IsAllowedTarget(f.ai, &enemy), "a defender can engage opponents inside its node boundary");
        enemy.x = 100;
        Check(!IsAllowedTarget(f.ai, &enemy), "a defender does not chase ordinary opponents away from its node");
        enemy.victim = f.bot; enemy.x = 10;
        Check(IsAllowedTarget(f.ai, &enemy), "close direct attackers remain valid self-defence targets");
        f.bot->x = 100; enemy.x = 130; f.ai->canMove = false;
        Check(IsAllowedTarget(f.ai, &enemy), "an immobilized defender may retaliate against a direct ranged attacker");
        f.ai->canMove = true;
        Check(!IsAllowedTarget(f.ai, &enemy), "a mobile defender does not use distant self-defence to abandon its node");
        f.bot->x = 0;
        Check(IsAtAssignedNode(f.ai, 40), "node arrival uses the published strategic objective");
        f.bot->z = 60;
        Check(!IsAtAssignedNode(f.ai, 40), "a different floor is not falsely counted as present at the node");
        f.bot->z = 12;
        Check(!IsAtAssignedNode(f.ai, 40), "a nearby lower tower floor does not satisfy flag-level arrival");
    }
    for (unsigned inactive = 0; inactive < 6; ++inactive)
    {
        Fixture f;
        if (inactive == 0) f.map.id = 489;
        if (inactive == 1) f.bg.type = 2;
        if (inactive == 2) f.bg.status = 0;
        if (inactive == 3) f.bot->inWorld = false;
        if (inactive == 4) f.bot->teleporting = true;
        if (inactive == 5) f.bot->alive = false;
        WorldLocation objective;
        Check(!SelectObjective(f.ai, objective), "inactive or dead bots do not receive live Alterac objectives");
    }
    for (Team team : {ALLIANCE, HORDE})
    {
        Fixture f(team, 5);
        const char* waitName = team == ALLIANCE ? "AV_ICEBLOOD_GARRISON_WAITING_ALLIANCE" : "AV_STONEHEART_OUTPOST_WAITING_HORDE";
        sRandomPlayerbotMgr.locations[waitName] = WorldLocation(30, 3000, 0, 0);
        Creature* captain = f.AddCreature(team == ALLIANCE ? BG_AV_CAPTAIN_H : BG_AV_CAPTAIN_A, 0, 3100);
        for (auto& member : f.players) member->x = 3000;
        auto& gathered = GetPlan(f.bot);
        Check(gathered.captainCommitted, "five nearby allies commit the captain group to the pull");
        for (auto& member : f.players) member->x = 3050;
        WorldTimer::now += 1001;
        const auto& advancing = GetPlan(f.bot);
        const Objective* target = nullptr;
        for (const auto& objective : advancing.objectives) if (objective.task == Task::Captain) target = &objective;
        Check(advancing.captainCommitted && target && target->position.x == 3100,
            "leaving the waiting circle never sends a committed captain squad backwards to regroup");
        captain->combat = true; WorldTimer::now += 1001; GetPlan(f.bot);
        captain->combat = false;
        for (auto& member : f.players) { member->alive = false; member->x = 0; }
        f.bot->alive = true; WorldTimer::now += 11000;
        const auto& wiped = GetPlan(f.bot);
        Check(!wiped.captainCommitted && wiped.assignments.at(f.bot->guid.value).task != Task::Captain,
            "after a wiped pull the first revived bot cannot inherit an unsupported solo captain attack");
        captain->alive = false; WorldTimer::now += 1001;
        Check(!f.Has(CaptainObjective, Task::Captain), "a dead captain is removed from the live strategic candidates");
    }
    {
        Fixture f(ALLIANCE, 2);
        f.State(3, NodeState::EnemyAssault);
        Check(f.Has(3, Task::Attack) && !f.Has(3, Task::Defend),
            "the first enemy opening of neutral Snowfall is an attack choice instead of an emergency friendly graveyard loss");
        f.State(3, NodeState::FriendlyOwned); GetPlan(f.bot);
        f.State(3, NodeState::EnemyAssault);
        Check(f.Has(3, Task::Defend), "an owned Snowfall graveyard being retaken does request urgent relief");
    }
    std::cout << checks << " Alterac tactics runtime checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
