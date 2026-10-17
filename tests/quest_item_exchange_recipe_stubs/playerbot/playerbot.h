#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using uint8 = std::uint8_t;
constexpr uint8 NULL_SLOT = 255;
constexpr uint32 INVTYPE_NON_EQUIP = 0;
constexpr uint32 ITEM_CLASS_WEAPON = 2;
constexpr uint32 ITEM_CLASS_ARMOR = 4;
constexpr uint32 QUEST_OBJECTIVES_COUNT = 4;
constexpr uint32 QUEST_ITEM_OBJECTIVES_COUNT = 4;
constexpr uint32 QUEST_REWARDS_COUNT = 4;
constexpr uint32 QUEST_REWARD_CHOICES_COUNT = 6;
enum QuestSpecialFlags { QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT = 2 };
class PlayerbotAI;
struct ItemPrototype
{
    uint32 ItemId = 0, Class = 0, Quality = 0, Bonding = 0, InventoryType = 0, MaxCount = 0;
};
struct Quest
{
    uint32 id = 0, method = 2, classes = 0, races = 0, special = 0, nextChain = 0;
    std::int32_t next = 0;
    bool active = true;
    uint32 ReqItemId[4]{}, ReqItemCount[4]{}, ReqCreatureOrGOCount[4]{}, ReqSpell[4]{};
    uint32 RewItemId[4]{}, RewItemCount[4]{}, RewChoiceItemId[6]{}, RewChoiceItemCount[6]{};
    bool HasSpecialFlag(QuestSpecialFlags flag) const { return (special & flag) != 0; }
    bool IsActive() const { return active; }
    uint32 GetQuestId() const { return id; }
    uint32 GetQuestMethod() const { return method; }
    uint32 GetRequiredClasses() const { return classes; }
    uint32 GetRequiredRaces() const { return races; }
    uint32 GetNextQuestInChain() const { return nextChain; }
    std::int32_t GetNextQuestId() const { return next; }
};
struct ObjectMgr
{
    using QuestMap = std::unordered_map<uint32, std::unique_ptr<Quest>>;
    QuestMap quests;
    std::unordered_map<uint32, ItemPrototype> items;
    const ItemPrototype* GetItemPrototype(uint32 id) const
    {
        auto it = items.find(id);
        return it == items.end() ? nullptr : &it->second;
    }
    const Quest* GetQuestTemplate(uint32 id) const
    {
        auto it = quests.find(id);
        return it == quests.end() ? nullptr : it->second.get();
    }
    const QuestMap& GetQuestTemplates() const { return quests; }
};
extern ObjectMgr sObjectMgr;
