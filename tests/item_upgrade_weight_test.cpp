#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int32 = std::int32_t;

// Only core inputs to the extracted production scorers are represented here.
struct ItemPrototype { uint32 weight; };
struct Player
{
    uint8 getClass() const { return 8; }
};
enum ItemSpecType { TestSpec };

struct Item
{
    const ItemPrototype* proto;
    uint32 enchant = 0;
    int32 randomProperty = 0;
    std::array<uint32, 4> gems{};
    uint32 socket = 0;
};

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

struct RandomItemMgr
{
    unsigned enchantLookups = 0;
    uint32 GetPlayerSpecId(Player*) { return 3; }
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
}

int main()
{
    Player bot;
    CheckScorer("equipment", [&](ItemQualifier& item) { return sRandomItemMgr.ItemStatWeight(&bot, item); });
    CheckScorer("exchange", [&](ItemQualifier& item) { return ItemWeight(&bot, 3, item); });

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
