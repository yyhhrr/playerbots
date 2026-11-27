#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/actions/PetAutocastAction.h"

namespace ai
{
    class NewPetTrigger : public Trigger
    {
    public:
        NewPetTrigger(PlayerbotAI* ai) : Trigger(ai, "new pet", 2) {}

        virtual bool IsActive() override
        {
            return PetAutocastAction::HasNewSpells(ai, bot->GetPet());
        }
    };
}
