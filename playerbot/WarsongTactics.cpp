#include "playerbot/playerbot.h"
#include "WarsongTactics.h"
#include "WarsongCarrierPolicy.h"
#include "WarsongCarrierActions.h"
#include "playerbot/AiFactory.h"
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
        CarrierPlanner carriers;
        CarrierSelection selection;
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

    std::vector<CarrierCandidate> CarrierCandidates(const std::vector<Player*>& players, const Flag& enemyFlag)
    {
        unsigned healers = 0;
        for (Player* member : players)
            if (member->IsAlive() && PlayerbotAI::IsHeal(member)) ++healers;
        std::vector<CarrierCandidate> result;
        for (Player* member : players)
        {
            PlayerbotAI* memberAI = member->GetPlayerbotAI();
            if (!memberAI || memberAI->IsRealPlayer()) continue;
            CarrierCandidate candidate;
            candidate.guid = member->GetObjectGuid().GetRawValue();
            candidate.alive = member->IsAlive();
            candidate.eligible = member->CanUseBattleGroundObject() && memberAI->CanMove();
            candidate.health = member->GetHealth();
            candidate.maxHealth = member->GetMaxHealth();
            candidate.distance = Distance(Location(member), enemyFlag.position);
            const uint32 mana = member->GetMaxPower(POWER_MANA);
            candidate.manaPercent = mana ? 100.0f * member->GetPower(POWER_MANA) / mana : 0.0f;
            candidate.healer = PlayerbotAI::IsHeal(member);
            candidate.soleHealer = candidate.healer && healers <= 1;
            candidate.underPressure = !member->getAttackers().empty();
            const auto abilities = InspectCarrierAbilities(memberAI);
            candidate.mobilityReady = abilities.mobilityReady;
            candidate.defenseReady = abilities.defenseReady;
            switch (member->getClass())
            {
            case CLASS_DRUID: candidate.carrierClass = CarrierClass::Druid; break;
            case CLASS_MAGE:
                if (AiFactory::GetPlayerSpecTab(member) == 2) candidate.carrierClass = CarrierClass::FrostMage;
                break;
            case CLASS_ROGUE: candidate.carrierClass = CarrierClass::Rogue; break;
            case CLASS_WARRIOR: candidate.carrierClass = CarrierClass::Warrior; break;
            case CLASS_PALADIN: candidate.carrierClass = CarrierClass::Paladin; break;
            default: break;
            }
            if (Item* shield = member->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                candidate.hasShield = shield->GetProto()->InventoryType == INVTYPE_SHIELD;
            for (Player* support : players)
                if (support != member && support->IsAlive() && PlayerbotAI::IsHeal(support) &&
                    member->IsWithinDistInMap(support, 40.0f)) candidate.hasHealerSupport = true;
            result.push_back(candidate);
        }
        return result;
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
        std::vector<Player*> players;
        for (const auto& ref : bot->GetMap()->GetPlayers())
        {
            Player* member = ref.getSource();
            if (!member || !member->IsInWorld() || member->IsBeingTeleported() || member->GetTeam() != bot->GetTeam()) continue;
            PlayerbotAI* memberAI = member->GetPlayerbotAI();
            players.push_back(member);
            members.push_back({member->GetObjectGuid().GetRawValue(), Location(member), memberAI && !memberAI->IsRealPlayer(),
                member->IsAlive(), memberAI && memberAI->IsHeal(member), member->IsInCombat()});
        }
        plan.selection = plan.carriers.Update(CarrierCandidates(players, enemyFlag), now,
            enemyFlag.state == FlagState::Carried ? enemyFlag.carrier : 0);
        const std::vector<uint64_t> runners = {plan.selection.primary, plan.selection.backup};
        plan.assignments = plan.planner.Update(ownFlag, enemyFlag, members, now, runners);
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
    if (IsCarrier(bot) || !bot->CanUseBattleGroundObject()) return false;
    const int32 state = FlagStateValue(bg, bg->GetOtherTeam(bot->GetTeam()));
    if (state != BG_WS_FLAG_STATE_ON_BASE && state != BG_WS_FLAG_STATE_ON_GROUND) return false;
    const bool dropped = state == BG_WS_FLAG_STATE_ON_GROUND;
    // Every nearby role may save a dropped flag. At the base, ordinary attack
    // members can approach, but the nominated available runners get first click.
    CarrierSelection selection;
    {
        std::lock_guard<std::mutex> lock(plansMutex);
        const auto plan = plans.find({bot->GetMapId(), bot->GetInstanceId(), bot->GetTeam()});
        if (plan != plans.end()) selection = plan->second.selection;
    }
    const uint64 botGuid = bot->GetObjectGuid().GetRawValue();
    if (!dropped && GetTask(ai) != Task::Attack &&
        botGuid != selection.primary && botGuid != selection.backup) return false;
    if (!bot->IsWithinDistInMap(flag, INTERACTION_DISTANCE)) return true;
    selection.carrier = 0; // The live flag state above supersedes an old carrier snapshot.
    std::vector<PickupCandidate> candidates;
    for (const auto& ref : bot->GetMap()->GetPlayers())
    {
        Player* member = ref.getSource();
        if (!member || !member->IsInWorld() || member->IsBeingTeleported() || member->GetTeam() != bot->GetTeam() ||
            !member->IsAlive() || !member->IsWithinDistInMap(flag, INTERACTION_DISTANCE)) continue;
        PlayerbotAI* memberAI = member->GetPlayerbotAI();
        if (!memberAI || memberAI->IsRealPlayer()) continue;
        // WSG flags use battleground rules rather than generic GO faction reaction.
        // The enemy flag must remain usable even when CanInteract reports hostility.
        const bool eligible = member->CanUseBattleGroundObject() && memberAI->CanMove() &&
            !member->IsNonMeleeSpellCasted(false) && sServerFacade.IsWithinLOSInMap(member, flag) &&
            (dropped || GetTask(memberAI) == Task::Attack ||
                member->GetObjectGuid().GetRawValue() == selection.primary ||
                member->GetObjectGuid().GetRawValue() == selection.backup);
        candidates.push_back({member->GetObjectGuid().GetRawValue(), member->GetDistance(flag), eligible});
    }
    return SelectFlagPicker(selection, candidates, dropped, INTERACTION_DISTANCE) == bot->GetObjectGuid().GetRawValue();
}
}
}
