#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <list>
#include <string>
#include <vector>

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int32 = std::int32_t;

// Simulate a warrior's main hand and bag; compile the real usage decision below.
enum { ITEM_QUALITY_POOR, ITEM_QUALITY_NORMAL };
enum { INVTYPE_NON_EQUIP, INVTYPE_WEAPON, INVTYPE_TABARD };
enum { ITEM_CLASS_CONTAINER, ITEM_CLASS_WEAPON, ITEM_CLASS_ARMOR, ITEM_CLASS_QUIVER };
enum { ITEM_SUBCLASS_CONTAINER, ITEM_SUBCLASS_AMMO_POUCH };
enum { CLASS_WARRIOR = 1, CLASS_HUNTER = 3, NULL_SLOT = 255 };
enum { ITEM_FIELD_DURABILITY, ITEM_FIELD_MAXDURABILITY };
enum class WeaponAttackType { RANGED_ATTACK };
enum ItemSubclassWeapon { ITEM_SUBCLASS_WEAPON_GUN, ITEM_SUBCLASS_WEAPON_BOW, ITEM_SUBCLASS_WEAPON_CROSSBOW };
enum InventoryResult { EQUIP_ERR_OK, EQUIP_ERR_CANT_USE_ITEM };
enum class ItemUsage { ITEM_USAGE_NONE, ITEM_USAGE_EQUIP, ITEM_USAGE_BAD_EQUIP, ITEM_USAGE_BROKEN_EQUIP, ITEM_USAGE_KEEP };
enum class ForceItemUsage { FORCE_USAGE_NONE, FORCE_USAGE_EQUIP };

struct ItemPrototype
{
    uint32 weight;
    uint32 Quality = ITEM_QUALITY_NORMAL;
    uint32 ItemId = 0;
    uint32 InventoryType = INVTYPE_WEAPON;
    uint32 Class = ITEM_CLASS_WEAPON;
    uint32 SubClass = 0;
    uint32 ItemLevel = 1;
    uint32 ContainerSlots = 0;
};
enum ItemSpecType { TestSpec };

struct Item
{
    const ItemPrototype* proto;
    uint32 enchant = 0;
    int32 randomProperty = 0;
    std::array<uint32, 4> gems{};
    uint32 socket = 0;

    const ItemPrototype* GetProto() const { return proto; }
    uint32 GetUInt32Value(uint32) const { return 1; }
};
using Bag = Item;

struct ItemQualifier
{
    explicit ItemQualifier(Item* item) : data(*item) {}
    const ItemPrototype* GetProto() { return data.proto; }
    uint32 GetEnchantId() { return data.enchant; }
    int32 GetRandomPropertyId() { return data.randomProperty; }
    uint32 GetGem1() { return data.gems[0]; }
    uint32 GetGem2() { return data.gems[1]; }
    uint32 GetGem3() { return data.gems[2]; }
    uint32 GetGem4() { return data.gems[3]; }
    Item data;
};

struct AiObjectContext
{
    std::list<Item*> inventory;
};

struct ChatHelper
{
    static std::string formatItem(const ItemPrototype*) { return {}; }
    static std::string formatItem(ItemQualifier&) { return {}; }
    static std::string formatSlot(uint8) { return {}; }
};

struct PlayerbotAI
{
    AiObjectContext context;
    ChatHelper chat;

    AiObjectContext* GetAiObjectContext() { return &context; }
    ChatHelper* GetChatHelper() { return &chat; }
    std::vector<Bag*> GetEquippedQuivers() const { return {}; }
    const void* GetMaster() const { return nullptr; }
    void TellDebug(const void*, const std::string&, const char*) const {}
};

struct Player
{
    PlayerbotAI ai;
    Item* mainHand = nullptr;
    bool proficiency = true;

    PlayerbotAI* GetPlayerbotAI() { return &ai; }
    uint8 getClass() const { return CLASS_WARRIOR; }
    InventoryResult CanUseItem(const ItemPrototype* proto) const { return proto && proficiency ? EQUIP_ERR_OK : EQUIP_ERR_CANT_USE_ITEM; }
    InventoryResult CanEquipItem(uint8, uint16& dest, Item*, bool, bool) const
    {
        dest = 15;
        return EQUIP_ERR_OK;
    }
    Item* GetWeaponForAttack(WeaponAttackType, bool, bool) const { return nullptr; }
    Item* GetItemByPos(uint16) const { return mainHand; }
};

struct RandomPlayerbotMgr
{
    static InventoryResult CanEquipUnseenItem(Player* bot, uint8 slot, uint16& dest, uint32)
    {
        return bot->CanEquipItem(slot, dest, nullptr, true, false);
    }
};

struct ItemUsageValue
{
    static ItemUsage QueryItemUsageForEquip(ItemQualifier&, Player*);
    static uint32 GetSmallestBagSize(Player*) { return 0; }
    static bool MustEquipForQuest(const ItemPrototype*, Player*) { return false; }
    static uint32 CurrentStacks(PlayerbotAI*, const ItemPrototype*) { return 0; }
    static Item* CurrentItem(const ItemPrototype*, Player*) { return nullptr; }
};

struct RandomItemMgr
{
    unsigned enchantLookups = 0;
    uint32 GetPlayerSpecId(Player*) { return 3; }
    bool ShouldEquipWeaponForSpec(uint8, uint32, const ItemPrototype*) const { return true; }
    bool ShouldEquipArmorForSpec(uint8, uint32, const ItemPrototype*) const { return true; }
    uint32 CalculateStatWeight(uint8, uint32, const ItemPrototype* proto, ItemSpecType&) { return proto->weight; }
    uint32 CalculateEnchantWeight(uint8, uint32, uint32) { ++enchantLookups; return 1000; }
    uint32 CalculateRandomPropertyWeight(uint8, uint32, int32 property)
    {
        // Random properties use the core enchantment storage too, but are
        // intrinsic item stats rather than an applied permanent enchantment.
        return std::abs(property) == 1 ? 40 : 5;
    }
    uint32 CalculateGemWeight(uint8, uint32, uint32 gem)
    {
#ifdef MANGOSBOT_ZERO
        return 0;
#else
        return gem;
#endif
    }
    uint32 CalculateSocketWeight(uint8, ItemQualifier& item, uint32)
    {
#ifdef MANGOSBOT_ZERO
        return 0;
#else
        return item.data.socket;
#endif
    }
    uint32 ItemStatWeight(Player*, ItemQualifier&);
    uint32 ItemStatWeight(Player*, Item*);
};

RandomItemMgr sRandomItemMgr;

#include "item_upgrade_weight_source.inc"

uint32 urand(uint32 low, uint32) { return low; }

#define AI_VALUE2(type, name, qualifier) (context->inventory)
#define AI_VALUE2_EXISTS(type, name, qualifier, fallback) (fallback)
#include "item_usage_equip_source.inc"
#undef AI_VALUE2_EXISTS
#undef AI_VALUE2

namespace
{
#include "item_exchange_weight_source.inc"

    unsigned checks = 0, failures = 0;

    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition)
        {
            ++failures;
            std::cerr << "FAILED: " << description << '\n';
        }
    }

    void CheckScorer(const std::string& name, const std::function<uint64(ItemQualifier&)>& score)
    {
        const ItemPrototype base{100}, better{120}, worse{90}, between{130};
        Item plainItem{&base}, enchantedItem{&base, 1}, betterItem{&better}, worseEnchantedItem{&worse, 1};
        ItemQualifier plain(&plainItem), enchanted(&enchantedItem), upgrade(&betterItem), downgrade(&worseEnchantedItem);

        Check(score(enchanted) == 100, name + ": permanent enchants add no need score");
        Check(score(plain) == score(enchanted), name + ": the same base item scores equally with or without an enchant");
        Check(score(upgrade) > score(enchanted), name + ": an enchanted current item cannot hide a base-stat upgrade");
        Check(score(downgrade) < score(plain), name + ": an enchanted candidate cannot hide a base-stat downgrade");

        Item randomItem{&base, 1, 1}, weakRandomItem{&base, 0, 2}, negativeRandomItem{&base, 1, -1}, betweenItem{&between};
        ItemQualifier random(&randomItem), weakRandom(&weakRandomItem), negativeRandom(&negativeRandomItem), mid(&betweenItem);
        Check(score(random) == 140, name + ": inherent random-property stats are retained");
        Check(score(negativeRandom) == 140, name + ": signed random-property identifiers are retained");
        Check(score(weakRandom) == 105, name + ": a weaker random property retains its own value");
        Check(score(random) > score(weakRandom), name + ": different random properties remain distinguishable");
        Check(score(mid) < score(random), name + ": removing applied enchants does not erase a strong inherent suffix");

        Item socketedItem{&base, 1, 1, {2, 3, 5, 7}, 11};
        ItemQualifier socketed(&socketedItem);
#ifdef MANGOSBOT_ZERO
        Check(score(socketed) == 140, name + ": Classic has no gem or socket score");
#else
        Check(score(socketed) == 168, name + ": expansion gem and socket stats remain included");
#endif
    }

    void CheckAutomaticWeaponUsage()
    {
        Player warrior;
        const ItemPrototype grayWeapon{100, ITEM_QUALITY_POOR, 1};
        const ItemPrototype grayWithoutStats{0, ITEM_QUALITY_POOR, 2};
        const ItemPrototype whiteWeapon{40, ITEM_QUALITY_NORMAL, 3};
        Item gray{&grayWeapon}, grayNoStats{&grayWithoutStats}, white{&whiteWeapon};
        const auto usage = [&](Item& item, bool inBags = true)
        {
            warrior.ai.context.inventory.clear();
            if (inBags)
                warrior.ai.context.inventory.push_back(&item);
            ItemQualifier qualifier(&item);
            return ItemUsageValue::QueryItemUsageForEquip(qualifier, &warrior);
        };

        Check(usage(gray) == ItemUsage::ITEM_USAGE_NONE, "an empty warrior main hand rejects a gray weapon with a positive score");
        Check(usage(grayNoStats) == ItemUsage::ITEM_USAGE_NONE, "a gray weapon is not a fallback for an empty main hand");
        Check(usage(gray, false) == ItemUsage::ITEM_USAGE_NONE, "a gray weapon outside the bags is not considered an equipment upgrade");
        Check(usage(white) == ItemUsage::ITEM_USAGE_EQUIP, "an empty warrior main hand accepts a suitable white weapon");
        Check(usage(white, false) == ItemUsage::ITEM_USAGE_EQUIP, "a suitable white weapon outside the bags remains eligible");

        warrior.mainHand = &gray;
        Check(usage(white) == ItemUsage::ITEM_USAGE_EQUIP, "a suitable white weapon replaces a worn gray weapon with a higher score");
        warrior.proficiency = false;
        Check(usage(white) == ItemUsage::ITEM_USAGE_NONE, "normal weapon proficiency still applies when replacing gray gear");
    }
}

int main()
{
    Player bot;
    CheckScorer("equipment", [&](ItemQualifier& item) { return sRandomItemMgr.ItemStatWeight(&bot, item); });
    CheckScorer("exchange", [&](ItemQualifier& item) { return ItemWeight(&bot, 3, item); });
    CheckAutomaticWeaponUsage();

    const ItemPrototype proto{100};
    for (uint32 enchant : {0u, 1u, 999u})
    {
        Item item{&proto, enchant, 1, {2, 3, 5, 7}, 11};
        ItemQualifier qualifier(&item);
        Check(sRandomItemMgr.ItemStatWeight(&bot, &item) == sRandomItemMgr.ItemStatWeight(&bot, qualifier),
            "the equipped Item* overload uses the same enchant-free score");
        Check(sRandomItemMgr.ItemStatWeight(&bot, qualifier) == ItemWeight(&bot, 3, qualifier),
            "equipment and exchange scorers agree for every applied enchantment");
    }
    Check(sRandomItemMgr.enchantLookups == 0, "upgrade scorers never request applied permanent enchantment weights");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
