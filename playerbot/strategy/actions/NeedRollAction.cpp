#include "playerbot/playerbot.h"
#include "NeedRollAction.h"
#include "NeedRollCommand.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/QuestItemExchange.h"

#include <stdexcept>

using namespace ai;

bool NeedRollAction::Execute(Event& event)
{
    Player* requester = event.getOwner();
    if (!requester || requester == bot || !bot->GetGroup() || requester->GetGroup() != bot->GetGroup())
        return false;

    if (requester->GetPlayerbotAI() && !requester->GetPlayerbotAI()->IsRealPlayer())
        return false;

    need_roll::ItemLink parsed;
    if (!need_roll::ParseItemLink(event.getParam(), parsed))
        return false;

    ItemQualifier itemQualifier;
    try
    {
        itemQualifier = ItemQualifier(parsed.qualifier, true);
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    catch (const std::out_of_range&)
    {
        return false;
    }

    if (!itemQualifier.GetProto())
        return false;

    const auto now = std::chrono::steady_clock::now();
    if (lastRequester == requester->GetObjectGuid() && lastItemQualifier == parsed.qualifier &&
        now - lastRollTime < std::chrono::seconds(10))
        return false;

    if (CalculateRollVote(itemQualifier) != ROLL_NEED && !NeedsQuestReward(itemQualifier))
        return false;

    // RollMaster collects one round for ten seconds; repeated announcements must not reroll.
    lastRequester = requester->GetObjectGuid();
    lastItemQualifier = parsed.qualifier;
    lastRollTime = now;

    WorldPacket packet(MSG_RANDOM_ROLL);
    packet << uint32(1) << uint32(100);
    bot->GetSession()->HandleRandomRollOpcode(packet);
    return true;
}

bool NeedRollAction::NeedsQuestReward(ItemQualifier& itemQualifier)
{
    if (sPlayerbotAIConfig.autoQuestItemExchange)
    {
        QuestItemExchangePlan exchange;
        return QuestItemExchange::FindBestPlan(ai, itemQualifier.GetId(), true, exchange);
    }

    const ItemPrototype* proto = itemQualifier.GetProto();
    if (!proto || proto->InventoryType != INVTYPE_NON_EQUIP || bot->CanUseItem(proto) != EQUIP_ERR_OK)
        return false;

    const ForceItemUsage forcedUsage = AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", proto->ItemId, ForceItemUsage::FORCE_USAGE_NONE);
    if (forcedUsage == ForceItemUsage::FORCE_USAGE_GREED || forcedUsage == ForceItemUsage::FORCE_USAGE_KEEP || forcedUsage == ForceItemUsage::FORCE_USAGE_BAG)
        return false;

    const std::set<uint32>& skippedItems = AI_VALUE(std::set<uint32>&, "skip loot list");
    if (skippedItems.find(proto->ItemId) != skippedItems.end())
        return false;

    const uint32 ownedCount = bot->GetItemCount(proto->ItemId, true);
    if (proto->MaxCount > 0 && ownedCount >= uint32(proto->MaxCount))
        return false;

    // Evaluate the exchanged equipment, independent of automatic quest syncing.
    for (const auto& [questId, questPtr] : sObjectMgr.GetQuestTemplates())
    {
        const Quest* quest = questPtr.get();
        if (!quest || !quest->IsActive())
            continue;

        bool needsToken = false;
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (quest->ReqItemId[i] == proto->ItemId && ownedCount < quest->ReqItemCount[i])
            {
                needsToken = true;
                break;
            }
        }
        if (!needsToken || (!quest->IsRepeatable() && bot->GetQuestRewardStatus(questId)))
            continue;

        const QuestStatus status = bot->GetQuestStatus(questId);
        const bool accepted = status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_COMPLETE;
        if (!accepted && !bot->CanTakeQuest(quest, false))
            continue;

        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
            if (IsRewardUpgrade(quest->RewChoiceItemId[i]))
                return true;

        for (uint8 i = 0; i < quest->GetRewItemsCount(); ++i)
            if (IsRewardUpgrade(quest->RewItemId[i]))
                return true;
    }

    return false;
}

bool NeedRollAction::IsRewardUpgrade(uint32 itemId)
{
    ItemQualifier reward(itemId);
    if (!reward.GetProto())
        return false;

    const ItemUsage usage = ItemUsageValue::QueryItemUsageForEquip(reward, bot);
    return usage == ItemUsage::ITEM_USAGE_EQUIP ||
        (usage == ItemUsage::ITEM_USAGE_BAD_EQUIP && (sPlayerbotAIConfig.rollBadItemsWithPlayer || !ai->HasRealPlayerMaster()));
}
