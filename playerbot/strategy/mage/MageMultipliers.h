#pragma once
#include "playerbot/strategy/Multiplier.h"

namespace ai
{
    class MageAoeCommitMultiplier : public Multiplier
    {
    public:
        MageAoeCommitMultiplier(PlayerbotAI* ai, const std::string& strategyName) :
            Multiplier(ai, "mage aoe commitment"), strategyName(strategyName) {}
        float GetValue(Action* action) override;

    private:
        std::string strategyName;
    };
}
