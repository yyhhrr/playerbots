#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace ai
{
    namespace warsong
    {
        constexpr uint32_t AssignmentHoldMs = 30000;
        constexpr float SupportRadius = 40.0f;

        struct Point
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        enum class FlagState { Base, Carried, Dropped, Respawning };

        struct Flag
        {
            FlagState state = FlagState::Base;
            Point position;
            uint64_t carrier = 0;
            bool positionKnown = false;
        };

        struct Member
        {
            uint64_t guid = 0;
            Point position;
            bool isBot = false;
            bool alive = true;
            bool healer = false;
            bool inCombat = false;
        };

        enum class Task { Attack, Escort, Recover, Carry };

        struct Assignment
        {
            Task task = Task::Attack;
            uint32_t assignedAt = 0;
            unsigned route = 0;
        };

        inline float Distance(const Point& first, const Point& second)
        {
            const float dx = first.x - second.x;
            const float dy = first.y - second.y;
            const float dz = first.z - second.z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            return std::isfinite(distance) ? distance : std::numeric_limits<float>::max();
        }

        // Keep one planner per battleground instance and faction. Flag positions
        // are supplied by the caller; unknown positions never imply map knowledge.
        class Planner
        {
        public:
            using Assignments = std::map<uint64_t, Assignment>;

            const Assignments& Update(const Flag& ownFlag, const Flag& enemyFlag,
                const std::vector<Member>& members, uint32_t now, const std::vector<uint64_t>& flagRunners = {})
            {
                const bool changed = !ready || ownState != ownFlag.state || enemyState != enemyFlag.state ||
                    ownCarrier != ownFlag.carrier || enemyCarrier != enemyFlag.carrier;
                const bool escorting = enemyFlag.state == FlagState::Carried && enemyFlag.carrier != 0;
                const bool recovering = ownFlag.state == FlagState::Carried || ownFlag.state == FlagState::Dropped;

                std::map<uint64_t, const Member*> bots;
                Point escortPosition = enemyFlag.position;
                bool escortPositionKnown = enemyFlag.positionKnown;
                for (const Member& member : members)
                {
                    if (!member.alive || !member.guid)
                        continue;
                    if (member.isBot)
                        bots[member.guid] = &member;
                    if (escorting && member.guid == enemyFlag.carrier)
                    {
                        escortPosition = member.position;
                        escortPositionKnown = true;
                    }
                }

                unsigned humanEscorts = 0;
                unsigned humanRecoverers = 0;
                for (const Member& member : members)
                {
                    if (!member.alive || !member.guid || member.isBot || member.guid == enemyFlag.carrier)
                        continue;
                    if (escorting && escortPositionKnown && Distance(member.position, escortPosition) <= SupportRadius)
                        ++humanEscorts;
                    else if (recovering && ownFlag.positionKnown && Distance(member.position, ownFlag.position) <= SupportRadius)
                        ++humanRecoverers;
                }

                const Assignments previous = assignments;
                assignments.clear();
                const auto carrier = bots.find(enemyFlag.carrier);
                if (escorting && carrier != bots.end())
                    Put(carrier->first, Task::Carry, previous, now);

                if (!escorting)
                {
                    // Reserve the nominated runners before selecting recoverers.
                    // A small team must still keep someone to return its own flag.
                    const unsigned limit = recovering && bots.size() > 1 ?
                        std::min(2u, unsigned(bots.size() - 1)) : 2u;
                    unsigned reserved = 0;
                    for (uint64_t guid : flagRunners)
                        if (reserved < limit && bots.count(guid) && !assignments.count(guid))
                        {
                            Put(guid, Task::Attack, previous, now);
                            ++reserved;
                        }
                }

                const unsigned available = unsigned(bots.size() - assignments.size());
                unsigned escorts = 0;
                if (escorting)
                {
                    // Small teams retain a protector without spending both of
                    // their remaining players on escort while their flag is lost.
                    const unsigned support = available + humanEscorts;
                    const unsigned desired = std::min(2u, recovering ? (support + 1u) / 2u : support);
                    escorts = std::min(available, desired > humanEscorts ? desired - humanEscorts : 0u);
                }
                unsigned recoverers = 0;
                if (recovering)
                {
                    const unsigned remaining = available - escorts;
                    if (escorting)
                        recoverers = remaining;
                    else
                    {
                        const unsigned desired = (available * 3u + 4u) / 5u;
                        recoverers = std::min(remaining, desired > humanRecoverers ? desired - humanRecoverers : 0u);
                    }
                    // A dropped enemy flag still needs a player to pick it up.
                    if (enemyFlag.state == FlagState::Dropped && available && recoverers == available)
                        --recoverers;
                }

                if (!changed)
                {
                    // Newly arriving humans can release a bot after its minimum
                    // duty, while a missing protector is replaced immediately.
                    // This also avoids role churn at the support-radius edge.
                    unsigned heldEscorts = 0;
                    unsigned heldRecoverers = 0;
                    for (const auto& entry : bots)
                    {
                        const auto old = previous.find(entry.first);
                        if (old == previous.end() || assignments.count(entry.first) ||
                            (!entry.second->inCombat && uint32_t(now - old->second.assignedAt) >= AssignmentHoldMs))
                            continue;
                        heldEscorts += old->second.task == Task::Escort;
                        heldRecoverers += old->second.task == Task::Recover;
                    }
                    if (escorting)
                        escorts = std::min(available, std::max(escorts, heldEscorts));
                    if (recovering)
                        recoverers = std::min(available - escorts, std::max(recoverers, heldRecoverers));
                    if (enemyFlag.state == FlagState::Dropped && available && recoverers == available)
                        --recoverers;
                }

                Select(Task::Escort, escorts, escortPosition, escortPositionKnown, bots, previous, changed, now);
                Select(Task::Recover, recoverers, ownFlag.position, ownFlag.positionKnown, bots, previous, changed, now);
                for (const auto& entry : bots)
                    if (!assignments.count(entry.first))
                        Put(entry.first, Task::Attack, previous, now);

                ownState = ownFlag.state;
                enemyState = enemyFlag.state;
                ownCarrier = ownFlag.carrier;
                enemyCarrier = enemyFlag.carrier;
                ready = true;
                return assignments;
            }

            void Clear()
            {
                assignments.clear();
                ready = false;
                ownCarrier = enemyCarrier = 0;
            }

        private:
            void Put(uint64_t guid, Task task, const Assignments& previous, uint32_t now)
            {
                const auto old = previous.find(guid);
                const unsigned route = old == previous.end() ? unsigned((guid ^ (guid >> 32)) % 10u) : old->second.route;
                const uint32_t assignedAt = old != previous.end() && old->second.task == task ? old->second.assignedAt : now;
                assignments[guid] = {task, assignedAt, route};
            }

            void Select(Task task, unsigned desired, const Point& target, bool known,
                const std::map<uint64_t, const Member*>& bots, const Assignments& previous, bool changed, uint32_t now)
            {
                std::vector<const Member*> candidates;
                for (const auto& entry : bots)
                    if (!assignments.count(entry.first))
                        candidates.push_back(entry.second);

                const auto priority = [&](const Member* member)
                {
                    const auto old = previous.find(member->guid);
                    if (changed || old == previous.end())
                        return 2;
                    if (old->second.task == task)
                        return member->inCombat || uint32_t(now - old->second.assignedAt) < AssignmentHoldMs ? 0 : 1;
                    // A free/new bot takes a new duty before a teammate whose
                    // existing ordinary assignment is still settling.
                    return member->inCombat ? 4 :
                        (uint32_t(now - old->second.assignedAt) < AssignmentHoldMs ? 3 : 2);
                };
                std::sort(candidates.begin(), candidates.end(), [&](const Member* first, const Member* second)
                {
                    const int firstPriority = priority(first);
                    const int secondPriority = priority(second);
                    if (firstPriority != secondPriority)
                        return firstPriority < secondPriority;
                    if (task == Task::Escort && first->healer != second->healer)
                        return first->healer;
                    const float firstDistance = known ? Distance(first->position, target) : 0.0f;
                    const float secondDistance = known ? Distance(second->position, target) : 0.0f;
                    return firstDistance != secondDistance ? firstDistance < secondDistance : first->guid < second->guid;
                });
                for (const Member* member : candidates)
                {
                    if (!desired)
                        break;
                    Put(member->guid, task, previous, now);
                    --desired;
                }
            }

            Assignments assignments;
            FlagState ownState = FlagState::Base;
            FlagState enemyState = FlagState::Base;
            uint64_t ownCarrier = 0;
            uint64_t enemyCarrier = 0;
            bool ready = false;
        };
    }
}
