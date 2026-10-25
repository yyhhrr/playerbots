#include "playerbot/playerbot.h"
#include "TargetPerception.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "MotionGenerators/MotionMaster.h"
#include "MotionGenerators/MovementGenerator.h"

#include <set>

bool ai::perception::CanUseTarget(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    return bot && (!target || target == bot || sServerFacade.IsFriendlyTo(bot, target) ||
        PossibleTargetsValue::CanPerceive(target, bot));
}

bool ai::perception::ReleaseLostTargets(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || !bot->IsAlive() || !bot->IsInWorld() || bot->IsBeingTeleported())
        return false;

    auto* context = ai->GetAiObjectContext();
    std::set<ObjectGuid> lost;
    bool released = false;
    const auto unseen = [&](Unit* target)
    {
        return target && !CanUseTarget(ai, target);
    };
    for (const char* name : { "current target", "attack target", "pull target", "old target" })
    {
        auto* value = context->GetValue<ObjectGuid>(name);
        const ObjectGuid guid = value->Get();
        Unit* target = guid ? ai->GetUnit(guid) : nullptr;
        if (guid && (!target || unseen(target)))
        {
            lost.insert(guid);
            value->Set(ObjectGuid());
            released = true;
        }
    }

    if (Unit* victim = bot->GetVictim())
        if (unseen(victim))
        {
            lost.insert(victim->GetObjectGuid());
            bot->AttackStop();
            released = true;
        }

    MotionMaster* motion = bot->GetMotionMaster();
    Unit* chaseTarget = motion->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE ?
        motion->GetCurrent()->GetCurrentTarget() : nullptr;
    if (unseen(chaseTarget))
    {
        lost.insert(chaseTarget->GetObjectGuid());
        ai->StopMoving();
        released = true;
    }

    // Do not disturb self/ground effects or already released projectiles. Only
    // pending targeted casts, channels and auto-repeat attacks need a live target.
    for (CurrentSpellTypes slot : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL })
    {
        Spell* spell = bot->GetCurrentSpell(slot);
        if (spell && spell->CanBeInterrupted() && unseen(spell->m_targets.getUnitTarget()))
        {
            lost.insert(spell->m_targets.getUnitTarget()->GetObjectGuid());
            bot->InterruptSpell(slot);
            released = true;
        }
    }

    if (Pet* pet = bot->GetPet())
    {
        Unit* target = pet->GetVictim();
        if (!target && pet->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            target = pet->GetMotionMaster()->GetCurrent()->GetCurrentTarget();
        // Pet detection still follows the pet's own native senses. An owner's
        // old target cannot order a new attack through the normal command gates.
        if (target && !sServerFacade.IsFriendlyTo(bot, target) &&
            !PossibleTargetsValue::CanPerceive(target, pet, pet))
        {
            pet->AttackStop();
            pet->GetMotionMaster()->MoveFollow(bot, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
            released = true;
        }
    }

    if (lost.count(bot->GetSelectionGuid()))
    {
        bot->SetSelectionGuid(ObjectGuid());
        bot->SetTarget(nullptr);
    }
    if (released)
    {
        context->GetValue<std::list<ObjectGuid>>("attackers")->Reset();
        context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Reset();
        context->GetValue<std::list<ObjectGuid>>("enemy player targets")->Reset();
        context->GetValue<ObjectGuid>("enemy player target")->Reset();
        context->GetValue<bool>("has enemy player targets")->Reset();
    }
    return released;
}
