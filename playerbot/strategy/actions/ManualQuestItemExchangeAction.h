#pragma once

#include "playerbot/strategy/Action.h"

namespace ai
{
    class ManualQuestItemExchangeAction : public Action
    {
    public:
        ManualQuestItemExchangeAction(PlayerbotAI* ai) : Action(ai, "quest item exchange", 0) {}
        virtual bool Execute(Event& event) override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() override { return "quest item exchange"; }
        virtual std::string GetHelpDescription() override
        {
            return "Manually exchange one linked key or old equipment item for a usable equipment upgrade.\n"
                "Use the Chinese exchange command or exchange item followed by one complete item link.\n"
                "All required keys must be carried or equipped. Their quantities are listed before consumption.\n"
                "Respects forced item protection and works when automatic exchange is disabled.\n";
        }
        virtual std::vector<std::string> GetUsedActions() override { return {}; }
        virtual std::vector<std::string> GetUsedValues() override { return { "force item usage", "skip loot list" }; }
#endif
    };
}
