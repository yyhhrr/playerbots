#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/TravelNode.h"

namespace ai
{
    class LastMovement
    {
    public:
        LastMovement()
        {
            clear();
        }

        LastMovement(LastMovement& other)
        {
            taxiNodes = other.taxiNodes;
            taxiMaster = other.taxiMaster;
            lastFollow = other.lastFollow;
            lastAreaTrigger = other.lastAreaTrigger;
            lastTransportEntry = other.lastTransportEntry;
            lastFlee = other.lastFlee;
            lastPath = other.lastPath;
            lastMoveShort = other.lastMoveShort;
            nextTeleport = other.nextTeleport;
            dungeonGoal = other.dungeonGoal;
            dungeonSplineId = other.dungeonSplineId;
            dungeonRepathTime = other.dungeonRepathTime;
            dungeonPurpose = other.dungeonPurpose;
            dungeonWalking = other.dungeonWalking;
            dungeonHazards = other.dungeonHazards;
            moveEvent = Event();
        }

        void clear()
        {
            lastPath.clear();
            lastFollow = NULL;
            lastAreaTrigger = 0;
            lastTransportEntry = 0;
            lastFlee = 0;
            lastMoveShort = WorldPosition();
            nextTeleport = 0;
            dungeonGoal = WorldPosition();
            dungeonSplineId = 0;
            dungeonRepathTime = 0;
            dungeonPurpose = 0;
            dungeonWalking = false;
            dungeonHazards.clear();
            moveEvent = Event();
        }

        void Set(Unit* lastFollow)
        {
            setPath(TravelPath());
            this->lastFollow = lastFollow;
        }

        void setPath(TravelPath path) { lastPath = path; }
    public:
        std::vector<uint32> taxiNodes;
        ObjectGuid taxiMaster;
        Unit* lastFollow;
        uint32 lastAreaTrigger;
        uint32 lastTransportEntry;
        time_t lastFlee;
        TravelPath lastPath;
        WorldPosition lastMoveShort;
        time_t nextTeleport;
        WorldPosition dungeonGoal;
        uint32 dungeonSplineId;
        uint32 dungeonRepathTime;
        uint8 dungeonPurpose;
        bool dungeonWalking;
        std::vector<std::pair<WorldPosition, float>> dungeonHazards;
        Event moveEvent;
    };

    class LastMovementValue : public ManualSetValue<LastMovement&>
    {
    public:
        LastMovementValue(PlayerbotAI* ai) : ManualSetValue<LastMovement&>(ai, data) {}
    private:
        LastMovement data = LastMovement();
    };

    class StayTimeValue : public ManualSetValue<time_t>
    {
    public:
        StayTimeValue(PlayerbotAI* ai) : ManualSetValue<time_t>(ai, 0) {}
    };

    class LastLongMoveValue : public CalculatedValue<WorldPosition>
    {
    public:
        LastLongMoveValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "last long move", 30) {}

        WorldPosition Calculate() override;
    };


    class HomeBindValue : public CalculatedValue<WorldPosition>
    {
    public:
        HomeBindValue(PlayerbotAI* ai) : CalculatedValue<WorldPosition>(ai, "home bind", 30) {}

        WorldPosition Calculate() override;

        virtual std::string Format() override;
    };
}
