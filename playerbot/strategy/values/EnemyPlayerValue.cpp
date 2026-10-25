
#include "playerbot/playerbot.h"
#include "EnemyPlayerValue.h"
#include "TargetValue.h"
#include "PositionValue.h"
#include "playerbot/ArathiBasinCombat.h"
#include "playerbot/WarsongCombat.h"
#include "playerbot/WarsongTactics.h"
#include "BattleGround/BattleGroundAB.h"
#include "BattleGround/BattleGroundWS.h"
#include "BattleGround/BattleGroundMgr.h"

using namespace ai;

namespace
{
    BattleGroundWS* GetActiveWarsong(Player* player)
    {
        BattleGround* battleground = player ? player->GetBattleGround() : nullptr;
        if (!battleground || battleground->GetStatus() != STATUS_IN_PROGRESS)
            return nullptr;

        BattleGroundTypeId type = battleground->GetTypeId();
#ifdef MANGOSBOT_TWO
        if (type == BATTLEGROUND_RB)
            type = battleground->GetTypeId(true);
#endif
        return type == BATTLEGROUND_WS ? static_cast<BattleGroundWS*>(battleground) : nullptr;
    }

    bool IsDirectlyAttackingCarrier(Unit* enemy, Player* carrier)
    {
        if (enemy->GetVictim() == carrier)
            return true;

        for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL })
        {
            Spell* spell = enemy->GetCurrentSpell(type);
            if (spell && spell->getState() != SPELL_STATE_FINISHED &&
                spell->m_targets.getUnitTarget() == carrier)
                return true;
        }

        return false;
    }

    BattleGround* GetActiveArathiBattleGround(Player* player)
    {
        BattleGround* battleground = player ? player->GetBattleGround() : nullptr;
        if (!battleground || battleground->GetStatus() != STATUS_IN_PROGRESS)
            return nullptr;

        BattleGroundTypeId type = battleground->GetTypeId();
#ifdef MANGOSBOT_TWO
        if (type == BATTLEGROUND_RB)
            type = battleground->GetTypeId(true);
#endif
        return type == BATTLEGROUND_AB ? battleground : nullptr;
    }

    bool IsDirectlyAttackingDefender(Unit* enemy, Player* defender)
    {
        Unit* pet = defender->GetPet();
        Unit* victim = enemy->GetVictim();
        if (victim == defender || (pet && victim == pet))
            return true;

        for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL })
        {
            Spell* spell = enemy->GetCurrentSpell(type);
            if (!spell || spell->getState() == SPELL_STATE_FINISHED)
                continue;

            Unit* spellTarget = spell->m_targets.getUnitTarget();
            if (spellTarget == defender || (pet && spellTarget == pet))
                return true;
        }

        return false;
    }

    bool CanDefenderEngage(Unit* enemy, Player* defender, bool playerControlled)
    {
        PlayerbotAI* ai = defender->GetPlayerbotAI();
        if (!ai || !ai->GetAiObjectContext()->GetValue<bool>("manual bool", "arathi defender")->Get())
            return true;

        PositionMap& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
        const auto objective = positions.find("bg objective");
        if (objective == positions.end() || !objective->second.valueSet || objective->second.mapId != defender->GetMapId())
            return true;

        const PositionEntry& center = objective->second;
        return IsArathiDefenderTargetAllowed(true, true,
            sServerFacade.GetDistance2d(enemy, center.x, center.y),
            sServerFacade.GetDistance2d(defender, enemy), IsDirectlyAttackingDefender(enemy, defender), playerControlled,
            ai->CanMove());
    }

    bool IsCapturingArathiBanner(Unit* enemy, Player* defender, BattleGround* battleground)
    {
        Player* enemyPlayer = dynamic_cast<Player*>(enemy);
        if (!enemyPlayer || enemyPlayer->GetBattleGround() != battleground ||
            !PossibleTargetsValue::IsValid(enemy, defender) ||
            !enemy->IsVisibleForOrDetect(defender, defender->GetCamera().GetBody(), true) ||
            !sServerFacade.IsWithinLOSInMap(defender, enemy))
            return false;

        Spell* spell = enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell || spell->m_spellInfo->Id != 21651 || spell->getState() == SPELL_STATE_FINISHED)
            return false;

        // Resolve the actual cast target on this map; the spell ID alone is not proof
        // of a flag capture, and despawned/replaced banners are no longer a threat.
        GameObject* banner = defender->GetMap()->GetGameObject(spell->m_targets.getGOTargetGuid());
        if (!banner || !banner->IsInWorld() || !sServerFacade.isSpawned(banner) ||
            banner->IsInUse() || banner->GetGoState() != GO_STATE_READY ||
            !enemyPlayer->CanUseBattleGroundObject() || !enemyPlayer->CanInteract(banner) ||
            !enemyPlayer->IsWithinDistInMap(banner, INTERACTION_DISTANCE))
            return false;

        switch (banner->GetEntry())
        {
        case BG_AB_BANNER_MINE:
        case BG_AB_BANNER_LUMBER_MILL:
        case BG_AB_BANNER_FARM:
        case BG_AB_BANNER_BLACKSMITH:
        case BG_AB_BANNER_STABLE:
        case BG_AB_BANNER_ALLIANCE:
        case BG_AB_BANNER_CONTESTED_A:
        case BG_AB_BANNER_HORDE:
        case BG_AB_BANNER_CONTESTED_H:
            break;
        default:
            return false;
        }

#ifdef CMANGOS
        const auto event = defender->GetMap()->GetMapDataContainer().GetGameObjectEventIndex(banner->GetDbGuid());
#else
        const auto event = sBattleGroundMgr.GetGameObjectEventIndex(banner->GetDbGuid());
#endif
        return CanAssaultArathiBanner(event.event1, event.event2, GetTeamIndexByTeamId(enemyPlayer->GetTeam())) &&
            battleground->IsActiveEvent(event.event1, event.event2);
    }
}

std::list<ObjectGuid> EnemyPlayersValue::Calculate()
{
    std::list<ObjectGuid> result;
    if (ai->AllowActivity(ALL_ACTIVITY))
    {
        if (bot->IsInWorld() && !bot->IsBeingTeleported())
        {
            // Check if we only need one attacker
            bool getOne = false;
            if (!qualifier.empty())
            {
                getOne = std::stoi(qualifier);
            }

            if (getOne)
            {
                // Try to get one enemy target
                result = AI_VALUE2(std::list<ObjectGuid>, "possible attack targets", 1);
                ApplyFilter(result, getOne);
            }

            // If the one enemy player failed, retry with multiple possible attack targets
            if (result.empty())
            {
                result = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
                ApplyFilter(result, getOne);
            }
        }
    }

    return result;
}

bool EnemyPlayersValue::IsValid(Unit* target, Player* player)
{
    if (!PossibleTargetsValue::CanPerceive(target, player))
        return false;
    if (target)
    {
        // If the target is a player
        Player* enemyPlayer = dynamic_cast<Player*>(target);
        if (enemyPlayer)
        {
            // If the target is friendly to the player
            if (sServerFacade.IsFriendlyTo(target, player))
            {
                return false;
            }

            // Check that the target is not a mind controlled ally
            if (target->HasAuraType(SPELL_AURA_MOD_CHARM) || target->HasAuraType(SPELL_AURA_MOD_POSSESS))
            {
                if (player && player->IsInGroup(target))
                {
                    return false;
                }
            }

            if (!IsReachable(target, player))
            {
                return false;
            }

            if (!IsAllowedArathiDefenderTarget(target, player) || !IsAllowedWarsongTarget(target, player))
            {
                return false;
            }

            /*
            // Check if too far away (Do we need this?)
            const float maxPvPDistance = GetMaxAttackDistance(player);
            const bool inCannon = player->GetPlayerbotAI() && player->GetPlayerbotAI()->IsInVehicle(false, true);
            uint32 const pvpDistance = (inCannon || player->GetHealth() > enemyPlayer->GetHealth()) ? maxPvPDistance : 20.0f;
            if (!player->IsWithinDist(enemyPlayer, pvpDistance, false))
            {
                return false;
            }
            */

            return true;
        }
    }

    return false;
}

bool EnemyPlayersValue::IsAllowedArathiDefenderTarget(Unit* target, Player* player)
{
    if (!target || !player || !GetActiveArathiBattleGround(player))
        return true;

    // The owner's combat must not let a pet or guardian bypass the flag boundary.
    // Ordinary battleground NPCs retain their existing engagement rules.
    const bool playerControlled = target->GetObjectGuid().IsPlayer() ||
        target->GetMasterGuid().IsPlayer() || target->GetOwnerGuid().IsPlayer();
    return CanDefenderEngage(target, player, playerControlled);
}

bool EnemyPlayersValue::IsAllowedWarsongTarget(Unit* target, Player* player)
{
    BattleGroundWS* battleground = GetActiveWarsong(player);
    if (!battleground)
        return true;

    const bool carrier = player->HasAura(23333) || player->HasAura(23335);
    PlayerbotAI* ai = player->GetPlayerbotAI();
    const bool escort = ai && warsong::GetTask(ai) == warsong::Task::Escort;
    if (!carrier && !escort)
        return true;

    if (!target || !target->IsInWorld() || target->GetMap() != player->GetMap())
        return false;

    const float distance = player->GetDistance(target);
    const bool directAttack = IsDirectlyAttackingCarrier(target, player);
    if (carrier)
        return warsong::CanEngageAsCarrier(true, distance, directAttack);

    PositionMap& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    const auto objective = positions.find("bg objective");
    if (objective == positions.end() || !objective->second.isSet() || objective->second.mapId != player->GetMapId())
        return true;

    const ObjectGuid friendlyCarrierGuid = battleground->GetFlagCarrierGuid(
        GetTeamIndexByTeamId(battleground->GetOtherTeam(player->GetTeam())));
    Player* friendlyCarrier = player->GetMap()->GetPlayer(friendlyCarrierGuid);
    if (!friendlyCarrier || !friendlyCarrier->IsAlive() || friendlyCarrier->GetTeam() != player->GetTeam() ||
        friendlyCarrier->GetMap() != player->GetMap() || friendlyCarrier->IsBeingTeleported())
        return true;

    // The objective denotes this friendly carrier, whose live position prevents
    // an old snapshot from leaving escorts defending a point the carrier has left.
    return warsong::CanEngageAsEscort(true, true, friendlyCarrier->GetDistance(target), distance, directAttack);
}

bool EnemyPlayersValue::IsReachable(Unit* target, Player* player)
{
    // A player standing under the map (or on a ledge with no way up) is close in 2D but can
    // never be reached, and would otherwise keep the bot in combat with it indefinitely.
    if (!player || !PlayerbotAI::IsSafe(player, target))
    {
        return true;
    }

    // The path check below decides reachability. This height gate only decides when it is
    // worth running: normal fights (slopes, stairs, small ledges) stay within a few yards
    // of height, so skipping them keeps the pathfinder out of the common case. The cases
    // this targets are far outside it (e.g. a player that fell ~30 yd under the map).
    // A lower value only costs more path checks, a higher one only misses shallower cases.
    const float maxHeightWithoutPathCheck = 10.0f;
    if (std::abs(target->GetPositionZ() - player->GetPositionZ()) <= maxHeightWithoutPathCheck)
    {
        return true;
    }

    return WorldPosition(player).canPathTo(WorldPosition(target), player);
}

void EnemyPlayersValue::ApplyFilter(std::list<ObjectGuid>& targets, bool getOne)
{
    std::list<ObjectGuid> filteredTargets;
    for (const ObjectGuid& targetGuid : targets)
    {
        Unit* target = ai->GetUnit(targetGuid);
        if (IsValid(target, bot))
        {
            filteredTargets.push_back(target->GetObjectGuid());

            if (getOne)
            {
                break;
            }
        }
    }

    targets = filteredTargets;
}

bool HasEnemyPlayersValue::Calculate()
{
    return !context->GetValue<std::list<ObjectGuid>>("enemy player targets", 1)->Get().empty();
}

ObjectGuid EnemyPlayerValue::Calculate()
{
    // Prioritize the duel opponent
    if(bot->duel && bot->duel->opponent && !sServerFacade.IsFriendlyTo(bot->duel->opponent, bot) &&
        PossibleTargetsValue::CanPerceive(bot->duel->opponent, bot))
    {
        return bot->duel->opponent->GetObjectGuid();
    }

    Unit* bestEnemyPlayer = nullptr;
    std::list<ObjectGuid> enemyPlayers = AI_VALUE(std::list<ObjectGuid>, "enemy player targets");
    // Visibility and role changes must invalidate stale cached opponents before
    // any battleground-specific or ordinary target scoring can reuse them.
    for (auto it = enemyPlayers.begin(); it != enemyPlayers.end();)
    {
        Unit* target = ai->GetUnit(*it);
        if (!target || !PossibleTargetsValue::IsValid(target, bot) || !EnemyPlayersValue::IsValid(target, bot))
            it = enemyPlayers.erase(it);
        else
            ++it;
    }
    if (!enemyPlayers.empty())
    {
        const bool isMelee = !ai->IsRanged(bot);
        BattleGround* arathi = GetActiveArathiBattleGround(bot);
        if (arathi)
        {
            ArathiCombatCandidate bestCandidate;
            for (const ObjectGuid& targetGuid : enemyPlayers)
            {
                Unit* target = ai->GetUnit(targetGuid);
                // The cached list may outlive a death, charm, or a defender assignment.
                if (!target || !PossibleTargetsValue::IsValid(target, bot) || !EnemyPlayersValue::IsValid(target, bot))
                    continue;

                const ArathiCombatCandidate candidate = {
                    IsCapturingArathiBanner(target, bot, arathi), target->GetDistance(bot, true), target->GetHealth()
                };
                if (!bestEnemyPlayer || IsBetterArathiCombatTarget(candidate, bestCandidate, isMelee))
                {
                    bestEnemyPlayer = target;
                    bestCandidate = candidate;
                }
            }
            return bestEnemyPlayer ? bestEnemyPlayer->GetObjectGuid() : ObjectGuid();
        }

        uint32 bestEnemyPlayerHealth = std::numeric_limits<uint32>::max();
        float bestEnemyPlayerDistance = std::numeric_limits<float>::max();
      
        // Use the first enemy player as a base
        Unit* firstTarget = ai->GetUnit(enemyPlayers.front());
        if (firstTarget)
        {
            bestEnemyPlayerDistance = firstTarget->GetDistance(bot, true);
            bestEnemyPlayerHealth = firstTarget->GetHealth();
            bestEnemyPlayer = firstTarget;
        }

        for (const ObjectGuid& targetGuid : enemyPlayers)
        {
            Unit* target = ai->GetUnit(targetGuid);
            if (target)
            {
                // Prioritize an enemy player if it has a battleground flag
                if ((bot->GetTeam() == HORDE && target->HasAura(23333)) ||
                    (bot->GetTeam() == ALLIANCE && target->HasAura(23335)))
                {
                    bestEnemyPlayer = target;
                    break;
                }

                if (isMelee)
                {
                    // Score best enemy player based on lowest distance
                    const float distanceToEnemyPlayer = target->GetDistance(bot, true);
                    if (distanceToEnemyPlayer < bestEnemyPlayerDistance)
                    {
                        bestEnemyPlayerDistance = distanceToEnemyPlayer;
                        bestEnemyPlayer = target;
                    }
                }
                else
                {
                    // Score best enemy player based on lowest health
                    const uint32 enemyPlayerHealth = target->GetHealth();
                    if (enemyPlayerHealth < bestEnemyPlayerHealth)
                    {
                        bestEnemyPlayerHealth = enemyPlayerHealth;
                        bestEnemyPlayer = target;
                    }
                }
            }
        }
    }

    return bestEnemyPlayer ? bestEnemyPlayer->GetObjectGuid() : ObjectGuid();
}


float EnemyPlayerValue::GetMaxAttackDistance(Player* bot)
{
    if (!bot->GetBattleGround())
        return 60.0f;

    if (bot->InBattleGround())
    {
        BattleGround* bg = bot->GetBattleGround();
        if (!bg)
            return 40.0f;

        BattleGroundTypeId bgType = bg->GetTypeId();

#ifdef MANGOSBOT_TWO
        if (bgType == BATTLEGROUND_RB)
            bgType = bg->GetTypeId(true);

        if (bgType == BATTLEGROUND_IC)
        {
            if (bot->GetPlayerbotAI()->IsInVehicle(false, true))
                return 120.0f;
        }
#endif
        if (bgType == BATTLEGROUND_AV)
        {
            bool strifeTime = bg->GetStartTime() < (uint32)(20 * MINUTE * IN_MILLISECONDS);
            return strifeTime ? 40.0f : 10.0f;
        }
    }

    return 40.0f;
}
