#include "playerbot/playerbot.h"
#include "AlteracCapture.h"
#include "AlteracCapturePolicy.h"
#include "AlteracValleyTactics.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "playerbot/strategy/values/LastSpellCastValue.h"
#include "BattleGround/BattleGroundAV.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

namespace ai::alterac
{
#ifdef MANGOSBOT_ZERO
namespace
{
    void DelayCapture(PlayerbotAI* ai)
    {
        auto* context = ai->GetAiObjectContext();
        context->GetValue<int32>("manual int", "alterac capture threat time")
            ->Set(int32(WorldTimer::getMSTime()));
        context->GetValue<bool>("manual bool", "alterac capture retry")->Set(true);
    }

    bool CanAttemptCapture(PlayerbotAI* ai)
    {
        if (GetImmediateCaptureThreat(ai))
        {
            DelayCapture(ai);
            return false;
        }
        auto* context = ai->GetAiObjectContext();
        const uint32 lastThreat = uint32(context->GetValue<int32>("manual int", "alterac capture threat time")->Get());
        auto* retry = context->GetValue<bool>("manual bool", "alterac capture retry");
        if (IsCaptureRetryHeld(retry->Get(), lastThreat, WorldTimer::getMSTime()))
            return false;
        retry->Set(false);
        return true;
    }

    bool IsNodeFlag(GameObject* flag)
    {
        switch (flag->GetEntry())
        {
        case BG_AV_GO_BANNER_ALLIANCE:
        case BG_AV_GO_BANNER_ALLIANCE_CONT:
        case BG_AV_GO_BANNER_HORDE:
        case BG_AV_GO_BANNER_HORDE_CONT:
        case BG_AV_GO_GY_BANNER_ALLIANCE:
        case BG_AV_GO_GY_BANNER_ALLIANCE_CONT:
        case BG_AV_GO_GY_BANNER_HORDE:
        case BG_AV_GO_GY_BANNER_HORDE_CONT:
        case BG_AV_GO_GY_BANNER_SNOWFALL:
            return true;
        default:
            return false;
        }
    }

    bool EligibleCapturer(PlayerbotAI* ai, GameObject* flag)
    {
        Player* bot = ai ? ai->GetBot() : nullptr;
        return bot && !ai->IsRealPlayer() && bot->IsAlive() && bot->IsInWorld() &&
            !bot->IsBeingTeleported() && ai->CanMove() && bot->CanUseBattleGroundObject() &&
            !bot->IsNonMeleeSpellCasted(false) && bot->CanInteract(flag) &&
            bot->IsWithinDistInMap(flag, INTERACTION_DISTANCE) &&
            sServerFacade.IsWithinLOSInMap(bot, flag) && CanUseNodeFlag(ai, flag) && CanAttemptCapture(ai);
    }

    bool ElectedForFlag(PlayerbotAI* ai, GameObject* flag)
    {
        Player* bot = ai->GetBot();
        std::vector<CaptureCandidate> candidates;
        for (const auto& ref : bot->GetMap()->GetPlayers())
        {
            Player* member = ref.getSource();
            if (!member || !member->IsInWorld() || !member->IsAlive() || member->GetMap() != bot->GetMap() ||
                member->GetTeam() != bot->GetTeam() || member->IsBeingTeleported())
                continue;
            CaptureCandidate candidate{member->GetObjectGuid().GetRawValue(), member->GetDistance(flag)};
            if (IsCapturing(member))
                candidate.capturingSameFlag = member->GetCurrentSpell(CURRENT_GENERIC_SPELL)
                    ->m_targets.getGOTargetGuid() == flag->GetObjectGuid();
            candidate.eligible = EligibleCapturer(member->GetPlayerbotAI(), flag);
            candidates.push_back(candidate);
        }
        return ElectCapturer(candidates) == bot->GetObjectGuid().GetRawValue();
    }

    void ClearCaptureRecord(PlayerbotAI* ai)
    {
        auto* context = ai->GetAiObjectContext();
        context->GetValue<int32>("manual int", "alterac capture health")->Set(0);
        LastSpellCast& cast = context->GetValue<LastSpellCast&>("last spell cast")->Get();
        if (cast.id == CaptureSpellId) cast.Reset();
    }
}

bool IsCapturing(Player* player)
{
    Spell* spell = player ? player->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
    return spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_spellInfo->Id == CaptureSpellId;
}

Unit* GetImmediateCaptureThreat(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!IsActive(bot) || !bot->IsAlive()) return nullptr;
    Unit* nearest = nullptr;
    float nearestDistance = CaptureThreatRadius + 1.0f;
    const auto consider = [&](Unit* enemy)
    {
        if (!enemy || enemy == bot || !enemy->IsInWorld() || !enemy->IsAlive() ||
            enemy->GetMap() != bot->GetMap() || !bot->CanAttack(enemy)) return;
        const float distance = bot->GetDistance(enemy);
        if (distance > nearestDistance || distance > CaptureThreatRadius) return;
        bool castingAtBot = false;
        for (CurrentSpellTypes slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
        {
            Spell* spell = enemy->GetCurrentSpell(slot);
            if (spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_targets.getUnitTarget() == bot)
            {
                castingAtBot = true;
                break;
            }
        }
        const CaptureThreat threat = {distance, true, PossibleTargetsValue::CanPerceive(enemy, bot),
            sServerFacade.IsWithinLOSInMap(bot, enemy),
            enemy->HasAuraType(SPELL_AURA_MOD_STUN) || enemy->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
                enemy->HasAuraType(SPELL_AURA_MOD_FEAR),
            enemy->GetVictim() == bot, castingAtBot};
        if (IsImmediateCaptureThreat(threat)) { nearest = enemy; nearestDistance = distance; }
    };

    // Live scans include NPC guards and ranged casts before an attacker list or
    // strategic target cache has caught up. Native perception still controls detection.
    for (Unit* attacker : bot->getAttackers()) consider(attacker);
    std::list<Unit*> nearby;
    MaNGOS::AnyUnitInObjectRangeCheck check(bot, CaptureThreatRadius);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(nearby, check);
    Cell::VisitAllObjects(bot, searcher, CaptureThreatRadius);
    for (Unit* enemy : nearby) consider(enemy);
    return nearest;
}

bool UpdateCaptureResponse(PlayerbotAI* ai)
{
    if (!ai || !ai->GetBot()) return false;
    Player* bot = ai->GetBot();
    auto* context = ai->GetAiObjectContext();
    auto* health = context->GetValue<int32>("manual int", "alterac capture health");
    if (!IsActive(bot) || !bot->IsAlive() || ai->IsRealPlayer())
    {
        if (health->Get()) ClearCaptureRecord(ai);
        context->GetValue<bool>("manual bool", "alterac capture retry")->Set(false);
        return false;
    }
    const uint32 previousHealth = uint32(health->Get());
    const bool capturing = IsCapturing(bot);
    if (!capturing && !previousHealth) return false;
    const uint32 currentHealth = bot->GetHealth();
    const bool damaged = previousHealth && currentHealth < previousHealth;
    health->Set(capturing ? int32(currentHealth) : 0);
    Unit* threat = GetImmediateCaptureThreat(ai);
    if (!threat && !damaged)
    {
        // Successful completion or a non-damaging native interruption must not
        // leave the engine sleeping for the old banner's entire cast duration.
        if (!capturing && previousHealth)
        {
            const uint32 started = uint32(context->GetValue<int32>("manual int", "alterac capture started")->Get());
            if (uint32(WorldTimer::getMSTime() - started) < 7000) DelayCapture(ai);
            ClearCaptureRecord(ai);
            return true;
        }
        return false;
    }
    if (capturing) bot->InterruptSpell(CURRENT_GENERIC_SPELL);
    ClearCaptureRecord(ai);
    DelayCapture(ai);
    context->GetValue<std::list<ObjectGuid>>("attackers")->Reset();
    context->GetValue<std::list<ObjectGuid>>("possible attack targets")->Reset();
    context->GetValue<std::list<ObjectGuid>>("enemy player targets")->Reset();
    context->GetValue<ObjectGuid>("enemy player target")->Reset();
    context->GetValue<bool>("has enemy player targets")->Reset();
    if (threat)
    {
        const ObjectGuid guid = threat->GetObjectGuid();
        bot->SetSelectionGuid(guid);
        bot->SetTarget(threat);
        context->GetValue<ObjectGuid>("current target")->Set(guid);
        context->GetValue<ObjectGuid>("attack target")->Set(guid);
    }
    return true;
}

bool TryCapture(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!IsActive(bot) || !bot->IsAlive() || ai->IsRealPlayer() || IsCapturing(bot) || !ai->CanMove() ||
        !bot->CanUseBattleGroundObject() || bot->IsNonMeleeSpellCasted(false)) return false;
    auto* context = ai->GetAiObjectContext();
    for (const ObjectGuid& guid : context->GetValue<std::list<ObjectGuid>>("closest game objects static los")->Get())
    {
        GameObject* flag = ai->GetGameObject(guid);
        if (!flag || !IsNodeFlag(flag) || !sServerFacade.isSpawned(flag) || flag->IsInUse() ||
            flag->GetGoState() != GO_STATE_READY || !bot->IsWithinDistInMap(flag, INTERACTION_DISTANCE) ||
            !EligibleCapturer(ai, flag) || !ElectedForFlag(ai, flag)) continue;
        SpellEntry const* entry = sServerFacade.LookupSpellInfo(CaptureSpellId);
        if (!entry) return false;
        if (bot->IsMounted()) bot->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
        if (bot->IsInDisallowedMountForm()) bot->RemoveSpellsCausingAura(SPELL_AURA_MOD_SHAPESHIFT);
        ai->StopMoving();
        context->GetValue<int32>("manual int", "alterac capture health")->Set(int32(bot->GetHealth()));
        context->GetValue<int32>("manual int", "alterac capture started")->Set(int32(WorldTimer::getMSTime()));
        context->GetValue<LastSpellCast&>("last spell cast")->Get().Set(CaptureSpellId, flag->GetObjectGuid(), time(0));
        Spell* spell = new Spell(bot, entry, false);
        spell->m_targets.setGOTarget(flag);
        if (spell->SpellStart(&spell->m_targets) != SPELL_CAST_OK)
        {
            // SpellStart owns the spell event even on failure. Do not delete it
            // or report a successful opening, and do not replace the stable task.
            ClearCaptureRecord(ai);
            DelayCapture(ai);
            return false;
        }
        return true;
    }
    return false;
}
#else
bool IsCapturing(Player*) { return false; }
Unit* GetImmediateCaptureThreat(PlayerbotAI*) { return nullptr; }
bool UpdateCaptureResponse(PlayerbotAI*) { return false; }
bool TryCapture(PlayerbotAI*) { return false; }
#endif
}
