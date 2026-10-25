
#include "playerbot/playerbot.h"
#include "InvalidTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "EnemyPlayerValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/DungeonMovement.h"

using namespace ai;

bool InvalidTargetValue::Calculate()
{
    Unit* target = ai->GetUnit(AI_VALUE(ObjectGuid, qualifier));
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
    {
        return true;
    }

    // A stale cached attackers list must not preserve a target on another floor.
    if (!dungeon::CanTarget(ai, target))
        return true;

    Unit* duelTarget = ai->GetUnit(AI_VALUE(ObjectGuid, "duel target"));
    if (duelTarget && duelTarget == target)
    {
        return false;
    }

    if (qualifier == "current target")
    {
        if (target->GetObjectGuid() != bot->GetSelectionGuid())
        {
            return true;
        }
    }

    // Do not let the fallback cached list keep a defender chasing outside its node.
    // The shared check includes hostile pets but leaves other BGs and ordinary NPCs alone.
    if (!EnemyPlayersValue::IsAllowedArathiDefenderTarget(target, bot) ||
        !EnemyPlayersValue::IsAllowedWarsongTarget(target, bot))
        return true;

    const bool validTarget = PossibleAttackTargetsValue::IsValid(target, bot);
    if (!validTarget)
    {
        std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        if (std::find(attackers.begin(), attackers.end(), target->GetObjectGuid()) != attackers.end())
        {
            return false;
        }
    }

    return !validTarget;
}
