#pragma once

#include "LootRollAction.h"
#include <chrono>

namespace ai
{
    class NeedRollAction : public RollAction
    {
    public:
        NeedRollAction(PlayerbotAI* ai) : RollAction(ai, "need roll") { SetDuration(0); }

        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() override { return "need roll"; }
        virtual std::string GetHelpDescription() override
        {
            return "Roll 1-100 for an item requested by a player in the same party or raid, only when the bot needs it.\n"
                "Supports equipment upgrades and quest tokens redeemable for equipment upgrades.\n";
        }
        virtual std::vector<std::string> GetUsedActions() override { return {}; }
        virtual std::vector<std::string> GetUsedValues() override { return { "item usage", "force item usage", "skip loot list" }; }
#endif

    private:
        bool NeedsQuestReward(ItemQualifier& itemQualifier);
        bool IsRewardUpgrade(uint32 itemId);

        ObjectGuid lastRequester;
        std::string lastItemQualifier;
        std::chrono::steady_clock::time_point lastRollTime{};
    };
}
