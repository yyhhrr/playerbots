#include "playerbot/AlteracValleyPolicy.h"

#include <cstdlib>
#include <iostream>
#include <set>

using namespace ai::alterac;
namespace
{
    unsigned checks = 0, failures = 0;
    void Check(bool condition, const char* description)
    {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAILED: " << description << '\n'; }
    }
    unsigned Count(const Planner::Assignments& assignments, unsigned id, Task task)
    {
        unsigned result = 0;
        for (const auto& entry : assignments)
            if (entry.second.id == id && entry.second.task == task) ++result;
        return result;
    }
    std::vector<Member> Team(unsigned size)
    {
        std::vector<Member> members;
        for (unsigned i = 0; i < size; ++i)
            members.push_back({i + 1, {float(i * 2), 0, 0}, true, true});
        return members;
    }
}

int main()
{
    const NodeState states[] = {NodeState::Unknown, NodeState::Neutral, NodeState::FriendlyOwned,
        NodeState::FriendlyAssault, NodeState::EnemyOwned, NodeState::EnemyAssault};
    for (NodeState first : states) for (NodeState second : states)
        for (NodeState third : states) for (NodeState fourth : states)
        {
            const std::array<NodeState, 4> towers{first, second, third, fourth};
            Check(TowersReady(towers) == (first == NodeState::FriendlyOwned && second == NodeState::FriendlyOwned &&
                third == NodeState::FriendlyOwned && fourth == NodeState::FriendlyOwned),
                "only four completed friendly tower captures authorize the boss phase");
        }
    {
        Planner planner; auto team = Team(12);
        const auto assignments = planner.Update({{7, Task::Attack, {0, 0, 0}}, {8, Task::Attack, {100, 0, 0}},
            {9, Task::Attack, {200, 0, 0}}}, team, 1000);
        Check(assignments.size() == 12, "every living bot receives a useful strategic task");
        for (unsigned id : {7, 8, 9})
            Check(Count(assignments, id, Task::Attack) >= 3 && Count(assignments, id, Task::Attack) <= 5,
                "nearby attack candidates distribute the raid instead of sending everyone to one tower");
    }
    {
        Planner planner; auto team = Team(2);
        const std::vector<Objective> points{{7, Task::Attack, {0, 0, 0}}, {8, Task::Attack, {1000, 0, 0}}};
        const auto before = planner.Update(points, team, 1000);
        for (auto& member : team) member.position.x = before.at(member.guid).id == 7 ? 1000 : 0;
        const auto held = planner.Update(points, team, 1000 + AssignmentHoldMs - 1);
        for (const auto& entry : before)
            Check(held.at(entry.first).id == entry.second.id && held.at(entry.first).assignedAt == 1000,
                "ordinary strategic objectives remain stable throughout the hold interval");
        const auto refreshed = planner.Update(points, team, 1000 + AssignmentHoldMs);
        bool changed = false;
        for (const auto& entry : before) changed |= refreshed.at(entry.first).id != entry.second.id;
        Check(changed, "a completed hold allows a clearly closer useful assignment");
    }
    {
        Planner planner; auto team = Team(8);
        planner.Update({{7, Task::Attack, {1000, 0, 0}}}, team, 1000);
        const auto assignments = planner.Update({{0, Task::Defend, {0, 0, 0}, 2, true},
            {7, Task::Attack, {1000, 0, 0}}}, team, 2000);
        Check(Count(assignments, 0, Task::Defend) == 2, "a threatened key graveyard receives its bounded relief quota");
        Check(assignments.at(1).task == Task::Defend && assignments.at(2).task == Task::Defend,
            "urgent relief uses the nearest available bots despite an ordinary task hold");
        Check(Count(assignments, 7, Task::Attack) == 6, "urgent defence leaves the remainder advancing");
    }
    {
        Planner planner; auto team = Team(6);
        auto assignments = planner.Update({{11, Task::Guard, {0, 0, 0}, 2},
            {12, Task::Attack, {100, 0, 0}}}, team, 1000);
        Check(Count(assignments, 11, Task::Guard) == 2, "an assaulted enemy tower keeps a small dedicated guard");
        assignments = planner.Update({{11, Task::Guard, {0, 0, 0}, 2},
            {12, Task::Attack, {100, 0, 0}}}, team, 200000);
        Check(Count(assignments, 11, Task::Guard) == 2, "tower guards remain assigned beyond the ordinary task timeout");
        assignments = planner.Update({{12, Task::Attack, {100, 0, 0}}}, team, 200001);
        Check(Count(assignments, 11, Task::Guard) == 0 && Count(assignments, 12, Task::Attack) == 6,
            "finishing or losing the guarded tower promptly releases its obsolete guard task");
    }
    {
        Planner planner; auto team = Team(6);
        team.push_back({100, {0, 0, 0}, false, true});
        auto assignments = planner.Update({{11, Task::Guard, {0, 0, 0}, 2},
            {12, Task::Attack, {100, 0, 0}}}, team, 1000);
        Check(Count(assignments, 11, Task::Guard) == 1 && !assignments.count(100),
            "a real player present at the flag covers one guard slot without receiving bot orders");
        team.back().position.x = 500;
        assignments = planner.Update({{11, Task::Guard, {0, 0, 0}, 2},
            {12, Task::Attack, {100, 0, 0}}}, team, 2000);
        Check(Count(assignments, 11, Task::Guard) == 2, "a departed human cannot leave the tower falsely counted as guarded");
    }
    {
        Planner planner; auto team = Team(4);
        planner.Update({{7, Task::Attack, {0, 0, 0}}}, team, 1000);
        team[0].capturing = true;
        const auto assignments = planner.Update({{0, Task::Defend, {0, 0, 0}, 2, true},
            {7, Task::Attack, {0, 0, 0}}}, team, 2000);
        Check(assignments.at(1).id == 7 && Count(assignments, 0, Task::Defend) == 2,
            "an active opener finishes its valid flag while other bots answer the urgent defence");
    }
    {
        Planner planner; auto team = Team(4); team[0].alive = false;
        const auto assignments = planner.Update({{7, Task::Attack, {0, 0, 0}}}, team, 1000);
        Check(assignments.size() == 3 && !assignments.count(1), "dead bots do not satisfy live team assignments");
        Check(planner.Update({}, team, 1001).empty(), "an empty candidate set clears stale objectives");
    }
    {
        Planner planner; auto team = Team(1);
        const auto before = planner.Update({{7, Task::Attack, {0, 0, 0}}}, team, 0xfffffff0u);
        const auto held = planner.Update({{7, Task::Attack, {0, 0, 0}}, {8, Task::Attack, {500, 0, 0}}},
            team, std::uint32_t(0xfffffff0u + AssignmentHoldMs - 1));
        Check(held.at(1).assignedAt == before.at(1).assignedAt, "assignment timing remains stable across the millisecond counter wrap");
        const auto changed = planner.Update({{8, Task::Attack, {500, 0, 0}}}, team, 10);
        Check(changed.at(1).id == 8, "a node becoming unavailable invalidates a held objective immediately");
    }
    {
        Planner planner; auto team = Team(3);
        const auto before = planner.Update({{11, Task::Guard, {0, 0, 0}, 1},
            {12, Task::Attack, {500, 0, 0}}}, team, 1000);
        const auto after = planner.Update({{0, Task::Defend, {0, 0, 0}, 3, true},
            {11, Task::Guard, {0, 0, 0}, 1}, {12, Task::Attack, {500, 0, 0}}}, team, 2000);
        Check(Count(before, 11, Task::Guard) == 1 && Count(after, 11, Task::Guard) == 1 &&
            Count(after, 0, Task::Defend) == 2, "an urgent relief order does not abandon the last physically present tower guard");
    }
    {
        Planner planner; auto team = Team(1);
        const auto assignments = planner.Update({{0, Task::Defend, {1000, 0, 0}, 1, true},
            {6, Task::Defend, {0, 0, 0}, 1, true}}, team, 1000);
        Check(assignments.at(1).id == 6, "urgent defence uses the globally nearest node instead of the node array order");
    }
    for (unsigned size : {1, 4, 5, 8})
    {
        Planner planner; auto team = Team(size);
        const auto assignments = planner.Update({{15, Task::Captain, {0, 0, 0}, 5},
            {7, Task::Attack, {500, 0, 0}}}, team, 1000);
        Check(Count(assignments, 15, Task::Captain) == (size < 5 ? 0u : 5u),
            "a fresh captain squad requires five available bots and does not strand a smaller group waiting");
    }
    {
        Planner planner; auto team = Team(8);
        const auto assignments = planner.Update({{0, Task::Defend, {0, 0, 0}, 4, true},
            {15, Task::Captain, {0, 0, 0}, 5}, {7, Task::Attack, {500, 0, 0}}}, team, 1000);
        Check(Count(assignments, 0, Task::Defend) == 4 && Count(assignments, 15, Task::Captain) == 0,
            "urgent relief reserves its members before forming a new captain group");
    }
    {
        Planner planner; auto team = Team(1);
        Check(Count(planner.Update({{15, Task::Captain, {0, 0, 0}, 1}}, team, 1000), 15, Task::Captain) == 1,
            "survivors can reinforce a committed captain fight without rebuilding a waiting group");
    }
    {
        Planner planner; auto team = Team(1);
        Check(planner.Update({{17, Task::Mine, {151, 0, 0}, 2}}, team, 1000).empty(),
            "a remote mine cannot become an opportunistic fallback for a distant bot");
        Check(Count(planner.Update({{17, Task::Mine, {150, 0, 0}, 2}}, team, 2000), 17, Task::Mine) == 1,
            "a mine within the nearby assistance range remains available");
        team[0].position.x = -2;
        Check(planner.Update({{17, Task::Mine, {150, 0, 0}, 2}}, team, 2001).empty(),
            "a held mine task cannot bypass the nearby assistance limit");
    }
    {
        Planner planner; auto team = Team(6);
        for (auto& member : team) member.position.z = 24;
        team.push_back({100, {0, 0, 0}, false, true});
        const auto assignments = planner.Update({{11, Task::Guard, {0, 0, 24}, 2},
            {12, Task::Attack, {100, 0, 24}}}, team, 1000);
        Check(Count(assignments, 11, Task::Guard) == 2,
            "a human on the ground below a tower does not replace a guard physically at its flag");
    }
    {
        Planner planner; auto team = Team(3);
        planner.Update({{11, Task::Guard, {0, 0, 24}, 1}, {12, Task::Attack, {500, 0, 0}}}, team, 1000);
        const auto assignments = planner.Update({{0, Task::Defend, {0, 0, 0}, 3, true},
            {11, Task::Guard, {0, 0, 24}, 1}, {12, Task::Attack, {500, 0, 0}}}, team, 2000);
        Check(Count(assignments, 0, Task::Defend) == 3,
            "a bot still below the flag cannot claim to be the tower's last physically present guard");
    }
    std::cout << checks << " Alterac policy checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
