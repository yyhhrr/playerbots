#pragma once
#include "GenericActions.h"

namespace ai
{
    class DestroyItemAction : public ChatCommandAction
    {
    public:
        DestroyItemAction(PlayerbotAI* ai, std::string name = "destroy") : ChatCommandAction(ai, name) {}
        virtual bool Execute(Event& event) override;
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "destroy"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This command will make the bot destroy a certain item.\n"
                "Usage: destroy [itemlink]\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return { "force item usage" }; }
#endif

    protected:
        void DestroyItem(FindItemVisitor* visitor, Player* requester);
    };

    class ClearBagsAction : public ChatCommandAction
    {
    public:
        ClearBagsAction(PlayerbotAI* ai) : ChatCommandAction(ai, "clear bags") {}
        bool Execute(Event& event) override;
        bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        std::string GetHelpName() override { return "clear bags"; }
        std::string GetHelpDescription() override
        {
            return "Destroy every item in the backpacks of bots in your party or raid.\n"
                "Usage: clear bags (also accepts the Chinese command for clear bags).\n"
                "Preserves equipped gear, equipped bags, bank items and the keyring.\n"
                "Works in party/raid chat or by whispering a bot in your group.\n"
                "Cannot be used while trading, casting or using a bag item.\n";
        }
        std::vector<std::string> GetUsedActions() override { return {}; }
        std::vector<std::string> GetUsedValues() override
        {
            return { "bag space", "inventory items", "inventory item ids", "item count",
                "item usage", "item for spell", "stack space for item" };
        }
#endif
    };

    class SmartDestroyItemAction : public DestroyItemAction 
    {
    public:
        SmartDestroyItemAction(PlayerbotAI* ai) : DestroyItemAction(ai, "smart destroy item") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override { return !ai->HasActivePlayerMaster(); }
        virtual bool isUsefulWhenStunned() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "smart destroy item"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This command will make the bot destroy a certain item.\n"
                "Usage: destroy [itemlink]\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {"destroy"}; }
        virtual std::vector<std::string> GetUsedValues() { return { "bag space", "force item usage" }; }
#endif
    };

    class DestroyAllGrayItemsAction : public DestroyItemAction
    {
    public:
        DestroyAllGrayItemsAction(PlayerbotAI* ai) : DestroyItemAction(ai, "destroy all gray") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override { return true; }

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "destroy all gray"; } //Must equal iternal name
        virtual std::string GetHelpDescription()
        {
            return "This command will make the bot destroy all gray items.\n"
                "Usage: destroy all gray\n";
        }
        virtual std::vector<std::string> GetUsedActions() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif
    };
}
