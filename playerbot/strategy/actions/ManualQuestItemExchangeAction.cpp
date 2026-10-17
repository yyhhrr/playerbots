#include "playerbot/playerbot.h"
#include "ManualQuestItemExchangeAction.h"
#include "AutoQuestItemExchangeAction.h"
#include "NeedRollCommand.h"
#include "playerbot/QuestItemExchange.h"
#include "playerbot/PlayerbotSecurity.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/ItemUsageValue.h"

#include <stdexcept>

using namespace ai;

namespace
{
    // Keep Chinese replies as UTF-8 bytes regardless of the MSVC source code page.
    void Reply(PlayerbotAI* ai, Player* requester, const std::string& message)
    {
        ai->TellPlayerNoFacing(requester, message, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL,
            true, false, true);
    }

    void ReportItems(PlayerbotAI* ai, Player* requester, const char* label,
        const std::vector<QuestItemExchangeItem>& items)
    {
        // One item per message keeps full links readable even for multi-item exchanges.
        for (const auto& item : items)
        {
            const ItemPrototype* proto = sObjectMgr.GetItemPrototype(item.itemId);
            const std::string link = proto ? ChatHelper::formatItem(proto) : std::to_string(item.itemId);
            Reply(ai, requester, std::string(label) + link + " x" + std::to_string(item.count));
        }
    }
}

bool ManualQuestItemExchangeAction::Execute(Event& event)
{
    Player* requester = event.getOwner();
    if (!requester || requester == bot || ai->IsRealPlayer() ||
        (requester->GetPlayerbotAI() && !requester->GetPlayerbotAI()->IsRealPlayer()) ||
        !ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, requester))
        return false;

    need_roll::ItemLink parsed;
    if (!need_roll::ParseItemLink(event.getParam(), parsed))
    {
        // Usage: exchange followed by one complete item link.
        Reply(ai, requester, "\xe7\x94\xa8\xe6\xb3\x95\xef\xbc\x9a\xe5\x85\x91\xe6\x8d\xa2\x20\x5b\xe7\x89\xa9\xe5\x93\x81\xe9\x93\xbe\xe6\x8e\xa5\x5d\xef\xbc\x8c\xe8\xaf\xb7\xe8\xb4\xb4\xe5\x87\xba\xe4\xb8\x80\xe4\xb8\xaa\xe5\xae\x8c\xe6\x95\xb4\xe7\x89\xa9\xe5\x93\x81\xe9\x93\xbe\xe6\x8e\xa5\xe3\x80\x82");
        return false;
    }

    ItemQualifier key;
    try
    {
        key = ItemQualifier(parsed.qualifier, true);
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    catch (const std::out_of_range&)
    {
        return false;
    }
    if (!key.GetProto())
        return false;

    if (!sServerFacade.IsAlive(bot) || bot->IsInCombat() || bot->GetTradeData())
    {
        Reply(ai, requester, "\xe5\xbd\x93\xe5\x89\x8d\xe6\x97\xa0\xe6\xb3\x95\xe5\x85\x91\xe6\x8d\xa2\xef\xbc\x9a\xe8\xaf\xb7\xe5\x9c\xa8\xe5\xad\x98\xe6\xb4\xbb\xe3\x80\x81\xe8\x84\xb1\xe6\x88\x98\xe4\xb8\x94\xe6\x9c\xaa\xe4\xba\xa4\xe6\x98\x93\xe6\x97\xb6\xe9\x87\x8d\xe8\xaf\x95\xe3\x80\x82");
        return false;
    }
    if (!bot->GetItemCount(key.GetId(), false))
    {
        Reply(ai, requester, "\xe6\x9c\xaa\xe6\x8c\x81\xe6\x9c\x89\xe8\xaf\xa5\xe5\x85\x91\xe6\x8d\xa2\xe4\xbb\xb6\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88\xe6\x94\xbe\xe5\x85\xa5\xe8\x83\x8c\xe5\x8c\x85\xef\xbc\x9b\xe5\xb7\xb2\xe7\xa9\xbf\xe6\x88\xb4\xe7\x9a\x84\xe5\x85\x91\xe6\x8d\xa2\xe4\xbb\xb6\xe4\xb9\x9f\xe5\x8f\xaf\xe4\xbb\xa5\xe4\xbd\xbf\xe7\x94\xa8\xe3\x80\x82");
        return false;
    }

    QuestItemExchangePlan plan;
    if (!QuestItemExchange::FindManualPlan(ai, key.GetId(), plan))
    {
        Reply(ai, requester, "\xe6\x97\xa0\xe6\xb3\x95\xe5\x85\x91\xe6\x8d\xa2\xef\xbc\x9a\xe9\x9c\x80\xe5\x87\x91\xe9\xbd\x90\xe5\x85\xa8\xe9\x83\xa8\xe5\x85\xb3\xe9\x94\xae\xe4\xbb\xb6\xef\xbc\x8c\xe5\xa5\x96\xe5\x8a\xb1\xe5\x8f\xaf\xe8\xa3\x85\xe5\xa4\x87\xe4\xb8\x94\xe6\x9c\x89\xe6\x8f\x90\xe5\x8d\x87\xef\xbc\x8c\xe5\x85\xb3\xe9\x94\xae\xe4\xbb\xb6\xe6\x9c\xaa\xe8\xa2\xab\xe8\xae\xbe\xe7\xbd\xae\xe4\xb8\xba\xe4\xbf\x9d\xe7\x95\x99\xe3\x80\x82");
        return false;
    }

    ReportItems(ai, requester, "\xe6\x9c\xac\xe6\xac\xa1\xe5\xb0\x86\xe6\xb6\x88\xe8\x80\x97\xef\xbc\x9a", plan.keys);
    AutoQuestItemExchangeAction exchange(ai);
    if (!exchange.Redeem(plan, true))
    {
        Reply(ai, requester, "\xe5\x85\x91\xe6\x8d\xa2\xe6\x9c\xaa\xe5\xae\x8c\xe6\x88\x90\xef\xbc\x8c\xe5\x85\xb3\xe9\x94\xae\xe4\xbb\xb6\xe5\xb7\xb2\xe4\xbf\x9d\xe7\x95\x99\xe3\x80\x82\xe8\xaf\xb7\xe6\xa3\x80\xe6\x9f\xa5\xe8\x83\x8c\xe5\x8c\x85\xe7\xa9\xba\xe9\x97\xb4\xe4\xb8\x8e\xe8\xa3\x85\xe5\xa4\x87\xe6\x9d\xa1\xe4\xbb\xb6\xe3\x80\x82");
        return false;
    }
    ReportItems(ai, requester, "\xe5\x85\x91\xe6\x8d\xa2\xe6\x88\x90\xe5\x8a\x9f\xef\xbc\x8c\xe5\xb7\xb2\xe8\xa3\x85\xe5\xa4\x87\xef\xbc\x9a", plan.rewards);
    return true;
}
