#pragma once

#include "playerbot/strategy/Value.h"
#include "playerbot/WarsongCombatProgressPolicy.h"

namespace ai
{
    class AlteracCombatProgressValue : public ManualSetValue<warsong::CombatProgress&>
    {
    public:
        AlteracCombatProgressValue(PlayerbotAI* ai) :
            ManualSetValue<warsong::CombatProgress&>(ai, data, "alterac combat progress") {}
        void Reset() override { data = {}; }
    private:
        warsong::CombatProgress data;
    };
}
