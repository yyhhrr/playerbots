#include "playerbot/playerbot.h"
#include "DungeonMovement.h"
#include "DungeonMovementPolicy.h"
#include "MotionGenerators/PathFinder.h"

#include <array>
#include <chrono>
#include <deque>

using namespace ai;

namespace
{
    using Clock = std::chrono::steady_clock;
    using Point = dungeon::policy::Point;

    Point AsPoint(const WorldPosition& p) { return { p.getX(), p.getY(), p.getZ() }; }

    struct CachedRoute
    {
        uint32 bot, map, instance;
        std::array<int64, 6> positions;
        dungeon::MovePurpose purpose;
        Clock::time_point time;
        bool safe;
        std::vector<WorldPosition> path;
    };
    // Map updates can run on different threads. Never share mutable route entries.
    thread_local std::deque<CachedRoute> routeCache;

    std::array<int64, 6> RouteKey(const WorldPosition& start, const WorldPosition& end)
    {
        return { int64(std::floor(start.getX() * 4)), int64(std::floor(start.getY() * 4)),
            int64(std::floor(start.getZ() * 4)), int64(std::floor(end.getX() * 4)),
            int64(std::floor(end.getY() * 4)), int64(std::floor(end.getZ() * 4)) };
    }

    bool HasGroundSupport(Player* bot, const std::vector<WorldPosition>& path, bool clearance)
    {
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            const Point from = AsPoint(path[i - 1]), to = AsPoint(path[i]);
            const int samples = std::max(1, int(std::ceil(dungeon::policy::Distance(from, to))));
            if (samples > 600)
                return false;
            for (int sample = 0; sample <= samples; ++sample)
            {
                const float fraction = float(sample) / samples;
                const float x = from.x + (to.x - from.x) * fraction;
                const float y = from.y + (to.y - from.y) * fraction;
                const float z = from.z + (to.z - from.z) * fraction;
                if (!dungeon::policy::IsGroundSampleSafe(z, bot->GetMap()->GetHeight(x, y, z + 0.5f)))
                    return false;
            }
        }
        if (clearance)
        {
            const WorldPosition& end = path.back();
            std::vector<float> heights;
            for (int i = 0; i < 8; ++i)
            {
                const float angle = i * M_PI_F / 4;
                heights.push_back(bot->GetMap()->GetHeight(end.getX() + std::cos(angle) * 0.6f,
                    end.getY() + std::sin(angle) * 0.6f, end.getZ() + 0.5f));
            }
            if (!dungeon::policy::HasSafeClearance(heights, end.getZ()))
                return false;
        }
        return true;
    }

    bool Route(Player* bot, const WorldPosition& start, const WorldPosition& destination,
        dungeon::MovePurpose purpose, std::vector<WorldPosition>& path)
    {
        path.clear();
        if (!start.isFinite() || !destination.isFinite() || start.getMapId() != bot->GetMapId() ||
            destination.getMapId() != bot->GetMapId() ||
            !MaNGOS::IsValidMapCoord(start.getX(), start.getY(), start.getZ(), 0.0f) ||
            !MaNGOS::IsValidMapCoord(destination.getX(), destination.getY(), destination.getZ(), 0.0f))
            return false;
        const auto now = Clock::now();
        while (!routeCache.empty() && (now - routeCache.front().time > std::chrono::milliseconds(250) || routeCache.size() >= 1024))
            routeCache.pop_front();
        const auto key = RouteKey(start, destination);
        for (const CachedRoute& cached : routeCache)
        {
            if (cached.bot == bot->GetGUIDLow() && cached.map == bot->GetMapId() &&
                cached.instance == bot->GetInstanceId() && cached.positions == key && cached.purpose == purpose)
            {
                path = cached.path;
                return cached.safe;
            }
        }

        PathFinder finder(bot);
        finder.setPathLengthLimit(purpose == dungeon::MovePurpose::Follow ?
            float(MAX_POINT_PATH_LENGTH * SMOOTH_PATH_STEP_SIZE) : 80.0f);
        finder.calculate(start.getVector3(), destination.getVector3(), false);
        const uint32 type = uint32(finder.getPathType());
        const auto actualEnd = finder.getActualEndPosition();
        const bool complete = type == PATHFIND_NORMAL &&
            dungeon::policy::HorizontalDistance({ actualEnd.x, actualEnd.y, actualEnd.z }, AsPoint(destination)) <= 1.0f &&
            std::abs(actualEnd.z - destination.getZ()) <= 1.75f;
        std::vector<Point> points;
        for (const auto& point : finder.getPath())
        {
            path.emplace_back(bot->GetMapId(), point.x, point.y, point.z);
            points.push_back({ point.x, point.y, point.z });
        }
        const bool safe = dungeon::policy::IsSafeRoute(points, AsPoint(start), AsPoint(destination), complete,
            purpose == dungeon::MovePurpose::Follow ? dungeon::policy::Mode::Follow : dungeon::policy::Mode::Local) &&
            HasGroundSupport(bot, path, purpose == dungeon::MovePurpose::Local);
        if (!safe)
            path.clear();
        routeCache.push_back({ bot->GetGUIDLow(), bot->GetMapId(), bot->GetInstanceId(), key, purpose, now, safe, path });
        return safe;
    }

    Player* Anchor(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        const auto usable = [bot](Player* player)
        {
            return player && player != bot && player->IsInWorld() && player->IsAlive() &&
                player->GetMap() == bot->GetMap() && !player->IsBeingTeleported() &&
                !player->GetTransport() && !player->IsFlying() && !player->IsFreeFlying() &&
                !player->IsSwimming() && !player->IsInWater();
        };
        Player* master = ai->GetMaster();
        if (!usable(master))
            master = ai->GetGroupMaster();
        return usable(master) ? master : nullptr;
    }

    bool SameWorld(Player* bot, const WorldObject* target)
    {
        return target && target->IsInWorld() && target->GetMap() == bot->GetMap();
    }
}

bool ai::dungeon::IsActive(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot)
        return false;
    const BotState state = ai->GetState();
    if ((state != BotState::BOT_STATE_COMBAT && state != BotState::BOT_STATE_NON_COMBAT) ||
        !ai->HasStrategy("dungeon safe", state))
        return false;
    return bot->IsInWorld() && bot->IsAlive() && bot->GetMap()->IsDungeon() &&
        !bot->InBattleGround() && !bot->IsBeingTeleported() && !bot->GetTransport() &&
        !bot->IsFlying() && !bot->IsFreeFlying() && !bot->IsSwimming() && !bot->IsInWater();
}

bool ai::dungeon::BuildSafePath(PlayerbotAI* ai, const WorldPosition& destination,
    MovePurpose purpose, std::vector<WorldPosition>& path)
{
    path.clear();
    if (!IsActive(ai))
        return false;
    Player* bot = ai->GetBot();
    if (!Route(bot, WorldPosition(bot), destination, purpose, path))
        return false;
    if (purpose != MovePurpose::Follow)
    {
        if (Player* master = Anchor(ai))
        {
            std::vector<WorldPosition> anchorPath;
            if (!Route(bot, WorldPosition(master), destination, purpose, anchorPath))
            {
                path.clear();
                return false;
            }
        }
    }
    return true;
}

bool ai::dungeon::CanMoveTo(PlayerbotAI* ai, const WorldPosition& destination, MovePurpose purpose)
{
    if (!IsActive(ai))
        return true;
    std::vector<WorldPosition> path;
    return BuildSafePath(ai, destination, purpose, path);
}

bool ai::dungeon::ValidatePath(PlayerbotAI* ai, const std::vector<WorldPosition>& path, MovePurpose purpose)
{
    if (!IsActive(ai))
        return true;
    if (path.size() < 2)
        return false;
    Player* bot = ai->GetBot();
    std::vector<Point> points;
    for (const WorldPosition& point : path)
    {
        if (point.getMapId() != bot->GetMapId())
            return false;
        points.push_back(AsPoint(point));
    }
    if (!policy::IsSafeRoute(points, AsPoint(WorldPosition(bot)), points.back(), true,
        purpose == MovePurpose::Follow ? policy::Mode::Follow : policy::Mode::Local) ||
        !HasGroundSupport(bot, path, purpose == MovePurpose::Local))
        return false;
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const WorldPosition& a = path[i - 1];
        const WorldPosition& b = path[i];
        if (!bot->GetMap()->IsInLineOfSight(a.getX(), a.getY(), a.getZ() + bot->GetCollisionHeight(),
            b.getX(), b.getY(), b.getZ() + bot->GetCollisionHeight(), false))
            return false;
    }
    return true;
}

bool ai::dungeon::CanUseWaterRoute(PlayerbotAI* ai, const WorldPosition& destination)
{
    if (!IsActive(ai))
        return true;
    if (destination.getMapId() != ai->GetBot()->GetMapId() || !destination.isFinite() || !destination.isInWater())
        return false;
    // Enter swimming from a shore at water level, not by falling into a pool below.
    return policy::IsGroundSampleSafe(ai->GetBot()->GetPositionZ(), destination.getWaterLevel());
}

bool ai::dungeon::IsSafeLocalPoint(PlayerbotAI* ai, const WorldPosition& reference, const WorldPosition& destination)
{
    if (!IsActive(ai))
        return true;
    std::vector<WorldPosition> path;
    return Route(ai->GetBot(), reference, destination, MovePurpose::Local, path);
}

bool ai::dungeon::CanTarget(PlayerbotAI* ai, const WorldObject* target)
{
    if (!target)
        return false;
    if (!IsActive(ai))
        return true;
    Player* bot = ai->GetBot();
    if (!SameWorld(bot, target))
        return false;
    if (const Unit* unit = dynamic_cast<const Unit*>(target))
    {
        // A nearby enemy actually hitting this bot must still be defended against.
        if (unit->GetVictim() == bot && bot->CanReachWithMeleeAttack(unit) &&
            bot->IsWithinLOSInMap(unit) &&
            std::abs(bot->GetPositionZ() - unit->GetPositionZ()) <= 3.0f &&
            WorldPosition(bot).sqDistance(WorldPosition(unit)) <= 64.0f)
            return true;

        // Retain ranged combat with an airborne boss above our reachable floor.
        // The chase layer still refuses to move to the target's airborne Z.
        if (unit->IsFlying() && unit->IsInCombat() && bot->IsInCombat() && bot->IsWithinLOSInMap(unit))
        {
            WorldPosition floor(unit);
            floor.setZ(bot->GetMap()->GetHeight(floor.getX(), floor.getY(), bot->GetPositionZ() + 0.5f));
            if (floor.isFinite() && floor.getZ() > -100000.0f && unit->GetPositionZ() >= floor.getZ() &&
                CanMoveTo(ai, floor, MovePurpose::Target))
                return true;
        }
    }
    // An enemy beside a wall or edge can be attacked from a safe contact point;
    // its center need not have enough clearance for the bot to stand there.
    return CanMoveTo(ai, WorldPosition(target), MovePurpose::Target);
}

bool ai::dungeon::FindMeleeApproach(PlayerbotAI* ai, Unit* target,
    const WorldPosition& preferred, WorldPosition& result)
{
    result = WorldPosition();
    if (!IsActive(ai) || !target || !target->IsAlive() || !CanTarget(ai, target))
        return false;

    Player* bot = ai->GetBot();
    const WorldPosition center(target);
    const float reach = bot->GetCombinedCombatReach(target, true);
    const auto safeContact = [&](const WorldPosition& point)
    {
        return point.isFinite() && point.getMapId() == bot->GetMapId() &&
            center.sqDistance(point) <= reach * reach &&
            target->IsWithinLOS(point.getX(), point.getY(), point.getZ() + bot->GetCollisionHeight(), true) &&
            IsSafeLocalPoint(ai, center, point) && CanMoveTo(ai, point);
    };
    if (preferred && safeContact(preferred))
    {
        result = preferred;
        return true;
    }

    // Start on the bot's side of the enemy, then try nearby sides before the
    // far side. Every actual standing point retains full edge/ground checks.
    const float front = std::atan2(bot->GetPositionY() - target->GetPositionY(),
        bot->GetPositionX() - target->GetPositionX());
    const float offsets[] = { 0, M_PI_F / 4, -M_PI_F / 4, M_PI_F / 2,
        -M_PI_F / 2, 3 * M_PI_F / 4, -3 * M_PI_F / 4, M_PI_F };
    for (float fraction : { 0.8f, 0.6f })
    {
        const float radius = reach * fraction;
        for (float offset : offsets)
        {
            const float x = target->GetPositionX() + std::cos(front + offset) * radius;
            const float y = target->GetPositionY() + std::sin(front + offset) * radius;
            float z = target->GetPositionZ();
            bot->UpdateAllowedPositionZ(x, y, z);
            const WorldPosition candidate(bot->GetMapId(), x, y, z);
            if (safeContact(candidate))
            {
                result = candidate;
                return true;
            }
        }
    }
    return false;
}

bool ai::dungeon::CanLoot(PlayerbotAI* ai, const WorldObject* target)
{
    return target && (!IsActive(ai) || (SameWorld(ai->GetBot(), target) && CanMoveTo(ai, WorldPosition(target))));
}
