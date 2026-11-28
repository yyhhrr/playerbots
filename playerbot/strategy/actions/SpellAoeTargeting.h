#pragma once

#include "playerbot/strategy/Value.h"
#include <vector>

namespace ai
{
    struct GroundAoeTarget
    {
        WorldLocation position;
        std::vector<ObjectGuid> targets;
        float radius = 0.0f;
    };

    bool FindGroundAoeTarget(PlayerbotAI* ai, uint32 spellId, GroundAoeTarget& result,
        size_t minimumTargets, bool avoidExistingArea = false);

    bool IsSpellAoeAreaSafe(PlayerbotAI* ai, uint32 spellId, float x, float y, float z);

    // Caster-centred damage requires a nearby enemy attacking the caster.
    Unit* FindCloseAoeTarget(PlayerbotAI* ai, uint32 damageSpellId, float radiusLimit, size_t minimumTargets);
}
