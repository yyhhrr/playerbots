#pragma once
#include "playerbot/strategy/Action.h"

class Pet;

namespace ai
{
    // Sets the default autocast for newly learned pet abilities.
    class PetAutocastAction : public Action
    {
    public:
        PetAutocastAction(PlayerbotAI* ai) : Action(ai, "pet autocast") {}
        virtual bool Execute(Event& event) override;
        virtual bool isUseful() override;
        static bool HasNewSpells(PlayerbotAI* ai, const Pet* pet);
        static bool ApplyDefaults(PlayerbotAI* ai, Pet* pet, uint32 manualSpell = 0);
    };
}
