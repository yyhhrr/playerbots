#include "playerbot/WarsongPolicy.h"

#include <cstdlib>
#include <iostream>

using namespace ai::warsong;

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

    Member Bot(uint64_t guid, float x, bool healer = false)
    {
        return {guid, {x, 0.0f, 0.0f}, true, true, healer, false};
    }

    Flag Carried(uint64_t guid, float x)
    {
        return {FlagState::Carried, {x, 0.0f, 0.0f}, guid, true};
    }

    unsigned Count(const Planner::Assignments& assignments, Task task)
    {
        unsigned result = 0;
        for (const auto& entry : assignments)
            result += entry.second.task == task;
        return result;
    }

    void Same(const Planner::Assignments& first, const Planner::Assignments& second, const char* description)
    {
        Check(first.size() == second.size(), description);
        for (const auto& entry : first)
        {
            Check(second.count(entry.first) != 0, description);
            const auto& other = second.at(entry.first);
            Check(entry.second.task == other.task, description);
            Check(entry.second.route == other.route, description);
            Check(entry.second.assignedAt == other.assignedAt, description);
        }
    }
}

int main()
{
    {
        Planner planner;
        Flag base;
        std::vector<Member> members{Bot(1, 0), Bot(2, 10), Bot(3, 20)};
        auto first = planner.Update(base, base, members, 100);
        Check(Count(first, Task::Attack) == 3, "flags at base allocate offensive players");
        for (const auto& entry : first)
            Check(entry.second.route < 10, "route is in supported range");
        members[0].position.x = 1000;
        members[1].inCombat = true;
        Same(first, planner.Update(base, base, members, 30099), "ordinary assignments hold through cooldown");
        Same(first, planner.Update(base, base, members, 100000), "unchanged situation does not reroll after cooldown");
    }

    {
        Planner planner;
        Flag base;
        Flag taken = Carried(1, 0);
        std::vector<Member> members{Bot(1, 0), Bot(2, 5), Bot(3, 200, true), Bot(4, 10), Bot(5, 20)};
        const auto result = planner.Update(base, taken, members, 100);
        Check(result.at(1).task == Task::Carry, "carrier is always assigned Carry");
        Check(Count(result, Task::Escort) == 2, "full team appoints two protectors");
        Check(result.at(3).task == Task::Escort, "healer is preferred for escort");
        Check(result.at(2).task == Task::Escort, "nearest other teammate escorts");
        Check(Count(result, Task::Attack) == 2, "remaining players attack");
        members[2].position.x = 900;
        members[1].position.x = 500;
        members[4].position.x = 1;
        Same(result, planner.Update(base, taken, members, 10000), "moving carrier and teammates do not shuffle escort");
        Same(result, planner.Update(base, taken, members, 100000), "escort stays stable after hold expires");
    }

    {
        Planner planner;
        Flag base;
        Flag stolen = Carried(99, 0);
        std::vector<Member> members;
        for (uint64_t guid = 1; guid <= 10; ++guid)
            members.push_back(Bot(guid, float(guid * 10)));
        const auto initial = planner.Update(base, base, members, 100);
        auto result = planner.Update(stolen, base, members, 101);
        Check(Count(result, Task::Recover) == 6, "single lost flag sends roughly sixty percent to recover");
        Check(result.at(1).task == Task::Recover && result.at(6).task == Task::Recover,
            "known enemy carrier chooses nearest recoverers");
        Check(Count(result, Task::Attack) == 4, "lost flag leaves an attacking group");
        for (const auto& entry : result)
            Check(entry.second.route == initial.at(entry.first).route, "emergency task changes keep route");
        stolen.position.x = 1000;
        members[9].position.x = 1000;
        Same(result, planner.Update(stolen, base, members, 60000), "carrier movement does not churn recovery team");
        const auto returned = planner.Update(base, base, members, 60001);
        Check(Count(returned, Task::Recover) == 0, "returned own flag ends recovery immediately");
        Check(Count(returned, Task::Attack) == 10, "returning flag restores attack group");
    }

    {
        Flag own = Carried(99, 500);
        Flag enemy = Carried(1, 0);
        for (unsigned size = 1; size <= 10; ++size)
        {
            Planner planner;
            std::vector<Member> members;
            for (unsigned guid = 1; guid <= size; ++guid)
                members.push_back(Bot(guid, float(guid)));
            const auto result = planner.Update(own, enemy, members, 0);
            Check(result.at(1).task == Task::Carry, "small-team flag carrier never recovers");
            const unsigned remaining = size - 1;
            const unsigned expectedEscorts = std::min(2u, (remaining + 1u) / 2u);
            Check(Count(result, Task::Escort) == expectedEscorts, "small team keeps a feasible escort group");
            Check(Count(result, Task::Recover) == remaining - expectedEscorts,
                "both flags carried sends remaining players to recover");
            Check(Count(result, Task::Attack) == 0, "both flags carried does not waste players at flag base");
        }
    }

    {
        Planner planner;
        Flag base;
        Flag enemy = Carried(1, 0);
        std::vector<Member> members{Bot(1, 0), Bot(2, 10), Bot(3, 20), Bot(4, 30)};
        Member human = Bot(100, 5);
        human.isBot = false;
        members.push_back(human);
        auto first = planner.Update(base, enemy, members, 0);
        Check(first.count(100) == 0, "real player is never assigned");
        Check(Count(first, Task::Escort) == 1, "nearby human contributes an escort");
        members.back().position.x = 100;
        auto next = planner.Update(base, enemy, members, 10);
        Check(Count(next, Task::Escort) == 2, "human leaving immediately fills escort shortage");
        members.back().position.x = SupportRadius;
        Check(Count(planner.Update(base, enemy, members, 20), Task::Escort) == 2,
            "human arriving does not cancel an escort before minimum duty");
        Check(Count(planner.Update(base, enemy, members, 30010), Task::Escort) == 1,
            "support radius includes boundary and releases surplus escort after minimum duty");
        members.back().alive = false;
        Check(Count(planner.Update(base, enemy, members, 30011), Task::Escort) == 2,
            "dead human cannot satisfy escort requirement");
        members.back().alive = true;
        members.back().position.z = SupportRadius;
        Check(Count(planner.Update(base, enemy, members, 60011), Task::Escort) == 2,
            "nearby support uses full three-dimensional distance");
    }

    {
        Planner planner;
        Flag own = Carried(99, 0);
        Flag base;
        std::vector<Member> members{Bot(1, 10), Bot(2, 20), Bot(3, 30), Bot(4, 40), Bot(5, 50)};
        Member human = Bot(100, 0);
        human.isBot = false;
        members.push_back(human);
        const auto result = planner.Update(own, base, members, 0);
        Check(Count(result, Task::Recover) == 2, "human already recovering reduces duplicate bots");
        members.back().position.x = 100;
        Check(Count(planner.Update(own, base, members, 1), Task::Recover) == 3,
            "departed human no longer counts toward recovery");
        members.back().position.x = 0;
        Check(Count(planner.Update(own, base, members, 2), Task::Recover) == 3,
            "returning human does not cancel newly dispatched recovery before minimum duty");
        Check(Count(planner.Update(own, base, members, 30001), Task::Recover) == 2,
            "surplus recovery can finish duty when human support remains");
    }

    {
        Planner planner;
        Flag base;
        Flag enemy = Carried(1, 0);
        std::vector<Member> members{Bot(1, 0), Bot(2, 10), Bot(3, 20, true), Bot(4, 30), Bot(5, 40)};
        const auto original = planner.Update(base, enemy, members, 100);
        members[1].alive = false;
        const auto dead = planner.Update(base, enemy, members, 200);
        Check(dead.count(2) == 0, "dead bot is removed immediately");
        Check(dead.at(4).task == Task::Escort, "dead escort receives nearest replacement");
        members.erase(members.begin() + 2);
        const auto departed = planner.Update(base, enemy, members, 300);
        Check(departed.count(3) == 0, "departed bot is pruned");
        Check(departed.at(5).task == Task::Escort, "departed escort receives replacement");
        Check(departed.at(1).task == Task::Carry, "roster changes preserve carrier identity");
        members[1].alive = true;
        const auto resurrected = planner.Update(base, enemy, members, 400);
        Check(resurrected.at(2).task == Task::Attack, "resurrected bot does not displace stable living escorts");
        Check(resurrected.at(2).route == original.at(2).route, "resurrected bot has deterministic route");
        enemy = Carried(2, 10);
        const auto changedCarrier = planner.Update(base, enemy, members, 401);
        Check(changedCarrier.at(2).task == Task::Carry, "new flag carrier overrides old attack task immediately");
        Check(changedCarrier.at(1).task != Task::Carry, "previous carrier loses Carry task immediately");
    }

    {
        Planner planner;
        Flag own{FlagState::Dropped, {0, 0, 0}, 0, true};
        Flag enemy{FlagState::Dropped, {100, 0, 0}, 0, true};
        for (unsigned size = 1; size <= 10; ++size)
        {
            std::vector<Member> members;
            for (unsigned guid = 1; guid <= size; ++guid)
                members.push_back(Bot(guid, float(guid)));
            const auto result = planner.Update(own, enemy, members, size);
            Check(Count(result, Task::Attack) >= 1, "both flags dropped retains a picker for enemy flag");
            Check(Count(result, Task::Carry) == 0, "dropped flags do not invent a carrier");
            Check(Count(result, Task::Escort) == 0, "dropped enemy flag no longer needs escort");
        }
        enemy.state = FlagState::Respawning;
        auto result = planner.Update(Flag{}, enemy, {Bot(1, 0), Bot(2, 10)}, 100);
        Check(Count(result, Task::Attack) == 2, "respawning flag clears obsolete special duties");
    }

    {
        Flag own = Carried(99, 50);
        Flag enemy = Carried(1, 0);
        std::vector<Member> members{Bot(1, 0), Bot(2, 10), Bot(3, 10), Bot(4, 20, true), Bot(5, 90)};
        Planner ordered;
        const auto first = ordered.Update(own, enemy, members, 100);
        std::reverse(members.begin(), members.end());
        Planner reversed;
        Same(first, reversed.Update(own, enemy, members, 100), "member input order does not affect plan");
        Check(first.at(2).task == Task::Escort, "equal-distance escort resolves by GUID");
        enemy.positionKnown = false;
        own.positionKnown = false;
        enemy.carrier = 1000;
        members[0].position.x = std::numeric_limits<float>::quiet_NaN();
        Planner unknown;
        auto result = unknown.Update(own, enemy, members, 100);
        Check(result.size() == 5, "unknown carrier position and invalid positions still produce a complete plan");
        Check(Count(result, Task::Escort) == 2, "unknown position still reserves escort roles");
    }

    {
        Planner planner;
        Flag base;
        Flag taken = Carried(1, 0);
        auto members = std::vector<Member>{Bot(1, 0), Bot(2, 10), Bot(3, 20), Bot(4, 30)};
        const auto before = planner.Update(base, taken, members, std::numeric_limits<uint32_t>::max() - 100);
        Same(before, planner.Update(base, taken, members, 100), "timer wrap does not invalidate assignments");
        planner.Clear();
        Check(planner.Update(base, base, {}, 200).empty(), "clear and empty roster remove stale tasks");
        auto reset = planner.Update(base, taken, members, 300);
        Check(reset.at(1).assignedAt == 300, "clear resets assignment age");
        for (const auto& entry : reset)
            Check(entry.second.route == before.at(entry.first).route, "clear preserves deterministic routes");
    }

    {
        Planner planner;
        Flag base;
        const auto members = std::vector<Member>{Bot(1, 0), Bot(2, 10), Bot(3, 20), Bot(4, 30)};
        const auto plan = planner.Update(Carried(99, 5), base, members, 0, {1, 2});
        Check(plan.at(1).task == Task::Attack && plan.at(2).task == Task::Attack,
            "primary and backup runners are reserved before recovery assignments");
        Check(Count(plan, Task::Recover) >= 1, "runner reservations still leave a recovery group");
        const auto live = planner.Update(base, Carried(3, 20), members, 100, {1, 2});
        Check(live.at(3).task == Task::Carry, "actual carrier replaces all advance nominations");
        Check(Count(live, Task::Escort) == 2, "advance nominations do not consume live escorts");
        Planner small;
        const auto two = small.Update(Carried(99, 5), base, {Bot(1, 0), Bot(2, 10)}, 0, {1, 2});
        Check(two.at(1).task == Task::Attack && two.at(2).task == Task::Recover,
            "two-member team keeps recovery instead of reserving both as runners");
    }

    std::cout << checks << " Warsong policy checks passed\n";
}
