#pragma once

#include "playerbot/strategy/Strategy.h"

namespace ai
{
    class DungeonSafeStrategy : public Strategy
    {
    public:
        DungeonSafeStrategy(PlayerbotAI* ai) : Strategy(ai) {}
        std::string getName() override { return "dungeon safe"; }
        void OnStrategyAdded(BotState state) override;
        void OnStrategyRemoved(BotState state) override;

#ifdef GenerateBotHelp
        std::string GetHelpName() override { return "dungeon safe"; }
        std::string GetHelpDescription() override
        {
            return "Keep dungeon movement, combat positioning, targeting and looting on safe walkable floors. "
                "Enable or disable independently with co +dungeon safe, co -dungeon safe, "
                "nc +dungeon safe and nc -dungeon safe.";
        }
        std::vector<std::string> GetRelatedStrategies() override { return { "dungeon", "follow" }; }
#endif

    private:
        void ResetMovement(BotState state);
    };
}
