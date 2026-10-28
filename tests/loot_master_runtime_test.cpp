#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using ObjectGuid = std::uint64_t;
using std::abs;
constexpr float INTERACTION_DISTANCE = 5.0f;
enum class BotState { BOT_STATE_NON_COMBAT };
enum class PlayerbotSecurityLevel { PLAYERBOT_SECURITY_ALLOW_ALL };
enum class LootMethod { MASTER_LOOT, GROUP_LOOT, FREE_FOR_ALL };
struct PlayerbotAI;
struct WorldObject
{
    ObjectGuid guid = 1;
    float x = 0, z = 0;
    bool valid = true, empty = false, possible = true, dungeonAllowed = true;
    float GetPositionZ() const { return z; }
    const char* GetName() const { return "corpse"; }
};
struct Group
{
    bool raid = false;
    LootMethod method = LootMethod::MASTER_LOOT;
    ObjectGuid masterLooter = 50;
    LootMethod GetLootMethod() const { return method; }
    ObjectGuid GetMasterLooterGuid() const { return masterLooter; }
};
struct Map { bool dungeon = true; bool IsDungeon() const { return dungeon; } };
struct Player : WorldObject
{
    PlayerbotAI* ai = nullptr;
    Group* group = nullptr;
    Map map;
    unsigned subgroup = 0;
    Group* GetGroup() const { return group; }
    Map* GetMap() { return &map; }
    ObjectGuid GetObjectGuid() const { return guid; }
    bool isRealPlayer() const { return true; }
};
struct Unit { const char* GetName() const { return "hostile"; } };
struct LootObjectStack
{
    std::set<ObjectGuid> queued;
    unsigned attempts = 0;
    bool Add(ObjectGuid guid) { ++attempts; return queued.insert(guid).second; }
};
template<class T> struct Value { T value{}; T Get() const { return value; } };
namespace ai
{
    class Trigger;
    class Event
    {
        std::string source, param;
        Player* owner = nullptr;
    public:
        Event() = default;
        Event(std::string source, std::string param, Player* owner = nullptr)
            : source(std::move(source)), param(std::move(param)), owner(owner) {}
        std::string getSource() const { return source; }
        std::string getParam() const { return param; }
        Player* getOwner() const { return owner; }
    };
    class AiObjectContext
    {
    public:
        PlayerbotAI* ai;
        Value<LootObjectStack*> available;
        Value<uint8> bagSpace;
        std::map<std::string, Value<std::list<ObjectGuid>>> lists;
        std::map<std::string, std::function<Trigger*(PlayerbotAI*)>> creators;
        std::map<std::string, std::unique_ptr<Trigger>> created;
        explicit AiObjectContext(PlayerbotAI* ai);
        Trigger* GetTrigger(const std::string& name);
        template<class T> Value<T>* GetValue(const std::string& name)
        {
            if constexpr (std::is_same_v<T, LootObjectStack*>) return &available;
            else if constexpr (std::is_same_v<T, uint8>) return &bagSpace;
            else return &lists[name];
        }
    };
    struct ChatHelper
    {
        static std::string parseValue(const std::string&, const std::string&) { return ""; }
        static bool parseable(const std::string&) { return false; }
        static std::string formatWorldobject(WorldObject* object) { return std::to_string(object->guid); }
        static std::list<ObjectGuid> parseGameobjects(const std::string& text)
        {
            if (text.rfind("object:", 0) != 0) return {};
            return {std::stoull(text.substr(7))};
        }
    };
}
struct PlayerbotAI
{
    Player* bot;
    Player* master = nullptr;
    bool leader = false, activeMaster = true, debug = false, safe = false;
    bool capacityAvailable = true, questItems = false, freeGraySpace = false;
    std::map<ObjectGuid, WorldObject*> objects;
    std::list<Unit*> hostiles;
    std::vector<std::string> cleanup;
    std::vector<std::string> errors;
    LootObjectStack loot;
    ai::AiObjectContext context;
    explicit PlayerbotAI(Player* bot) : bot(bot), context(this) { context.available.value = &loot; }
    bool HasStrategy(const std::string&, BotState) const { return debug; }
    bool IsGroupLeader() const { return leader; }
    bool HasActivePlayerMaster() const { return activeMaster; }
    WorldObject* GetWorldObject(ObjectGuid guid)
    {
        const auto it = objects.find(guid);
        return it == objects.end() ? nullptr : it->second;
    }
    void TellDebug(Player*, const std::string&, const std::string&) {}
    void TellError(Player*, const std::string& text) { errors.push_back(text); }
    void TellPlayer(Player*, const std::string& text, PlayerbotSecurityLevel, bool) { errors.push_back(text); }
    std::list<Unit*> GetAllHostileNPCNonPetUnitsAroundWO(WorldObject*, float) const { return hostiles; }
    bool CanLootSomethingFromWO(WorldObject*) const { return capacityAvailable; }
    bool HasQuestItemsInWOLootList(WorldObject*) const { return questItems; }
    bool DoSpecificAction(const std::string& name)
    {
        cleanup.push_back(name);
        if (freeGraySpace && name == "destroy all gray") { context.bagSpace.value = 50; return true; }
        return false;
    }
};
struct LootObject
{
    ObjectGuid guid;
    Player* bot;
    LootObject(Player* bot, ObjectGuid guid) : guid(guid), bot(bot) {}
    void Refresh(Player*, ObjectGuid, bool) {}
    WorldObject* GetWorldObject(Player*) const
    {
        WorldObject* object = bot->ai->GetWorldObject(guid);
        return object && object->valid ? object : nullptr;
    }
    bool IsEmpty() const { WorldObject* object = GetWorldObject(bot); return !object || object->empty; }
    bool IsLootPossible(Player*) const { WorldObject* object = GetWorldObject(bot); return object && object->possible; }
};
struct Config
{
    float lootDistance = 25, groupMemberLootDistanceWithActiveMaster = 10, groupMemberLootDistance = 15;
} sPlayerbotAIConfig;
struct Facade
{
    bool IsDistanceGreaterThan(float distance, float limit) const { return distance > limit; }
    float GetDistance2d(Player* player, WorldObject* object) const { return std::abs(player->x - object->x); }
} sServerFacade;
namespace dungeon
{
    bool IsActive(PlayerbotAI* ai) { return ai->safe; }
    bool CanLoot(PlayerbotAI*, WorldObject* object) { return object->dungeonAllowed; }
}
namespace ai
{
    class Trigger
    {
        std::string name;
    protected:
        bool triggered = false;
        std::string param;
        Player* owner = nullptr;
    public:
        Trigger(PlayerbotAI*, std::string name) : name(std::move(name)) {}
        virtual ~Trigger() = default;
        std::string getName() const { return name; }
        virtual void ExternalEvent(std::string, Player* = nullptr) {}
        virtual Event Check() { return Event(); }
        virtual void Reset() {}
        virtual void ExternalEventForce(std::string value, Player* player = nullptr)
        { param = std::move(value); owner = player; triggered = true; }
    };
    class AddAllLootAction
    {
        PlayerbotAI* ai;
        Player* bot;
        AiObjectContext* context;
    public:
        explicit AddAllLootAction(PlayerbotAI* ai) : ai(ai), bot(ai->bot), context(&ai->context) {}
        Player* GetMaster() { return ai->master; }
        bool Execute(Event& event);
        bool AddLoot(Player* requester, ObjectGuid guid);
    };
}
#include "clear_bags_chat_trigger.inc"
ai::AiObjectContext::AiObjectContext(PlayerbotAI* ai) : ai(ai)
{
    using ai::ChatCommandTrigger;
#include "loot_master_trigger_registration.inc"
}
ai::Trigger* ai::AiObjectContext::GetTrigger(const std::string& name)
{
    const auto factory = creators.find(name);
    if (factory == creators.end()) return nullptr;
    auto& result = created[name];
    if (!result) result.reset(factory->second(ai));
    return result.get();
}
namespace ai
{
    class ExternalEventHelper
    {
        AiObjectContext* aiObjectContext;
    public:
        explicit ExternalEventHelper(AiObjectContext* context) : aiObjectContext(context) {}
#include "clear_bags_parse_command.inc"
#include "clear_bags_handle_command.inc"
    };
    struct ActionRegistry
    {
        std::map<std::string, std::function<AddAllLootAction*(PlayerbotAI*)>> creators;
        ActionRegistry()
        {
#include "loot_master_action_registration.inc"
        }
    };
    struct NextAction
    {
        std::string name;
        float relevance;
        NextAction(std::string name, float relevance) : name(std::move(name)), relevance(relevance) {}
        using Actions = std::vector<std::pair<std::string, float>>;
        template<class T> static void Append(Actions& result, T value)
        {
            if constexpr (std::is_pointer_v<T>)
            {
                if (value) { result.emplace_back(value->name, value->relevance); delete value; }
            }
            else if (value != 0) std::abort();
        }
        template<class... T> static Actions array(int, T... values)
        { Actions result; (Append(result, values), ...); return result; }
    };
    struct TriggerNode
    {
        std::string name;
        NextAction::Actions actions;
        TriggerNode(std::string name, NextAction::Actions actions) : name(std::move(name)), actions(std::move(actions)) {}
    };
    struct ManualChains
    {
        std::list<TriggerNode*> triggers;
        ManualChains()
        {
            const float relevance = 100;
#include "loot_master_command_chain.inc"
        }
        ~ManualChains() { for (auto* trigger : triggers) delete trigger; }
    };
}
using namespace ai;
#define AI_VALUE(type, name) context->GetValue<type>(name)->Get()
#include "loot_master_scan.inc"
#include "loot_master_admission.inc"

namespace
{
    unsigned checks = 0;
    void Check(bool condition, const char* message)
    {
        ++checks;
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    }
    struct Fixture
    {
        Group group;
        Player bot, requester;
        PlayerbotAI ai{&bot};
        AddAllLootAction action{&ai};
        WorldObject corpse;
        Fixture()
        {
            bot.guid = 10; bot.ai = &ai; bot.group = requester.group = &group;
            requester.guid = group.masterLooter; ai.master = &requester;
            corpse.guid = 100; corpse.x = 3; ai.objects[corpse.guid] = &corpse;
            ai.context.lists["nearest corpses"].value = {corpse.guid};
        }
        bool Scan(const std::string& param = "", Player* owner = nullptr)
        { Event event("loot", param, owner ? owner : &requester); return action.Execute(event); }
    };
    void MasterLootAdmission()
    {
        Fixture f;
        Check(f.group.method == LootMethod::MASTER_LOOT && f.group.masterLooter != f.bot.guid && f.bot.map.dungeon,
            "fixture reproduces non-master bot in a master-loot dungeon");
        Check(f.Scan(), "master-loot non-master can queue an otherwise permitted corpse");
        Check(f.ai.loot.queued.count(f.corpse.guid) == 1, "valid master-loot corpse reaches real admission queue");
        for (auto method : {LootMethod::GROUP_LOOT, LootMethod::FREE_FOR_ALL})
        {
            Fixture other; other.group.method = method;
            Check(other.Scan(), "other group loot methods continue to admit loot");
        }
        { Fixture other; other.group.masterLooter = other.bot.guid; Check(other.Scan(), "bot master looter still works"); }
        { Fixture other; other.bot.map.dungeon = false; Check(other.Scan(), "outdoor master-loot group still works"); }
        { Fixture other; other.bot.group = nullptr; Check(other.Scan(), "ungrouped normal scan remains available"); }
    }
    void Restrictions()
    {
        { Fixture f; f.corpse.possible = false; Check(!f.Scan(), "native no-loot-rights decision is preserved"); Check(f.ai.loot.queued.empty(), "inaccessible loot never enters queue"); }
        { Fixture f; f.corpse.empty = true; Check(!f.Scan(), "empty loot rejected"); }
        { Fixture f; f.corpse.valid = false; Check(!f.Scan(), "missing object rejected"); }
        { Fixture f; f.corpse.x = 11; Check(!f.Scan(), "active-master follower distance remains ten yards"); }
        { Fixture f; f.ai.activeMaster = false; f.corpse.x = 14; Check(f.Scan(), "ordinary follower uses fifteen-yard range"); }
        { Fixture f; f.ai.activeMaster = false; f.corpse.x = 16; Check(!f.Scan(), "ordinary follower limit remains enforced"); }
        { Fixture f; f.ai.leader = true; f.corpse.x = 24; Check(f.Scan(), "leader keeps twenty-five-yard range"); }
        { Fixture f; f.ai.leader = true; f.corpse.x = 26; Check(!f.Scan(), "leader range remains bounded"); }
        { Fixture f; f.requester.x = 20; Check(!f.Scan(), "range remains anchored to command requester"); }
        { Fixture f; f.corpse.z = 6; Check(!f.Scan(), "inactive dungeon safety retains old height guard"); }
        { Fixture f; f.ai.safe = true; f.corpse.z = 6; Check(f.Scan(), "safe traversable stairs may change height"); }
        { Fixture f; f.ai.safe = true; f.corpse.dungeonAllowed = false; Check(!f.Scan(), "unsafe dungeon route remains blocked"); }
        { Fixture f; Unit hostile; f.ai.hostiles.push_back(&hostile); Check(!f.Scan(), "nearby hostile prevents follower looting"); }
        { Fixture f; f.ai.context.bagSpace.value = 100; f.ai.capacityAvailable = false; Check(!f.Scan(), "full bag remains blocked"); }
        { Fixture f; f.ai.context.bagSpace.value = 100; f.ai.capacityAvailable = true; Check(f.Scan(), "stackable available loot may still be queued with full slots"); }
        { Fixture f; f.ai.context.bagSpace.value = 100; f.ai.capacityAvailable = false; f.ai.questItems = true; f.ai.freeGraySpace = true; Check(f.Scan(), "quest-item cleanup can free space before admission"); Check(f.ai.cleanup == std::vector<std::string>{"destroy all gray"}, "existing gray cleanup path remains available"); }
        { Fixture f; f.ai.context.bagSpace.value = 100; f.ai.capacityAvailable = false; f.ai.questItems = true; Check(!f.Scan(), "quest loot still refused if cleanup cannot make space"); Check(f.ai.cleanup.size() == 2, "original gray and smart cleanup attempts retained"); }
    }
    void ObjectParameters()
    {
        Fixture f;
        WorldObject chest; chest.guid = 200; chest.x = 2;
        f.ai.objects[chest.guid] = &chest;
        f.ai.context.lists["nearest game objects no los"].value = {chest.guid};
        Check(f.Scan("object:200"), "explicit object parameter is forwarded to admission");
        Check(f.ai.loot.queued == std::set<ObjectGuid>{200}, "explicit object scan does not sweep nearby corpse");
        f.ai.loot.queued.clear();
        Check(f.Scan(), "empty parameter scans world objects and corpses");
        Check(f.ai.loot.queued == std::set<ObjectGuid>({100, 200}), "both nearby categories are queued");
        { Fixture other; Check(!other.Scan("all"), "unknown parameter does not become a force-loot mode"); }
    }
    void Commands()
    {
        const std::string chinese = "\xE6\x8B\xBE\xE5\x8F\x96";
        ManualChains chains;
        Check(chains.triggers.size() == 3, "three manual aliases each have a reaction chain");
        const std::vector<std::string> expected{"add all loot", "loot", "move to loot", "open loot"};
        for (auto* node : chains.triggers)
        {
            Check(node->actions.size() == expected.size(), "each alias has all four pickup steps");
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                Check(node->actions[i].first == expected[i], "alias uses canonical scan/select/move/open sequence");
                if (i) Check(node->actions[i - 1].second > node->actions[i].second, "scan steps preserve priority ordering");
            }
        }
        for (const std::string command : {std::string("loot"), chinese, std::string("add all loot")})
        {
            for (bool raid : {false, true})
            {
                Fixture f; f.group.raid = raid; f.bot.subgroup = raid ? 7 : 0; f.requester.subgroup = 0;
                ExternalEventHelper helper(&f.ai.context);
                Check(helper.ParseChatCommand(command, &f.requester), "real parser accepts English/Chinese/group aliases");
                Trigger* active = f.ai.context.GetTrigger(command);
                Check(active && active->getName() == command, "cached trigger keeps its alias name");
                Event event = active->Check();
                Check(event.getSource() == command && event.getOwner() == &f.requester, "event preserves source and party/raid sender");
                Check(f.action.Execute(event), "party and cross-subgroup raid aliases queue permitted loot");
                for (const auto& alias : {std::string("loot"), chinese, std::string("add all loot")})
                    if (alias != command) Check(f.ai.context.GetTrigger(alias)->Check().getSource().empty(), "one alias does not arm another cached trigger");
                active->Reset();
                Check(active->Check().getSource().empty(), "consumed alias resets once");
                Check(helper.ParseChatCommand(command, &f.requester) && active->Check().getSource() == command, "same alias can be issued again after reset");
            }
        }
        { Fixture f; ActionRegistry actions; Check(actions.creators.count(chinese) == 1, "Chinese alias is also available in action context"); std::unique_ptr<AddAllLootAction> action(actions.creators[chinese](&f.ai)); Event event(chinese, "", &f.requester); Check(action->Execute(event), "Chinese direct action invokes real admission"); }
        { Fixture f; ExternalEventHelper helper(&f.ai.context); Check(helper.ParseChatCommand(chinese + " object:100", &f.requester), "Chinese command accepts existing object parameter"); Event event = f.ai.context.GetTrigger(chinese)->Check(); Check(event.getParam() == "object:100", "Chinese object parameter is preserved"); Check(f.action.Execute(event), "Chinese object parameter queues desired object"); }
    }
}
int main()
{
    MasterLootAdmission();
    Restrictions();
    ObjectParameters();
    Commands();
    std::cout << checks << " master-loot and command runtime checks passed\n";
}
