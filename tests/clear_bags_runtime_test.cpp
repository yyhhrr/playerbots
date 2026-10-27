#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
constexpr uint8 INVENTORY_SLOT_BAG_0 = 255;
constexpr uint8 INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr uint8 INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39;
constexpr uint8 BANK_SLOT_ITEM_START = 39, KEYRING_SLOT_START = 86;
enum class PlayerbotSecurityLevel { PLAYERBOT_SECURITY_ALLOW_ALL };
struct PlayerbotAI;
struct Group { bool raid = false; };
struct Item
{
    uint32 count;
    bool usedInSpell = false;
    std::string description;
    explicit Item(uint32 count = 1, std::string description = "item") : count(count), description(std::move(description)) {}
    virtual ~Item() = default;
    uint32 GetCount() const { return count; }
    bool IsUsedInSpell() const { return usedInSpell; }
};
struct Bag : Item
{
    std::vector<Item*> items;
    explicit Bag(uint32 size = 16) : Item(1, "container"), items(size, nullptr) {}
    uint32 GetBagSize() const { return static_cast<uint32>(items.size()); }
    Item* GetItemByPos(uint32 slot) { return slot < items.size() ? items[slot] : nullptr; }
};
struct Player
{
    Group* group = nullptr;
    PlayerbotAI* ai = nullptr;
    bool realPlayer = true, trading = false, casting = false, channeling = false, delayedCast = false;
    unsigned subgroup = 0;
    std::map<uint8, Item*> root;
    std::vector<std::pair<uint8, uint8>> destroyed;
    std::vector<std::unique_ptr<Item>> storage;
    Group* GetGroup() const { return group; }
    PlayerbotAI* GetPlayerbotAI() const { return ai; }
    bool isRealPlayer() const { return realPlayer; }
    void* GetTradeData() { return trading ? this : nullptr; }
    bool IsNonMeleeSpellCasted(bool withDelayed) const { return casting || channeling || (withDelayed && delayedCast); }
    Item* GetItemByPos(uint8 bag, uint8 slot)
    {
        if (bag == INVENTORY_SLOT_BAG_0)
        {
            const auto found = root.find(slot);
            return found == root.end() ? nullptr : found->second;
        }
        const auto found = root.find(bag);
        Bag* container = found == root.end() ? nullptr : dynamic_cast<Bag*>(found->second);
        return container ? container->GetItemByPos(slot) : nullptr;
    }
    void DestroyItem(uint8 bag, uint8 slot, bool update)
    {
        if (!update || !GetItemByPos(bag, slot))
            std::abort();
        destroyed.emplace_back(bag, slot);
        if (bag == INVENTORY_SLOT_BAG_0)
            root.erase(slot);
        else
            static_cast<Bag*>(root.at(bag))->items.at(slot) = nullptr;
    }
    Item* Put(uint8 slot, uint32 count = 1, const std::string& description = "item")
    {
        storage.push_back(std::make_unique<Item>(count, description));
        return root[slot] = storage.back().get();
    }
    Bag* PutBag(uint8 slot, uint32 size = 16)
    {
        auto container = std::make_unique<Bag>(size);
        Bag* result = container.get();
        root[slot] = result;
        storage.push_back(std::move(container));
        return result;
    }
    Item* PutInBag(uint8 bag, uint8 slot, uint32 count = 1, const std::string& description = "item")
    {
        storage.push_back(std::make_unique<Item>(count, description));
        Item* result = storage.back().get();
        static_cast<Bag*>(root.at(bag))->items.at(slot) = result;
        return result;
    }
};
struct Security
{
    bool allowed = true;
    unsigned checks = 0;
    bool CheckLevelFor(PlayerbotSecurityLevel, bool silent, Player*)
    {
        if (!silent) std::abort();
        ++checks;
        return allowed;
    }
};
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
        PlayerbotAI* ai = nullptr;
        std::vector<std::string> cleared;
        std::map<std::string, std::function<Trigger*(PlayerbotAI*)>> creators;
        std::map<std::string, std::unique_ptr<Trigger>> created;
        explicit AiObjectContext(PlayerbotAI* ai);
        Trigger* GetTrigger(const std::string& name);
        void ClearValues(const std::string& name) { cleared.push_back(name); }
    };
}
struct PlayerbotAI
{
    struct Reply { Player* player; std::string text; bool isPrivate, noRepeat, ignoreSilent; };
    Player* bot;
    bool realPlayer = false;
    Security security;
    ai::AiObjectContext context;
    std::vector<Reply> replies;
    explicit PlayerbotAI(Player* bot) : bot(bot), context(this) {}
    bool IsRealPlayer() const { return realPlayer; }
    Security* GetSecurity() { return &security; }
    bool TellPlayerNoFacing(Player* player, const std::string& text, PlayerbotSecurityLevel,
        bool isPrivate, bool noRepeat, bool ignoreSilent)
    {
        replies.push_back({player, text, isPrivate, noRepeat, ignoreSilent});
        return true;
    }
};
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
    class ClearBagsAction
    {
        PlayerbotAI* ai;
        Player* bot;
        AiObjectContext* context;
    public:
        explicit ClearBagsAction(PlayerbotAI* ai) : ai(ai), bot(ai->bot), context(&ai->context) {}
        bool Execute(Event& event);
    };
}
#include "clear_bags_chat_trigger.inc"
ai::AiObjectContext::AiObjectContext(PlayerbotAI* ai) : ai(ai)
{
    using ai::ChatCommandTrigger;
#include "clear_bags_trigger_registration.inc"
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
    struct ChatHelper
    {
        static std::string parseValue(const std::string&, const std::string&) { return ""; }
        static bool parseable(const std::string&) { return false; }
    };
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
        std::map<std::string, std::function<ClearBagsAction*(PlayerbotAI*)>> creators;
        ActionRegistry()
        {
#include "clear_bags_action_registration.inc"
        }
    };
    struct SupportedCommands
    {
        std::list<std::string> supported;
        SupportedCommands()
        {
#include "clear_bags_supported.inc"
        }
    };
}
using namespace ai;
#include "clear_bags_action_source.inc"

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
        Group party, other;
        Player bot, requester;
        PlayerbotAI ai{&bot};
        ClearBagsAction action{&ai};
        Fixture()
        {
            bot.group = requester.group = &party;
            bot.ai = &ai;
            bot.realPlayer = false;
        }
        bool Execute(Player* owner, const std::string& param = "")
        { Event event("clear bags", param, owner); return action.Execute(event); }
        void Seed() { bot.Put(INVENTORY_SLOT_ITEM_START, 20, "food"); }
        void CheckUntouched(const char* message)
        {
            Check(bot.destroyed.empty(), message);
            Check(bot.GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_ITEM_START) != nullptr, "rejected command keeps inventory");
            Check(ai.context.cleared.empty(), "rejected command keeps cached inventory");
        }
    };
    bool Dispatch(Fixture& fixture, const std::string& command, Player* owner)
    {
        ExternalEventHelper helper(&fixture.ai.context);
        Check(helper.ParseChatCommand(command, owner), "actual parser recognizes registered command");
        SupportedCommands supported;
        ActionRegistry actions;
        bool executed = false;
        for (const std::string& name : supported.supported)
        {
            Trigger* trigger = fixture.ai.context.GetTrigger(name);
            Check(trigger != nullptr, "supported command has trigger");
            Event event = trigger->Check();
            if (event.getSource().empty()) continue;
            const auto factory = actions.creators.find(name);
            Check(factory != actions.creators.end(), "trigger has action registration");
            std::unique_ptr<ClearBagsAction> action(factory->second(&fixture.ai));
            executed = action->Execute(event) || executed;
            trigger->Reset();
        }
        return executed;
    }
    void Authorization()
    {
        { Fixture f; f.Seed(); Check(!f.Execute(nullptr), "no requester is rejected"); f.CheckUntouched("null requester cannot destroy"); }
        { Fixture f; f.Seed(); Check(!f.Execute(&f.bot), "self request rejected"); f.CheckUntouched("bot cannot command itself"); }
        { Fixture f; f.Seed(); f.ai.realPlayer = true; Check(!f.Execute(&f.requester), "real player target rejected"); f.CheckUntouched("real character inventory preserved"); }
        { Fixture f; f.Seed(); f.bot.group = nullptr; Check(!f.Execute(&f.requester), "ungrouped bot rejected"); f.CheckUntouched("ungrouped inventory preserved"); }
        { Fixture f; f.Seed(); f.requester.group = nullptr; Check(!f.Execute(&f.requester), "requester left group rejected"); f.CheckUntouched("queued command cannot survive leaving"); }
        { Fixture f; f.Seed(); f.requester.group = &f.other; Check(!f.Execute(&f.requester), "different group rejected"); f.CheckUntouched("other group inventory preserved"); }
        { Fixture f; f.Seed(); PlayerbotAI sender(&f.requester); f.requester.ai = &sender; Check(!f.Execute(&f.requester), "automated bot sender rejected"); f.CheckUntouched("bot messages cannot destroy"); }
        { Fixture f; f.Seed(); f.ai.security.allowed = false; Check(!f.Execute(&f.requester), "security rejection honored"); f.CheckUntouched("security denied inventory preserved"); }
        { Fixture f; f.Seed(); f.bot.trading = true; Check(!f.Execute(&f.requester), "active trade rejected"); f.CheckUntouched("traded items preserved"); Check(f.ai.replies.size() == 1, "trade rejection tells requester"); }
        { Fixture f; f.Seed(); Check(!f.Execute(&f.requester, "please"), "extra arguments rejected"); f.CheckUntouched("sentence prefix cannot destroy"); Check(f.ai.replies.size() == 1, "usage reply supplied"); }
        { Fixture f; f.Seed(); PlayerbotAI sender(&f.requester); sender.realPlayer = true; f.requester.ai = &sender; Check(f.Execute(&f.requester), "AI-backed real player can command"); Check(f.bot.destroyed.size() == 1, "real player request clears inventory"); }
    }
    void Contents()
    {
        Fixture f;
        Item* equipped = f.bot.Put(0, 1, "equipped epic");
        Item* banked = f.bot.Put(BANK_SLOT_ITEM_START, 3, "bank contents");
        Item* key = f.bot.Put(KEYRING_SLOT_START, 1, "dungeon key");
        Bag* bankBag = f.bot.PutBag(63);
        f.bot.PutInBag(63, 0, 2, "bank bag contents");
        Item* bankBagItem = bankBag->items[0];
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            f.bot.PutBag(bag, 6);
        f.bot.Put(INVENTORY_SLOT_ITEM_START, 20, "white food");
        f.bot.Put(INVENTORY_SLOT_ITEM_END - 1, 5, "quest material");
        f.bot.PutInBag(INVENTORY_SLOT_BAG_START, 0, 1, "epic spare equipment");
        f.bot.PutInBag(INVENTORY_SLOT_BAG_START + 1, 5, 40, "ammunition");
        f.bot.PutInBag(INVENTORY_SLOT_BAG_END - 1, 5, 2, "common crafting material");
        f.bot.PutBag(INVENTORY_SLOT_ITEM_START + 1, 16);
        Check(f.Execute(&f.requester), "party clear succeeds");
        Check(f.bot.destroyed.size() == 6, "clears every occupied main/additional bag stack");
        Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, 0) == equipped, "equipped gear preserved");
        Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, BANK_SLOT_ITEM_START) == banked, "bank preserved");
        Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, KEYRING_SLOT_START) == key, "keyring preserved");
        Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, 63) == bankBag && bankBag->items[0] == bankBagItem, "bank bags and contents preserved");
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            auto* container = dynamic_cast<Bag*>(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, bag));
            Check(container != nullptr, "equipped bag preserved");
            Check(std::all_of(container->items.begin(), container->items.end(), [](Item* item) { return item == nullptr; }), "all additional bag slots cleared");
        }
        Check(f.ai.replies.size() == 1, "one summary instead of per-item spam");
        Check(f.ai.replies[0].player == &f.requester, "summary sent to original requester");
        Check(f.ai.replies[0].isPrivate && !f.ai.replies[0].noRepeat && f.ai.replies[0].ignoreSilent, "private summary works with silent strategy and repeat commands");
        Check(f.ai.replies[0].text.find("6 ") != std::string::npos && f.ai.replies[0].text.find("69 ") != std::string::npos, "summary counts stacks and total item quantity");
        for (const std::string name : {"bag space", "inventory items", "inventory item ids", "item count", "item usage", "item for spell", "stack space for item"})
            Check(std::find(f.ai.context.cleared.begin(), f.ai.context.cleared.end(), name) != f.ai.context.cleared.end(), "inventory-dependent caches invalidated");
        Check(std::find(f.ai.context.cleared.begin(), f.ai.context.cleared.end(), "force item usage") == f.ai.context.cleared.end(), "persistent user item preferences retained");
    }
    void Commands()
    {
        const std::string chinese = "\xE6\xB8\x85\xE7\xA9\xBA\xE8\x83\x8C\xE5\x8C\x85";
        for (const std::string command : {std::string("clear bags"), chinese})
        {
            Fixture f; f.Seed(); Check(Dispatch(f, command, &f.requester), "English/Chinese registered command executes");
            Check(f.bot.destroyed.size() == 1, "one command destroys each stack once");
            Check(!Dispatch(f, command + " ?", &f.requester), "registered prefix with question does not delete");
        }
        { Fixture f; f.party.raid = true; f.bot.subgroup = 7; f.requester.subgroup = 0; f.Seed(); Check(Dispatch(f, chinese, &f.requester), "raid command reaches bot in another subgroup"); Check(f.bot.destroyed.size() == 1, "raid bot clears bag"); }
        { Fixture f; ExternalEventHelper helper(&f.ai.context); Check(helper.ParseChatCommand(chinese, &f.requester), "queue command while in party"); f.Seed(); f.requester.group = nullptr; Check(!Dispatch(f, chinese, &f.requester), "membership rechecked on execution"); f.CheckUntouched("queued departure cannot erase inventory"); }
        { Fixture f; Check(f.Execute(&f.requester), "already empty bags succeed"); Check(f.bot.destroyed.empty(), "empty bags have no removal calls"); Check(f.ai.replies.size() == 1, "empty bags report success"); }
        { Fixture f; f.bot.Put(INVENTORY_SLOT_ITEM_START, UINT32_MAX); f.bot.Put(INVENTORY_SLOT_ITEM_START + 1, UINT32_MAX); Check(f.Execute(&f.requester), "large quantities supported"); Check(f.ai.replies[0].text.find("8589934590") != std::string::npos, "total quantity uses 64 bits"); }
    }
    void ItemsInUse()
    {
        for (unsigned phase = 0; phase != 3; ++phase)
        {
            Fixture f; f.Seed();
            f.bot.casting = phase == 0;
            f.bot.channeling = phase == 1;
            f.bot.delayedCast = phase == 2;
            Check(!f.Execute(&f.requester), "casting, channeling and delayed spells reject clearing");
            f.CheckUntouched("active spells preserve inventory until cast finishes");
            Check(f.ai.replies.size() == 1, "active spell reports retry requirement");
            f.bot.casting = f.bot.channeling = f.bot.delayedCast = false;
            Check(f.Execute(&f.requester), "can retry after spell finishes");
            Check(f.bot.destroyed.size() == 1, "retry actually clears inventory");
        }
        {
            Fixture f; f.Seed();
            f.bot.Put(INVENTORY_SLOT_ITEM_END - 1, 3, "ordinary late main-bag stack");
            f.bot.PutBag(INVENTORY_SLOT_BAG_END - 1, 16);
            Item* busy = f.bot.PutInBag(INVENTORY_SLOT_BAG_END - 1, 15, 1, "last bag item used by spell");
            busy->usedInSpell = true;
            Check(!f.Execute(&f.requester), "used item in last slot rejects whole operation");
            f.CheckUntouched("preflight must not destroy earlier main-bag contents");
            Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_ITEM_END - 1) != nullptr, "other earlier item also survives preflight");
            Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_END - 1, 15) == busy, "item referenced by a spell remains alive");
            busy->usedInSpell = false;
            Check(f.Execute(&f.requester), "can retry once item is no longer used by spell");
            Check(f.bot.destroyed.size() == 3, "retry clears all original stacks");
        }
        {
            Fixture f; f.Seed();
            f.bot.Put(0, 1, "equipped item used by spell")->usedInSpell = true;
            f.bot.Put(BANK_SLOT_ITEM_START, 1, "bank item used by spell")->usedInSpell = true;
            f.bot.Put(KEYRING_SLOT_START, 1, "keyring item used by spell")->usedInSpell = true;
            Check(f.Execute(&f.requester), "busy excluded slots do not block clearing bag contents");
            Check(f.bot.destroyed.size() == 1, "only ordinary bag stack removed");
            Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, 0) != nullptr, "busy equipment stays equipped");
            Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, BANK_SLOT_ITEM_START) != nullptr, "busy bank item preserved");
            Check(f.bot.GetItemByPos(INVENTORY_SLOT_BAG_0, KEYRING_SLOT_START) != nullptr, "busy keyring item preserved");
        }
    }
}
int main()
{
    Authorization();
    Contents();
    Commands();
    ItemsInUse();
    std::cout << checks << " clear-bags runtime checks passed\n";
}
