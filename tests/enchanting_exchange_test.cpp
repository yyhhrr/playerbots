#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// Compile the production exchange commit section. Inventory and spell effects
// are represented by a small event log; the exchange planner is tested separately.
using uint32 = std::uint32_t;
constexpr unsigned PERM_ENCHANTMENT_SLOT = 0;
struct Item
{
    uint32 guid = 0;
    unsigned slot = 0, enchant = 0;
    bool equipped = false;
    bool IsEquipped() const { return equipped; }
    unsigned GetSlot() const { return slot; }
    unsigned GetEnchantmentId(unsigned) const { return enchant; }
};
struct PlayerbotAIConfig { bool autoEnchantUpgradeLoot = true; } sPlayerbotAIConfig;
struct Player
{
    std::map<uint32, Item> items;
    std::vector<std::string> events;
    Item* GetItemByGuid(uint32 guid)
    {
        const auto found = items.find(guid);
        return found == items.end() ? nullptr : &found->second;
    }
    void DestroyItemCount(Item& item, uint32&, bool)
    {
        const auto guid = item.guid;
        events.push_back("consume:" + std::to_string(guid));
        items.erase(guid);
    }
    void SendNewItem(Item* item, unsigned, bool, bool)
    {
        events.push_back("notify:" + std::to_string(item->guid));
    }
};
struct PlayerbotAI { Player* bot; bool allowEnchant = true; };
namespace enchanting
{
    bool EnchantItem(PlayerbotAI* ai, Item* item)
    {
        ai->bot->events.push_back("attempt:" + std::to_string(item->guid));
        if (!ai->allowEnchant)
            return false;
        item->enchant = 123;
        ai->bot->events.push_back("enchant:" + std::to_string(item->guid));
        return true;
    }
}
struct Context { void ClearValues(const std::string&) {} };
struct Key { uint32 guid; uint32 count; };
struct Reward { uint32 guid; unsigned slot; };

void Commit(Player* bot, PlayerbotAI* ai, const std::vector<Key>& reserved,
    const std::vector<Reward>& created)
{
    Context fixtureContext;
    Context* context = &fixtureContext;
#include "enchanting_exchange_commit.inc"
}

struct Fixture
{
    Player bot;
    PlayerbotAI ai{&bot};
    std::vector<Key> keys{{1, 1}, {2, 1}};
    std::vector<Reward> rewards{{100, 4}, {101, 9}};
    Fixture()
    {
        sPlayerbotAIConfig.autoEnchantUpgradeLoot = true;
        bot.items.emplace(1, Item{1});
        bot.items.emplace(2, Item{2});
        bot.items.emplace(100, Item{100, 4, 0, true});
        bot.items.emplace(101, Item{101, 9, 0, true});
    }
    void Run() { Commit(&bot, &ai, keys, rewards); }
    std::size_t Position(const std::string& event) const
    {
        return std::find(bot.events.begin(), bot.events.end(), event) - bot.events.begin();
    }
    bool Has(const std::string& event) const { return Position(event) < bot.events.size(); }
};

unsigned checks = 0, failures = 0;
void Check(bool passed, const std::string& reason)
{
    ++checks;
    if (!passed) { ++failures; std::cerr << "FAILED: " << reason << '\n'; }
}

int main()
{
    Fixture success;
    success.Run();
    Check(success.bot.GetItemByGuid(100)->enchant != 0, "already-equipped chest reward receives automatic enchanting");
    Check(success.bot.GetItemByGuid(101)->enchant != 0, "already-equipped glove reward receives automatic enchanting");
    Check(!success.bot.GetItemByGuid(1) && !success.bot.GetItemByGuid(2), "exchange keys remain consumed");
    Check(success.Position("consume:2") < success.Position("attempt:100"), "every key is consumed before enchanting starts");
    Check(success.Position("enchant:100") < success.Position("notify:100"), "the new-item notification follows enchanting");

    Fixture disabled;
    sPlayerbotAIConfig.autoEnchantUpgradeLoot = false;
    disabled.Run();
    Check(!disabled.Has("attempt:100") && !disabled.Has("attempt:101"), "the auto-enchant setting is respected");
    Check(disabled.Has("notify:100") && !disabled.bot.GetItemByGuid(1), "disabled enchanting does not block the exchange");

    Fixture existing;
    existing.bot.GetItemByGuid(100)->enchant = 999;
    existing.Run();
    Check(!existing.Has("attempt:100") && existing.bot.GetItemByGuid(100)->enchant == 999,
        "an existing permanent enchant is preserved");
    Check(existing.Has("enchant:101"), "another unenchanted reward can still be enchanted");

    Fixture unequipped;
    unequipped.bot.GetItemByGuid(100)->equipped = false;
    unequipped.bot.GetItemByGuid(101)->slot = 8;
    unequipped.Run();
    Check(!unequipped.Has("attempt:100"), "a reward not actually equipped is not enchanted");
    Check(!unequipped.Has("attempt:101"), "a reward in a different slot is not enchanted");

    Fixture rejected;
    rejected.ai.allowEnchant = false;
    rejected.Run();
    Check(rejected.Has("attempt:100") && !rejected.Has("enchant:100"), "a policy-rejected enchant can be skipped");
    Check(rejected.Has("notify:100") && !rejected.bot.GetItemByGuid(1),
        "enchantment failure cannot roll back an already successful exchange");

    std::cout << "Enchanting exchange: " << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
