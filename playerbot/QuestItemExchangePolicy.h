#pragma once

#include <cstdint>

namespace ai::quest_exchange_policy
{
    struct KeyInfo
    {
        unsigned itemClass = 0;
        unsigned quality = 0;
        unsigned bonding = 0;
        unsigned inventoryType = 0;
        bool explicitKey = false;
        unsigned maxCount = 0;
    };

    inline bool IsKeyItem(const KeyInfo& item)
    {
        if (item.explicitKey)
            return true;

        // Old equipment can itself be the key for an upgraded quest reward.
        if (item.inventoryType != 0)
            return (item.itemClass == 2 || item.itemClass == 4) && item.quality >= 2 &&
                (item.bonding == 1 || item.bonding == 2);

        // Unique quest objects include low-quality dungeon turn-ins such as Kelris's head.
        if ((item.itemClass == 12 || item.itemClass == 15) && item.maxCount == 1 &&
            (item.bonding == 1 || item.bonding == 4 || item.bonding == 5))
            return true;

        // Legendary quest objects such as the purified Atiesh key are not always bound.
        if ((item.itemClass == 12 || item.itemClass == 15) && item.quality >= 5)
            return true;

        // Bind-on-pickup rare quest/miscellaneous items identify exchange tokens.
        // Profession materials and common quest collection items are excluded.
        return (item.itemClass == 12 || item.itemClass == 15) &&
            item.quality >= 3 && item.bonding == 1;
    }

    inline bool IsWearableKey(const KeyInfo& item)
    {
        return item.inventoryType != 0;
    }

    inline bool IsAutomaticKey(const KeyInfo& item)
    {
        // Explicit configuration can recognize a manual input, never authorize
        // automatic destruction of equipment, including equipped bags.
        return !IsWearableKey(item) && IsKeyItem(item);
    }

    struct Candidate
    {
        std::uint32_t questId = 0;
        std::uint64_t scoreGain = 0;
        std::uint32_t quality = 0;
        std::uint32_t itemLevel = 0;
        std::uint32_t rewardId = 0;
        bool usable = false;
        bool keysAvailable = false;
        bool duplicate = false;
        bool forcedKeep = false;
    };

    inline bool IsEligible(const Candidate& candidate)
    {
        return candidate.usable && candidate.keysAvailable && !candidate.duplicate &&
            !candidate.forcedKeep && candidate.scoreGain > 0;
    }

    inline bool IsBetter(const Candidate& candidate, const Candidate& current)
    {
        if (!IsEligible(candidate))
            return false;
        if (!IsEligible(current))
            return true;
        if (candidate.scoreGain != current.scoreGain)
            return candidate.scoreGain > current.scoreGain;
        if (candidate.quality != current.quality)
            return candidate.quality > current.quality;
        if (candidate.itemLevel != current.itemLevel)
            return candidate.itemLevel > current.itemLevel;
        if (candidate.questId != current.questId)
            return candidate.questId < current.questId;
        return candidate.rewardId < current.rewardId;
    }
}
