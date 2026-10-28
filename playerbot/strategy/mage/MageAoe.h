#pragma once

#include "playerbot/strategy/Value.h"
#include "MageAoePolicy.h"
#include <vector>

namespace ai
{
    constexpr size_t MAGE_GROUND_AOE_MIN_TARGETS = MageAoeMinimumTargets;

    struct MageAoeTarget
    {
        WorldLocation position;
        std::vector<ObjectGuid> targets;
        float radius = 0.0f;
    };

    // Count and cast at the same spell-specific, visible ground location.
    bool FindMageAoeTarget(PlayerbotAI* ai, uint32 spellId, MageAoeTarget& result,
        bool avoidExistingFlamestrike = false);

    // Check an existing cast's fixed damage area without requiring another pack.
    bool IsMageAoeAreaSafe(PlayerbotAI* ai, uint32 spellId, float x, float y, float z);

    // Recheck a caster-centred explosion: three valid foes, local aggro and safe crowd control.
    Unit* FindMageArcaneExplosionTarget(PlayerbotAI* ai, bool levelingOnly = false);

    // Find the closest visible PvE opponent that is inside the mage's casting space.
    Unit* FindMagePveRetreatTarget(PlayerbotAI* ai);
}
