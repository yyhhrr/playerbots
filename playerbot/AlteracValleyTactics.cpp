#include "playerbot/playerbot.h"
#include "AlteracValleyTactics.h"
#include "AlteracValleyPolicy.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/strategy/values/PossibleTargetsValue.h"
#include "BattleGround/BattleGroundAV.h"

#include <array>
#include <map>
#include <mutex>
#include <tuple>

namespace ai::alterac
{
namespace
{
    Point Location(WorldObject* object)
    {
        return {object->GetPositionX(), object->GetPositionY(), object->GetPositionZ()};
    }
}
#ifdef MANGOSBOT_ZERO
namespace
{
    constexpr unsigned CaptainObjective = NodeCount;
    constexpr unsigned BossObjective = NodeCount + 1;
    constexpr unsigned MineObjective = NodeCount + 2;
    const std::array<const char*, NodeCount> nodeNames = {
        "AV_STORMPIKE_AID_STATION", "AV_STORMPIKE_GRAVEYARD", "AV_STONEHEART_GRAVEYARD",
        "AV_SNOWFALL_GRAVEYARD", "AV_ICEBLOOD_GRAVEYARD", "AV_FROSTWOLF_GRAVEYARD",
        "AV_FROSTWOLF_RELIEF_HUT", "AV_DUNBALDAR_SOUTH", "AV_DUNBALDAR_NORTH",
        "AV_ICEWING_BUNKER", "AV_STONEHEART_BUNKER", "AV_ICEBLOOD_TOWER", "AV_TOWERPOINT",
        "AV_EAST_FROSTWOLF_TOWER", "AV_WEST_FROSTWOLF_TOWER"
    };

    struct TeamPlan
    {
        Planner planner;
        Planner::Assignments assignments;
        std::array<NodeState, NodeCount> states{};
        std::array<Point, NodeCount> positions{};
        std::array<bool, NodeCount> valid{};
        std::array<bool, NodeCount> ownedBefore{};
        std::vector<Objective> objectives;
        uint32 updated = 0, seen = 0, battleTime = 0;
        bool ready = false, positionsRead = false;
        bool captainCommitted = false, captainWasInCombat = false;
    };
    using PlanKey = std::tuple<uint32, uint32, Team>;
    std::map<PlanKey, TeamPlan> plans;
    std::mutex plansMutex;

    bool IsTower(unsigned node) { return node >= BG_AV_NODES_DUNBALDAR_SOUTH && node < NodeCount; }
    bool InitiallyFriendly(Team team, unsigned node)
    {
        const bool alliance = node <= BG_AV_NODES_STONEHEART_GRAVE ||
            (node >= BG_AV_NODES_DUNBALDAR_SOUTH && node <= BG_AV_NODES_STONEHEART_BUNKER);
        return node != BG_AV_NODES_SNOWFALL_GRAVE && (alliance == (team == ALLIANCE));
    }
    NodeState ReadState(BattleGround* bg, Team team, unsigned node)
    {
        const uint8 own = GetTeamIndexByTeamId(team) * BG_AV_MAX_STATES;
        const uint8 enemy = GetTeamIndexByTeamId(bg->GetOtherTeam(team)) * BG_AV_MAX_STATES;
        if (bg->IsActiveEvent(node, own + POINT_CONTROLLED)) return NodeState::FriendlyOwned;
        if (bg->IsActiveEvent(node, own + POINT_ASSAULTED)) return NodeState::FriendlyAssault;
        if (bg->IsActiveEvent(node, enemy + POINT_CONTROLLED)) return NodeState::EnemyOwned;
        if (bg->IsActiveEvent(node, enemy + POINT_ASSAULTED)) return NodeState::EnemyAssault;
        if (bg->IsActiveEvent(node, BG_AV_NODE_STATUS_NEUTRAL_OCCUPIED)) return NodeState::Neutral;
        return NodeState::Unknown;
    }
    bool Capturing(Player* member)
    {
        Spell* spell = member->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        return spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_spellInfo->Id == 21651;
    }
    unsigned FriendlyCount(Player* bot, const Point& point, float radius)
    {
        unsigned count = 0;
        for (const auto& ref : bot->GetMap()->GetPlayers())
        {
            Player* member = ref.getSource();
            if (member && member->IsAlive() && member->IsInWorld() && member->GetTeam() == bot->GetTeam() &&
                !member->IsInCombat() && Distance(Location(member), point) <= radius) ++count;
        }
        return count;
    }
    void ReadPositions(TeamPlan& plan)
    {
        for (unsigned node = 0; node < NodeCount; ++node)
        {
            WorldLocation location;
            plan.valid[node] = sRandomPlayerbotMgr.GetNamedLocation(nodeNames[node], location) && location.mapid == 30;
            if (plan.valid[node]) plan.positions[node] = {location.coord_x, location.coord_y, location.coord_z};
        }
        plan.positionsRead = true;
    }
    void AddCaptainAndMine(TeamPlan& plan, Player* bot, uint32 now)
    {
        BattleGround* bg = bot->GetBattleGround();
        const bool alliance = bot->GetTeam() == ALLIANCE;
        const uint8 deathEvent = alliance ? BG_AV_NODE_CAPTAIN_DEAD_H : BG_AV_NODE_CAPTAIN_DEAD_A;
        const uint8 captainEvent = alliance ? BG_AV_CAPTAIN_H : BG_AV_CAPTAIN_A;
        if (!bg->IsActiveEvent(deathEvent, 0))
            if (Creature* captain = bot->GetMap()->GetCreature(bg->GetSingleCreatureGuid(captainEvent, 0)))
                if (captain->IsAlive())
                {
                    WorldLocation location;
                    const char* waiting = alliance ? "AV_ICEBLOOD_GARRISON_WAITING_ALLIANCE" : "AV_STONEHEART_OUTPOST_WAITING_HORDE";
                    if (sRandomPlayerbotMgr.GetNamedLocation(waiting, location) && location.mapid == 30)
                    {
                        Point point{location.coord_x, location.coord_y, location.coord_z};
                        unsigned survivors = 0;
                        for (const auto& ref : bot->GetMap()->GetPlayers())
                        {
                            Player* member = ref.getSource();
                            if (!member || !member->IsAlive() || !member->IsInWorld() ||
                                member->GetTeam() != bot->GetTeam()) continue;
                            const auto assigned = plan.assignments.find(member->GetObjectGuid().GetRawValue());
                            if (assigned != plan.assignments.end() && assigned->second.task == Task::Captain) ++survivors;
                        }
                        const bool fighting = captain->IsInCombat();
                        // Evade after a wipe ends the committed pull even if
                        // our first subsequent refresh is a resurrected member.
                        if (plan.captainCommitted && !fighting &&
                            (!survivors || plan.captainWasInCombat ||
                             (plan.ready && uint32(now - plan.updated) > 10000)))
                            plan.captainCommitted = false;
                        if (FriendlyCount(bot, point, 10.0f) >= 5 || fighting) plan.captainCommitted = true;
                        plan.captainWasInCombat = fighting;
                        if (plan.captainCommitted) point = Location(captain);
                        plan.objectives.push_back({CaptainObjective, Task::Captain, point,
                            plan.captainCommitted ? 1u : 5u, false});
                    }
                }

        // Retain opportunistic mine work without pulling remote bots away from
        // towers or a threatened graveyard. Distance is rechecked per member.
        const uint8 mine = alliance ? BG_AV_MINE_BOSSES_SOUTH : BG_AV_MINE_BOSSES_NORTH;
        const uint8 enemy = GetTeamIndexByTeamId(bg->GetOtherTeam(bot->GetTeam()));
        for (uint8 owner : {uint8(TEAM_INDEX_NEUTRAL), enemy})
            if (bg->IsActiveEvent(mine, owner))
                if (Creature* boss = bot->GetMap()->GetCreature(bg->GetSingleCreatureGuid(mine, owner)))
                    if (boss->IsAlive()) plan.objectives.push_back({MineObjective, Task::Mine, Location(boss), 2, false});
    }
    void Refresh(TeamPlan& plan, Player* bot, uint32 now)
    {
        BattleGround* bg = bot->GetBattleGround();
        if (plan.ready && bg->GetStartTime() < plan.battleTime) plan = {};
        plan.seen = now;
        plan.battleTime = bg->GetStartTime();
        std::array<NodeState, NodeCount> states;
        for (unsigned node = 0; node < NodeCount; ++node) states[node] = ReadState(bg, bot->GetTeam(), node);
        if (plan.ready && states == plan.states && uint32(now - plan.updated) < 1000) return;
        if (!plan.positionsRead) ReadPositions(plan);
        plan.states = states;
        plan.objectives.clear();
        for (unsigned node = 0; node < NodeCount; ++node)
        {
            if (!plan.valid[node]) continue;
            const bool original = InitiallyFriendly(bot->GetTeam(), node);
            if (states[node] == NodeState::FriendlyOwned) plan.ownedBefore[node] = true;
            if (original && states[node] != NodeState::Unknown) plan.ownedBefore[node] = true;
            if (IsTower(node))
            {
                if (original && states[node] == NodeState::EnemyAssault)
                    plan.objectives.push_back({node, Task::Defend, plan.positions[node], 4, true});
                else if (!original && states[node] == NodeState::FriendlyAssault)
                    plan.objectives.push_back({node, Task::Guard, plan.positions[node], 2, false});
                else if (!original && states[node] == NodeState::EnemyOwned)
                    plan.objectives.push_back({node, Task::Attack, plan.positions[node], 6, false});
                // An enemy tower controlled by us is burnt, and our tower
                // controlled by the enemy is lost permanently: neither is a flag.
                continue;
            }
            // With two teams, an enemy-contested non-neutral graveyard was
            // controlled by us even when this planner joined after its assault.
            if (states[node] == NodeState::EnemyAssault &&
                (node != BG_AV_NODES_SNOWFALL_GRAVE || plan.ownedBefore[node]))
                plan.objectives.push_back({node, Task::Defend, plan.positions[node], node == 0 || node == 6 ? 4u : 3u, true});
            else if (states[node] == NodeState::EnemyOwned && plan.ownedBefore[node])
                plan.objectives.push_back({node, Task::Defend, plan.positions[node], 3, false});
            else if (states[node] == NodeState::FriendlyAssault)
                plan.objectives.push_back({node, Task::Guard, plan.positions[node], 2, false});
            else if (states[node] == NodeState::EnemyOwned || states[node] == NodeState::Neutral ||
                (states[node] == NodeState::EnemyAssault && node == BG_AV_NODES_SNOWFALL_GRAVE))
                plan.objectives.push_back({node, Task::Attack, plan.positions[node], 6, false});
        }
        const bool alliance = bot->GetTeam() == ALLIANCE;
        const std::array<unsigned, 4> towers = alliance ? std::array<unsigned, 4>{11, 12, 13, 14} : std::array<unsigned, 4>{7, 8, 9, 10};
        std::array<NodeState, 4> towerStates;
        for (unsigned i = 0; i < 4; ++i) towerStates[i] = states[towers[i]];
        const unsigned terminal = alliance ? BG_AV_NODES_FROSTWOLF_HUT : BG_AV_NODES_FIRSTAID_STATION;
        bool bossReady = TowersReady(towerStates) && states[terminal] == NodeState::FriendlyOwned;
        Creature* boss = bossReady ? bot->GetMap()->GetCreature(bg->GetSingleCreatureGuid(alliance ? BG_AV_BOSS_H : BG_AV_BOSS_A, 0)) : nullptr;
        if ((boss && boss->IsAlive()) || (bossReady && !boss && plan.valid[terminal]))
        {
            plan.objectives.erase(std::remove_if(plan.objectives.begin(), plan.objectives.end(),
                [](const Objective& objective) { return !IsGuardTask(objective.task); }), plan.objectives.end());
            // A remote boss grid may not exist yet. Its captured terminal
            // graveyard is public and close enough to load the final approach;
            // a subsequent refresh replaces this with the live boss position.
            // A known dead boss must never be replaced by an invented target.
            plan.objectives.push_back({BossObjective, Task::Boss,
                boss ? Location(boss) : plan.positions[terminal], 40, false});
        }
        else AddCaptainAndMine(plan, bot, now);

        std::vector<Member> members;
        for (const auto& ref : bot->GetMap()->GetPlayers())
        {
            Player* member = ref.getSource();
            if (!member || !member->IsInWorld() || member->IsBeingTeleported() || member->GetTeam() != bot->GetTeam()) continue;
            PlayerbotAI* ai = member->GetPlayerbotAI();
            members.push_back({member->GetObjectGuid().GetRawValue(), Location(member), ai && !ai->IsRealPlayer(),
                member->IsAlive(), member->IsInCombat(), Capturing(member)});
        }
        plan.assignments = plan.planner.Update(plan.objectives, members, now);
        plan.updated = now;
        plan.ready = true;
    }
    TeamPlan& GetPlan(Player* bot)
    {
        const uint32 now = WorldTimer::getMSTime();
        for (auto it = plans.begin(); it != plans.end();)
            if (uint32(now - it->second.seen) > 5 * MINUTE * IN_MILLISECONDS) it = plans.erase(it);
            else ++it;
        TeamPlan& plan = plans[{bot->GetMapId(), bot->GetInstanceId(), bot->GetTeam()}];
        Refresh(plan, bot, now);
        return plan;
    }
}
#endif

bool IsActive(Player* player)
{
#ifdef MANGOSBOT_ZERO
    BattleGround* bg = player ? player->GetBattleGround() : nullptr;
    return player && player->IsInWorld() && !player->IsBeingTeleported() && player->GetMapId() == 30 &&
        bg && bg->GetTypeId() == BATTLEGROUND_AV && bg->GetStatus() == STATUS_IN_PROGRESS;
#else
    return false;
#endif
}

bool SelectObjective(PlayerbotAI* ai, WorldLocation& objective)
{
#ifdef MANGOSBOT_ZERO
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!IsActive(bot) || !bot->IsAlive() || ai->IsRealPlayer()) return false;
    std::lock_guard<std::mutex> lock(plansMutex);
    TeamPlan& plan = GetPlan(bot);
    auto* context = ai->GetAiObjectContext();
    context->GetValue<bool>("manual bool", "alterac assigned")->Set(false);
    context->GetValue<bool>("manual bool", "alterac defender")->Set(false);
    const auto assigned = plan.assignments.find(bot->GetObjectGuid().GetRawValue());
    if (assigned == plan.assignments.end())
    {
        context->GetValue<PositionMap&>("position")->Get()["bg objective"].Reset();
        return false;
    }
    const Assignment& task = assigned->second;
    for (const Objective& goal : plan.objectives)
        if (goal.id == task.id && goal.task == task.task)
        {
            objective = WorldLocation(bot->GetMapId(), goal.position.x, goal.position.y, goal.position.z);
            context->GetValue<PositionMap&>("position")->Get()["bg objective"].Set(
                goal.position.x, goal.position.y, goal.position.z, bot->GetMapId());
            context->GetValue<bool>("manual bool", "alterac assigned")->Set(true);
            context->GetValue<bool>("manual bool", "alterac defender")->Set(IsGuardTask(task.task));
            context->GetValue<int>("manual int", "alterac node")->Set(task.id < NodeCount ? int(task.id) : int(NoNode));
            context->GetValue<int>("manual int", "alterac task")->Set(int(task.task));
            return true;
        }
#endif
    return false;
}

unsigned GetAssignedNode(PlayerbotAI* ai)
{
    if (!ai || !IsActive(ai->GetBot()) || !ai->GetAiObjectContext()->GetValue<bool>("manual bool", "alterac assigned")->Get())
        return NoNode;
    const int node = ai->GetAiObjectContext()->GetValue<int>("manual int", "alterac node")->Get();
    return node >= 0 && node < int(NodeCount) ? unsigned(node) : NoNode;
}
bool IsDefender(PlayerbotAI* ai)
{
    return ai && !ai->IsRealPlayer() && IsActive(ai->GetBot()) &&
        ai->GetAiObjectContext()->GetValue<bool>("manual bool", "alterac defender")->Get();
}
bool IsAtAssignedNode(PlayerbotAI* ai, float radius)
{
    if (!ai || !IsActive(ai->GetBot())) return false;
    auto& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    const auto found = positions.find("bg objective");
    return found != positions.end() && found->second.isSet() && found->second.mapId == ai->GetBot()->GetMapId() &&
        std::abs(ai->GetBot()->GetPositionZ() - found->second.z) <= 8.0f &&
        Distance(Location(ai->GetBot()), {found->second.x, found->second.y, found->second.z}) <= radius;
}
bool CanUseNodeFlag(PlayerbotAI* ai, GameObject* flag)
{
#ifdef MANGOSBOT_ZERO
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!IsActive(bot) || !bot->IsAlive() || ai->IsRealPlayer() || !flag || flag->GetMap() != bot->GetMap()) return false;
    const auto event = bot->GetMap()->GetMapDataContainer().GetGameObjectEventIndex(flag->GetDbGuid());
    BattleGround* bg = bot->GetBattleGround();
    if (event.event1 >= NodeCount || !bg->IsActiveEvent(event.event1, event.event2)) return false;
    const unsigned node = event.event1;
    const NodeState state = ReadState(bg, bot->GetTeam(), node);
    if (state != NodeState::EnemyOwned && state != NodeState::EnemyAssault && state != NodeState::Neutral) return false;
    if (IsTower(node) && InitiallyFriendly(bot->GetTeam(), node) && state == NodeState::EnemyOwned) return false;
    std::lock_guard<std::mutex> lock(plansMutex);
    TeamPlan& plan = GetPlan(bot);
    if (state == NodeState::EnemyAssault && IsTower(node) && !InitiallyFriendly(bot->GetTeam(), node)) return false;
    const auto task = plan.assignments.find(bot->GetObjectGuid().GetRawValue());
    return (task != plan.assignments.end() && task->second.id == node) ||
        (state == NodeState::EnemyAssault && bot->IsWithinDistInMap(flag, INTERACTION_DISTANCE));
#else
    return false;
#endif
}
bool IsAllowedTarget(PlayerbotAI* ai, Unit* target)
{
    if (!IsDefender(ai)) return true;
    Player* bot = ai->GetBot();
    if (!target || target == bot || sServerFacade.IsFriendlyTo(bot, target)) return true;
    if (!target || !target->IsInWorld() || !target->IsAlive() || target->GetMap() != bot->GetMap()) return false;
    auto& position = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get()["bg objective"];
    if (!position.isSet() || position.mapId != bot->GetMapId()) return true;
    if (Distance(Location(target), {position.x, position.y, position.z}) <= GuardRadius + 10.0f) return true;
    bool directThreat = target->GetVictim() == bot;
    for (CurrentSpellTypes slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL})
    {
        Spell* spell = target->GetCurrentSpell(slot);
        if (spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_targets.getUnitTarget() == bot)
            directThreat = true;
    }
    return directThreat && bot->IsWithinDistInMap(target, ai->CanMove() ? 12.0f : 40.0f);
}
}
