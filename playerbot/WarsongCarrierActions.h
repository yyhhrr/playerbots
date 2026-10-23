#pragma once

#include "playerbot/strategy/Action.h"

class PlayerbotAI;

namespace ai
{
namespace warsong
{
    struct CarrierAbilityState
    {
        bool mobilityReady = false;
        bool defenseReady = false;
    };

    CarrierAbilityState InspectCarrierAbilities(PlayerbotAI* ai);
}

    class WarsongCarrierMobilityAction : public Action
    {
    public:
        explicit WarsongCarrierMobilityAction(PlayerbotAI* ai) : Action(ai, "bg warsong mobility") {}
        bool isUseful() override;
        bool isUsefulWhenStunned() override { return true; }
        bool Execute(Event& event) override;

    private:
        uint32 lastFormChange = 0;
        bool changedForm = false;
    };
}
