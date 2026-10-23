#include "playerbot/WarsongCarrierAbilities.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    unsigned checks = 0;
    void Check(bool result, const std::string& description)
    {
        ++checks;
        if (!result)
        {
            std::cerr << "FAILED: " << description << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace ai::warsong;
    CarrierAbilitySituation base;
    base.activeCarrier = true;
    base.kind = CarrierKind::Druid;
    base.outdoors = true;
    base.objectiveDistance = 100.0f;
    base.travelReady = base.catReady = base.bearReady = true;

    Check(ChooseCarrierAbility(base) == CarrierAbility::TravelForm, "druid accelerates an outdoor return without burning a cooldown");
    auto state = base;
    state.objectiveDistance = 0.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "safe hiding point does not trigger repeated form changes");
    state = base;
    state.inTravel = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "active travel form is preserved");
    state = base;
    state.outdoors = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "travel form is not attempted in a tunnel");
    state = base;
    state.dashReady = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::CatForm, "dash prepares cat form before using the cat-only ability");
    state.inCat = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::Dash, "prepared cat uses ready dash on a long return");
    state.dashActive = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "dash is not lost to an unnecessary travel-form switch");
    state = base;
    state.dashReady = true;
    state.objectiveDistance = 25.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::TravelForm, "short return preserves dash cooldown");
    state = base;
    state.dashReady = true;
    state.catReady = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::TravelForm, "unavailable cat form cannot reserve an uncastable dash sequence");

    state = base;
    state.pressured = true;
    state.healthPercent = 60.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::BearForm, "wounded druid under melee pressure chooses armor over speed");
    state.barkskinReady = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::Barkskin, "castable barkskin is used before the defensive form");
    state.barkskinActive = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::BearForm, "an existing barkskin is not recast");
    state.inBear = true;
    state.healthPercent = 75.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "bear hysteresis prevents toggling after a small heal");
    state.healthPercent = 85.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::TravelForm, "well-healed bear can resume the escape");
    state.pressured = false;
    state.healthPercent = 50.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::TravelForm, "cleared pursuers release a wounded bear to travel");

    state = base;
    state.inTravel = true;
    state.rooted = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::CatForm, "rooted travel form changes directly into a different ready form");
    state.inTravel = false;
    state.inCat = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::TravelForm, "rooted cat changes directly to outdoor travel form");
    state.inCat = false;
    state.slowed = true;
    state.rooted = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::TravelForm, "a movement slow is also cleared with a legal form change");
    state.formChangeAllowed = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "recent form changes cannot spend mana every action tick");
    state.inCat = state.rooted = state.dashReady = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "a throttled root escape does not waste dash before the next legal shift");
    state = base;
    state.rooted = true;
    state.catReady = state.travelReady = state.bearReady = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "insufficient mana or cooldowns do not cancel the existing form");
    state = base;
    state.rooted = true;
    state.rootCanBeShifted = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "a root without a core-removable mechanic cannot cause repeated form changes");
    state.pressured = true;
    state.healthPercent = 40.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::BearForm, "an unbreakable root still permits a useful defensive bear change");
    state = base;
    state.rooted = true;
    state.pressured = true;
    state.healthPercent = 40.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::BearForm, "root escape under heavy pressure still selects the defensive form");

    state = {};
    state.activeCarrier = true;
    state.kind = CarrierKind::Rogue;
    state.sprintReady = true;
    state.objectiveDistance = 70.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::Sprint, "rogue uses sprint on a long return");
    state.sprintActive = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "active sprint is not refreshed");
    state.sprintActive = false;
    state.rooted = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "sprint is not wasted on an unresolved root");
    state.rooted = false;
    state.objectiveDistance = 25.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "an unpressured short run saves sprint");
    state.pursued = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::Sprint, "a nearby pursuer justifies sprint on a shorter return");

    state = {};
    state.activeCarrier = true;
    state.kind = CarrierKind::Mage;
    state.blinkReady = true;
    state.safeBlink = true;
    state.objectiveDistance = 70.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::Blink, "mage blinks only along a vetted return segment");
    state.safeBlink = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "a ready blink cannot bypass an unsafe path segment");
    state.safeBlink = true;
    state.blinkReady = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "unknown unlearned or cooling blink is not requested");
    state.blinkReady = true;
    state.objectiveDistance = 0.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "a mage at the hold point does not blink for no reason");

    state = {};
    state.activeCarrier = true;
    state.kind = CarrierKind::Shaman;
    state.wolfReady = true;
    state.outdoors = true;
    state.objectiveDistance = 100.0f;
    Check(ChooseCarrierAbility(state) == CarrierAbility::GhostWolf, "unpressured shaman can prepare ghost wolf");
    state.pursued = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "shaman does not stop to hard-cast wolf while pursued");
    state.wolfInstant = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::GhostWolf, "an actually instant wolf remains useful during pursuit");
    state.inWolf = true;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "active wolf is not repeatedly refreshed");
    state.inWolf = false;
    state.outdoors = false;
    Check(ChooseCarrierAbility(state) == CarrierAbility::None, "outdoor-only wolf is not attempted inside");

    for (CarrierKind kind : {CarrierKind::Druid, CarrierKind::Rogue, CarrierKind::Mage, CarrierKind::Shaman})
    {
        state = base;
        state.kind = kind;
        state.activeCarrier = false;
        state.rooted = state.pressured = true;
        state.blinkReady = state.safeBlink = state.sprintReady = state.wolfReady = true;
        Check(ChooseCarrierAbility(state) == CarrierAbility::None, "flag drop immediately disables every dedicated escape skill");
        state.activeCarrier = true;
        state.objectiveDistance = std::numeric_limits<float>::quiet_NaN();
        Check(ChooseCarrierAbility(state) == CarrierAbility::None, "unknown objective distance cannot request escape skills");
    }

    Check(SafeCarrierBlink(28.0f, 3.0f, true, true), "blink safety boundaries permit a clear level-enough segment");
    Check(SafeCarrierBlink(50.0f, -3.0f, true, true), "a modest downward segment remains valid");
    Check(!SafeCarrierBlink(27.99f, 0.0f, true, true), "blink cannot overshoot a near flag or hold point");
    Check(!SafeCarrierBlink(60.0f, 3.01f, true, true), "blink rejects a steep rise");
    Check(!SafeCarrierBlink(60.0f, -3.01f, true, true), "blink rejects a steep drop");
    Check(!SafeCarrierBlink(60.0f, 0.0f, false, true), "blink rejects a wall across its segment");
    Check(!SafeCarrierBlink(60.0f, 0.0f, true, false), "blink is not simulated while falling jumping or swimming");
    Check(!SafeCarrierBlink(std::numeric_limits<float>::infinity(), 0.0f, true, true), "invalid objective cannot authorize blink");
    Check(!SafeCarrierBlink(60.0f, std::numeric_limits<float>::quiet_NaN(), true, true), "unknown terrain height cannot authorize blink");
    Check(ShapeshiftRemovesSlow(true, false, false), "ordinary snare mechanics can be cleared by a form change");
    Check(!ShapeshiftRemovesSlow(false, false, false), "mechanic-free encounter slows cannot reserve form changes");
    Check(!ShapeshiftRemovesSlow(true, true, false), "core-preserved crowd control or daze is not treated as shift-removable");
    Check(!ShapeshiftRemovesSlow(true, false, true), "legacy daze icon and dispel combination is preserved");
    Check(!ShapeshiftRemovesSlow(true, true, true), "multiple preservation reasons remain nonremovable");

    std::cout << checks << " Warsong carrier ability checks passed\n";
    return EXIT_SUCCESS;
}
