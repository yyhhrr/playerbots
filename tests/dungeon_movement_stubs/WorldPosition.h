#pragma once
#include "playerbot/playerbot.h"

struct DungeonTestWaterData
{
    bool water = false;
    float level = 20.0f;
};
inline DungeonTestWaterData dungeonTestWater;

namespace ai
{
    struct WorldPosition
    {
        uint32 mapid = 0;
        float coord_x = 0, coord_y = 0, coord_z = 0;
        WorldPosition() = default;
        WorldPosition(uint32 map, float x, float y, float z, float = 0.0f) : mapid(map), coord_x(x), coord_y(y), coord_z(z) {}
        WorldPosition(const WorldObject* object) : WorldPosition(object->GetMapId(), object->x, object->y, object->z) {}
        bool operator==(const WorldPosition&) const = default;
        uint32 getMapId() const { return mapid; }
        float getX() const { return coord_x; }
        float getY() const { return coord_y; }
        float getZ() const { return coord_z; }
        void setZ(float value) { coord_z = value; }
        Vector3 getVector3() const { return {coord_x, coord_y, coord_z}; }
        bool isFinite() const { return std::isfinite(coord_x) && std::isfinite(coord_y) && std::isfinite(coord_z); }
        bool isInWater() const { return dungeonTestWater.water; }
        float getWaterLevel() const { return dungeonTestWater.level; }
        float distance(const WorldPosition& other) const
        {
            const float dx = coord_x - other.coord_x, dy = coord_y - other.coord_y, dz = coord_z - other.coord_z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        float sqDistance2d(const WorldPosition& other) const
        {
            const float dx = coord_x - other.coord_x, dy = coord_y - other.coord_y;
            return dx * dx + dy * dy;
        }
        float sqDistance2d(const WorldObject* other) const { return sqDistance2d(WorldPosition(other)); }
        float sqDistance(const WorldPosition& other) const
        {
            const float dz = coord_z - other.coord_z;
            return sqDistance2d(other) + dz * dz;
        }
        explicit operator bool() const { return mapid != 0; }
    };
}
