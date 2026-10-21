#include "playerbot/playerbot.h"
#include "ArathiBasinTactics.h"
#include "ArathiBasinPolicy.h"
#include "ArathiCapturePolicy.h"
#include "playerbot/ServerFacade.h"
#include "BattleGround/BattleGroundAB.h"
#include "playerbot/strategy/values/PositionValue.h"
#include <array>
#include <map>
#include <mutex>
#include <tuple>

namespace ai
{
namespace arathi
{
namespace
{
    constexpr uint32 CaptureSpell = 21651;
    constexpr uint32 PlanIntervalMs = 3000;
    constexpr uint32 CacheExpiryMs = 5 * 60 * 1000;

    struct TeamPlan
    {
        Planner planner;
        std::array<Node, NodeCount> nodes;
        Planner::Assignments assignments;
        uint32 updated = 0;
        uint32 seen = 0;
        uint32 battleTime = 0;
        bool ready = false;
        bool positionsRead = false;
    };

    // No world pointers survive a call. Map workers may update different BGs concurrently.
    using PlanKey = std::tuple<uint32, uint32, Team>;
    std::map<PlanKey, TeamPlan> plans;
    std::mutex plansMutex;

    bool IsArathi(Player* player)
    {
        return player && player->IsInWorld() && !player->IsBeingTeleported() &&
            player->GetMapId() == 529 && player->GetBattleGround() &&
            player->GetBattleGround()->GetStatus() == STATUS_IN_PROGRESS;
    }

    Point Location(Player* player)
    {
        return {player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()};
    }

    NodeState ReadState(BattleGround* bg, Team team, unsigned node)
    {
        const auto ally = GetTeamIndexByTeamId(team);
        const auto enemy = ally == TEAM_INDEX_ALLIANCE ? TEAM_INDEX_HORDE : TEAM_INDEX_ALLIANCE;
        auto& states = bg->GetBgMap()->GetVariableManager();
        // The public world states change immediately; the banner events lag by 3 seconds.
        if (states.GetVariable(abOccupiedStates[ally][node])) return NodeState::FriendlyOwned;
        if (states.GetVariable(abContestedStates[ally][node])) return NodeState::FriendlyAssault;
        if (states.GetVariable(abOccupiedStates[enemy][node])) return NodeState::EnemyOwned;
        if (states.GetVariable(abContestedStates[enemy][node])) return NodeState::EnemyAssault;
        return NodeState::Neutral;
    }

    void ReadPositions(BattleGround* bg, std::array<Node, NodeCount>& nodes)
    {
        const std::array<uint32, NodeCount> entries = {BG_AB_BANNER_STABLE, BG_AB_BANNER_BLACKSMITH,
            BG_AB_BANNER_FARM, BG_AB_BANNER_LUMBER_MILL, BG_AB_BANNER_MINE};
        for (Node& node : nodes) node.valid = false;
        unsigned found = 0;
        // Spawn data is available before grids load. The five public flag locations
        // must not depend on a player first travelling close enough to load a banner.
        auto read = [&](const GameObjectDataPair& entry)
        {
            const GameObjectData& data = entry.second;
            if (data.mapid == bg->GetMapId())
            {
                for (unsigned node = 0; node < NodeCount; ++node)
                {
                    if (!nodes[node].valid && data.id == entries[node])
                    {
                        nodes[node].position = {data.posX, data.posY, data.posZ};
                        nodes[node].valid = true;
                        ++found;
                    }
                }
            }
            return found == NodeCount;
        };
        sObjectMgr.DoGOData(read);
    }

    void Refresh(TeamPlan& plan, Player* bot, uint32 now)
    {
        BattleGround* bg = bot->GetBattleGround();
        if (plan.ready && bg->GetStartTime() < plan.battleTime)
            plan = TeamPlan{};

        plan.seen = now;
        plan.battleTime = bg->GetStartTime();
        if (plan.ready && uint32(now - plan.updated) < PlanIntervalMs)
            return;

        if (!plan.positionsRead)
        {
            ReadPositions(bg, plan.nodes);
            plan.positionsRead = true;
        }

        std::vector<Member> members;
        std::vector<Player*> observers;
        std::vector<Player*> enemies;
        for (unsigned node = 0; node < NodeCount; ++node)
        {
            plan.nodes[node].state = ReadState(bg, bot->GetTeam(), node);
            plan.nodes[node].enemyCount = 0;
        }

        for (const auto& ref : bot->GetMap()->GetPlayers())
        {
            Player* player = ref.getSource();
            if (!player || !player->IsInWorld() || player->IsBeingTeleported())
                continue;
            if (player->GetTeam() != bot->GetTeam())
            {
                if (player->IsAlive()) enemies.push_back(player);
                continue;
            }
            const bool controlledBot = player->GetPlayerbotAI() && !player->GetPlayerbotAI()->IsRealPlayer();
            members.push_back({player->GetObjectGuid().GetRawValue(), Location(player), controlledBot,
                player->IsAlive(), player->IsInCombat(), IsCapturing(player)});
            if (player->IsAlive()) observers.push_back(player);
        }

        for (Player* enemy : enemies)
        {
            bool seen = false;
            for (Player* observer : observers)
            {
                if (observer->IsWithinDistInMap(enemy, sPlayerbotAIConfig.sightDistance) &&
                    enemy->IsVisibleForOrDetect(observer, observer->GetCamera().GetBody(), true) &&
                    sServerFacade.IsWithinLOSInMap(observer, enemy))
                {
                    seen = true;
                    break;
                }
            }
            if (!seen) continue;

            unsigned nearest = NodeCount;
            float distance = 60.0f;
            for (unsigned node = 0; node < NodeCount; ++node)
            {
                if (!plan.nodes[node].valid) continue;
                const float candidate = Distance(Location(enemy), plan.nodes[node].position);
                if (candidate < distance)
                {
                    distance = candidate;
                    nearest = node;
                }
            }
            if (nearest < NodeCount) ++plan.nodes[nearest].enemyCount;
        }

        plan.assignments = plan.planner.Update(plan.nodes, members, now);
        plan.updated = now;
        plan.ready = true;
    }

    TeamPlan& GetPlan(Player* bot)
    {
        const uint32 now = WorldTimer::getMSTime();
        for (auto it = plans.begin(); it != plans.end();)
        {
            if (uint32(now - it->second.seen) > CacheExpiryMs) it = plans.erase(it);
            else ++it;
        }
        TeamPlan& plan = plans[{bot->GetMapId(), bot->GetInstanceId(), bot->GetTeam()}];
        Refresh(plan, bot, now);
        return plan;
    }
}

bool IsCapturing(Player* player)
{
    Spell* spell = player ? player->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
    return spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_spellInfo->Id == CaptureSpell;
}

bool IsCoveringCapture(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    if (!IsArathi(bot)) return false;
    const PositionMap& positions = ai->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    const auto assigned = positions.find("bg objective");
    if (assigned == positions.end() || !assigned->second.valueSet) return false;
    for (const auto& ref : bot->GetMap()->GetPlayers())
    {
        Player* member = ref.getSource();
        if (!member || !member->IsAlive() || member->GetTeam() != bot->GetTeam() || !IsCapturing(member)) continue;
        Spell* spell = member->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        GameObject* flag = bot->GetMap()->GetGameObject(spell->m_targets.getGOTargetGuid());
        if (flag && flag->GetDistance2d(assigned->second.x, assigned->second.y) < 10.0f)
            return true;
    }
    return false;
}

bool SelectObjective(PlayerbotAI* ai, WorldLocation& objective)
{
    Player* bot = ai->GetBot();
    auto* context = ai->GetAiObjectContext();
    if (!IsArathi(bot) || !bot->IsAlive())
    {
        context->GetValue<bool>("manual bool", "arathi defender")->Set(false);
        return false;
    }

    std::lock_guard<std::mutex> lock(plansMutex);
    TeamPlan& plan = GetPlan(bot);
    const auto assigned = plan.assignments.find(bot->GetObjectGuid().GetRawValue());
    if (assigned == plan.assignments.end())
    {
        context->GetValue<bool>("manual bool", "arathi defender")->Set(false);
        context->GetValue<PositionMap&>("position")->Get()["bg objective"].Reset();
        return false;
    }
    const Assignment& task = assigned->second;
    const Point& point = plan.nodes[task.node].position;
    objective = WorldLocation(bot->GetMapId(), point.x, point.y, point.z);
    context->GetValue<PositionMap&>("position")->Get()["bg objective"].Set(point.x, point.y, point.z, bot->GetMapId());
    context->GetValue<bool>("manual bool", "arathi defender")->Set(task.defend);
    return true;
}

bool CanCapture(PlayerbotAI* ai, GameObject* flag)
{
    Player* bot = ai->GetBot();
    if (!IsArathi(bot) || !flag || flag->GetMap() != bot->GetMap() || !bot->CanUseBattleGroundObject() ||
        IsCapturing(bot) || !sServerFacade.isSpawned(flag) || flag->IsInUse() || flag->GetGoState() != GO_STATE_READY ||
        !bot->CanInteract(flag) || !bot->IsWithinDistInMap(flag, INTERACTION_DISTANCE))
        return false;

    const auto event = bot->GetMap()->GetMapDataContainer().GetGameObjectEventIndex(flag->GetDbGuid());
    BattleGround* bg = bot->GetBattleGround();
    if (event.event1 >= NodeCount || !bg->IsActiveEvent(event.event1, event.event2)) return false;
    const NodeState state = ReadState(bg, bot->GetTeam(), event.event1);
    if (state == NodeState::FriendlyOwned || state == NodeState::FriendlyAssault) return false;

    std::lock_guard<std::mutex> lock(plansMutex);
    TeamPlan& plan = GetPlan(bot);
    std::vector<CaptureCandidate> candidates;
    for (const auto& ref : bot->GetMap()->GetPlayers())
    {
        Player* member = ref.getSource();
        if (!member || member->GetTeam() != bot->GetTeam() || !member->IsAlive() ||
            !member->IsInWorld() || member->IsBeingTeleported()) continue;
        CaptureCandidate candidate{member->GetObjectGuid().GetRawValue(), member->GetDistance(flag)};
        if (Spell* spell = member->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        {
            candidate.capturingSameFlag = spell->m_spellInfo->Id == CaptureSpell &&
                spell->getState() != SPELL_STATE_FINISHED && spell->m_targets.getGOTargetGuid() == flag->GetObjectGuid();
        }
        const auto assigned = plan.assignments.find(member->GetObjectGuid().GetRawValue());
        candidate.eligible = member->GetPlayerbotAI() && !member->GetPlayerbotAI()->IsRealPlayer() &&
            member->GetPlayerbotAI()->CanMove() &&
            member->CanUseBattleGroundObject() && member->CanInteract(flag) &&
            member->getAttackers().empty() && !member->IsNonMeleeSpellCasted(false) &&
            member->IsWithinDistInMap(flag, INTERACTION_DISTANCE) && sServerFacade.IsWithinLOSInMap(member, flag) &&
            assigned != plan.assignments.end() && assigned->second.node == event.event1;
        candidates.push_back(candidate);
    }
    return ElectCapturer(candidates) == bot->GetObjectGuid().GetRawValue();
}
}
}
