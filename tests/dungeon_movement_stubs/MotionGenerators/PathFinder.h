#pragma once
#include "playerbot/playerbot.h"

constexpr unsigned MAX_POINT_PATH_LENGTH = 148;
constexpr float SMOOTH_PATH_STEP_SIZE = 4.0f;

enum PathType
{
    PATHFIND_BLANK = 0,
    PATHFIND_NORMAL = 1,
    PATHFIND_SHORTCUT = 2,
    PATHFIND_INCOMPLETE = 4,
    PATHFIND_NOPATH = 8,
    PATHFIND_NOT_USING_PATH = 16,
    PATHFIND_SHORT = 32
};
struct DungeonTestPathDriver
{
    unsigned calls = 0;
    PathType type = PATHFIND_NORMAL;
    PointsArray path;
    bool snapped = false;
    Vector3 snappedEnd;
    bool actualEndOverride = false;
    Vector3 actualEnd;
};
inline DungeonTestPathDriver dungeonTestPath;

struct PathFinder
{
    const Unit* owner;
    PointsArray path;
    PathType type = PATHFIND_NORMAL;
    Vector3 end;
    PathFinder(const Unit* owner, bool = false) : owner(owner) {}
    void setPathLengthLimit(float) {}
    bool calculate(float x, float y, float z, bool force = false, bool straight = false)
    {
        return calculate({owner->x, owner->y, owner->z}, {x, y, z}, force, straight);
    }
    bool calculate(const Vector3& start, const Vector3& dest, bool = false, bool = false)
    {
        ++dungeonTestPath.calls;
        type = dungeonTestPath.type;
        path = dungeonTestPath.path;
        if (path.empty())
        {
            const float dx = dest.x - start.x, dy = dest.y - start.y, dz = dest.z - start.z;
            const unsigned count = std::max(1u, unsigned(std::ceil(std::sqrt(dx * dx + dy * dy + dz * dz) / 2.0f)));
            for (unsigned i = 0; i <= count; ++i)
            {
                const float t = float(i) / count;
                path.emplace_back(start.x + t * dx, start.y + t * dy, start.z + t * dz);
            }
        }
        if (dungeonTestPath.snapped) path.back() = dungeonTestPath.snappedEnd;
        end = dungeonTestPath.actualEndOverride ? dungeonTestPath.actualEnd : path.back();
        return true;
    }
    PathType getPathType() const { return type; }
    PointsArray& getPath() { return path; }
    Vector3 getActualEndPosition() const { return end; }
};
