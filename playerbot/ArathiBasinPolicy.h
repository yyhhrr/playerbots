#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace ai
{
    namespace arathi
    {
        constexpr unsigned NodeCount = 5;
        constexpr float GuardRadius = 40.0f;
        constexpr uint32_t AssignmentHoldMs = 30000;

        enum class NodeState { Neutral, FriendlyAssault, FriendlyOwned, EnemyAssault, EnemyOwned };

        struct Point
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct Node
        {
            NodeState state = NodeState::Neutral;
            Point position;
            unsigned enemyCount = 0;
            bool valid = true;
        };

        struct Member
        {
            uint64_t guid = 0;
            Point position;
            bool isBot = false;
            bool alive = true;
            bool inCombat = false;
            bool capturing = false;
        };

        struct Assignment
        {
            unsigned node = 0;
            bool defend = false;
            uint32_t assignedAt = 0;
        };

        inline float Distance(const Point& first, const Point& second)
        {
            const float dx = first.x - second.x;
            const float dy = first.y - second.y;
            const float dz = first.z - second.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        // One planner belongs to one battleground instance and one faction. The
        // caller supplies visible enemy reports, never an omniscient enemy list.
        class Planner
        {
        public:
            using Assignments = std::map<uint64_t, Assignment>;

            const Assignments& Update(const std::array<Node, NodeCount>& nodes,
                const std::vector<Member>& members, uint32_t nowMs)
            {
                if (std::none_of(nodes.begin(), nodes.end(), [](const Node& node) { return node.valid; }))
                {
                    assignments.clear();
                    return assignments;
                }
                std::array<bool, NodeCount> defend{};
                for (unsigned node = 0; node < NodeCount; ++node)
                {
                    if (!nodes[node].valid)
                        continue;
                    if (nodes[node].state == NodeState::FriendlyOwned)
                        friendlyOwner[node] = true;
                    else if (nodes[node].state == NodeState::EnemyOwned || nodes[node].state == NodeState::Neutral)
                        friendlyOwner[node] = false;
                    defend[node] = nodes[node].state == NodeState::FriendlyOwned ||
                        nodes[node].state == NodeState::FriendlyAssault ||
                        (nodes[node].state == NodeState::EnemyAssault && friendlyOwner[node]);
                }

                std::map<uint64_t, const Member*> bots;
                std::array<unsigned, NodeCount> humans{};
                std::map<uint64_t, unsigned> actualNode;
                for (const Member& member : members)
                {
                    if (!member.alive)
                        continue;
                    const unsigned nearby = NearestNode(nodes, member.position);
                    if (Distance(member.position, nodes[nearby].position) <= GuardRadius)
                    {
                        if (!member.isBot)
                            ++humans[nearby];
                        else
                            actualNode[member.guid] = nearby;
                    }
                    if (member.isBot)
                        bots[member.guid] = &member;
                }

                const Assignments previous = assignments;
                assignments.clear();
                std::map<uint64_t, bool> fixed;
                for (const auto& entry : bots)
                {
                    const Member& member = *entry.second;
                    auto old = previous.find(member.guid);
                    const bool validOld = old != previous.end() && old->second.node < NodeCount && nodes[old->second.node].valid;
                    if (validOld)
                        assignments[member.guid] = {old->second.node, defend[old->second.node], old->second.assignedAt};
                    if (member.capturing || (member.inCombat && !validOld))
                        Put(member.guid, NearestNode(nodes, member.position), defend, previous, nowMs);
                    fixed[member.guid] = member.capturing || member.inCombat;
                }

                // A replacement on the road does not release the last defender
                // who is physically at the flag. Humans count only while present.
                for (unsigned node = 0; node < NodeCount; ++node)
                {
                    if (!defend[node] || humans[node])
                        continue;
                    uint64_t guard = 0;
                    float best = std::numeric_limits<float>::max();
                    for (const auto& entry : actualNode)
                    {
                        if (entry.second != node)
                            continue;
                        const Member& member = *bots.at(entry.first);
                        const auto old = assignments.find(member.guid);
                        float cost = Distance(member.position, nodes[node].position);
                        if (old != assignments.end() && old->second.node == node)
                            cost -= 100.0f;
                        if (member.capturing)
                            cost -= 1000.0f;
                        if (cost < best)
                        {
                            guard = member.guid;
                            best = cost;
                        }
                    }
                    if (guard)
                    {
                        Put(guard, node, defend, previous, nowMs);
                        fixed[guard] = true;
                    }
                }

                auto projected = [&]()
                {
                    std::array<unsigned, NodeCount> count = humans;
                    for (const auto& entry : assignments)
                        ++count[entry.second.node];
                    return count;
                };

                // Fill uncovered friendly flags before assigning attack groups.
                // An incoming guard is reserved, but is never treated as present.
                for (unsigned node = 0; node < NodeCount; ++node)
                {
                    if (!defend[node] || humans[node])
                        continue;
                    uint64_t guard = 0;
                    float best = std::numeric_limits<float>::max();
                    for (const auto& entry : assignments)
                    {
                        if (entry.second.node != node)
                            continue;
                        float distance = Distance(bots.at(entry.first)->position, nodes[node].position);
                        if (distance < best)
                        {
                            guard = entry.first;
                            best = distance;
                        }
                    }
                    if (!guard)
                    {
                        const auto counts = projected();
                        guard = Reinforcement(node, nodes, bots, assignments, fixed, counts, defend, nowMs, false);
                        if (guard)
                            Put(guard, node, defend, previous, nowMs);
                    }
                    if (guard)
                        fixed[guard] = true;
                }

                // A flag that is still being contested needs an opener and a
                // cover player. Reserve the second player only after every
                // friendly flag has had a chance to receive its first guard.
                for (unsigned node = 0; node < NodeCount; ++node)
                {
                    if (!defend[node] || (nodes[node].state != NodeState::FriendlyAssault &&
                        nodes[node].state != NodeState::EnemyAssault))
                        continue;
                    unsigned reserved = humans[node];
                    for (const auto& entry : assignments)
                        if (entry.second.node == node && fixed[entry.first])
                            ++reserved;
                    while (reserved < 2)
                    {
                        uint64_t cover = 0;
                        float best = std::numeric_limits<float>::max();
                        for (const auto& entry : assignments)
                        {
                            if (entry.second.node != node || fixed[entry.first])
                                continue;
                            const float distance = Distance(bots.at(entry.first)->position, nodes[node].position);
                            if (distance < best)
                            {
                                cover = entry.first;
                                best = distance;
                            }
                        }
                        if (!cover)
                        {
                            cover = Reinforcement(node, nodes, bots, assignments, fixed,
                                projected(), defend, nowMs, true);
                            if (cover)
                                Put(cover, node, defend, previous, nowMs);
                        }
                        if (!cover)
                            break;
                        fixed[cover] = true;
                        ++reserved;
                    }
                }

                // Nearby enemies justify overriding ordinary assignment cooldowns.
                // Count committed reinforcements so each update does not dispatch
                // another full group to the same report.
                for (unsigned node = 0; node < NodeCount; ++node)
                {
                    if (!defend[node] || !nodes[node].enemyCount)
                        continue;
                    const unsigned wanted = nodes[node].enemyCount + 1;
                    auto counts = projected();
                    while (counts[node] < wanted)
                    {
                        uint64_t reinforcement = Reinforcement(node, nodes, bots, assignments, fixed,
                            counts, defend, nowMs, true);
                        if (!reinforcement)
                            break;
                        Put(reinforcement, node, defend, previous, nowMs);
                        fixed[reinforcement] = true;
                        counts = projected();
                    }
                    // Keep the useful group in place even after its cooldown.
                    unsigned kept = humans[node];
                    for (const auto& entry : assignments)
                        if (entry.second.node == node && fixed[entry.first])
                            ++kept;
                    for (const auto& entry : assignments)
                        if (entry.second.node == node && kept < wanted && !fixed[entry.first])
                        {
                            fixed[entry.first] = true;
                            ++kept;
                        }
                }

                std::vector<uint64_t> available;
                for (const auto& entry : bots)
                {
                    const auto current = assignments.find(entry.first);
                    if (fixed[entry.first] || (current != assignments.end() &&
                        uint32_t(nowMs - current->second.assignedAt) < AssignmentHoldMs))
                        continue;
                    assignments.erase(entry.first);
                    available.push_back(entry.first);
                }

                auto counts = projected();
                for (uint64_t guid : available)
                {
                    const Member& member = *bots.at(guid);
                    const auto old = previous.find(guid);
                    unsigned selected = 0;
                    float best = -std::numeric_limits<float>::max();
                    for (unsigned node = 0; node < NodeCount; ++node)
                    {
                        if (!nodes[node].valid)
                            continue;
                        // Attack groups favor uncommitted objectives; safe flags
                        // keep a small garrison rather than absorbing the team.
                        const float value = defend[node] ? (nodes[node].enemyCount ? 7.0f : 1.0f) :
                            (nodes[node].state == NodeState::Neutral ? 6.0f : 5.0f);
                        float score = value / float(counts[node] + 1) -
                            std::min(Distance(member.position, nodes[node].position), 3000.0f) / 1500.0f;
                        if (old != previous.end() && old->second.node == node)
                            score += 0.30f;
                        if (score > best)
                        {
                            selected = node;
                            best = score;
                        }
                    }
                    Put(guid, selected, defend, previous, nowMs);
                    ++counts[selected];
                }
                return assignments;
            }

            void Clear()
            {
                assignments.clear();
                friendlyOwner.fill(false);
            }

        private:
            static unsigned NearestNode(const std::array<Node, NodeCount>& nodes, const Point& point)
            {
                unsigned nearest = NodeCount;
                for (unsigned node = 0; node < NodeCount; ++node)
                    if (nodes[node].valid && (nearest == NodeCount ||
                        Distance(point, nodes[node].position) < Distance(point, nodes[nearest].position)))
                        nearest = node;
                return nearest;
            }

            static uint64_t Reinforcement(unsigned node, const std::array<Node, NodeCount>& nodes,
                const std::map<uint64_t, const Member*>& bots, const Assignments& current,
                const std::map<uint64_t, bool>& fixed, const std::array<unsigned, NodeCount>& counts,
                const std::array<bool, NodeCount>& defend, uint32_t nowMs, bool protectThreats)
            {
                uint64_t selected = 0;
                float best = std::numeric_limits<float>::max();
                for (const auto& entry : bots)
                {
                    if (fixed.at(entry.first))
                        continue;
                    auto old = current.find(entry.first);
                    float cost = Distance(entry.second->position, nodes[node].position);
                    if (old != current.end())
                    {
                        const unsigned from = old->second.node;
                        if (from == node || (defend[from] && counts[from] <= 1))
                            continue;
                        if (protectThreats && defend[from] && nodes[from].enemyCount &&
                            counts[from] <= nodes[from].enemyCount + 1)
                            continue;
                        if (uint32_t(nowMs - old->second.assignedAt) < AssignmentHoldMs)
                            cost += 100.0f;
                    }
                    if (cost < best)
                    {
                        selected = entry.first;
                        best = cost;
                    }
                }
                return selected;
            }

            void Put(uint64_t guid, unsigned node, const std::array<bool, NodeCount>& defend,
                const Assignments& previous, uint32_t nowMs)
            {
                auto old = previous.find(guid);
                assignments[guid] = {node, defend[node],
                    old != previous.end() && old->second.node == node ? old->second.assignedAt : nowMs};
            }

            Assignments assignments;
            std::array<bool, NodeCount> friendlyOwner{};
        };
    }
}
