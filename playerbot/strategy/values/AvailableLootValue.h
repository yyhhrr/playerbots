#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/LootObjectStack.h"
#include "playerbot/ServerFacade.h"

namespace ai
{

    class AvailableLootValue : public ManualSetValue<LootObjectStack*>
	{
	public:
        AvailableLootValue(PlayerbotAI* ai, std::string name = "available loot") : ManualSetValue<LootObjectStack*>(ai, NULL, name)
        {
            value = new LootObjectStack(bot);
        }

        virtual ~AvailableLootValue() override
        {
            if (value)
                delete value;
        }
    };

    class LootTargetValue : public ManualSetValue<LootObject>
    {
    public:
        LootTargetValue(PlayerbotAI* ai, std::string name = "loot target") : ManualSetValue<LootObject>(ai, LootObject(), name) {}
    };

    class CanLootValue : public BoolCalculatedValue
    {
    public:
        CanLootValue(PlayerbotAI* ai, std::string name = "can loot") : BoolCalculatedValue(ai, name) {}

        virtual bool Calculate() override
        {
            LootObject loot = AI_VALUE(LootObject, "loot target");

            return !loot.IsEmpty() &&
                    !AI_VALUE(LootObjectStack*, "available loot")->IsDeferred(loot.guid) &&
                    !AI_VALUE(LootObjectStack*, "available loot")->IsLootOpening(loot.guid) &&
                    loot.IsLootPossible(bot) &&
                    loot.IsWithinLootDistance(bot);
        }
    };
}
