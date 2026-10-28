#pragma once
#include "playerbot.h"

namespace ai
{
    class ItemQualifier;

    class LootObject
    {
    public:
        LootObject() : skillId(0), reqSkillValue(0), reqItem(0) {}
        LootObject(Player* bot, ObjectGuid guid);
        LootObject(const LootObject& other);

    public:
        bool IsEmpty() { return !guid; }
        bool IsLootPossible(Player* bot);
        bool IsWithinLootDistance(Player* bot);
        void Refresh(Player* bot, ObjectGuid guid, bool debug = false);
        WorldObject* GetWorldObject(Player* bot);
        ObjectGuid guid;

        uint32 skillId;
        uint32 reqSkillValue;
        uint32 reqItem;
    };

    class LootTarget
    {
    public:
        LootTarget(ObjectGuid guid);
        LootTarget(LootTarget const& other);

    public:
        LootTarget& operator=(LootTarget const& other);
        bool operator< (const LootTarget& other) const;

    public:
        ObjectGuid guid;
        time_t asOfTime;
    };

    class LootTargetList : public std::set<LootTarget>
    {
    public:
        void shrink(time_t fromTime);
    };

    class LootObjectStack
    {
    public:
        LootObjectStack(Player* bot) : bot(bot) {}

    public:
        bool Add(ObjectGuid guid);
        void Remove(ObjectGuid guid);
        void Clear();
        bool CanLoot(float maxDistance);
        LootObject GetLoot(float maxDistance = 0);
        bool StartLoot(ObjectGuid guid);
        void CompleteLoot(ObjectGuid guid);
        void DeferLoot(ObjectGuid guid, uint32 delayMs = 5000);
        bool IsDeferred(ObjectGuid guid);
        void MarkLootOpened(ObjectGuid guid);
        bool IsLootOpening(ObjectGuid guid = ObjectGuid());
        bool ShouldHoldFollow();

    private:
        std::vector<LootObject> OrderByDistance(float maxDistance = 0);
        void ResetCurrentLoot(ObjectGuid guid);
        void PruneLootRetries(uint32 now);

    private:
        Player* bot;
        LootTargetList availableLoot;
        struct LootRetry
        {
            uint32 started;
            uint32 duration;
        };
        std::map<ObjectGuid, LootRetry> lootRetries;
        ObjectGuid activeLoot;
        uint32 lootStarted = 0;
        uint32 lootProgress = 0;
        uint32 lootOpened = 0;
        bool lootOpening = false;
        float lootProgressX = 0;
        float lootProgressY = 0;
        float lootProgressZ = 0;
        float lootBestDistance = 0;
    };

};
