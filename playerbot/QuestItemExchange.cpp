#include "playerbot/playerbot.h"
#include "QuestItemExchange.h"
#include "QuestItemExchangePolicy.h"
#include "QuestItemExchangeRecipes.h"
#include "RandomItemMgr.h"
#include "RandomPlayerbotMgr.h"
#include "strategy/values/ItemUsageValue.h"

#include <algorithm>
#include <array>
#include <functional>
#include <unordered_map>

using namespace ai;

namespace
{
    using RecipeIndex = std::unordered_map<uint32, std::vector<const QuestItemExchangeRecipe*>>;

    const RecipeIndex& RecipesByKey()
    {
        static const RecipeIndex empty;
        const auto& recipes = GetQuestItemExchangeRecipes();
        if (recipes.empty())
            return empty;
        static const RecipeIndex index = []
        {
            RecipeIndex result;
            for (const auto& recipe : GetQuestItemExchangeRecipes())
                for (const auto& key : recipe.keys)
                    result[key.itemId].push_back(&recipe);
            return result;
        }();
        return index;
    }

    enum class KeyMode { Loot, Redeem, Keep, Manual, ManualKeep };

    ForceItemUsage ForcedUsage(PlayerbotAI* ai, uint32 itemId)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        return AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", itemId, ForceItemUsage::FORCE_USAGE_NONE);
    }

    bool ProtectWorn(PlayerbotAI* ai, uint32 itemId)
    {
        const ForceItemUsage forced = ForcedUsage(ai, itemId);
        return forced == ForceItemUsage::FORCE_USAGE_EQUIP || forced == ForceItemUsage::FORCE_USAGE_BAG;
    }

    bool ProtectKey(PlayerbotAI* ai, uint32 itemId)
    {
        const ForceItemUsage forced = ForcedUsage(ai, itemId);
        return ProtectWorn(ai, itemId) || forced == ForceItemUsage::FORCE_USAGE_KEEP ||
            forced == ForceItemUsage::FORCE_USAGE_GREED;
    }

    uint64 ItemWeight(Player* bot, uint32 spec, ItemQualifier& item)
    {
        ItemSpecType itemSpec;
        uint64 weight = sRandomItemMgr.CalculateStatWeight(bot->getClass(), spec, item.GetProto(), itemSpec);
        if (item.GetEnchantId())
            weight += sRandomItemMgr.CalculateEnchantWeight(bot->getClass(), spec, item.GetEnchantId());
        if (item.GetRandomPropertyId())
            weight += sRandomItemMgr.CalculateRandomPropertyWeight(bot->getClass(), spec, item.GetRandomPropertyId());
#ifndef MANGOSBOT_ZERO
        for (uint32 gem : { item.GetGem1(), item.GetGem2(), item.GetGem3(), item.GetGem4() })
            if (gem)
                weight += sRandomItemMgr.CalculateGemWeight(bot->getClass(), spec, gem);
        weight += sRandomItemMgr.CalculateSocketWeight(bot->getClass(), item, spec);
#endif
        return weight;
    }

    struct SlotState
    {
        const ItemPrototype* proto = nullptr;
        uint64 weight = 0;
        bool protectedItem = false;
        bool reward = false;
    };
    using Loadout = std::array<SlotState, EQUIPMENT_SLOT_END>;

    uint64 TotalWeight(const Loadout& items)
    {
        uint64 weight = 0;
        for (const auto& slot : items)
            weight += slot.weight;
        return weight;
    }

    Loadout CurrentLoadout(PlayerbotAI* ai, uint32 spec)
    {
        Player* bot = ai->GetBot();
        Loadout result;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                ItemQualifier qualifier(item);
                result[slot] = { item->GetProto(), ItemWeight(bot, spec, qualifier), ProtectWorn(ai, item->GetEntry()), false };
            }
        }
        return result;
    }

    bool AvailableItem(Player* bot, Item* item)
    {
        return item && item->GetOwnerGuid() == bot->GetObjectGuid() && !item->IsInTrade() &&
            item->GetSlot() != NULL_SLOT && bot->GetItemByPos(item->GetBagSlot(), item->GetSlot()) == item;
    }

    bool RemoveConsumedKeys(PlayerbotAI* ai, const QuestItemExchangeRecipe& recipe, KeyMode mode, Loadout& loadout)
    {
        Player* bot = ai->GetBot();
        // Match the transaction reservation order: carried bags/keyring, then worn slots.
        const std::vector<Item*> carried = ai->GetInventoryItems();
        for (const auto& key : recipe.keys)
        {
            uint32 remaining = key.count;
            for (Item* item : carried)
                if (remaining && AvailableItem(bot, item) && item->GetEntry() == key.itemId)
                    remaining -= std::min(remaining, item->GetCount());
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END && remaining; ++slot)
            {
                Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!AvailableItem(bot, item) || item->GetEntry() != key.itemId)
                    continue;
                const uint32 consumed = std::min(remaining, item->GetCount());
                remaining -= consumed;
                if (consumed == item->GetCount())
                    loadout[slot] = {};
            }
            if (remaining && (mode == KeyMode::Redeem || mode == KeyMode::Manual))
                return false;
            // Loot/keep checks can anticipate missing keys arriving in bags later.
        }
        return true;
    }

    struct RewardOption
    {
        QuestItemExchangeItem item;
        const ItemPrototype* proto = nullptr;
        uint64 weight = 0;
        uint32 copies = 1;
        std::vector<uint8> slots;
    };

    bool IsTemporaryEquipFailure(InventoryResult result)
    {
        // Need/keep evaluation must work during combat and when bags are full.
        // The transaction repeats the real checks immediately before equipping.
        return result == EQUIP_ERR_NOT_IN_COMBAT || result == EQUIP_ERR_YOU_ARE_STUNNED ||
            result == EQUIP_ERR_YOU_ARE_DEAD || result == EQUIP_ERR_CANT_DO_RIGHT_NOW ||
            result == EQUIP_ERR_INVENTORY_FULL || result == EQUIP_ERR_ITEMS_CANT_BE_SWAPPED ||
            result == EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED;
    }

    bool MakeRewardOption(PlayerbotAI* ai, uint32 spec, const QuestItemExchangeItem& reward,
        const std::set<uint32>& skipped, RewardOption& result)
    {
        Player* bot = ai->GetBot();
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(reward.itemId);
        const ForceItemUsage forced = ForcedUsage(ai, reward.itemId);
        if (!proto || !reward.count || proto->InventoryType == INVTYPE_NON_EQUIP || proto->GetMaxStackSize() != 1 ||
            skipped.count(reward.itemId) || forced == ForceItemUsage::FORCE_USAGE_BAG ||
            forced == ForceItemUsage::FORCE_USAGE_GREED || bot->CanUseItem(proto) != EQUIP_ERR_OK)
            return false;
        if ((proto->Class == ITEM_CLASS_WEAPON && !sRandomItemMgr.ShouldEquipWeaponForSpec(bot->getClass(), spec, proto)) ||
            (proto->Class == ITEM_CLASS_ARMOR && !sRandomItemMgr.ShouldEquipArmorForSpec(bot->getClass(), spec, proto)))
            return false;

        uint32 wornCopies = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (item->GetEntry() == reward.itemId)
                    wornCopies += item->GetCount();
        const uint32 ownedCopies = bot->GetItemCount(reward.itemId, true);
        if (ownedCopies > wornCopies)
            return false; // Equip an existing bag/bank copy before creating another one.

        uint8 viable[4] = { NULL_SLOT, NULL_SLOT, NULL_SLOT, NULL_SLOT };
        if (!bot->ViableEquipSlots(proto, viable))
            return false;
        Item* temporary = RandomPlayerbotMgr::CreateTempItem(reward.itemId, 1, bot);
        if (!temporary)
            return false;
        const bool canUse = bot->CanUseItem(temporary, false) == EQUIP_ERR_OK;
        if (canUse)
        {
            for (uint8 slot : viable)
            {
                if (slot >= EQUIPMENT_SLOT_END ||
                    (proto->InventoryType == INVTYPE_2HWEAPON && slot != EQUIPMENT_SLOT_MAINHAND))
                    continue;
                uint16 destination = 0;
                // direct_action=true permits the core's normal two-hand/offhand swap checks.
                const InventoryResult check = bot->CanEquipItem(slot, destination, temporary, true);
                if (check == EQUIP_ERR_OK || IsTemporaryEquipFailure(check))
                    result.slots.push_back(slot);
            }
        }
        temporary->RemoveFromUpdateQueueOf(bot);
        if (!bot->GetItemUpdateQueue().empty() && !bot->GetItemUpdateQueue().back())
            bot->GetItemUpdateQueue().pop_back();
        delete temporary;
        if (result.slots.empty())
            return false;

        result.item = { reward.itemId, 1, NULL_SLOT };
        result.proto = proto;
        ItemQualifier qualifier(reward.itemId);
        result.weight = ItemWeight(bot, spec, qualifier);
        result.copies = uint32(result.slots.size());
        if (proto->MaxCount > 0)
            result.copies = std::min(result.copies, uint32(proto->MaxCount));
        if (proto->Flags & ITEM_FLAG_UNIQUE_EQUIPPABLE)
            result.copies = 1;
        return result.weight > 0 && ownedCopies < result.copies;
    }

    bool PlaceReward(const RewardOption& option, uint8 slot, Loadout& loadout)
    {
        if (loadout[slot].reward || loadout[slot].protectedItem)
            return false;
        if (slot == EQUIPMENT_SLOT_OFFHAND && loadout[EQUIPMENT_SLOT_MAINHAND].proto &&
            loadout[EQUIPMENT_SLOT_MAINHAND].proto->InventoryType == INVTYPE_2HWEAPON)
            return false;
        if (option.proto->InventoryType == INVTYPE_2HWEAPON)
        {
            if (loadout[EQUIPMENT_SLOT_OFFHAND].reward || loadout[EQUIPMENT_SLOT_OFFHAND].protectedItem)
                return false;
            loadout[EQUIPMENT_SLOT_OFFHAND] = {};
        }
        if (option.proto->Flags & ITEM_FLAG_UNIQUE_EQUIPPABLE)
            for (uint8 other = EQUIPMENT_SLOT_START; other < EQUIPMENT_SLOT_END; ++other)
                if (other != slot && loadout[other].proto && loadout[other].proto->ItemId == option.item.itemId)
                    return false;
        loadout[slot] = { option.proto, option.weight, false, true };
        return true;
    }

    unsigned EquipOrder(const RewardOption& option)
    {
        if (std::find(option.slots.begin(), option.slots.end(), EQUIPMENT_SLOT_MAINHAND) != option.slots.end())
            return 0;
        if (std::find(option.slots.begin(), option.slots.end(), EQUIPMENT_SLOT_OFFHAND) != option.slots.end())
            return 1;
        return 2;
    }

    bool EvaluateRecipe(PlayerbotAI* ai, uint32 spec, const QuestItemExchangeRecipe& recipe, KeyMode mode,
        const Loadout& original, const std::set<uint32>& skipped, QuestItemExchangePlan& plan,
        quest_exchange_policy::Candidate& rank)
    {
        Loadout afterKeys = original;
        if (!RemoveConsumedKeys(ai, recipe, mode, afterKeys))
            return false;
        const uint64 beforeWeight = TotalWeight(original);
        std::vector<RewardOption> fixed;
        for (const auto& reward : recipe.fixedRewards)
        {
            RewardOption option;
            if (MakeRewardOption(ai, spec, reward, skipped, option))
                for (uint32 i = 0; i < std::min(reward.count, option.copies); ++i)
                    fixed.push_back(option);
        }
        std::vector<std::vector<RewardOption>> choices(1); // A useful fixed reward does not require an off-spec choice.
        for (const auto& reward : recipe.choiceRewards)
        {
            RewardOption option;
            if (!MakeRewardOption(ai, spec, reward, skipped, option))
                continue;
            choices.emplace_back(std::min(reward.count, option.copies), option);
        }

        for (const auto& choice : choices)
        {
            std::vector<RewardOption> options = fixed;
            options.insert(options.end(), choice.begin(), choice.end());
            std::stable_sort(options.begin(), options.end(), [](const RewardOption& left, const RewardOption& right)
            {
                return EquipOrder(left) < EquipOrder(right);
            });
            std::vector<QuestItemExchangeItem> selected;
            std::function<void(std::size_t, const Loadout&)> search = [&](std::size_t next, const Loadout& loadout)
            {
                if (next == options.size())
                {
                    const uint64 afterWeight = TotalWeight(loadout);
                    if (selected.empty() || afterWeight <= beforeWeight)
                        return;
                    quest_exchange_policy::Candidate value;
                    value.questId = recipe.questId;
                    value.scoreGain = afterWeight - beforeWeight;
                    value.usable = value.keysAvailable = true;
                    for (const auto& reward : selected)
                    {
                        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(reward.itemId);
                        value.quality = std::max(value.quality, proto->Quality);
                        value.itemLevel = std::max(value.itemLevel, proto->ItemLevel);
                        value.rewardId = value.rewardId ? std::min(value.rewardId, reward.itemId) : reward.itemId;
                    }
                    bool better = !rank.usable || value.scoreGain > rank.scoreGain;
                    if (rank.usable && value.scoreGain == rank.scoreGain)
                    {
                        // Equal final value should not create an unnecessary non-upgrade reward.
                        better = selected.size() < plan.rewards.size() ||
                            (selected.size() == plan.rewards.size() && quest_exchange_policy::IsBetter(value, rank));
                    }
                    if (better)
                    {
                        rank = value;
                        plan.questId = recipe.questId;
                        plan.keys = recipe.keys;
                        plan.rewards = selected;
                        plan.scoreGain = value.scoreGain;
                    }
                    return;
                }
                search(next + 1, loadout); // Ignore fixed rewards which do not improve the final setup.
                const RewardOption& option = options[next];
                uint32 createdCopies = 0;
                for (const auto& reward : selected)
                    if (reward.itemId == option.item.itemId)
                        ++createdCopies;
                if (ai->GetBot()->GetItemCount(option.item.itemId, true) + createdCopies >= option.copies)
                    return;
                for (uint8 slot : option.slots)
                {
                    Loadout updated = loadout;
                    if (!PlaceReward(option, slot, updated))
                        continue;
                    selected.push_back({ option.item.itemId, 1, slot });
                    search(next + 1, updated);
                    selected.pop_back();
                }
            };
            search(0, afterKeys);
        }
        return rank.usable && !plan.rewards.empty();
    }

    bool FindPlan(PlayerbotAI* ai, uint32 keyItemId, KeyMode mode, QuestItemExchangePlan& plan)
    {
        plan = {};
        const bool manual = mode == KeyMode::Manual || mode == KeyMode::ManualKeep;
        if (!ai || (!manual && !sPlayerbotAIConfig.autoQuestItemExchange) || ai->IsRealPlayer())
            return false;
        const auto& index = RecipesByKey();
        const auto candidates = index.find(keyItemId);
        if (candidates == index.end())
            return false;
        Player* bot = ai->GetBot();
        AiObjectContext* context = ai->GetAiObjectContext();
        const std::set<uint32>& skipped = AI_VALUE(std::set<uint32>&, "skip loot list");
        if (skipped.count(keyItemId))
            return false;
        uint32 spec = sRandomItemMgr.GetPlayerSpecId(bot);
        if (bot->getClass() == CLASS_DRUID && (spec == 30 || spec == 32))
            spec = PlayerbotAI::IsTank(bot) ? 30 : 32;
        const Loadout original = CurrentLoadout(ai, spec);
        quest_exchange_policy::Candidate best;
        for (const auto* recipe : candidates->second)
        {
            if (!manual && !IsAutomaticQuestItemExchangeRecipe(*recipe))
                continue;
            if ((recipe->requiredClasses && !(recipe->requiredClasses & bot->getClassMask())) ||
                (recipe->requiredRaces && !(recipe->requiredRaces & bot->getRaceMask())))
                continue;
            bool allowed = true;
            for (const auto& key : recipe->keys)
            {
                if (ProtectKey(ai, key.itemId) || skipped.count(key.itemId) ||
                    (mode == KeyMode::Loot && key.itemId == keyItemId && bot->GetItemCount(key.itemId, true) >= key.count))
                {
                    allowed = false;
                    break;
                }
            }
            if (!allowed)
                continue;
            QuestItemExchangePlan current;
            quest_exchange_policy::Candidate rank;
            if (EvaluateRecipe(ai, spec, *recipe, mode, original, skipped, current, rank) &&
                quest_exchange_policy::IsBetter(rank, best))
            {
                plan = std::move(current);
                best = rank;
            }
        }
        return best.usable && !plan.rewards.empty();
    }
}

bool QuestItemExchange::IsKeyItem(uint32 itemId)
{
    if (!sPlayerbotAIConfig.autoQuestItemExchange || IsWearableKey(itemId) ||
        !IsQuestExchangeKeyPrototype(sObjectMgr.GetItemPrototype(itemId)))
        return false;
    const auto& index = RecipesByKey();
    const auto recipes = index.find(itemId);
    return recipes != index.end() && std::any_of(recipes->second.begin(), recipes->second.end(), [](const QuestItemExchangeRecipe* recipe)
    {
        return IsAutomaticQuestItemExchangeRecipe(*recipe);
    });
}

bool QuestItemExchange::IsWearableKey(uint32 itemId)
{
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
    return proto && proto->InventoryType != INVTYPE_NON_EQUIP;
}

bool QuestItemExchange::FindBestPlan(PlayerbotAI* ai, uint32 keyItemId, bool forLoot, QuestItemExchangePlan& plan)
{
    return FindPlan(ai, keyItemId, forLoot ? KeyMode::Loot : KeyMode::Redeem, plan);
}

bool QuestItemExchange::FindManualPlan(PlayerbotAI* ai, uint32 keyItemId, QuestItemExchangePlan& plan)
{
    return FindPlan(ai, keyItemId, KeyMode::Manual, plan);
}

bool QuestItemExchange::ShouldKeepKey(PlayerbotAI* ai, uint32 keyItemId)
{
    QuestItemExchangePlan plan;
    return FindPlan(ai, keyItemId, KeyMode::Keep, plan);
}

bool QuestItemExchange::ShouldKeepForManualExchange(PlayerbotAI* ai, uint32 keyItemId)
{
    QuestItemExchangePlan plan;
    return FindPlan(ai, keyItemId, KeyMode::ManualKeep, plan);
}
