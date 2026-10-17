#pragma once

#include "GenericActions.h"

namespace ai
{
    struct QuestItemExchangePlan;

    class AutoQuestItemExchangeAction : public Action
    {
    public:
        AutoQuestItemExchangeAction(PlayerbotAI* ai) : Action(ai, "auto quest item exchange", 0) {}

        virtual bool Execute(Event& event) override;
        virtual bool isPossible() override;

        // Manual commands explicitly opt in to consuming equipment keys.
        // The manual path ignores the automatic-exchange setting.
        bool Redeem(const QuestItemExchangePlan& plan, bool allowEquipment = false);

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() override { return "auto quest item exchange"; }
        virtual std::string GetHelpDescription() override
        {
            return "Automatically exchange carried quest tokens for usable equipment upgrades, then equip the rewards.\n"
                "Wearable equipment keys require a manual exchange command.\n"
                "Only the key items are consumed; quest progress and other materials are unchanged.\n";
        }
        virtual std::vector<std::string> GetUsedActions() override { return {}; }
        virtual std::vector<std::string> GetUsedValues() override { return { "inventory items", "item usage" }; }
#endif

    };
}
