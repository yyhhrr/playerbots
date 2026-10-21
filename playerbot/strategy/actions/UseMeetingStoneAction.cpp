
#include "playerbot/playerbot.h"
#include "UseMeetingStoneAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "playerbot/strategy/values/PositionValue.h"
#include "Entities/Transports.h"

using namespace MaNGOS;

bool UseMeetingStoneAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    if (!requester)
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid guid;
    p >> guid;

    if (requester->GetSelectionGuid() && requester->GetSelectionGuid() != bot->GetObjectGuid())
        return false;

    if (!requester->GetSelectionGuid() && requester->GetGroup() != bot->GetGroup())
        return false;

    if (requester->IsBeingTeleported())
        return false;

    if (sServerFacade.IsInCombat(bot))
    {
        ai->TellPlayerNoFacing(requester, "I am in combat");
        return false;
    }

    Map* map = requester->GetMap();
    if (!map)
        return false;

    GameObject *gameObject = map->GetGameObject(guid);
    if (!gameObject)
        return false;

    const GameObjectInfo* goInfo = gameObject->GetGOInfo();
    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SUMMONING_RITUAL)
        return false;

    return Teleport(requester, requester, bot);
}

namespace
{
    bool CanDirectSummon(Player* bot, Player* requester)
    {
        return bot && requester && bot != requester && bot->GetSession() &&
            bot->GetPlayerbotAI() && !bot->GetPlayerbotAI()->IsRealPlayer() &&
            requester->GetGroup() && bot->GetGroup() == requester->GetGroup();
    }
}

bool SummonAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    if (!CanDirectSummon(bot, requester))
        return false;

    // Chat may arrive on another map's thread. Resolve both players on the world
    // thread while map updates are idle; no summon packet or AI reaction wait.
    QueueDirectSummon(bot->GetObjectGuid(), requester->GetObjectGuid());
    return true;
}

void SummonAction::QueueDirectSummon(ObjectGuid botGuid, ObjectGuid requesterGuid, uint32 attempts)
{
    sWorld.GetMessager().AddMessage([botGuid, requesterGuid, attempts](World*)
    {
        Player* target = sObjectAccessor.FindPlayer(botGuid);
        Player* requester = sObjectAccessor.FindPlayer(requesterGuid);
        if (!CanDirectSummon(target, requester))
            return; // Also handles leaving/disbanding the group before execution.

        PlayerbotAI* targetAI = target->GetPlayerbotAI();
        if (target->IsBeingTeleported())
            targetAI->HandleTeleportAck();

        // A real player's map transfer must finish before its position is usable.
        // Retain the command through this transition instead of silently dropping it.
        if (!target->IsInWorld() || target->IsBeingTeleported() ||
            !requester->IsInWorld() || requester->IsBeingTeleported())
        {
            if (attempts < 200)
                QueueDirectSummon(botGuid, requesterGuid, attempts + 1);
            else
                targetAI->TellPlayerNoFacing(requester, "Summon failed: map transfer did not finish.");
            return;
        }

        SummonAction action(targetAI);
        if (!action.DirectSummon(requester))
            targetAI->TellPlayerNoFacing(requester, "Summon failed: could not reach your map and position.");
    });
}

bool SummonAction::DirectSummon(Player* requester)
{
    if (!CanDirectSummon(bot, requester) || !requester->GetMap())
        return false;

    // The player's valid world position also works inside buildings and on transports.
    Map* destinationMap = requester->GetMap();
    const uint32 mapId = requester->GetMapId();
    const float x = requester->GetPositionX();
    const float y = requester->GetPositionY();
    const float z = requester->GetPositionZ();
    const float orientation = requester->GetOrientation();

    bot->BreakCharmIncoming();
    bot->InterruptNonMeleeSpells(true);
    bot->CombatStopWithPets(true);
    bot->TaxiFlightInterrupt();
    bot->GetMotionMaster()->Clear();

    const bool resurrect = !bot->IsAlive();
    if (resurrect)
    {
        bot->ResurrectPlayer(1.0f, false);
        bot->SpawnCorpseBones();
    }

    // Expire any older pending summon so its accept event cannot move us away later.
    bot->SetSummonPoint(mapId, x, y, z, requester->GetObjectGuid());
    bot->SummonIfPossible(false, requester->GetObjectGuid());

    if (bot->GetBattleGroundId() && bot->GetBattleGroundId() != requester->GetBattleGroundId())
        bot->LeaveBattleground(false);
    if (destinationMap->IsBattleGround())
    {
        if (!bot->GetMap()->IsBattleGround())
            bot->SetBattleGroundEntryPoint();
        bot->SetBattleGroundId(requester->GetBattleGroundId(), requester->GetBattleGroundTypeId());
    }

    // CMaNGOS entry/instance hooks see this override only during this synchronous
    // transfer. No GM permissions or saved instance bindings are changed.
    ai->directSummonMap = destinationMap;
    struct ClearSummonMap
    {
        Map*& map;
        ~ClearSummonMap() { map = nullptr; }
    } clearSummonMap{ai->directSummonMap};

    if (!bot->TeleportTo(mapId, x, y, z, orientation))
        return false;
    ai->HandleTeleportAck();
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->GetMap() != destinationMap ||
        bot->GetDistance(requester) > 5.0f)
        return false;

    if (GenericTransport* transport = requester->GetTransport())
    {
        bot->m_movementInfo.t_pos = requester->m_movementInfo.GetTransportPos();
        transport->AddPassenger(bot, false);
    }

    if (resurrect)
        bot->SetHealth(bot->GetMaxHealth());
    if (ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
        SET_AI_VALUE2(PositionEntry, "pos", "stay", PositionEntry(x, y, z, mapId));
    if (ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
        SET_AI_VALUE2(PositionEntry, "pos", "guard", PositionEntry(x, y, z, mapId));
    return true;
}

bool SummonAction::Teleport(Player* requester, Player *summoner, Player *player)
{
    if (!summoner->IsBeingTeleported() && !player->IsBeingTeleported() && summoner != player)
    {
        float followAngle = GetFollowAngle();
        for (double angle = followAngle - M_PI; angle <= followAngle + M_PI; angle += M_PI / 4)
        {
            uint32 mapId = summoner->GetMapId();
            float x = summoner->GetPositionX() + cos(angle) * ai->GetRange("follow");
            float y = summoner->GetPositionY() + sin(angle) * ai->GetRange("follow");
            float z = summoner->GetPositionZ();
            summoner->UpdateGroundPositionZ(x, y, z);

            if (!summoner->IsWithinLOS(x, y, z + player->GetCollisionHeight(), true))
            {
                x = summoner->GetPositionX();
                y = summoner->GetPositionY();
                z = summoner->GetPositionZ();
            }

            if (summoner->IsWithinLOS(x, y, z + player->GetCollisionHeight(), true))
            {
                bool resurrectPlayer = false;
                if (sServerFacade.UnitIsDead(player) && sServerFacade.IsAlive(summoner))
                {
                    if (!ai->IsSafe(player) || !ai->IsSafe(summoner))
                        return false;

                    resurrectPlayer = true;
                }

                // Dead target: a summon request cannot be accepted, so send a resurrect request
                // instead - the target teleports itself to the spot and resurrects itself. Alive
                // target: a normal summon request. Only a transport boarding or a rejected request
                // falls back to a direct, thread-safe teleport on the acting bot's own thread.
                ObjectGuid summonerGuid = summoner->GetObjectGuid();
                bool moveOnTransport = (summoner->GetTransport() != nullptr);

                bool handled = false;
                if (resurrectPlayer)
                    handled = PlayerbotAI::SendResurrectRequest(summoner, player, mapId, x, y, z);
                if (!handled && !moveOnTransport)
                    handled = PlayerbotAI::SendSummonRequest(summoner, player, mapId, x, y, z);

                if (!handled)
                {
                    ai->RunOnOwningThread(player, [mapId, x, y, z, resurrectPlayer, moveOnTransport, summonerGuid](Player* p)
                    {
                        if (resurrectPlayer)
                        {
                            p->ResurrectPlayer(1.0f, false);
                            p->SpawnCorpseBones();
                        }

                        if (p->IsTaxiFlying())
                        {
                            p->TaxiFlightInterrupt();
                            p->GetMotionMaster()->MovementExpired();
                        }

                        p->GetMotionMaster()->Clear();
                        p->TeleportTo(mapId, x, y, z, 0);
                        if (p->isRealPlayer())
                            p->SendHeartBeat();

                        if (moveOnTransport)
                        {
                            if (Player* s = sObjectAccessor.FindPlayer(summonerGuid))
                            {
                                if (GenericTransport* transport = s->GetTransport())
                                {
                                    // Board on the summoner's spot. We do this explicitly instead of
                                    // passing the transport to TeleportTo: that path is client-oriented
                                    // (it wants local coords on a far teleport but world coords on a near
                                    // one). UpdatePassengerPosition skips us until we are on the
                                    // transport's map, so boarding a still-teleporting player is safe.
                                    p->m_movementInfo.t_pos = s->m_movementInfo.GetTransportPos();
                                    transport->AddPassenger(p, false);
                                }
                            }
                        }
                    });
                }

                if (resurrectPlayer)
                    ai->TellPlayerNoFacing(requester, "I live, again!");

                if(ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT))
                    SET_AI_VALUE2(PositionEntry, "pos", "stay", PositionEntry(x, y, z, mapId));
                if (ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
                    SET_AI_VALUE2(PositionEntry, "pos", "guard", PositionEntry(x, y, z, mapId));

                return true;
            }
        }
    }

    if(summoner != player)
        ai->TellPlayerNoFacing(requester, "Not enough place to summon");
    return false;
}

bool AcceptSummonAction::Execute(Event& event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid summonerGuid;
    p >> summonerGuid;

    WorldPacket response(CMSG_SUMMON_RESPONSE);
    response << summonerGuid;
#if defined(MANGOSBOT_ONE) || defined(MANGOSBOT_TWO)
    response << uint8(1);
#endif
    bot->GetSession()->HandleSummonResponseOpcode(response);

    return true;
}
