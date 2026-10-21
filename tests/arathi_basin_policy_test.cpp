#include "playerbot/ArathiBasinPolicy.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ai::arathi;

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const char* description)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << description << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    std::array<Node, NodeCount> Nodes()
    {
        std::array<Node, NodeCount> nodes{};
        for (unsigned node = 0; node < NodeCount; ++node)
            nodes[node].position = {float(node * 160), 0.0f, 0.0f};
        return nodes;
    }

    Member Bot(uint64_t guid, float x, float y = 0.0f)
    {
        return {guid, {x, y, 0.0f}, true, true, false, false};
    }

    unsigned Assigned(const Planner::Assignments& result, unsigned node)
    {
        unsigned count = 0;
        for (const auto& entry : result)
            count += entry.second.node == node;
        return count;
    }
}

int main()
{
    {
        Planner planner;
        auto nodes = Nodes();
        std::vector<Member> members{Bot(1, 0.0f)};
        const auto first = planner.Update(nodes, members, 100);
        members[0].position = nodes[4].position;
        const auto held = planner.Update(nodes, members, 30099);
        Check(held.at(1).node == first.at(1).node, "ordinary task holds for 30 seconds");
        Check(held.at(1).assignedAt == 100, "keeping a task does not refresh its timer");
        const auto changed = planner.Update(nodes, members, 30100);
        Check(changed.at(1).node == 4, "expired task can move to a materially better objective");
        members[0].inCombat = true;
        members[0].position = nodes[0].position;
        Check(planner.Update(nodes, members, 90000).at(1).node == 4, "combat keeps existing task after cooldown");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyOwned;
        std::vector<Member> members{Bot(1, 0), Bot(2, 100), Bot(3, 150)};
        planner.Update(nodes, members, 0);
        nodes[4].state = NodeState::FriendlyOwned;
        nodes[4].enemyCount = 8;
        const auto result = planner.Update(nodes, members, 60000);
        Check(result.at(1).node == 0 && result.at(1).defend, "last actual guard cannot be diverted to another emergency");
        Check(Assigned(result, 4) == 2, "available teammates reinforce without pulling the last guard");
        Check(result.at(1).assignedAt == 0, "guard task retains its start time");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyOwned;
        Member human = Bot(99, 0);
        human.isBot = false;
        std::vector<Member> members{Bot(1, 90), Bot(2, 320), human};
        const auto first = planner.Update(nodes, members, 0);
        Check(first.count(99) == 0, "humans contribute strength but never receive tasks");
        Check(Assigned(first, 0) == 0, "human already guarding avoids unnecessary duplicate guard");
        members.back().position = {1000, 1000, 0};
        const auto next = planner.Update(nodes, members, 1000);
        Check(Assigned(next, 0) == 1, "human leaving dispatches a replacement despite task cooldown");
        Check(next.at(1).node == 0, "nearest available bot replaces departing human");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyOwned;
        std::vector<Member> members{Bot(1, 0), Bot(2, 110)};
        nodes[0].enemyCount = 1;
        const auto first = planner.Update(nodes, members, 0);
        Check(Assigned(first, 0) == 2, "threat dispatches an incoming replacement or reinforcement");
        nodes[0].enemyCount = 0;
        const auto result = planner.Update(nodes, members, 60000);
        Check(result.at(1).node == 0, "incoming ally never counts as completed guard relief");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[2].state = NodeState::FriendlyAssault;
        std::vector<Member> members{Bot(1, 320), Bot(2, 340), Bot(3, 500)};
        members[0].capturing = true;
        auto result = planner.Update(nodes, members, 100);
        Check(result.at(1).node == 2 && result.at(1).defend, "active flag opener is kept at its flag");
        members[0].capturing = false;
        result = planner.Update(nodes, members, 60100);
        Check(result.at(1).node == 2 && result.at(1).defend, "assaulted flag retains a guard until actual ownership");
        nodes[2].state = NodeState::FriendlyOwned;
        planner.Update(nodes, members, 61000);
        nodes[2].state = NodeState::EnemyAssault;
        result = planner.Update(nodes, members, 62000);
        Check(result.at(1).node == 2 && result.at(1).defend, "enemy assault does not erase previous friendly ownership");
        nodes[2].state = NodeState::EnemyOwned;
        result = planner.Update(nodes, members, 63000);
        Check(!result.at(1).defend, "completed enemy capture clears remembered friendly ownership");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyOwned;
        std::vector<Member> members{Bot(1, 0), Bot(2, 160), Bot(3, 320), Bot(4, 480), Bot(5, 640), Bot(6, 600)};
        planner.Update(nodes, members, 0);
        nodes[0].enemyCount = 2;
        auto result = planner.Update(nodes, members, 1000);
        Check(Assigned(result, 0) == 3, "danger can override stable tasks for enough reinforcements");
        auto again = planner.Update(nodes, members, 2000);
        Check(Assigned(again, 0) == 3, "in-flight reinforcement is counted without dispatching duplicates");
        Check(again.size() == members.size(), "every alive bot has exactly one task");
        for (const auto& entry : result)
            Check(again.at(entry.first).node == entry.second.node, "unchanged report does not churn tasks");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyOwned;
        nodes[1].state = NodeState::FriendlyOwned;
        nodes[2].state = NodeState::FriendlyAssault;
        std::vector<Member> members{Bot(1, 0)};
        const auto result = planner.Update(nodes, members, 0);
        Check(result.size() == 1 && result.at(1).node == 0, "short-handed team keeps real guard instead of oscillating among empty flags");
        Check(planner.Update(nodes, {}, 1000).empty(), "leaving battleground clears bot tasks");
        planner.Update(nodes, members, 2000);
        members[0].alive = false;
        Check(planner.Update(nodes, members, 3000).empty(), "dead bots do not count as defense or receive tasks");
        members[0].alive = true;
        Check(planner.Update(nodes, members, 4000).at(1).assignedAt == 4000, "revived bot receives a fresh task");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        std::vector<Member> members;
        for (unsigned id = 1; id <= 15; ++id)
            members.push_back(Bot(id, 320, 100));
        const auto result = planner.Update(nodes, members, 0);
        for (unsigned node = 0; node < NodeCount; ++node)
        {
            Check(Assigned(result, node) > 0, "uncontested objectives all receive an attack group");
            Check(Assigned(result, node) <= 4, "attack allocation avoids team-wide pileup");
        }
    }

    {
        Planner planner;
        auto nodes = Nodes();
        std::vector<Member> members{Bot(1, 0)};
        const uint32_t start = std::numeric_limits<uint32_t>::max() - 1000;
        const auto first = planner.Update(nodes, members, start);
        members[0].position = nodes[4].position;
        Check(planner.Update(nodes, members, uint32_t(start + 29999)).at(1).node == first.at(1).node,
            "task cooldown survives millisecond timer wraparound");
        Check(planner.Update(nodes, members, uint32_t(start + 30000)).at(1).node == 4,
            "task cooldown expires correctly after wraparound");
        planner.Clear();
        nodes[4].state = NodeState::EnemyAssault;
        Check(!planner.Update(nodes, members, 50).at(1).defend, "new instance does not inherit old ownership or assignments");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        std::vector<Member> members{Bot(1, 0)};
        Check(planner.Update(nodes, members, 0).at(1).node == 0, "initial target is nearest valid flag");
        nodes[0].valid = false;
        Check(planner.Update(nodes, members, 1000).at(1).node == 1, "invalid target bypasses ordinary task cooldown");
        members[0].inCombat = true;
        nodes[1].valid = false;
        Check(planner.Update(nodes, members, 2000).at(1).node == 2, "invalid coordinates cannot remain combat target");
        for (Node& node : nodes)
            node.valid = false;
        Check(planner.Update(nodes, members, 3000).empty(), "no valid flag clears tasks without indexing an absent node");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyOwned;
        nodes[1].state = NodeState::FriendlyOwned;
        nodes[2].state = NodeState::FriendlyOwned;
        nodes[3].state = NodeState::EnemyOwned;
        nodes[4].state = NodeState::EnemyOwned;
        std::vector<Member> members;
        for (unsigned id = 1; id <= 15; ++id)
            members.push_back(Bot(id, 320, 100));
        auto expected = planner.Update(nodes, members, 0);
        for (unsigned node = 0; node < 3; ++node)
            Check(Assigned(expected, node) >= 1, "three controlled flags each receive a defender");
        for (unsigned tick = 1; tick <= 8; ++tick)
        {
            const auto result = planner.Update(nodes, members, tick * 30000);
            for (const auto& entry : expected)
                Check(result.at(entry.first).node == entry.second.node,
                    "unchanged three-point situation does not trigger periodic travel reversals");
        }
        for (Member& member : members)
            member.position = nodes[expected.at(member.guid).node].position;
        expected = planner.Update(nodes, members, 270000);
        for (unsigned tick = 10; tick <= 17; ++tick)
        {
            const auto result = planner.Update(nodes, members, tick * 30000);
            for (const auto& entry : expected)
                Check(result.at(entry.first).node == entry.second.node,
                    "arrived groups hold stable tasks through repeated cooldown expiry");
        }
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyOwned;
        nodes[1].state = NodeState::FriendlyOwned;
        nodes[2].state = NodeState::FriendlyAssault;
        std::vector<Member> members{Bot(1, 0), Bot(2, 160), Bot(3, 320), Bot(4, 322), Bot(5, 324)};
        members[2].capturing = true;
        planner.Update(nodes, members, 0);
        members[2].capturing = false;
        auto result = planner.Update(nodes, members, 31000);
        Check(Assigned(result, 0) >= 1 && Assigned(result, 1) >= 1,
            "capture support never consumes another friendly flag's only guard");
        Check(Assigned(result, 2) >= 2, "contested flag retains opener and cover after 30-second expiry");
        const auto expected = result;
        for (unsigned tick = 2; tick <= 6; ++tick)
        {
            result = planner.Update(nodes, members, tick * 31000);
            Check(Assigned(result, 2) >= 2, "cover persists until formal capture rather than one cooldown");
            for (const auto& entry : expected)
                Check(result.at(entry.first).node == entry.second.node,
                    "contested flag support does not rotate merely because time passed");
        }
        nodes[2].state = NodeState::FriendlyOwned;
        result = planner.Update(nodes, members, 220000);
        Check(Assigned(result, 2) >= 1, "completed capture retains its actual guard");
    }

    {
        Planner planner;
        auto nodes = Nodes();
        nodes[0].state = NodeState::FriendlyAssault;
        nodes[1].state = NodeState::FriendlyOwned;
        std::vector<Member> members{Bot(1, 0), Bot(2, 1)};
        const auto result = planner.Update(nodes, members, 0);
        Check(Assigned(result, 0) == 1 && Assigned(result, 1) == 1,
            "short-handed team gives every flag one guard before adding capture cover");
    }

    std::cout << checks << " Arathi Basin policy checks passed\n";
}
