#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
enum LootSlotType
{
    LOOT_SLOT_NORMAL, LOOT_SLOT_VIEW, LOOT_SLOT_MASTER, LOOT_SLOT_REQS,
#ifndef MANGOSBOT_ZERO
    LOOT_SLOT_OWNER,
#endif
    MAX_LOOT_SLOT_TYPE
};
enum LootMethod { NOT_GROUP_TYPE_LOOT, MASTER_LOOT, GROUP_LOOT };
enum LootType { LOOT_CORPSE, LOOT_SKINNING };
constexpr uint32 GAMEOBJECT_TYPE_GOOBER = 10;
constexpr uint32 SPELL_EFFECT_CREATE_ITEM = 24;
struct Player { uint32 instance = 1; uint32 GetInstanceId() const { return instance; } };
struct Loot;
struct LootItem
{
    uint32 itemId = 100, count = 1;
    bool isBlocked = false, allowed = true;
    LootSlotType slot = LOOT_SLOT_NORMAL;
    bool IsAllowed(Player const*, Loot const*) const { return allowed; }
    LootSlotType GetSlotTypeForSharedLoot(Player const*, Loot const*) const { return slot; }
};
struct Loot
{
    uint32 gold = 0;
    LootMethod m_lootMethod = NOT_GROUP_TYPE_LOOT;
    bool m_isChecked = true;
    LootType m_lootType = LOOT_CORPSE;
    std::vector<LootItem*> items;
    uint32 GetGoldAmount() const { return gold; }
    std::vector<LootItem*> GetLootContentFor(Player*) const { return items; }
    // Match both module and native representations without duplicating the calculation.
    std::vector<LootItem*> const& GetLootItems() const { return items; }
    bool IsGroupLootPendingCheck() const { return m_lootMethod != NOT_GROUP_TYPE_LOOT && !m_isChecked; }
    LootType GetLootType() const { return m_lootType; }
};
using LootAccess = Loot;
struct WorldObject
{
    Loot* m_loot = nullptr;
    uint32 instance = 1;
    virtual ~WorldObject() = default;
    virtual bool IsGameObject() const { return false; }
};
struct GameObject : WorldObject
{
    uint32 type = GAMEOBJECT_TYPE_GOOBER, spellId = 0;
    bool IsGameObject() const override { return true; }
    uint32 GetGoType() const { return type; }
    uint32 GetSpellId() const { return spellId; }
};
struct WorldPosition { explicit WorldPosition(Player*) {} };
std::map<uint64, WorldObject*> worldObjects;
struct GuidPosition
{
    uint64 value;
    GuidPosition(uint64 value, WorldPosition) : value(value) {}
    explicit operator bool() const { return value != 0; }
    WorldObject* GetWorldObject(uint32 instance) const
    {
        const auto found = worldObjects.find(value);
        return found != worldObjects.end() && found->second->instance == instance ? found->second : nullptr;
    }
};
struct SpellEntry { uint32 Effect[3] = {}, EffectItemType[3] = {}; };
struct SpellStore
{
    std::map<uint32, SpellEntry> entries;
    template<class T> T const* LookupEntry(uint32 id) const
    {
        static_assert(std::is_same_v<T, SpellEntry>);
        const auto found = entries.find(id);
        return found == entries.end() ? nullptr : &found->second;
    }
} spellStore;
SpellStore* GetSpellStore() { return &spellStore; }
template<class T> struct Value { T value{}; T Get() const { return value; } };
struct AiObjectContext
{
    std::map<uint32, Value<uint32>> space;
    uint32 lookups = 0;
    template<class T> Value<T>* GetValue(const std::string& name, uint32 item)
    {
        static_assert(std::is_same_v<T, uint32>);
        if (name != "stack space for item") std::abort();
        ++lookups;
        if (!space.count(item)) space[item].value = 20;
        return &space[item];
    }
};
struct PlayerbotAI
{
    // The existing item-use policy is an input boundary of this fixture.
    std::map<uint32, bool> itemPolicy;
    uint32 policyChecks = 0;
};
namespace ai
{
    struct ItemQualifier
    {
        uint32 id;
        explicit ItemQualifier(uint32 id) : id(id) {}
        explicit ItemQualifier(LootItem* item) : id(item->itemId) {}
    };
    struct StoreLootAction
    {
        static bool IsLootAllowed(ItemQualifier const& item, PlayerbotAI* ai)
        {
            ++ai->policyChecks;
            const auto found = ai->itemPolicy.find(item.id);
            return found == ai->itemPolicy.end() || found->second;
        }
    };
    struct ShouldLootObject
    {
        Player* bot;
        PlayerbotAI* ai;
        AiObjectContext* context;
        std::string qualifier = "123";
        std::string getQualifier() const { return qualifier; }
        bool Calculate();
    };
}
#define AI_VALUE2(type, name, qualifier) context->GetValue<type>(name, qualifier)->Get()
using namespace ai;
using std::stoull;
#include "loot_permissions_source.inc"

unsigned checks = 0;
void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "FAIL: " << label << '\n';
        std::exit(1);
    }
}
struct Fixture
{
    Player bot;
    PlayerbotAI ai;
    AiObjectContext context;
    Loot loot;
    WorldObject corpse;
    GameObject object;
    std::list<LootItem> itemStorage;
    ShouldLootObject value{ &bot, &ai, &context };
    Fixture()
    {
        worldObjects.clear();
        spellStore.entries.clear();
        corpse.m_loot = &loot;
        worldObjects[123] = &corpse;
    }
    LootItem& Add(uint32 id = 100, LootSlotType slot = LOOT_SLOT_NORMAL)
    {
        itemStorage.emplace_back();
        LootItem& item = itemStorage.back();
        item.itemId = id;
        item.slot = slot;
        loot.items.push_back(&item);
        return item;
    }
    void UseUnknownObject() { worldObjects[123] = &object; }
    bool Calculate() { return value.Calculate(); }
};

int main()
{
    {
        Fixture f;
        Check(!f.Calculate(), "a known empty corpse is not worth another opening");
        f.value.qualifier = "0";
        Check(!f.Calculate(), "an empty target guid is rejected");
        f.value.qualifier = "456";
        Check(!f.Calculate(), "a removed target is rejected");
        f.value.qualifier = "123";
        f.bot.instance = 2;
        Check(!f.Calculate(), "a target in another instance is rejected");
    }
    {
        Fixture f;
        f.corpse.m_loot = nullptr;
        Check(f.Calculate(), "unknown corpse loot can be opened initially");
        f.UseUnknownObject();
        f.object.type = 3;
        Check(f.Calculate(), "an uninitialized chest can be opened initially");
        f.object.type = GAMEOBJECT_TYPE_GOOBER;
        Check(f.Calculate(), "a quest object without an item-creation spell remains eligible");
        f.object.spellId = 10;
        Check(f.Calculate(), "an object with missing spell metadata retains its old behavior");
        SpellEntry& spell = spellStore.entries[10];
        Check(f.Calculate(), "a non-item quest object retains its old behavior");
        spell.Effect[0] = SPELL_EFFECT_CREATE_ITEM;
        spell.EffectItemType[0] = 100;
        f.context.space[100].value = 0;
        Check(!f.Calculate(), "item-creating quest objects need inventory space");
        f.context.space[100].value = 1;
        f.ai.itemPolicy[100] = false;
        Check(!f.Calculate(), "item-creating quest objects honor the item rejection policy");
        f.ai.itemPolicy[100] = true;
        Check(f.Calculate(), "item-creating quest objects honor the item acceptance policy");
    }
    {
        Fixture f;
        LootItem& item = f.Add(100, LOOT_SLOT_MASTER);
        item.isBlocked = true;
        item.allowed = false;
        f.loot.gold = 1;
        Check(f.Calculate(), "gold remains eligible even when all displayed equipment needs assignment");
        Check(f.context.lookups == 0 && f.ai.policyChecks == 0, "gold is independent of inventory and item filters");
        f.loot.gold = 0;
        Check(!f.Calculate(), "a corpse stops being eligible after its last gold is removed");
    }
    for (LootMethod method : { MASTER_LOOT, GROUP_LOOT })
    {
        Fixture f;
        f.loot.m_lootMethod = method;
        f.loot.m_isChecked = false;
        LootItem& item = f.Add(100, LOOT_SLOT_MASTER);
        item.isBlocked = true;
        Check(f.Calculate(), "an unchecked grouped corpse may be opened once to initialize distribution");
        Check(f.context.lookups == 0 && f.ai.policyChecks == 0, "initial distribution checking precedes per-item filters");
        // Native Loot::GroupCheck sets this on the first content response for both methods.
        f.loot.m_isChecked = true;
        Check(!f.Calculate(), "initialized distribution cannot endlessly requeue a blocked assignment item");
        item.isBlocked = false;
        item.slot = LOOT_SLOT_NORMAL;
        Check(f.Calculate(), "an assigned or released item becomes eligible without changing loot method");
    }
    for (LootMethod method : { NOT_GROUP_TYPE_LOOT, MASTER_LOOT, GROUP_LOOT })
    {
        Fixture f;
        f.loot.m_lootMethod = method;
        LootItem& item = f.Add();
        Check(f.Calculate(), "a permitted ordinary or free-for-all quest slot can be collected");
        item.allowed = false;
        Check(!f.Calculate(), "normal slot display never bypasses player-specific eligibility");
        item.allowed = true;
        item.isBlocked = true;
        Check(!f.Calculate(), "a pending roll cannot be collected merely because its item is useful");
        item.isBlocked = false;
        for (LootSlotType slot : { LOOT_SLOT_MASTER, LOOT_SLOT_VIEW, LOOT_SLOT_REQS, MAX_LOOT_SLOT_TYPE })
        {
            item.slot = slot;
            f.context.lookups = f.ai.policyChecks = 0;
            Check(!f.Calculate(), "non-collectible item slots do not make a corpse worth looting");
            Check(f.context.lookups == 0 && f.ai.policyChecks == 0, "non-collectible slots are rejected before item or capacity work");
        }
#ifndef MANGOSBOT_ZERO
        item.slot = LOOT_SLOT_OWNER;
        Check(f.Calculate(), "expansions retain directly collectible owner-only slots");
#else
        item.slot = static_cast<LootSlotType>(99);
        Check(!f.Calculate(), "Classic accepts no extension-only or unknown slot types");
#endif
    }
    {
        Fixture f;
        f.loot.items.push_back(nullptr);
        f.Add(0);
        LootItem& item = f.Add(101);
        item.count = 5;
        f.context.space[101].value = 4;
        Check(!f.Calculate(), "null, zero-id and oversized item stacks do not create phantom loot");
        f.context.space[101].value = 5;
        Check(f.Calculate(), "a collectible stack fitting exactly is eligible");
        f.ai.itemPolicy[101] = false;
        Check(!f.Calculate(), "a collectible slot still respects blacklist and usage rejection");
        f.ai.itemPolicy[101] = true;
        Check(f.Calculate(), "a collectible slot still respects whitelist and usage acceptance");
        f.Add(102, LOOT_SLOT_VIEW);
        Check(f.Calculate(), "an uncollectible displayed item does not hide a collectible stack");
        item.allowed = false;
        Check(!f.Calculate(), "a corpse with only uncollectible remaining stacks becomes ineligible");
    }
    {
        Fixture f;
        f.loot.m_lootType = LOOT_SKINNING;
        LootItem& item = f.Add();
        f.ai.itemPolicy[100] = false;
        Check(f.Calculate(), "skinning retains the existing item-use filter exception");
        Check(f.ai.policyChecks == 0, "skinning does not invoke the normal item-use filter");
        item.isBlocked = true;
        Check(!f.Calculate(), "skinning does not bypass a blocked slot");
        item.isBlocked = false;
        item.slot = LOOT_SLOT_MASTER;
        Check(!f.Calculate(), "skinning does not bypass a master-only slot");
        item.slot = LOOT_SLOT_NORMAL;
        f.context.space[100].value = 0;
        Check(!f.Calculate(), "skinning still needs inventory capacity");
    }
    std::cout << checks << " loot permission runtime checks passed\n";
    return 0;
}
