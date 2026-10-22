#include "playerbot/playerbot.h"
#include "WarsongTactics.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "BattleGround/BattleGroundWS.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#include <map>
#include <mutex>
#include <tuple>

namespace ai
{
namespace warsong
{
namespace
{
    struct TeamPlan
    {
        Planner planner;
        Planner::Assignments assignments;
        Flag ownFlag;
        Flag enemyFlag;
        uint32 updated = 0;
        uint32 seen = 0;
        uint32 battleTime = 0;
        bool ready = false;
    };

    // Instance/faction snapshots contain no retained world pointers.
    std::map<std::tuple<uint32, uint32, Team>, TeamPlan> plans;
    std::mutex plansMutex;

    BattleGroundWS* ActiveWarsong(Player* player)
    {
        BattleGround* bg = player ? player->GetBattleGround() : nullptr;
        return player && player->IsInWorld() && !player->IsBeingTeleported() &&
            player->GetMapId() == 489 && bg && bg->GetStatus() == STATUS_IN_PROGRESS ?
            static_cast<BattleGroundWS*>(bg) : nullptr;
    }

    Point Location(WorldObject* object)
    {
        return {object->GetPositionX(), object->GetPositionY(), object->GetPositionZ()};
    }

    int32 FlagStateValue(BattleGroundWS* bg, Team owner)
    {
        return int32(bg->GetBgMap()->GetVariableManager().GetVariable(wsFlagPickedUp[GetTeamIndexByTeamId(owner)]));
    }

    Flag ReadFlag(BattleGroundWS* bg, Team owner, const Point& base, Team observerTeam)
    {
        Flag flag;
        flag.position = base;
        flag.positionKnown = true;
        switch (FlagStateValue(bg, owner))
        {
        case BG_WS_FLAG_STATE_ON_PLAYER:
        {
            flag.state = FlagState::Carried;
            const ObjectGuid guid = bg->GetFlagCarrierGuid(GetTeamIndexByTeamId(owner));
            flag.carrier = guid.GetRawValue();
            Player* carrier = bg->GetBgMap()->GetPlayer(guid);
            flag.positionKnown = carrier && carrier->IsAlive() && carrier->IsInWorld() && !carrier->IsBeingTeleported();
            if (flag.positionKnown) flag.position = Location(carrier);
            break;
        }
        case BG_WS_FLAG_STATE_ON_GROUND:
        {
            flag.state = FlagState::Dropped;
            flag.positionKnown = false;
            const uint32 entry = owner == ALLIANCE ? GO_WS_SILVERWING_FLAG_DROP : GO_WS_WARSONG_FLAG_DROP;
            for (const auto& ref : bg->GetBgMap()->GetPlayers())
            {
                Player* observer = ref.getSource();
                if (!observer || !observer->IsAlive() || !observer->IsInWorld() ||
                    observer->IsBeingTeleported() || observer->GetTeam() != observerTeam) continue;
                GameObject* dropped = nullptr;
                MaNGOS::NearestGameObjectEntryInObjectRangeCheck check(*observer, entry, sPlayerbotAIConfig.sightDistance);
                MaNGOS::GameObjectLastSearcher<MaNGOS::NearestGameObjectEntryInObjectRangeCheck> searcher(dropped, check);
                Cell::VisitAllObjects(observer, searcher, sPlayerbotAIConfig.sightDistance);
                if (!dropped) continue;
                if (dropped->GetEntry() == entry && dropped->IsInWorld() && sServerFacade.isSpawned(dropped))
                {
                    flag.position = Location(dropped);
                    flag.positionKnown = true;
                    break;
                }
            }
            break;
        }
        default:
            flag.state = FlagState::Base;
            break;
        }
        return flag;
    }

    bool SameSituation(const Flag& first, const Flag& second)
    {
        return first.state == second.state && first.carrier == second.carrier;
    }

    TeamPlan& Refresh(Player* bot, BattleGroundWS* bg, const Flag& ownFlag, const Flag& enemyFlag)
    {
        const uint32 now = WorldTimer::getMSTime();
        for (auto it = plans.begin(); it != plans.end();)
        {
            if (uint32(now - it->second.seen) > 5 * MINUTE * IN_MILLISECONDS) it = plans.erase(it);
            else ++it;
        }
        TeamPlan& plan = plans[{bot->GetMapId(), bot->GetInstanceId(), bot->GetTeam()}];
        if (plan.ready && bg->GetStartTime() < plan.battleTime) plan = TeamPlan{};
        plan.seen = now;
        plan.battleTime = bg->GetStartTime();
        if (plan.ready && SameSituation(plan.ownFlag, ownFlag) && SameSituation(plan.enemyFlag, enemyFlag) &&
            uint32(now - plan.updated) < 2000)
            return plan;

        std::vector<Member> members;
        for (const auto& ref : bot->GetMap()->GetPlayers())
        {
            Player* member = ref.getSource();
            if (!member || !member->IsInWorld() || member->IsBeingTeleported() || member->GetTeam() != bot->GetTeam()) continue;
            PlayerbotAI* memberAI = member->GetPlayerbotAI();
            members.push_back({member->GetObjectGuid().GetRawValue(), Location(member), memberAI && !memberAI->IsRealPlayer(),
                member->IsAlive(), memberAI && memberAI->IsHeal(member), member->IsInCombat()});
        }
        plan.assignments = plan.planner.Update(ownFlag, enemyFlag, members, now);
        plan.ownFlag = ownFlag;
        plan.enemyFlag = enemyFlag;
        plan.updated = now;
        plan.ready = true;
        return plan;
    }
}

bool IsCarrier(Player* player)
{
    BattleGroundWS* bg = ActiveWarsong(player);
    return bg && player->IsAlive() &&
        bg->GetFlagCarrierGuid(GetTeamIndexByTeamId(bg->GetOtherTeam(player->GetTeam()))) == player->GetObjectGuid();
}

Task GetTask(PlayerbotAI* ai)
{
    if (IsCarrier(ai->GetBot())) return Task::Carry;
    const int task = ai->GetAiObjectContext()->GetValue<int>("manual int", "warsong task")->Get();
    return task >= int(Task::Attack) && task < int(Task::Carry) ? Task(task) : Task::Attack;
}

bool SelectObjective(PlayerbotAI* ai, const Point& home, const Point& enemyHome,
    const std::vector<Point>& hidingSpots, WorldLocation& objective)
{
    Player* bot = ai->GetBot();
    BattleGroundWS* bg = ActiveWarsong(bot);
    if (!bg || !bot->IsAlive()) return false;
    const Flag ownFlag = ReadFlag(bg, bot->GetTeam(), home, bot->GetTeam());
    const Flag enemyFlag = ReadFlag(bg, bg->GetOtherTeam(bot->GetTeam()), enemyHome, bot->GetTeam());
    Assignment task;
    {
        std::lock_guard<std::mutex> lock(plansMutex);
        TeamPlan& plan = Refresh(bot, bg, ownFlag, enemyFlag);
        const auto found = plan.assignments.find(bot->GetObjectGuid().GetRawValue());
        if (found == plan.assignments.end()) return false;
        task = found->second;
    }
    // Identity is checked live, so a pick-up never waits for the snapshot timer.
    if (IsCarrier(bot)) task.task = Task::Carry;
    Point destination = enemyHome;
    switch (task.task)
    {
    case Task::Carry:
        destination = home;
        if (ownFlag.state != FlagState::Base && !hidingSpots.empty())
            destination = hidingSpots[bot->GetGUIDLow() % hidingSpots.size()];
        break;
    case Task::Escort:
        destination = enemyFlag.positionKnown ? enemyFlag.position : home;
        break;
    case Task::Recover:
        destination = ownFlag.positionKnown ? ownFlag.position : home;
        break;
    case Task::Attack:
        if (enemyFlag.state == FlagState::Dropped && enemyFlag.positionKnown)
            destination = enemyFlag.position;
        else if (enemyFlag.state == FlagState::Carried || enemyFlag.state == FlagState::Respawning)
            destination = {1227.446f, 1476.235f, 307.484f}; // Screen the midfield while the flag is away.
        break;
    }
    auto* context = ai->GetAiObjectContext();
    context->GetValue<int>("manual int", "warsong task")->Set(int(task.task));
    context->GetValue<uint32>("bg role")->Set(task.route);
    context->GetValue<PositionMap&>("position")->Get()["bg objective"].Set(destination.x, destination.y, destination.z, bot->GetMapId());
    objective = WorldLocation(bot->GetMapId(), destination.x, destination.y, destination.z);
    return true;
}

bool CanUseFlag(PlayerbotAI* ai, GameObject* flag)
{
    Player* bot = ai->GetBot();
    BattleGroundWS* bg = ActiveWarsong(bot);
    if (!bg || !bot->IsAlive() || !flag || flag->GetMap() != bot->GetMap()) return false;
    const bool alliance = bot->GetTeam() == ALLIANCE;
    const uint32 ownBase = alliance ? GO_WS_SILVERWING_FLAG : GO_WS_WARSONG_FLAG;
    const uint32 ownDrop = alliance ? GO_WS_SILVERWING_FLAG_DROP : GO_WS_WARSONG_FLAG_DROP;
    if (flag->GetEntry() == ownBase) return IsCarrier(bot) && FlagStateValue(bg, bot->GetTeam()) == BG_WS_FLAG_STATE_ON_BASE;
    if (flag->GetEntry() == ownDrop) return FlagStateValue(bg, bot->GetTeam()) == BG_WS_FLAG_STATE_ON_GROUND;
    if (IsCarrier(bot) || GetTask(ai) != Task::Attack || !bot->CanUseBattleGroundObject()) return false;
    const int32 state = FlagStateValue(bg, bg->GetOtherTeam(bot->GetTeam()));
    return state == BG_WS_FLAG_STATE_ON_BASE || state == BG_WS_FLAG_STATE_ON_GROUND;
}
}
}
