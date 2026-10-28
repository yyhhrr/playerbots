#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int32 = std::int32_t;
using LootSlotType = uint8;
struct ObjectGuid
{
    uint64 value = 0;
    ObjectGuid(uint64 value = 0) : value(value) {}
    operator uint64() const { return value; }
    uint64 GetRawValue() const { return value; }
    friend bool operator==(ObjectGuid a, ObjectGuid b) { return a.value == b.value; }
    friend bool operator!=(ObjectGuid a, ObjectGuid b) { return !(a == b); }
    friend bool operator<(ObjectGuid a, ObjectGuid b) { return a.value < b.value; }
};
constexpr float INTERACTION_DISTANCE = 5.0f;
constexpr uint8 LOOT_SLOT_NORMAL = 0, LOOT_SLOT_VIEW = 1, LOOT_SLOT_MASTER = 2, LOOT_SLOT_OWNER = 3;
constexpr uint8 MAX_LOOT_SLOT_TYPE = 7, LOOT_SKINNING = 6;
constexpr uint32 CMSG_LOOT_RELEASE = 1, CMSG_LOOT_MONEY = 2, CMSG_AUTOSTORE_LOOT_ITEM = 3;
constexpr uint32 ITEM_QUALITY_NORMAL = 1, ITEM_QUALITY_RARE = 3;
constexpr uint32 TEXTEMOTE_CHEER = 1;
enum class BotState { BOT_STATE_NON_COMBAT };
enum class PlayerbotSecurityLevel { PLAYERBOT_SECURITY_ALLOW_ALL };
struct Player;
struct PlayerbotAI;
struct WorldObject
{
    ObjectGuid guid = 1;
    float x = 0, y = 0, z = 0;
    bool valid = true, possible = true;
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
};
struct WorldPosition
{
    WorldObject* object;
    explicit WorldPosition(WorldObject* object) : object(object) {}
    float sqDistance2d(WorldObject* other) const
    { return (object->x-other->x)*(object->x-other->x)+(object->y-other->y)*(object->y-other->y); }
};
struct ItemPrototype { uint32 ItemId = 1, Quality = 0; const char* Name1 = "loot"; };
struct LootItem
{
    uint32 itemId = 1, count = 1;
    uint8 slotType = LOOT_SLOT_NORMAL;
    bool isBlocked = false, allowed = true;
    uint8 GetSlotTypeForSharedLoot(Player*, struct Loot*) const { return slotType; }
    bool IsAllowed(Player*, struct Loot*) const { return allowed; }
};
struct Loot
{
    uint32 gold = 0;
    std::map<uint8, LootItem> items;
    LootItem* GetLootItemInSlot(uint8 slot)
    { auto it = items.find(slot); return it == items.end() ? nullptr : &it->second; }
    uint32 GetGoldAmount() const { return gold; }
};
struct WorldPacket
{
    uint32 opcode = 0;
    std::vector<uint64> data;
    std::size_t cursor = 0, bytes = 0;
    explicit WorldPacket(uint32 opcode = 0, uint32 = 0) : opcode(opcode) {}
    template<class T> WorldPacket& operator<<(T value)
    { data.push_back(uint64(value)); bytes += sizeof(T); return *this; }
    template<class T> WorldPacket& operator>>(T& value)
    { if (cursor >= data.size()) std::abort(); value = T(data[cursor++]); return *this; }
    template<class T> void read_skip() { T ignored; *this >> ignored; }
    void rpos(uint32 pos) { if(pos) std::abort(); cursor = 0; }
    std::size_t size() const { return bytes; }
};
struct Session
{
    Player* owner = nullptr;
    Loot* loot = nullptr;
    bool acceptItems = true, acceptMoney = true;
    unsigned releases = 0, stores = 0, moneyCalls = 0;
    void HandleLootReleaseOpcode(WorldPacket&) { ++releases; }
    void HandleLootMoneyOpcode(WorldPacket&);
    void HandleAutostoreLootItemOpcode(WorldPacket&);
};
struct Player : WorldObject
{
    PlayerbotAI* ai = nullptr;
    Session session;
    ObjectGuid lootGuid;
    uint64 money = 0;
    std::map<uint32, uint32> inventory;
    Player() { session.owner = this; }
    uint32 GetMapId() const { return 1; }
    ObjectGuid GetObjectGuid() const { return guid; }
    ObjectGuid GetLootGuid() const { return lootGuid; }
    void SetLootGuid(ObjectGuid id) { lootGuid = id; }
    Session* GetSession() { return &session; }
    uint64 GetMoney() const { return money; }
    uint32 GetItemCount(uint32 id, bool = false) const
    { auto it = inventory.find(id); return it == inventory.end() ? 0 : it->second; }
    float GetDistance(WorldObject* other) const
    { return std::sqrt((x-other->x)*(x-other->x)+(y-other->y)*(y-other->y)+(z-other->z)*(z-other->z)); }
    bool IsWithinDistInMap(WorldObject* other, float distance) const { return GetDistance(other) <= distance; }
};
void Session::HandleLootMoneyOpcode(WorldPacket&)
{
    ++moneyCalls;
    if (loot && acceptMoney) { owner->money += loot->gold; loot->gold = 0; }
}
void Session::HandleAutostoreLootItemOpcode(WorldPacket& packet)
{
    ++stores;
    uint8 index; packet >> index;
    if (loot && acceptItems)
        if (LootItem* item = loot->GetLootItemInSlot(index))
        { owner->inventory[item->itemId] += item->count; item->count = 0; }
}
struct LootManager
{
    Loot* GetLoot(Player* bot) const { return bot->session.loot; }
} sLootMgr;
struct ItemStorage
{
    std::map<uint32, ItemPrototype> prototypes;
    template<class T> const T* LookupEntry(uint32 id)
    { auto it = prototypes.find(id); return it == prototypes.end() ? nullptr : &it->second; }
} sItemStorage;
struct ItemQualifier
{
    uint32 id;
    ItemQualifier(uint32 id, int32 = 0) : id(id) {}
};
struct LootObject
{
    ObjectGuid guid;
    uint32 skillId = 0, reqSkillValue = 0, reqItem = 0;
    LootObject(ObjectGuid guid = {}) : guid(guid) {}
    bool IsEmpty() const { return !guid.value; }
    bool IsLootPossible(Player* bot);
    WorldObject* GetWorldObject(Player* bot);
    bool IsWithinLootDistance(Player* bot);
};
struct LootObjectStack
{
    ObjectGuid selected, active, pending;
    std::map<ObjectGuid, uint32> deferred;
    unsigned starts = 0, completed = 0, failures = 0;
    bool StartLoot(ObjectGuid id) { ++starts; if(deferred.count(id)) return false; active = id; return true; }
    void CompleteLoot(ObjectGuid id) { ++completed; if(active == id) active = {}; if(pending == id) pending = {}; }
    void DeferLoot(ObjectGuid id, uint32 delayMs = 5000)
    { ++failures; deferred[id] = delayMs; if(active == id) active = {}; if(pending == id) pending = {}; }
    void MarkLootOpened(ObjectGuid id) { pending = id; }
    bool IsLootOpening(ObjectGuid id = {}) const { return pending.value && (!id.value || pending == id); }
    bool IsDeferred(ObjectGuid id) const { return deferred.count(id); }
    LootObject GetLoot(float) const { return LootObject(selected); }
    void Remove(ObjectGuid) {}
};
template<class T> struct Value
{
    T value{};
    T Get() const { return value; }
    void Set(T next) { value = next; }
    void Reset() { value = T(); }
};
struct Context
{
    std::map<std::string, std::any> values;
    template<class T> Value<T>* GetValue(const std::string& name)
    {
        auto it = values.find(name);
        if(it == values.end()) it = values.emplace(name, Value<T>{}).first;
        return &std::any_cast<Value<T>&>(it->second);
    }
    template<class T, class Q> Value<T>* GetValue(const std::string& name, const Q& qualifier)
    { std::ostringstream out; out << qualifier; return GetValue<T>(name+"::"+out.str()); }
};
struct ChatHelper
{
    static std::string formatWorldobject(WorldObject*) { return "corpse"; }
    std::string formatItem(ItemQualifier&) { return "item"; }
};
struct PlayerbotAI
{
    Player* bot;
    Player* master = nullptr;
    Context context;
    ChatHelper chat;
    std::map<ObjectGuid, WorldObject*> objects;
    unsigned respawns = 0;
    explicit PlayerbotAI(Player* bot) : bot(bot) { bot->ai = this; }
    bool HasStrategy(const std::string&, BotState) const { return false; }
    WorldObject* GetWorldObject(ObjectGuid id) const
    { auto it = objects.find(id); return it == objects.end() ? nullptr : it->second; }
    Player* GetMaster() const { return master; }
    void TellPlayerNoFacing(Player*, const std::string&, PlayerbotSecurityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool = true) {}
    void TellPlayerNoFacing(Player*, std::ostringstream&, PlayerbotSecurityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool = true) {}
    void PlayEmote(uint32) {}
    void AccelerateRespawn(ObjectGuid) { ++respawns; }
};
WorldObject* LootObject::GetWorldObject(Player* bot) { return bot->ai->GetWorldObject(guid); }
bool LootObject::IsLootPossible(Player* bot)
{ WorldObject* object = GetWorldObject(bot); return object && object->valid && object->possible; }
struct Config
{
    float lootDistance = 25, sightDistance = 60, contactDistance = 0.5f;
    template<class... T> void logEvent(T&&...) {}
} sPlayerbotAIConfig;
struct ServerFacade
{
    bool los = true;
    bool IsWithinLOSInMap(Player*, WorldObject*) const { return los; }
} sServerFacade;
struct RandomBotManager
{
    bool IsRandomBot(Player*) const { return false; }
    void AddTradeDiscount(Player*, Player*, uint32) {}
} sRandomPlayerbotMgr;
struct ItemUsageValue { static uint32 GetBotBuyPrice(const ItemPrototype*, Player*) { return 0; } };
struct BroadcastHelper { template<class... T> static void BroadcastLootingItem(T&&...) {} };
inline uint32 urand(uint32, uint32) { return 1; }
#define BOT_TEXT2(name, args) std::string(name)
#define AI_VALUE(type, name) context->GetValue<type>(name)->Get()
#define AI_VALUE2(type, name, param) context->GetValue<type>(name, param)->Get()
#define RESET_AI_VALUE(type, name) context->GetValue<type>(name)->Reset()
#define RESET_AI_VALUE2(type, name, param) context->GetValue<type>(name, param)->Reset()
struct Event
{
    Player* owner = nullptr;
    WorldPacket packet;
    Player* getOwner() { return owner; }
    WorldPacket& getPacket() { return packet; }
};
struct Action
{
    PlayerbotAI* ai;
    Player* bot;
    Context* context;
    ChatHelper* chat;
    unsigned nearCalls = 0, toCalls = 0;
    bool movementWorks = true;
    uint32 duration = 0;
    explicit Action(PlayerbotAI* ai) : ai(ai), bot(ai->bot), context(&ai->context), chat(&ai->chat) {}
    Player* GetMaster() const { return ai->master; }
    bool MoveNear(WorldObject*, float) { ++nearCalls; return movementWorks; }
    bool MoveTo(WorldPosition) { ++toCalls; return movementWorks; }
    void WaitForReach(float) {}
    void SetDuration(uint32 value) { duration = value; }
};
struct LootAction : Action { using Action::Action; bool Execute(Event&); };
struct OpenLootAction : Action
{
    using Action::Action;
    bool openWorks = true;
    unsigned opens = 0;
    bool Execute(Event&);
    bool DoLoot(LootObject&) { ++opens; return openWorks; }
};
struct StoreLootAction : Action
{
    using Action::Action;
    bool Execute(Event&);
    static bool IsLootAllowed(ItemQualifier& item, PlayerbotAI*) { return item.id != 99; }
};
struct MoveToLootAction : Action { using Action::Action; bool Execute(Event&); };
struct FarFromCurrentLootTrigger : Action { using Action::Action; bool IsActive(); };
namespace ai
{
    struct BoolCalculatedValue : Action
    {
        BoolCalculatedValue(PlayerbotAI* ai, const std::string&) : Action(ai) {}
        virtual bool Calculate() = 0;
    };
}

#include "loot_flow_distance.inc"
#include "loot_flow_select.inc"
#include "loot_flow_open.inc"
#include "loot_flow_move.inc"
#include "loot_flow_store.inc"
#include "loot_flow_can_loot.inc"
#include "loot_flow_far_trigger.inc"

static unsigned checks = 0;
static void Check(bool condition, const char* label)
{ ++checks; if(!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); } }
struct Fixture
{
    Player bot, master;
    PlayerbotAI ai{&bot};
    WorldObject corpse;
    Loot loot;
    LootObjectStack stack;
    Event event;
    Fixture()
    {
        sServerFacade.los = true;
        corpse.guid = 10;
        ai.master = &master;
        ai.objects[corpse.guid] = &corpse;
        bot.session.loot = &loot;
        ai.context.GetValue<LootObjectStack*>("available loot")->Set(&stack);
        ai.context.GetValue<LootObject>("loot target")->Set(LootObject(corpse.guid));
        ai.context.GetValue<bool>("has available loot")->Set(true);
        stack.selected = corpse.guid;
        event.owner = &master;
        for(uint32 id : {1u,2u,99u})
        {
            sItemStorage.prototypes[id] = ItemPrototype{id};
            ai.context.GetValue<uint32>("stack space for item", id)->Set(100);
        }
    }
    void PreparePacket(uint32 gold, const std::vector<std::pair<uint8,LootItem>>& entries)
    {
        loot.gold = gold;
        event.packet = WorldPacket();
        event.packet << corpse.guid << uint8(1) << gold << uint8(entries.size());
        for(const auto& [index,item] : entries)
        {
            loot.items[index] = item;
            event.packet << index << item.itemId << item.count << uint32(0) << uint32(0)
                << uint32(0) << item.slotType;
        }
        stack.active = corpse.guid;
        stack.pending = corpse.guid;
        bot.lootGuid = corpse.guid;
    }
};

int main()
{
    {
        Fixture f; LootObject target(f.corpse.guid);
        ai::CanLootValue canLoot(&f.ai); FarFromCurrentLootTrigger far(&f.ai);
        for(float d : {0.0f,4.9f,5.0f})
        {
            f.bot.x=d;
            Check(target.IsWithinLootDistance(&f.bot), "loot within the exact five-yard boundary is in range");
            Check(canLoot.Calculate(), "can-loot agrees with the same arrival boundary");
            Check(!far.IsActive(), "a reached corpse does not also trigger more movement");
        }
        for(float d : {5.01f,5.1f,6.0f})
        {
            f.bot.x=d;
            Check(!target.IsWithinLootDistance(&f.bot), "no tolerant five-point-one-yard false arrival");
            Check(!canLoot.Calculate(), "can-loot rejects the boundary gap");
            Check(far.IsActive(), "a corpse outside the boundary still requires approach");
        }
        f.bot.x=0;f.bot.z=8;Check(!target.IsWithinLootDistance(&f.bot), "a corpse on another floor cannot be opened from below");
    }
    {
        Fixture f; LootAction select(&f.ai);
        Check(select.Execute(f.event), "selection starts a loot task");
        Check(f.stack.active==f.corpse.guid, "selected corpse is held for the approach");
    }
    {
        Fixture f; MoveToLootAction move(&f.ai); f.bot.x=8;
        Check(move.Execute(f.event), "a reachable corpse starts movement");
        Check(move.nearCalls==1, "approach uses the existing safe movement entry point");
        move.movementWorks=false;
        Check(!move.Execute(f.event), "failed movement terminates this attempt");
        Check(f.stack.deferred.count(f.corpse.guid), "failed path is deferred rather than immediately reselected");
    }
    {
        Fixture f; MoveToLootAction move(&f.ai);
        Check(move.Execute(f.event), "already reached loot does not need another path");
        Check(move.nearCalls==0 && move.toCalls==0, "a reached corpse cannot cause extra movement jitter");
        f.bot.x=8;sServerFacade.los=false;move.movementWorks=false;
        Check(!move.Execute(f.event), "failed indirect route ends the approach");
        Check(move.toCalls==1 && f.stack.failures==1, "failure of the indirect route also activates retry cooldown");
    }
    {
        Fixture f; OpenLootAction open(&f.ai);f.bot.x=5.1f;
        Check(!open.Execute(f.event), "five-point-one yards cannot dispatch an open request");
        Check(open.opens==0 && f.stack.failures==0, "still approaching is not treated as a failed corpse");
        f.bot.x=5.0f;
        Check(open.Execute(f.event) && open.opens==1, "the same request opens when it reaches the shared boundary");
    }
    {
        Fixture f; OpenLootAction open(&f.ai);
        Check(open.Execute(f.event), "opening submits the request");
        Check(f.stack.pending==f.corpse.guid, "opened corpse remains pending until its response");
        Check(open.Execute(f.event), "pending open does not restart the request");
        Check(open.opens==1, "one request is issued while pending");
        LootAction select(&f.ai);MoveToLootAction move(&f.ai);
        ai::CanLootValue canLoot(&f.ai);FarFromCurrentLootTrigger far(&f.ai);
        Check(!select.Execute(f.event), "a pending response cannot switch loot targets");
        Check(move.Execute(f.event) && move.nearCalls==0 && move.toCalls==0, "a pending response cannot restart movement");
        Check(!canLoot.Calculate() && !far.IsActive(), "ordinary triggers leave pending loot alone");
    }
    {
        Fixture f; OpenLootAction open(&f.ai);open.openWorks=false;
        Check(!open.Execute(f.event), "a rejected open ends unsuccessfully");
        Check(f.stack.deferred.count(f.corpse.guid), "a rejected open gets a retry cooldown");
    }
    {
        Fixture f;f.corpse.possible=false;OpenLootAction open(&f.ai);
        Check(!open.Execute(f.event), "loss of loot eligibility ends the attempt");
        Check(open.opens==0 && f.stack.failures==1, "an ineligible corpse is deferred without an open packet");
        Fixture g;g.corpse.valid=false;MoveToLootAction move(&g.ai);
        Check(!move.Execute(g.event) && g.stack.failures==1, "a disappeared corpse does not keep an active approach");
    }
    {
        Fixture f;f.PreparePacket(7,{{0,LootItem{1,2}}});StoreLootAction store(&f.ai);
        Check(store.Execute(f.event), "normal loot response is processed");
        Check(f.bot.money==7 && f.bot.GetItemCount(1)==2, "money and eligible items enter bot inventory");
        Check(f.stack.completed==1 && f.stack.failures==0, "real progress completes the task");
    }
    {
        Fixture f;f.PreparePacket(0,{{0,LootItem{1,1,LOOT_SLOT_MASTER}},{1,LootItem{2,1,LOOT_SLOT_VIEW}}});StoreLootAction store(&f.ai);
        Check(store.Execute(f.event), "master-loot display response closes normally");
        Check(f.bot.session.stores==0, "master and view-only slots are never claimed");
        Check(f.stack.failures==1 && f.stack.completed==0, "an empty open gets a bounded retry cooldown");
    }
    {
        Fixture f;f.PreparePacket(0,{{0,LootItem{1,1}}});f.bot.session.acceptItems=false;StoreLootAction store(&f.ai);
        Check(store.Execute(f.event), "silent native item rejection still releases the response");
        Check(f.stack.failures==1, "requesting an item is not misreported as successful loot");
    }
    {
        Fixture f;f.PreparePacket(0,{{0,LootItem{1,1}}});f.loot.items[0].slotType=LOOT_SLOT_VIEW;StoreLootAction store(&f.ai);
        Check(store.Execute(f.event), "a stale response is closed normally");
        Check(f.bot.session.stores==0 && f.stack.failures==1, "live slot permissions override an older normal-slot packet");
    }
    for(bool blocked : {false,true})
    {
        Fixture f;f.PreparePacket(0,{{0,LootItem{1,1}}});StoreLootAction store(&f.ai);
        f.loot.items[0].isBlocked=blocked;f.loot.items[0].allowed=blocked;
        Check(store.Execute(f.event), "a blocked or no-longer-allowed response is safely released");
        Check(f.bot.session.stores==0 && f.stack.failures==1, "blocked and ineligible items cannot be claimed or counted as progress");
    }
    {
        Fixture f;f.PreparePacket(0,{{0,LootItem{99,1}}});StoreLootAction store(&f.ai);
        Check(store.Execute(f.event) && f.bot.session.stores==0 && f.stack.failures==1, "item filters still prevent inventory requests");
        Fixture g;g.PreparePacket(0,{{0,LootItem{1,2}}});g.ai.context.GetValue<uint32>("stack space for item",1)->Set(1);StoreLootAction full(&g.ai);
        Check(full.Execute(g.event) && g.bot.session.stores==0 && g.stack.failures==1, "insufficient stack space cannot become a successful attempt");
    }
    {
        Fixture f;f.PreparePacket(5,{});f.bot.session.acceptMoney=false;StoreLootAction store(&f.ai);
        Check(store.Execute(f.event) && f.bot.money==0 && f.stack.failures==1, "silent native money rejection does not count as looting");
        Fixture g;g.PreparePacket(0,{});g.bot.session.loot=nullptr;StoreLootAction missing(&g.ai);
        Check(!missing.Execute(g.event) && g.stack.failures==1, "missing native loot state abandons the pending attempt");
    }
    {
        Fixture f;f.PreparePacket(5,{{0,LootItem{1,1}}});
        f.bot.lootGuid=20;f.stack.active=20;f.stack.pending=20;StoreLootAction store(&f.ai);
        Check(!store.Execute(f.event), "a late response for an older corpse is ignored");
        Check(f.bot.lootGuid==ObjectGuid(20) && f.stack.active==ObjectGuid(20) && f.stack.pending==ObjectGuid(20), "a stale response cannot replace or cancel a newer loot request");
        Check(f.bot.session.stores==0 && f.bot.session.moneyCalls==0 && f.bot.session.releases==0, "a stale response cannot claim or release another corpse");
        Check(f.stack.deferred.count(f.corpse.guid) && !f.stack.deferred.count(ObjectGuid(20)), "only the stale corpse gets a retry cooldown");
    }
    std::cout << checks << " loot flow runtime checks passed\n";
}
