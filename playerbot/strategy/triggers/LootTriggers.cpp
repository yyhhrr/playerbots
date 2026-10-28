
#include "playerbot/playerbot.h"
#include "LootTriggers.h"
#include "playerbot/LootObjectStack.h"

#include "playerbot/ServerFacade.h"
using namespace ai;

bool LootAvailableTrigger::IsActive()
{
    LootObject currentLoot = AI_VALUE(LootObject, "loot target");
    return AI_VALUE(bool, "has available loot") &&
            (
                    currentLoot.IsEmpty() || currentLoot.IsWithinLootDistance(bot) ||
                    AI_VALUE(std::list<ObjectGuid>, "all targets").empty()
            ) &&
            !AI_VALUE2(bool, "combat", "self target") &&
            !AI_VALUE2(bool, "mounted", "self target");
}

bool FarFromCurrentLootTrigger::IsActive()
{
    LootObject loot = AI_VALUE(LootObject, "loot target");

    if (loot.IsEmpty() || AI_VALUE(LootObjectStack*, "available loot")->IsDeferred(loot.guid) ||
        AI_VALUE(LootObjectStack*, "available loot")->IsLootOpening(loot.guid) || !loot.IsLootPossible(bot))
        return false;

    return !loot.IsWithinLootDistance(bot);
}

bool CanLootTrigger::IsActive()
{
    return AI_VALUE(bool, "can loot");
}
