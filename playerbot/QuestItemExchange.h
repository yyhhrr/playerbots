#pragma once

#include "playerbot/playerbot.h"

namespace ai
{
    struct QuestItemExchangeItem
    {
        uint32 itemId = 0;
        uint32 count = 0;
        // Equipment slot selected by the loadout evaluator; unused for keys.
        uint8 slot = NULL_SLOT;
    };

    struct QuestItemExchangePlan
    {
        uint32 questId = 0;
        std::vector<QuestItemExchangeItem> keys;
        std::vector<QuestItemExchangeItem> rewards;
        uint64 scoreGain = 0;
    };

    class QuestItemExchange
    {
    public:
        // Automatic plans never consume wearable inputs. forLoot anticipates missing keys.
        static bool FindBestPlan(PlayerbotAI* ai, uint32 keyItemId, bool forLoot, QuestItemExchangePlan& plan);
        // Explicit requests may consume equipment inputs and work with automatic exchange disabled.
        static bool FindManualPlan(PlayerbotAI* ai, uint32 keyItemId, QuestItemExchangePlan& plan);
        // Retain a useful key while waiting for other keys or inventory space.
        static bool ShouldKeepKey(PlayerbotAI* ai, uint32 keyItemId);
        // Preserve an owned manual input without requesting extra loot or consuming it.
        static bool ShouldKeepForManualExchange(PlayerbotAI* ai, uint32 keyItemId);
        static bool IsKeyItem(uint32 itemId);
        static bool IsWearableKey(uint32 itemId);
    };
}
