
#include "playerbot/playerbot.h"
#include "DestroyItemAction.h"
#include "playerbot/PlayerbotSecurity.h"
#include "playerbot/strategy/values/ItemCountValue.h"

using namespace ai;

bool DestroyItemAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    ItemIds ids = chat->parseItems(text);

    for (ItemIds::iterator i =ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        DestroyItem(&visitor, requester);
    }

    return true;
}

void DestroyItemAction::DestroyItem(FindItemVisitor* visitor, Player* requester)
{
    ai->InventoryIterateItems(visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    std::list<Item*> items = visitor->GetResult();
	for (std::list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
    {
		Item* item = *i;
        std::ostringstream out; out << chat->formatItem(item) << " destroyed";
        bot->DestroyItem(item->GetBagSlot(),item->GetSlot(), true);
        ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    }
}

bool SmartDestroyItemAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    uint8 bagSpace = AI_VALUE(uint8, "bag space");

    if (bagSpace < 90)
        return false;

    bool onlyDestroyGray = false;

    if (ai->HasRealPlayerMaster() || ai->IsInRealGuild())
        onlyDestroyGray = true;

    if(sPlayerbotAIConfig.IsFreeAltBot(bot) && !ai->HasActivePlayerMaster())
        onlyDestroyGray = false;

    // only destroy grey items if with real player/guild
    if (onlyDestroyGray)
    {
        std::set<Item*> items;
        FindItemsToTradeByQualityVisitor visitor(ITEM_QUALITY_POOR, 5);
        ai->InventoryIterateItems(&visitor, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        items.insert(visitor.GetResult().begin(), visitor.GetResult().end());

        for (auto& item : items)
        {
            if (HAS_AI_VALUE2("force item usage", item->GetProto()->ItemId))
                continue;

            FindItemByIdVisitor visitor(item->GetProto()->ItemId);
            DestroyItem(&visitor, requester);

            bagSpace = AI_VALUE(uint8, "bag space");

            if (bagSpace < 90)
                return true;
        }

        return true;
    }

    std::vector<ItemUsage> bestToDestroy = { ItemUsage::ITEM_USAGE_NONE }; //First destroy anything useless.

    if (!AI_VALUE(bool, "can sell") && AI_VALUE(bool, "should get money")) //We need money so quest items are less important since they can't directly be sold.
    {
        bestToDestroy.push_back(ItemUsage::ITEM_USAGE_QUEST);
    }
    else //We don't need money so destroy the cheapest stuff.
    {
        bestToDestroy.push_back(ItemUsage::ITEM_USAGE_VENDOR);
        bestToDestroy.push_back(ItemUsage::ITEM_USAGE_BROKEN_AH);
        bestToDestroy.push_back(ItemUsage::ITEM_USAGE_AH);
    }

    //If we still need room 
    bestToDestroy.push_back(ItemUsage::ITEM_USAGE_SKILL); //Items that might help tradeskill are more important than above but still expendable.
    bestToDestroy.push_back(ItemUsage::ITEM_USAGE_USE); //These are more likely to be useful 'soon' but still expendable.

    for (auto& usage : bestToDestroy)
    {
        std::list<uint32> items = AI_VALUE2(std::list<uint32>, "inventory item ids", "usage " + std::to_string((uint8)usage));

        items.reverse();

        for (auto& item : items)
        {
            if (HAS_AI_VALUE2("force item usage", item))
                continue;

            FindItemByIdVisitor visitor(item);
            DestroyItem(&visitor, requester);

            bagSpace = AI_VALUE(uint8, "bag space");

            if(bagSpace < 90)
                return true;
        }
    }

    return false;
}

bool DestroyAllGrayItemsAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    bool hasDestroyedAnyItems = false;

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    if (const ItemPrototype* proto = pItem->GetProto())
                    {
                        if (proto->Quality == ITEM_QUALITY_POOR)
                        {
                            std::ostringstream out; out << ai->GetChatHelper()->formatItem(pItem->GetProto()) << " destroyed";
                            sLog.outDetail("%s via DestroyAllGrayItemsAction", out.str().c_str());
                            bot->DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
                            ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

                            hasDestroyedAnyItems = true;
                        }
                    }
                }
            }
        }
    }

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (const ItemPrototype* proto = pItem->GetProto())
            {
                if (proto->Quality == ITEM_QUALITY_POOR)
                {
                    std::ostringstream out; out << ai->GetChatHelper()->formatItem(pItem->GetProto()) << " destroyed";
                    sLog.outDetail("%s via DestroyAllGrayItemsAction", out.str().c_str());
                    bot->DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
                    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

                    hasDestroyedAnyItems = true;
                }
            }
        }
    }

    return hasDestroyedAnyItems;
}

bool ClearBagsAction::Execute(Event& event)
{
    Player* requester = event.getOwner();
    // Recheck membership when executing, including commands queued before leaving a group.
    if (!requester || requester == bot || ai->IsRealPlayer() || !bot->GetGroup() ||
        requester->GetGroup() != bot->GetGroup() ||
        (requester->GetPlayerbotAI() && !requester->GetPlayerbotAI()->IsRealPlayer()) ||
        !ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, requester))
        return false;

    if (!event.getParam().empty())
    {
        // Usage: clear bags / 清空背包, without extra arguments.
        ai->TellPlayerNoFacing(requester, "\xE7\x94\xA8\xE6\xB3\x95\xEF\xBC\x9A\xE6\xB8\x85\xE7\xA9\xBA\xE8\x83\x8C\xE5\x8C\x85 (clear bags)",
            PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false, true);
        return false;
    }

    if (bot->GetTradeData())
    {
        // Finish trading before clearing the bags.
        ai->TellPlayerNoFacing(requester, "\xE6\xAD\xA3\xE5\x9C\xA8\xE4\xBA\xA4\xE6\x98\x93\xEF\xBC\x8C\xE8\xAF\xB7\xE7\xBB\x93\xE6\x9D\x9F\xE4\xBA\xA4\xE6\x98\x93\xE5\x90\x8E\xE9\x87\x8D\xE8\xAF\x95\xE6\xB8\x85\xE7\xA9\xBA\xE8\x83\x8C\xE5\x8C\x85\xE3\x80\x82",
            PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false, true);
        return false;
    }

    // Spell may retain pointers to both a cast item and an item being enchanted.
    const char* busyMessage = "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x96\xBD\xE6\xB3\x95\xE6\x88\x96\xE4\xBD\xBF\xE7\x94\xA8\xE7\x89\xA9\xE5\x93\x81\xEF\xBC\x8C\xE8\xAF\xB7\xE7\xBB\x93\xE6\x9D\x9F\xE5\x90\x8E\xE9\x87\x8D\xE8\xAF\x95\xE6\xB8\x85\xE7\xA9\xBA\xE8\x83\x8C\xE5\x8C\x85\xE3\x80\x82";
    if (bot->IsNonMeleeSpellCasted(true))
    {
        ai->TellPlayerNoFacing(requester, busyMessage, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL,
            true, false, true);
        return false;
    }

    bool itemInUse = false;
    std::vector<std::pair<uint8, uint8>> positions;
    auto collectAt = [&](uint8 bag, uint8 slot)
    {
        if (Item* item = bot->GetItemByPos(bag, slot))
        {
            itemInUse = itemInUse || item->IsUsedInSpell();
            positions.emplace_back(bag, slot);
        }
    };

    // The generic inventory visitor also includes the keyring. Use only bag contents here.
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        collectAt(INVENTORY_SLOT_BAG_0, slot);

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* container = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                collectAt(bag, static_cast<uint8>(slot));

    // Preflight every bag before deleting anything, including items held by delayed spells.
    if (itemInUse)
    {
        ai->TellPlayerNoFacing(requester, busyMessage, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL,
            true, false, true);
        return false;
    }

    uint32 stacksDestroyed = 0;
    uint64 itemsDestroyed = 0;
    for (const auto& [bag, slot] : positions)
        if (Item* item = bot->GetItemByPos(bag, slot))
        {
            itemsDestroyed += item->GetCount();
            ++stacksDestroyed;
            // Native removal updates quest objectives and the normal inventory save queue.
            bot->DestroyItem(bag, slot, true);
        }

    for (const char* name : { "bag space", "inventory items", "inventory item ids", "item count",
        "item usage", "item for spell", "stack space for item" })
        context->ClearValues(name);

    // Bags cleared: N stacks (M items). Reply once per bot, privately to the requester.
    const std::string message = "\xE8\x83\x8C\xE5\x8C\x85\xE5\xB7\xB2\xE6\xB8\x85\xE7\xA9\xBA\xEF\xBC\x8C\xE5\x85\xB1\xE9\x94\x80\xE6\xAF\x81 " +
        std::to_string(stacksDestroyed) + " \xE7\xBB\x84\xE7\x89\xA9\xE5\x93\x81\xEF\xBC\x88" +
        std::to_string(itemsDestroyed) + " \xE4\xBB\xB6\xEF\xBC\x89\xE3\x80\x82";
    ai->TellPlayerNoFacing(requester, message, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL,
        true, false, true);
    return true;
}
