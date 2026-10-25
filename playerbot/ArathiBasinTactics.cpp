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

    void DelayCapture(PlayerbotAI* ai)
    {
        ai->GetAiObjectContext()->GetValue<int32>("manual int", "arathi capture threat time")
            ->Set(int32(WorldTimer::getMSTime()));
    }

    bool CanAttemptCapture(PlayerbotAI* ai)
    {
        if (GetImmediateCaptureThreat(ai))
        {
            DelayCapture(ai);
            return false;
        }
        const uint32 lastThreat = uint32(ai->GetAiObjectContext()->GetValue<int32>(
            "manual int", "arathi capture threat time")->Get());
        return !IsCaptureRetryHeld(lastThreat, WorldTimer::getMSTime());
    }
}

bool IsCapturing(Player* player)
{
    Spell* spell = player ? player->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
    return spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_spellInfo->Id == CaptureSpell;
}

void RecordCaptureStart(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (IsArathi(bot) && bot->IsAlive())
        ai->GetAiObjectContext()->GetValue<int32>("manual int", "arathi capture health")
            ->Set(int32(bot->GetHealth()));
}

Unit* GetImmediateCaptureThreat(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!IsArathi(bot) || !bot->IsAlive())
        return nullptr;

    Unit* nearest = nullptr;
    float nearestDistance = CaptureThreatRadius + 1.0f;
    const auto consider = [&](Unit* enemy)
    {
        if (!enemy || enemy == bot || !enemy->IsInWorld() || enemy->GetMap() != bot->GetMap() ||
            !enemy->IsAlive() || !bot->CanAttack(enemy))
            return;
        const float distance = bot->GetDistance(enemy);
        if (distance > nearestDistance || distance > CaptureThreatRadius)
            return;

        bool castingAtBot = false;
        for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL })
        {
            Spell* spell = enemy->GetCurrentSpell(type);
            if (spell && spell->getState() != SPELL_STATE_FINISHED && spell->m_targets.getUnitTarget() == bot)
            {
                castingAtBot = true;
                break;
            }
        }
        const CaptureThreat threat = { distance, true,
            enemy->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true),
            sServerFacade.IsWithinLOSInMap(bot, enemy),
            enemy->HasAuraType(SPELL_AURA_MOD_STUN) || enemy->HasAuraType(SPELL_AURA_MOD_CONFUSE) ||
                enemy->HasAuraType(SPELL_AURA_MOD_FEAR),
            enemy->GetVictim() == bot, castingAtBot };
        if (IsImmediateCaptureThreat(threat))
        {
            nearest = enemy;
            nearestDistance = distance;
        }
    };

    // A ranged cast may not yet have entered getAttackers(). Read live players
    // and their pets as well; do not depend on cached or defender-filtered lists.
    for (Unit* attacker : bot->getAttackers())
        consider(attacker);
    for (const auto& ref : bot->GetMap()->GetPlayers())
    {
        Player* player = ref.getSource();
        if (!player || player->GetTeam() == bot->GetTeam())
            continue;
        consider(player);
        consider(player->GetPet());
    }
    return nearest;
}

bool UpdateCaptureResponse(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!IsArathi(bot))
        return false;

    auto* context = ai->GetAiObjectContext();
    auto* health = context->GetValue<int32>("manual int", "arathi capture health");
    if (!bot->IsAlive())
    {
        health->Set(0);
        return false;
    }
    const uint32 previousHealth = uint32(health->Get());
    const uint32 currentHealth = bot->GetHealth();
    const bool capturing = IsCapturing(bot);
    if (!capturing && !previousHealth)
        return false;
    const bool damaged = previousHealth && currentHealth < previousHealth;
    // Healing raises the reference too, so later damage is still detected. A
    // native damage interruption may finish the cast before this outer update.
    health->Set(capturing ? int32(currentHealth) : 0);
    if (!capturing && !damaged)
        return false;
    Unit* threat = GetImmediateCaptureThreat(ai);
    if (!threat && !damaged)
        return false;

    // Interrupt this banner cast only. The outer AI update wakes its ordinary
    // combat engine immediately, even if it was sleeping for the full cast time.
    if (capturing)
        bot->InterruptSpell(CURRENT_GENERIC_SPELL);
    health->Set(0);
    DelayCapture(ai);
    context->GetValue<std::list<ObjectGuid>>("enemy player targets")->Reset();
    context->GetValue<bool>("has enemy player targets")->Reset();
    context->GetValue<ObjectGuid>("enemy player target")->Reset();
    if (threat)
    {
        const ObjectGuid guid = threat->GetObjectGuid();
        bot->SetSelectionGuid(guid);
        bot->SetTarget(threat);
        context->GetValue<ObjectGuid>("enemy player target")->Set(guid);
        context->GetValue<ObjectGuid>("current target")->Set(guid);
        context->GetValue<ObjectGuid>("attack target")->Set(guid);
    }
    return true;
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
        !bot->CanInteract(flag) || !bot->IsWithinDistInMap(flag, INTERACTION_DISTANCE) || !CanAttemptCapture(ai))
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
            !member->IsNonMeleeSpellCasted(false) &&
            member->IsWithinDistInMap(flag, INTERACTION_DISTANCE) && sServerFacade.IsWithinLOSInMap(member, flag) &&
            assigned != plan.assignments.end() && assigned->second.node == event.event1 &&
            CanAttemptCapture(member->GetPlayerbotAI());
        candidates.push_back(candidate);
    }
    return ElectCapturer(candidates) == bot->GetObjectGuid().GetRawValue();
}
}
}
