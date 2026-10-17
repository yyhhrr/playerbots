#include "playerbot/playerbot.h"
#include "QuestItemExchangeRecipes.h"
#include "QuestItemExchangePolicy.h"
#include "PlayerbotAIConfig.h"

#include <algorithm>
#include <set>

namespace ai
{
    namespace
    {
        quest_exchange_policy::KeyInfo KeyInfoForPrototype(const ItemPrototype& proto)
        {
            const auto& configuredKeys = sPlayerbotAIConfig.questItemExchangeKeys;
            quest_exchange_policy::KeyInfo info;
            info.itemClass = proto.Class;
            info.quality = proto.Quality;
            info.bonding = proto.Bonding;
            info.inventoryType = proto.InventoryType;
            info.maxCount = proto.MaxCount;
            info.explicitKey = std::find(configuredKeys.begin(), configuredKeys.end(), proto.ItemId) != configuredKeys.end();
            return info;
        }
    }

    bool IsQuestExchangeKeyPrototype(const ItemPrototype* proto)
    {
        return proto && quest_exchange_policy::IsKeyItem(KeyInfoForPrototype(*proto));
    }

    bool IsAutomaticQuestItemExchangeRecipe(const QuestItemExchangeRecipe& recipe)
    {
        return !recipe.keys.empty() && std::all_of(recipe.keys.begin(), recipe.keys.end(), [](const QuestItemExchangeItem& key)
        {
            const ItemPrototype* proto = sObjectMgr.GetItemPrototype(key.itemId);
            return key.count && proto && quest_exchange_policy::IsAutomaticKey(KeyInfoForPrototype(*proto));
        });
    }

    namespace
    {
        bool IsEquipmentReward(uint32 itemId)
        {
            const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
            return proto && proto->InventoryType != INVTYPE_NON_EQUIP &&
                (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR);
        }

        bool HasRequiredItems(const Quest* quest)
        {
            for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            {
                if (quest->ReqItemId[i] && quest->ReqItemCount[i])
                    return true;
            }

            return false;
        }

        void AddItem(std::vector<QuestItemExchangeItem>& items, uint32 itemId, uint32 count)
        {
            auto existing = std::find_if(items.begin(), items.end(), [itemId](const QuestItemExchangeItem& item)
            {
                return item.itemId == itemId;
            });
            if (existing == items.end())
                items.push_back({ itemId, count });
            else
                existing->count += count;
        }

        void GetEquipmentRewards(const Quest* quest, QuestItemExchangeRecipe& recipe)
        {
            for (uint32 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            {
                if (quest->RewItemCount[i] && IsEquipmentReward(quest->RewItemId[i]))
                    AddItem(recipe.fixedRewards, quest->RewItemId[i], quest->RewItemCount[i]);
            }

            // Each choice is an alternative; never combine two choice slots into one grant.
            for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
            {
                if (quest->RewChoiceItemCount[i] && IsEquipmentReward(quest->RewChoiceItemId[i]))
                    recipe.choiceRewards.push_back({ quest->RewChoiceItemId[i], quest->RewChoiceItemCount[i] });
            }
        }

        bool IntersectMask(uint32& current, uint32 next)
        {
            if (!next)
                return true;
            if (!current)
            {
                current = next;
                return true;
            }

            current &= next;
            return current != 0;
        }

        bool RewardsContainKey(const QuestItemExchangeRecipe& recipe)
        {
            for (const auto& key : recipe.keys)
            {
                auto matchesKey = [&key](const QuestItemExchangeItem& reward)
                {
                    return reward.itemId == key.itemId;
                };
                if (std::any_of(recipe.fixedRewards.begin(), recipe.fixedRewards.end(), matchesKey) ||
                    std::any_of(recipe.choiceRewards.begin(), recipe.choiceRewards.end(), matchesKey))
                    return true;
            }

            return false;
        }

        bool FindRewards(const Quest* initialQuest, QuestItemExchangeRecipe& recipe)
        {
            const Quest* quest = initialQuest;
            std::set<uint32> visited;
            bool isFollowup = false;
            while (quest)
            {
                if (!quest->IsActive() || !visited.insert(quest->GetQuestId()).second)
                    return false;

                // Quest objectives are bypassed, but another collection needs its own key.
                if (isFollowup && (HasRequiredItems(quest) ||
                    (quest->GetQuestMethod() != 0 && quest->GetQuestMethod() != 2)))
                    return false;

                if (!IntersectMask(recipe.requiredClasses, quest->GetRequiredClasses()) ||
                    !IntersectMask(recipe.requiredRaces, quest->GetRequiredRaces()))
                    return false;

                GetEquipmentRewards(quest, recipe);
                if (!recipe.fixedRewards.empty() || !recipe.choiceRewards.empty())
                    return !RewardsContainKey(recipe);

                uint32 nextQuestId = quest->GetNextQuestInChain();
                if (!nextQuestId && quest->GetNextQuestId() > 0)
                    nextQuestId = static_cast<uint32>(quest->GetNextQuestId());
                if (!nextQuestId)
                    return false;

                quest = sObjectMgr.GetQuestTemplate(nextQuestId);
                isFollowup = true;
            }

            return false;
        }

        std::vector<QuestItemExchangeRecipe> BuildRecipes()
        {
            std::vector<QuestItemExchangeRecipe> recipes;
            for (const auto& [questId, questPtr] : sObjectMgr.GetQuestTemplates())
            {
                const Quest* quest = questPtr.get();
                if (!quest || !quest->IsActive() || quest->GetQuestMethod() != 2)
                    continue;

                QuestItemExchangeRecipe recipe;
                recipe.questId = questId;
                for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                {
                    if (quest->ReqItemId[i] && quest->ReqItemCount[i] &&
                        IsQuestExchangeKeyPrototype(sObjectMgr.GetItemPrototype(quest->ReqItemId[i])))
                        AddItem(recipe.keys, quest->ReqItemId[i], quest->ReqItemCount[i]);
                }

                if (recipe.keys.empty() || !FindRewards(quest, recipe))
                    continue;

                recipes.push_back(std::move(recipe));
            }

            // QuestMap is unordered; a stable order also makes equal-score selection reproducible.
            std::sort(recipes.begin(), recipes.end(), [](const QuestItemExchangeRecipe& left, const QuestItemExchangeRecipe& right)
            {
                return left.questId < right.questId;
            });
            return recipes;
        }
    }

    const std::vector<QuestItemExchangeRecipe>& GetQuestItemExchangeRecipes()
    {
        // Do not permanently cache an empty world if queried before the quest store is loaded.
        static const std::vector<QuestItemExchangeRecipe> empty;
        if (sObjectMgr.GetQuestTemplates().empty())
            return empty;

        static const std::vector<QuestItemExchangeRecipe> recipes = BuildRecipes();
        return recipes;
    }
}
