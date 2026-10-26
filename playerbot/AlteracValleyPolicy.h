#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace ai::alterac
{
    constexpr unsigned NodeCount = 15;
    constexpr unsigned NoNode = NodeCount;
    constexpr std::uint32_t AssignmentHoldMs = 30000;
    constexpr float GuardRadius = 40.0f;
    enum class Task { Attack, Guard, Defend, Captain, Boss, Mine };
    enum class NodeState { Unknown, Neutral, FriendlyOwned, FriendlyAssault, EnemyOwned, EnemyAssault };

    struct Point { float x = 0, y = 0, z = 0; };
    inline float Distance(const Point& a, const Point& b)
    {
        const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
        return std::sqrt(x * x + y * y + z * z);
    }
    inline bool AtGuardPosition(const Point& member, const Point& flag)
    {
        return Distance(member, flag) <= GuardRadius && std::abs(member.z - flag.z) <= 8.0f;
    }

    struct Objective
    {
        unsigned id = NoNode;
        Task task = Task::Attack;
        Point position;
        unsigned wanted = 2;
        bool urgent = false;
    };
    struct Member
    {
        std::uint64_t guid = 0;
        Point position;
        bool isBot = false, alive = true, inCombat = false, capturing = false;
    };
    struct Assignment
    {
        unsigned id = NoNode;
        Task task = Task::Attack;
        std::uint32_t assignedAt = 0;
    };

    inline bool IsGuardTask(Task task) { return task == Task::Guard || task == Task::Defend; }
    inline bool TowersReady(const std::array<NodeState, 4>& enemyTowers)
    {
        return std::all_of(enemyTowers.begin(), enemyTowers.end(),
            [](NodeState state) { return state == NodeState::FriendlyOwned; });
    }

    class Planner
    {
    public:
        using Assignments = std::map<std::uint64_t, Assignment>;
        const Assignments& Update(const std::vector<Objective>& objectives,
            const std::vector<Member>& members, std::uint32_t now)
        {
            const Assignments previous = assignments;
            assignments.clear();
            std::map<std::uint64_t, const Member*> bots;
            for (const Member& member : members)
                if (member.isBot && member.alive) bots[member.guid] = &member;
            std::map<unsigned, unsigned> counts;
            const auto findObjective = [&](unsigned id, Task task) -> const Objective*
            {
                for (const Objective& objective : objectives)
                    if (objective.id == id && objective.task == task) return &objective;
                return nullptr;
            };
            const auto put = [&](std::uint64_t guid, const Objective& objective)
            {
                auto old = previous.find(guid);
                const bool same = old != previous.end() && old->second.id == objective.id && old->second.task == objective.task;
                assignments[guid] = {objective.id, objective.task, same ? old->second.assignedAt : now};
                ++counts[objective.id];
            };

            // Do not pull a live opener away in the middle of its flag cast.
            for (const auto& entry : bots)
            {
                auto old = previous.find(entry.first);
                if (entry.second->capturing && old != previous.end())
                    if (const Objective* objective = findObjective(old->second.id, old->second.task)) put(entry.first, *objective);
            }
            // Do not leave a live assault banner empty while its replacement
            // is still travelling. Keep one physically present guard until the
            // objective completes, even when another node needs assistance.
            for (const Objective& objective : objectives)
            {
                if (objective.task != Task::Guard || counts[objective.id]) continue;
                bool human = false;
                for (const Member& member : members)
                    if (!member.isBot && member.alive && AtGuardPosition(member.position, objective.position)) human = true;
                if (human) continue;
                std::uint64_t nearest = 0;
                float cost = std::numeric_limits<float>::max();
                for (const auto& entry : bots)
                {
                    if (assignments.count(entry.first)) continue;
                    float candidate = Distance(entry.second->position, objective.position);
                    if (!AtGuardPosition(entry.second->position, objective.position)) continue;
                    auto old = previous.find(entry.first);
                    if (old != previous.end() && old->second.id == objective.id && old->second.task == Task::Guard) candidate -= 100.0f;
                    if (candidate < cost) { cost = candidate; nearest = entry.first; }
                }
                if (nearest) put(nearest, objective);
            }

            const auto fill = [&](const Objective& objective)
            {
                unsigned humans = 0;
                for (const Member& member : members)
                    if (IsGuardTask(objective.task) && !member.isBot && member.alive &&
                        AtGuardPosition(member.position, objective.position)) ++humans;
                const unsigned wanted = objective.wanted > humans ? objective.wanted - humans : 0;
                while (counts[objective.id] < wanted)
                {
                    std::uint64_t best = 0;
                    float cost = std::numeric_limits<float>::max();
                    for (const auto& entry : bots)
                    {
                        if (assignments.count(entry.first)) continue;
                        const Member& member = *entry.second;
                        float candidate = Distance(member.position, objective.position);
                        auto old = previous.find(entry.first);
                        if (old != previous.end() && old->second.id == objective.id && old->second.task == objective.task) candidate -= 120.0f;
                        if (member.inCombat && candidate > GuardRadius) candidate += 200.0f;
                        if (candidate < cost) { best = entry.first; cost = candidate; }
                    }
                    if (!best) break;
                    put(best, objective);
                }
            };
            // Relieve threatened friendly nodes before preserving ordinary road
            // assignments. New orders are taken by the nearest available bots.
            for (;;)
            {
                const Objective* bestObjective = nullptr;
                std::uint64_t bestMember = 0;
                float bestCost = std::numeric_limits<float>::max();
                for (const Objective& objective : objectives)
                {
                    if (objective.task != Task::Defend || !objective.urgent) continue;
                    unsigned humans = 0;
                    for (const Member& member : members)
                        if (!member.isBot && member.alive && AtGuardPosition(member.position, objective.position)) ++humans;
                    if (counts[objective.id] + humans >= objective.wanted) continue;
                    for (const auto& entry : bots)
                    {
                        if (assignments.count(entry.first)) continue;
                        float candidate = Distance(entry.second->position, objective.position);
                        auto old = previous.find(entry.first);
                        if (old != previous.end() && old->second.id == objective.id && old->second.task == objective.task) candidate -= 120.0f;
                        if (entry.second->inCombat && candidate > GuardRadius) candidate += 200.0f;
                        if (candidate < bestCost)
                        { bestCost = candidate; bestObjective = &objective; bestMember = entry.first; }
                    }
                }
                if (!bestMember) break;
                put(bestMember, *bestObjective);
            }
            for (const Objective& objective : objectives)
                if (IsGuardTask(objective.task) && !(objective.task == Task::Defend && objective.urgent)) fill(objective);

            // A fresh captain group must be large enough to leave its waiting
            // point. A committed engagement (wanted == 1) may accept survivors
            // and reinforcements without forcing them back to that point.
            for (const Objective& objective : objectives)
                if (objective.task == Task::Captain)
                {
                    const unsigned free = unsigned(bots.size() - assignments.size());
                    if (objective.wanted <= 1 || free >= objective.wanted) fill(objective);
                }

            for (const auto& entry : bots)
            {
                if (assignments.count(entry.first)) continue;
                auto old = previous.find(entry.first);
                if (old == previous.end() || std::uint32_t(now - old->second.assignedAt) >= AssignmentHoldMs) continue;
                if (const Objective* objective = findObjective(old->second.id, old->second.task))
                    if (!IsGuardTask(objective->task) && (objective->task != Task::Mine ||
                        Distance(entry.second->position, objective->position) <= 150.0f) &&
                        (objective->task != Task::Captain || objective->wanted <= 1 ||
                         counts[objective->id] >= objective->wanted)) put(entry.first, *objective);
            }
            for (const auto& entry : bots)
            {
                if (assignments.count(entry.first)) continue;
                const Objective* best = nullptr;
                float cost = std::numeric_limits<float>::max();
                for (const Objective& objective : objectives)
                {
                    if (IsGuardTask(objective.task)) continue;
                    if (objective.task == Task::Mine && Distance(entry.second->position, objective.position) > 150.0f) continue;
                    if (objective.task == Task::Captain && objective.wanted > 1) continue;
                    float candidate = Distance(entry.second->position, objective.position) +
                        counts[objective.id] * (objective.task == Task::Boss ? 0.0f : 150.0f);
                    if (objective.task == Task::Mine) candidate += 500.0f;
                    auto old = previous.find(entry.first);
                    if (old != previous.end() && old->second.id == objective.id && old->second.task == objective.task) candidate -= 100.0f;
                    if (candidate < cost) { best = &objective; cost = candidate; }
                }
                // A tiny team may have nothing left beyond an urgent defence.
                // Keep it useful there without sending every large team to it.
                if (!best)
                    for (const Objective& objective : objectives)
                        if ((objective.task != Task::Mine || Distance(entry.second->position, objective.position) <= 150.0f) &&
                            (objective.task != Task::Captain || objective.wanted <= 1) &&
                            (!best || Distance(entry.second->position, objective.position) < cost))
                        { best = &objective; cost = Distance(entry.second->position, objective.position); }
                if (best) put(entry.first, *best);
            }
            return assignments;
        }
    private:
        Assignments assignments;
    };
}
