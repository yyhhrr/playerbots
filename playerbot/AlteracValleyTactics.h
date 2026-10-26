#pragma once

class PlayerbotAI;
class Player;
class Unit;
class GameObject;
struct WorldLocation;

namespace ai::alterac
{
    bool IsActive(Player* player);
    bool SelectObjective(PlayerbotAI* ai, WorldLocation& objective);
    unsigned GetAssignedNode(PlayerbotAI* ai);
    bool IsDefender(PlayerbotAI* ai);
    bool IsAtAssignedNode(PlayerbotAI* ai, float radius = 40.0f);
    bool CanUseNodeFlag(PlayerbotAI* ai, GameObject* flag);
    bool IsAllowedTarget(PlayerbotAI* ai, Unit* target);
}
