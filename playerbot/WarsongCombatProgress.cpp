#include "playerbot/playerbot.h"
#include "WarsongCombatProgress.h"
#include "WarsongCombatProgressPolicy.h"
#include "WarsongTactics.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "BattleGround/BattleGround.h"
#include "MotionGenerators/MotionMaster.h"
#include "MotionGenerators/MovementGenerator.h"
#include "Movement/MoveSpline.h"

namespace ai::warsong
{
namespace
{
    bool ActiveCombat(Player* bot)
    {
        return bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsBeingTeleported() &&
            bot->GetMapId() == 489 && bot->GetBattleGround() &&
            bot->GetBattleGround()->GetStatus() == STATUS_IN_PROGRESS;
    }

    CombatProgress& Progress(PlayerbotAI* ai)
    {
        return ai->GetAiObjectContext()->GetValue<CombatProgress&>("warsong combat progress")->Get();
    }

    bool ValidEnemy(Player* bot, Unit* target)
    {
        return target && target->IsInWorld() && target->IsAlive() && target->GetMap() == bot->GetMap() &&
            bot->CanAttack(target) && PossibleTargetsValue::CanPerceive(target, bot);
    }

    bool RangedAttack(PlayerbotAI* ai, Unit* target, const char* spell)
    {
        float maximum = 0, minimum = 0;
        if (!ai->GetSpellRange(spell, &maximum, &minimum) || maximum <= 0)
            return false;
        const float distance = ai->GetBot()->GetDistance(target, true, DIST_CALC_COMBAT_REACH);
        return distance <= maximum && distance >= minimum &&
            ai->CanCastSpell(spell, target, 0, nullptr, false, false, true);
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
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || !ValidEnemy(bot, target) || !bot->IsWithinLOSInMap(target))
        return false;
    if (bot->CanReachWithMeleeAttack(target))
        return true;
    for (CurrentSpellTypes slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
    {
        Spell* spell = bot->GetCurrentSpell(slot);
        if (spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_targets.getUnitTarget() == target)
        {
            float maximum = 0, minimum = 0;
            const float distance = bot->GetDistance(target, true, DIST_CALC_COMBAT_REACH);
            if (ai->GetSpellRange(spell->m_spellInfo->SpellName[0], &maximum, &minimum) &&
                maximum > 0 && distance <= maximum && distance >= minimum)
                return true;
        }
    }
    if (!ai->IsRanged(bot))
        return false;
    // Use learned, damaging attacks with their actual minimum/maximum range.
    // A configured generic range must not authorize casting through a ledge.
    switch (bot->getClass())
    {
    case CLASS_MAGE:
        return RangedAttack(ai, target, "frostbolt") || RangedAttack(ai, target, "fireball") ||
            RangedAttack(ai, target, "arcane missiles") || RangedAttack(ai, target, "scorch");
    case CLASS_WARLOCK:
        return RangedAttack(ai, target, "shadow bolt") || RangedAttack(ai, target, "immolate") ||
            RangedAttack(ai, target, "drain life") || RangedAttack(ai, target, "shoot");
    case CLASS_PRIEST:
        return RangedAttack(ai, target, "smite") || RangedAttack(ai, target, "mind blast") ||
            RangedAttack(ai, target, "mind flay") || RangedAttack(ai, target, "shoot");
    case CLASS_SHAMAN:
        return RangedAttack(ai, target, "lightning bolt") || RangedAttack(ai, target, "chain lightning");
    case CLASS_DRUID:
        return RangedAttack(ai, target, "wrath") || RangedAttack(ai, target, "starfire");
    case CLASS_HUNTER:
        return RangedAttack(ai, target, "auto shot") || RangedAttack(ai, target, "arcane shot");
    default:
        return false;
    }
}

bool IsCombatTargetSuppressed(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!ActiveCombat(bot) || !target || ai->IsRealPlayer() || IsCarrier(bot))
        return false;
    CombatProgress& state = Progress(ai);
    return state.active && state.instance == bot->GetInstanceId() &&
        state.IsSuppressed(target->GetObjectGuid().GetRawValue(), WorldTimer::getMSTime()) &&
        !CanFightFromHere(ai, target);
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
    const int descent = ai->GetAiObjectContext()->GetValue<int>("manual int", "warsong graveyard spline")->Get();
    if (descent && !bot->movespline->Finalized() && bot->movespline->GetId() == uint32(descent) &&
        bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
    {
        state.ResetPursuit();
        state.sampled = false;
        return false;
    }
    // Immobilization is not a failed route, and the dedicated flag-carrier
    // actions own movement even while a nearby opponent remains selected.
    if (IsCarrier(bot) || !ai->CanMove() || ai->HasStrategy("stay", ai->GetState()))
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
