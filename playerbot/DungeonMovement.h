#pragma once

#include "WorldPosition.h"

class PlayerbotAI;
class WorldObject;
class Unit;

namespace ai::dungeon
{
    enum class MovePurpose { Local, Follow, Target };

    bool IsActive(PlayerbotAI* ai);
    bool BuildSafePath(PlayerbotAI* ai, const WorldPosition& destination,
        MovePurpose purpose, std::vector<WorldPosition>& path);
    bool CanMoveTo(PlayerbotAI* ai, const WorldPosition& destination, MovePurpose purpose = MovePurpose::Local);
    bool ValidatePath(PlayerbotAI* ai, const std::vector<WorldPosition>& path, MovePurpose purpose);
    bool CanUseWaterRoute(PlayerbotAI* ai, const WorldPosition& destination);
    bool IsSafeLocalPoint(PlayerbotAI* ai, const WorldPosition& reference, const WorldPosition& destination);
    bool CanTarget(PlayerbotAI* ai, const WorldObject* target);
    bool FindMeleeApproach(PlayerbotAI* ai, Unit* target, const WorldPosition& preferred, WorldPosition& result);
    bool CanLoot(PlayerbotAI* ai, const WorldObject* target);
}
