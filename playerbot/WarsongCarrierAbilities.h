#pragma once

#include <cmath>

namespace ai
{
namespace warsong
{
    enum class CarrierKind { Other, Druid, Rogue, Mage, Shaman };
    enum class CarrierAbility { None, TravelForm, CatForm, BearForm, Dash, Barkskin, Sprint, Blink, GhostWolf };

    struct CarrierAbilitySituation
    {
        CarrierKind kind = CarrierKind::Other;
        bool activeCarrier = false;
        bool pressured = false;
        bool pursued = false;
        bool rooted = false;
        bool rootCanBeShifted = true;
        bool slowed = false;
        bool outdoors = false;
        bool inCat = false;
        bool inBear = false;
        bool inTravel = false;
        bool inWolf = false;
        bool dashActive = false;
        bool sprintActive = false;
        bool barkskinActive = false;
        bool formChangeAllowed = true;
        bool travelReady = false;
        bool catReady = false;
        bool bearReady = false;
        bool dashReady = false;
        bool barkskinReady = false;
        bool sprintReady = false;
        bool blinkReady = false;
        bool wolfReady = false;
        bool wolfInstant = false;
        bool safeBlink = false;
        float healthPercent = 100.0f;
        float objectiveDistance = 0.0f;
    };

    inline bool SafeCarrierBlink(float objectiveDistance, float heightChange, bool lineOfSight, bool grounded)
    {
        // A full blink must make useful progress without overshooting a flag/hold
        // point, crossing a wall, or taking the carrier over a steep edge.
        return grounded && lineOfSight && std::isfinite(objectiveDistance) &&
            objectiveDistance >= 28.0f && std::isfinite(heightChange) && std::fabs(heightChange) <= 3.0f;
    }

    inline bool ShapeshiftRemovesSlow(bool hasMechanic, bool preservedMechanic, bool specialDaze)
    {
        return hasMechanic && !preservedMechanic && !specialDaze;
    }

    inline CarrierAbility ChooseCarrierAbility(const CarrierAbilitySituation& state)
    {
        if (!state.activeCarrier || !std::isfinite(state.healthPercent) ||
            state.healthPercent < 0.0f || state.healthPercent > 100.0f ||
            !std::isfinite(state.objectiveDistance) || state.objectiveDistance < 0.0f)
            return CarrierAbility::None;

        const bool travelling = state.objectiveDistance > 18.0f;
        const bool impaired = (state.rooted && state.rootCanBeShifted) || state.slowed;
        if (!travelling && !state.pressured && !state.pursued && !impaired)
            return CarrierAbility::None;

        switch (state.kind)
        {
        case CarrierKind::Druid:
            if (state.pressured && state.healthPercent <= 70.0f &&
                state.barkskinReady && !state.barkskinActive)
                return CarrierAbility::Barkskin;

            // Cast a different form directly. The core applies the new form and
            // removes roots/slows atomically; never cancel a form hoping to cast later.
            if (impaired && state.formChangeAllowed)
            {
                if (state.pressured && state.healthPercent <= 65.0f && state.bearReady && !state.inBear)
                    return CarrierAbility::BearForm;
                if (state.outdoors && state.travelReady && !state.inTravel)
                    return CarrierAbility::TravelForm;
                if (state.catReady && !state.inCat)
                    return CarrierAbility::CatForm;
                if (state.bearReady && !state.inBear)
                    return CarrierAbility::BearForm;
            }
            if (state.pressured && state.healthPercent <= 65.0f && !state.inBear &&
                state.bearReady && state.formChangeAllowed)
                return CarrierAbility::BearForm;
            if (state.rooted)
                return CarrierAbility::None;
            // Hysteresis avoids oscillating bear/travel at the defensive threshold.
            if (state.inBear && state.pressured && state.healthPercent < 85.0f)
                return CarrierAbility::None;
            if (!travelling || state.dashActive)
                return CarrierAbility::None;
            if (state.objectiveDistance >= 35.0f && state.dashReady)
            {
                if (state.inCat) return CarrierAbility::Dash;
                if (state.catReady && state.formChangeAllowed) return CarrierAbility::CatForm;
            }
            if (state.outdoors && !state.inTravel && state.travelReady && state.formChangeAllowed)
                return CarrierAbility::TravelForm;
            break;
        case CarrierKind::Rogue:
            if (travelling && !state.rooted && state.sprintReady && !state.sprintActive &&
                (state.objectiveDistance >= 35.0f || state.pursued || state.pressured))
                return CarrierAbility::Sprint;
            break;
        case CarrierKind::Mage:
            if (state.blinkReady && state.safeBlink && (travelling || state.rooted || state.pressured))
                return CarrierAbility::Blink;
            break;
        case CarrierKind::Shaman:
            // An untalented cast-time wolf is prepared while clear of pursuit;
            // it must not stop a carrier to hard-cast under pressure.
            if (travelling && state.outdoors && !state.inWolf && state.wolfReady &&
                !state.rooted && (!(state.pressured || state.pursued) || state.wolfInstant))
                return CarrierAbility::GhostWolf;
            break;
        default:
            break;
        }
        return CarrierAbility::None;
    }
}
}
