#pragma once

#include "playerbot/strategy/Value.h"
#include "playerbot/WarsongCombatProgressPolicy.h"

namespace ai
{
    class WarsongCombatProgressValue : public ManualSetValue<warsong::CombatProgress&>
    {
    public:
        WarsongCombatProgressValue(PlayerbotAI* ai) :
            ManualSetValue<warsong::CombatProgress&>(ai, data, "warsong combat progress") {}

        void Reset() override { data = {}; }

    private:
        warsong::CombatProgress data;
    };
}
