#include "playerbot/playerbot.h"
#include "DungeonSafeStrategy.h"
#include "MotionGenerators/MotionMaster.h"
#include "MotionGenerators/MovementGenerator.h"
#include "Movement/MoveSpline.h"
#include "playerbot/strategy/values/LastMovementValue.h"

using namespace ai;

void DungeonSafeStrategy::OnStrategyAdded(BotState state)
{
    ResetMovement(state);
}

void DungeonSafeStrategy::OnStrategyRemoved(BotState state)
{
    ResetMovement(state);
}

void DungeonSafeStrategy::ResetMovement(BotState state)
{
    if (state != BotState::BOT_STATE_COMBAT && state != BotState::BOT_STATE_NON_COMBAT)
        return;

    LastMovement& lastMove = ai->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get();
    // Default strategies are constructed before PlayerbotAI's current state is
    // initialized. There cannot yet be an owned movement spline at that point.
    if (!lastMove.dungeonSplineId || state != ai->GetState())
        return;

    Player* bot = ai->GetBot();
    if (!bot->movespline->Finalized() &&
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == PATH_MOTION_TYPE &&
        bot->movespline->GetId() == lastMove.dungeonSplineId)
        ai->StopMoving();

    lastMove.dungeonGoal = WorldPosition();
    lastMove.dungeonSplineId = 0;
    lastMove.dungeonRepathTime = 0;
    lastMove.dungeonPurpose = 0;
    lastMove.dungeonWalking = false;
    lastMove.dungeonHazards.clear();
}
