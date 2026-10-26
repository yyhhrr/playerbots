#pragma once

#include <cmath>

namespace ai::warsong
{
    struct GraveyardPoint
    {
        float x, y, z;
    };

    struct GraveyardExit
    {
        GraveyardPoint edge, landing;
    };

    inline const GraveyardExit& GetGraveyardExit(bool alliance)
    {
        static const GraveyardExit horde = {
            {1055.182251f, 1396.967529f, 339.361511f},
            {1076.778076f, 1396.0f, 324.0f}
        };
        static const GraveyardExit ally = {
            {1422.106201f, 1529.851196f, 342.0f},
            {1398.7f, 1534.6f, 322.5f}
        };
        return alliance ? ally : horde;
    }

    inline bool IsOnGraveyardPlatform(bool alliance, const GraveyardPoint& point)
    {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            return false;
        return alliance ? point.x > 1388.0f && point.x < 1460.0f && point.y > 1515.0f &&
            point.y < 1585.0f && point.z > 335.0f && point.z < 355.0f :
            point.x > 985.0f && point.x < 1075.0f && point.y > 1340.0f &&
            point.y < 1400.0f && point.z > 330.0f && point.z < 351.0f;
    }

    inline bool CanStartGraveyardDrop(bool alliance, const GraveyardPoint& start, float ground)
    {
        const GraveyardExit& exit = GetGraveyardExit(alliance);
        const float dx = start.x - exit.edge.x, dy = start.y - exit.edge.y;
        const float drop = start.z - ground;
        return std::isfinite(start.x) && std::isfinite(start.y) && std::isfinite(start.z) &&
            std::isfinite(ground) && std::abs(ground - exit.landing.z) <= 4.0f &&
            dx * dx + dy * dy <= 64.0f && std::abs(start.z - exit.edge.z) <= 6.0f &&
            drop >= 8.0f && drop <= 30.0f;
    }
}
