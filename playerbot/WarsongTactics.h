#pragma once

#include "WarsongPolicy.h"

class PlayerbotAI;
class Player;
class GameObject;
struct WorldLocation;

namespace ai
{
namespace warsong
{
    bool IsCarrier(Player* player);
    Task GetTask(PlayerbotAI* ai);
    bool IsPressuring(PlayerbotAI* ai);
    bool SelectObjective(PlayerbotAI* ai, const Point& home, const Point& enemyHome,
        const std::vector<Point>& hidingSpots, WorldLocation& objective);
    bool CanUseFlag(PlayerbotAI* ai, GameObject* flag);
}
}
