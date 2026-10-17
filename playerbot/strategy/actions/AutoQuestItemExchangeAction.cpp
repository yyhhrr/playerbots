#include "playerbot/playerbot.h"
#include "AutoQuestItemExchangeAction.h"
#include "playerbot/QuestItemExchange.h"
#include "playerbot/ServerFacade.h"

#include <algorithm>
#include <limits>

using namespace ai;

namespace
{
    bool CanRedeemNow(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        return !ai->IsRealPlayer() && sServerFacade.IsAlive(bot) && !bot->IsInCombat() && !bot->GetTradeData();
    }

    struct ReservedKey
    {
        ObjectGuid guid;
        uint32 itemId;
        uint32 count;
    };

    struct CreatedReward
    {
        ObjectGuid guid;
        uint8 slot;
    };

    uint8 EquipOrder(uint8 slot)
    {
        if (slot == EQUIPMENT_SLOT_MAINHAND)
            return 0;
        if (slot == EQUIPMENT_SLOT_OFFHAND)
            return 1;
        return 2;
    }

    bool EquipAtSlot(Player* bot, ObjectGuid guid, uint8 slot)
    {
        Item* item = bot->GetItemByGuid(guid);
        if (!item || item->GetOwnerGuid() != bot->GetObjectGuid() || item->IsInTrade() || slot >= EQUIPMENT_SLOT_END)
            return false;
        if (item->GetBagSlot() == INVENTORY_SLOT_BAG_0 && item->GetSlot() == slot)
            return true;

        uint16 dest;
        if (bot->CanEquipItem(slot, dest, item, true) != EQUIP_ERR_OK ||
            dest != (uint16(INVENTORY_SLOT_BAG_0) << 8 | slot) || bot->CanUnequipItem(dest, true) != EQUIP_ERR_OK)
            return false;

        // A replaced item must fit back into the reward's current inventory slot.
        if (Item* old = bot->GetItemByPos(dest))
        {
            ItemPosCountVec reverseDest;
            uint8 bagSlot = 0;
            if (!Player::IsInventoryPos(item->GetPos()) ||
                bot->CanStoreItem(item->GetBagSlot(), item->GetSlot(), reverseDest, old, bagSlot, true) != EQUIP_ERR_OK)
                return false;
        }

        WorldPacket packet(CMSG_AUTOEQUIP_ITEM_SLOT);
        packet << guid << slot;
        bot->GetSession()->HandleAutoEquipItemSlotOpcode(packet);
        Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        return equipped && equipped->GetObjectGuid() == guid;
    }

    void RestoreEquipment(Player* bot, const std::vector<ObjectGuid>& originalEquipment)
    {
        // Restore the main hand first, so an old offhand can follow a two-hand swap.
        for (uint8 order = 0; order < 3; ++order)
            for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
                if (EquipOrder(slot) == order && originalEquipment[slot])
                    EquipAtSlot(bot, originalEquipment[slot], slot);
    }

    bool IsAvailableKey(Player* bot, Item* item)
    {
        if (!item || item->GetOwnerGuid() != bot->GetObjectGuid() || item->IsInTrade() || item->GetSlot() == NULL_SLOT)
            return false;

        const uint8 bag = item->GetBagSlot();
        const uint8 slot = item->GetSlot();
        const bool equipped = bag == INVENTORY_SLOT_BAG_0 && slot < EQUIPMENT_SLOT_END;
        return (equipped || Player::IsInventoryPos(bag, slot)) && bot->GetItemByPos(bag, slot) == item;
    }

    std::vector<Item*> CarriedItems(PlayerbotAI* ai)
    {
        std::vector<Item*> items = ai->GetInventoryItems();
        Player* bot = ai->GetBot();
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                items.push_back(item);
        return items;
    }

    bool ReserveKeys(PlayerbotAI* ai, const QuestItemExchangePlan& plan, std::vector<ReservedKey>& reserved)
    {
        std::map<uint32, uint32> missing;
        for (const auto& key : plan.keys)
        {
            if (!key.itemId || !key.count || key.count > std::numeric_limits<uint32>::max() - missing[key.itemId])
                return false;
            missing[key.itemId] += key.count;
        }
        if (missing.empty())
            return false;

        Player* bot = ai->GetBot();
        std::set<ObjectGuid> seen;
        for (Item* item : CarriedItems(ai))
        {
            if (!IsAvailableKey(bot, item) || !seen.insert(item->GetObjectGuid()).second)
                continue;

            auto need = missing.find(item->GetEntry());
            if (need == missing.end() || !need->second)
                continue;

            const uint32 count = std::min(need->second, item->GetCount());
            reserved.push_back({ item->GetObjectGuid(), item->GetEntry(), count });
            need->second -= count;
        }

        for (const auto& [itemId, count] : missing)
            if (count)
                return false;
        return true;
    }

    bool KeysStillAvailable(Player* bot, const std::vector<ReservedKey>& reserved)
    {
        std::set<ObjectGuid> seen;
        for (const auto& key : reserved)
        {
            Item* item = bot->GetItemByGuid(key.guid);
            if (!seen.insert(key.guid).second || !IsAvailableKey(bot, item) ||
                item->GetEntry() != key.itemId || item->GetCount() < key.count)
                return false;
        }
        return true;
    }

    bool UnblockConsumedTwoHandKey(Player* bot, const std::vector<ReservedKey>& reserved,
        const std::vector<CreatedReward>& rewards)
    {
        bool equipsMainHand = false;
        bool equipsOffHand = false;
        for (const auto& reward : rewards)
        {
            equipsMainHand = equipsMainHand || reward.slot == EQUIPMENT_SLOT_MAINHAND;
            equipsOffHand = equipsOffHand || reward.slot == EQUIPMENT_SLOT_OFFHAND;
        }
        // A new main hand is equipped first and naturally unblocks its own offhand.
        if (!equipsOffHand || equipsMainHand)
            return true;

        Item* mainHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        if (!mainHand || mainHand->GetProto()->InventoryType != INVTYPE_2HWEAPON)
            return true;

        const ObjectGuid guid = mainHand->GetObjectGuid();
        const auto key = std::find_if(reserved.begin(), reserved.end(), [guid](const ReservedKey& candidate)
        {
            return candidate.guid == guid;
        });
        if (key == reserved.end() || key->count != mainHand->GetCount() || !IsAvailableKey(bot, mainHand) ||
            bot->CanUnequipItem(mainHand->GetPos(), false) != EQUIP_ERR_OK)
            return false;

        ItemPosCountVec dest;
        uint8 bagSlot = 0;
        if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, mainHand, bagSlot, false) != EQUIP_ERR_OK ||
            dest.size() != 1 || dest.front().count != mainHand->GetCount())
            return false;

        // Preserve the original object until every reward has been equipped successfully.
        const uint16 destination = dest.front().pos;
        bot->SwapItem(mainHand->GetPos(), destination);
        mainHand = bot->GetItemByGuid(guid);
        return IsAvailableKey(bot, mainHand) && Player::IsInventoryPos(mainHand->GetPos()) &&
            mainHand->GetPos() == destination;
    }

    void RollBackRewards(Player* bot, const std::vector<CreatedReward>& rewards)
    {
        for (const auto& reward : rewards)
        {
            Item* item = bot->GetItemByGuid(reward.guid);
            if (!item || item->GetOwnerGuid() != bot->GetObjectGuid())
                continue;

            uint32 count = item->GetCount();
            bot->DestroyItemCount(*item, count, true);
            // DestroyItemCount can immediately delete a newly created item.
        }
    }
}

bool AutoQuestItemExchangeAction::isPossible()
{
    return sPlayerbotAIConfig.autoQuestItemExchange && CanRedeemNow(ai);
}

bool AutoQuestItemExchangeAction::Execute(Event& event)
{
    if (!isPossible())
        return false;

    bool exchanged = false;
    for (uint8 transaction = 0; transaction < 8; ++transaction)
    {
        std::set<uint32> keyIds;
        for (Item* item : CarriedItems(ai))
            if (IsAvailableKey(bot, item) && item->GetProto()->InventoryType == INVTYPE_NON_EQUIP &&
                QuestItemExchange::IsKeyItem(item->GetEntry()))
                keyIds.insert(item->GetEntry());

        bool changed = false;
        for (uint32 itemId : keyIds)
        {
            QuestItemExchangePlan plan;
            if (!QuestItemExchange::FindBestPlan(ai, itemId, false, plan) || !Redeem(plan))
                continue;

            exchanged = true;
            changed = true;
            break; // Rebuild the inventory and evaluate upgrades after each exchange.
        }

        if (!changed)
            break;
    }
    return exchanged;
}

bool AutoQuestItemExchangeAction::Redeem(const QuestItemExchangePlan& plan, bool allowEquipment)
{
    if ((!allowEquipment && !sPlayerbotAIConfig.autoQuestItemExchange) || !CanRedeemNow(ai))
        return false;

    // Guard every key: a token recipe must never consume a wearable companion key automatically.
    for (const auto& key : plan.keys)
    {
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(key.itemId);
        if (!proto || (!allowEquipment && proto->InventoryType != INVTYPE_NON_EQUIP))
            return false;
    }

    std::vector<ReservedKey> reserved;
    if (plan.rewards.empty() || !ReserveKeys(ai, plan, reserved))
        return false;

    std::map<uint32, uint32> baselineCounts;
    std::map<uint32, uint32> expectedCounts;
    std::set<uint8> rewardSlots;
    for (const auto& reward : plan.rewards)
    {
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(reward.itemId);
        if (!proto || reward.count != 1 || reward.slot >= EQUIPMENT_SLOT_END || !rewardSlots.insert(reward.slot).second ||
            proto->InventoryType == INVTYPE_NON_EQUIP || proto->GetMaxStackSize() != 1 ||
            bot->CanUseItem(proto) != EQUIP_ERR_OK ||
            reward.count > std::numeric_limits<uint32>::max() - expectedCounts[reward.itemId])
            return false;

        for (const auto& key : plan.keys)
            if (key.itemId == reward.itemId)
                return false;
        baselineCounts[reward.itemId] = bot->GetItemCount(reward.itemId, true);
        expectedCounts[reward.itemId] += reward.count;
    }

    std::vector<ObjectGuid> originalEquipment(EQUIPMENT_SLOT_END);
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            originalEquipment[slot] = item->GetObjectGuid();

    std::vector<CreatedReward> created;
    const auto rollBack = [&]()
    {
        RestoreEquipment(bot, originalEquipment);
        RollBackRewards(bot, created);
        // Deleting newly equipped rewards can unblock restoration of a former two-hand setup.
        RestoreEquipment(bot, originalEquipment);
        context->ClearValues("inventory items");
        context->ClearValues("item usage");
    };
    for (const auto& reward : plan.rewards)
    {
        for (uint32 i = 0; i < reward.count; ++i)
        {
            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, reward.itemId, 1) != EQUIP_ERR_OK ||
                dest.size() != 1 || dest.front().count != 1)
            {
                rollBack();
                return false;
            }

            Item* item = bot->StoreNewItem(dest, reward.itemId, true, Item::GenerateItemRandomPropertyId(reward.itemId));
            if (!item)
            {
                rollBack();
                return false;
            }
            created.push_back({ item->GetObjectGuid(), reward.slot });
        }
    }

    bool valid = KeysStillAvailable(bot, reserved);
    for (const auto& [itemId, count] : expectedCounts)
        valid = valid && uint64(bot->GetItemCount(itemId, true)) == uint64(baselineCounts[itemId]) + count;
    if (!valid)
    {
        rollBack();
        return false;
    }

    std::stable_sort(created.begin(), created.end(), [](const CreatedReward& left, const CreatedReward& right)
    {
        return EquipOrder(left.slot) < EquipOrder(right.slot);
    });
    if (!UnblockConsumedTwoHandKey(bot, reserved, created))
    {
        rollBack();
        return false;
    }
    for (const auto& reward : created)
    {
        if (!EquipAtSlot(bot, reward.guid, reward.slot))
        {
            rollBack();
            return false;
        }
    }
    if (!KeysStillAvailable(bot, reserved))
    {
        rollBack();
        return false;
    }

    // Every reward is equipped at its planned slot before any key is consumed.
    for (const auto& key : reserved)
    {
        Item* item = bot->GetItemByGuid(key.guid);
        uint32 count = key.count;
        bot->DestroyItemCount(*item, count, true);
        // No access to item after destruction: a new item can be deleted immediately.
    }

    context->ClearValues("inventory items");
    context->ClearValues("item usage");
    for (const auto& reward : created)
    {
        if (Item* item = bot->GetItemByGuid(reward.guid))
        {
            bot->SendNewItem(item, 1, true, false);
        }
    }
    context->ClearValues("inventory items");
    context->ClearValues("item usage");

    sLog.outDetail("Bot #%u <%s> exchanged quest item keys for equipment from quest %u", bot->GetGUIDLow(), bot->GetName(), plan.questId);
    return true;
}
