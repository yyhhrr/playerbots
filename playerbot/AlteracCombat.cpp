#include "playerbot/playerbot.h"
#include "AlteracCombat.h"
#include "AlteracValleyTactics.h"
#include "AlteracCapture.h"
#include "WarsongCombatProgress.h"
#include "WarsongCombatProgressPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "BattleGround/BattleGround.h"
#include "BattleGround/BattleGroundAV.h"
#include "MotionGenerators/MotionMaster.h"
#include "MotionGenerators/MovementGenerator.h"
#include "Movement/MoveSpline.h"

namespace ai::alterac
{
using CombatProgress = warsong::CombatProgress;
using PursuitObservation = warsong::PursuitObservation;
namespace
{
    bool ActiveCombat(Player* bot) { return IsActive(bot) && bot->IsAlive(); }

    CombatProgress& Progress(PlayerbotAI* ai)
    {
        return ai->GetAiObjectContext()->GetValue<CombatProgress&>("alterac combat progress")->Get();
    }

    bool ValidEnemy(Player* bot, Unit* target)
    {
        return target && target->IsInWorld() && target->IsAlive() && target->GetMap() == bot->GetMap() &&
            bot->CanAttack(target) && PossibleTargetsValue::CanPerceive(target, bot);
    }

    void ReleaseTarget(PlayerbotAI* ai, Unit* target)
    {
        Player* bot = ai->GetBot();
        auto* context = ai->GetAiObjectContext();
        const ObjectGuid guid = target->GetObjectGuid();
        for (const char* name : {"current target", "attack target", "pull target", "old target"})
        {
            auto* value = context->GetValue<ObjectGuid>(name);
            if (value->Get() == guid) value->Set(ObjectGuid());
        }
        if (bot->GetVictim() == target) bot->AttackStop();
        if (bot->GetSelectionGuid() == guid)
        {
            bot->SetSelectionGuid(ObjectGuid());
            bot->SetTarget(nullptr);
        }
        // Native chase and the short point moves used by ChaseTo can both
        // report an accepted command without ever leaving the current ledge.
        ai->StopMoving();
        if (Pet* pet = bot->GetPet())
        {
            Unit* petTarget = pet->GetVictim();
            if (!petTarget && pet->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
                petTarget = pet->GetMotionMaster()->GetCurrent()->GetCurrentTarget();
            if (petTarget == target)
            {
                pet->AttackStop();
                pet->GetMotionMaster()->MoveFollow(bot, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
            }
        }
        for (CurrentSpellTypes slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
        {
            Spell* spell = bot->GetCurrentSpell(slot);
            if (spell && spell->CanBeInterrupted() && spell->m_targets.getUnitTarget() == target)
                bot->InterruptSpell(slot);
        }
        context->GetValue<std::list<ObjectGuid>>("attackers")->Reset();
        context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Reset();
        context->GetValue<std::list<ObjectGuid>>("enemy player targets")->Reset();
        context->GetValue<ObjectGuid>("enemy player target")->Reset();
        context->GetValue<bool>("has enemy player targets")->Reset();
    }
}

bool CanFightFromHere(PlayerbotAI* ai, Unit* target)
{
    return warsong::CanFightFromHere(ai, target);
}

bool IsCombatTargetSuppressed(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!ActiveCombat(bot) || !target || ai->IsRealPlayer()) return false;
    CombatProgress& state = Progress(ai);
    return state.active && state.instance == bot->GetInstanceId() &&
        state.IsSuppressed(target->GetObjectGuid().GetRawValue(), WorldTimer::getMSTime()) &&
        !CanFightFromHere(ai, target);
}

bool IsCapturingEnemy(Player* observer, Unit* target)
{
#ifdef MANGOSBOT_ZERO
    Player* enemy = dynamic_cast<Player*>(target);
    if (!IsActive(observer) || !enemy || enemy->GetTeam() == observer->GetTeam() ||
        !ValidEnemy(observer, enemy)) return false;
    Spell* spell = enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell || spell->getState() == SPELL_STATE_FINISHED || spell->m_spellInfo->Id != 21651)
        return false;
    GameObject* flag = spell->m_targets.getGOTarget();
    if (!flag || flag->GetMap() != observer->GetMap() || !sServerFacade.isSpawned(flag)) return false;
    const auto& event = observer->GetMap()->GetMapDataContainer().GetGameObjectEventIndex(flag->GetDbGuid());
    return event.event1 < BG_AV_MAX_NODES &&
        observer->GetBattleGround()->IsActiveEvent(event.event1, event.event2);
#else
    return false;
#endif
}

bool UpdateCombatProgress(PlayerbotAI* ai)
{
    if (!ai || !ai->GetBot()) return false;
    Player* bot = ai->GetBot();
    CombatProgress& state = Progress(ai);
    if (!ActiveCombat(bot) || ai->IsRealPlayer())
    {
        state = {};
        return false;
    }
    if (!state.active || state.instance != bot->GetInstanceId())
    {
        state = {};
        state.active = true;
        state.instance = bot->GetInstanceId();
    }
    // Capture casts and immobilization must not be mistaken for a failed chase.
    if (IsCapturing(bot) || !ai->CanMove() || ai->HasStrategy("stay", ai->GetState()))
    {
        state.ResetPursuit();
        state.sampled = false;
        return false;
    }

    const uint32 now = WorldTimer::getMSTime();
    if (state.sampled && uint32(now - state.sampledAt) < 250)
        return false;
    state.sampled = true;
    state.sampledAt = now;
    state.Prune(now);
    auto* context = ai->GetAiObjectContext();
    Unit* target = ai->GetUnit(context->GetValue<ObjectGuid>("current target")->Get());
    if (bot->GetVictim() != target && CanFightFromHere(ai, bot->GetVictim()))
    {
        state.ResetPursuit();
        return false;
    }
    if (!ValidEnemy(bot, target)) target = bot->GetVictim();
    if (!ValidEnemy(bot, target) && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        target = bot->GetMotionMaster()->GetCurrent()->GetCurrentTarget();
    if (!ValidEnemy(bot, target) || CanFightFromHere(ai, target))
    {
        state.ResetPursuit();
        return false;
    }
    if (state.IsSuppressed(target->GetObjectGuid().GetRawValue(), now))
    {
        state.ResetPursuit();
        return false;
    }

    PursuitObservation observation;
    observation.target = target->GetObjectGuid().GetRawValue();
    observation.x = bot->GetPositionX(); observation.y = bot->GetPositionY(); observation.z = bot->GetPositionZ();
    observation.distance = std::sqrt(bot->GetDistance(target->GetPositionX(), target->GetPositionY(),
        target->GetPositionZ(), DIST_CALC_NONE));
    if (bot->movespline->Initialized() && !bot->movespline->Finalized() &&
        !bot->movespline->isCyclic() && bot->movespline->Duration())
    {
        observation.spline = bot->movespline->GetId();
        observation.elapsed = bot->movespline->GetElapsedValue();
        const auto destination = bot->movespline->FinalDestination();
        const float remaining = std::sqrt(target->GetDistance(destination.x, destination.y,
            destination.z, DIST_CALC_NONE));
        observation.routeToTarget = remaining <= 8.0f || remaining + 1.0f < observation.distance;
    }
    if (!state.Observe(observation, now)) return false;

    state.Suppress(observation.target, now);
    ReleaseTarget(ai, target);
    return true;
}
}
