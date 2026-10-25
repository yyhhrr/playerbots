#include "playerbot/ArathiCapturePolicy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    unsigned checks = 0;

    void Check(bool condition, const std::string& description)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << description << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace ai::arathi;

    CaptureThreat threat;
    threat.distance = 12.0f;
    threat.attackable = threat.visible = threat.lineOfSight = threat.directAttack = true;
    Check(IsImmediateCaptureThreat(threat), "a visible nearby attacker interrupts the flag opener");
    threat.directAttack = false;
    Check(!IsImmediateCaptureThreat(threat), "nearby enemies fighting teammates do not indefinitely block a safe capture");
    threat.spellTargetsBot = true;
    Check(IsImmediateCaptureThreat(threat), "a hostile cast targeting the opener counts before native attacker links are populated");
    threat.distance = 40.0f;
    Check(IsImmediateCaptureThreat(threat), "a direct ranged attacker at the response boundary interrupts capture");
    threat.distance = 40.1f;
    Check(!IsImmediateCaptureThreat(threat), "a distant stale attack link does not indefinitely block capture");
    threat.distance = 12.0f; threat.disabled = true;
    Check(!IsImmediateCaptureThreat(threat), "an enemy disabled by breakable control can be left controlled while opening");
    threat.disabled = false; threat.visible = false;
    Check(!IsImmediateCaptureThreat(threat), "an invisible attacker is not fabricated into a reachable response target");
    threat.visible = true; threat.lineOfSight = false;
    Check(!IsImmediateCaptureThreat(threat), "a blocked distant caster does not force an impossible response");
    threat.lineOfSight = true; threat.attackable = false;
    Check(!IsImmediateCaptureThreat(threat), "a dead friendly or unattackable cached enemy cannot block capture");
    threat.attackable = true; threat.distance = -1.0f;
    Check(!IsImmediateCaptureThreat(threat), "negative threat distance is invalid");
    threat.distance = std::numeric_limits<float>::quiet_NaN();
    Check(!IsImmediateCaptureThreat(threat), "unknown threat distance is invalid");
    threat.distance = std::numeric_limits<float>::infinity();
    Check(!IsImmediateCaptureThreat(threat), "an infinite threat distance cannot force a response");

    Check(!IsCaptureRetryHeld(0, 1000), "a fresh opener with no threat history is free to capture");
    Check(IsCaptureRetryHeld(1000, 1000), "a fresh interruption prevents an immediate capture retry");
    Check(IsCaptureRetryHeld(1000, 3499), "the interruption hold persists until its short cooldown ends");
    Check(!IsCaptureRetryHeld(1000, 3500), "safe capture resumes at the cooldown boundary");
    Check(IsCaptureRetryHeld(std::numeric_limits<std::uint32_t>::max() - 1000, 1000),
        "capture retry hold remains valid across the monotonic clock wrap");
    Check(!IsCaptureRetryHeld(std::numeric_limits<std::uint32_t>::max() - 1000, 2000),
        "capture retry hold expires normally after a monotonic clock wrap");

    Check(ElectCapturer({}) == 0, "empty flag candidate set has no capturer");
    Check(ElectCapturer({{11, 2.0f, true, false}}) == 11, "one available bot opens the flag");
    Check(ElectCapturer({{11, 2.0f, false, false}}) == 0, "ordinary human is not assigned capture duty");
    Check(ElectCapturer({{11, 2.0f, true, false}, {12, 1.0f, true, false}}) == 12,
        "closest eligible bot is selected");
    Check(ElectCapturer({{12, 1.0f, true, false}, {11, 1.0f, true, false}}) == 11,
        "equal distance is resolved by the lower GUID");

    std::vector<CaptureCandidate> flagOne = {{11, 1.0f, true, false}, {12, 2.0f, true, false},
        {90, 0.5f, false, true}};
    const std::vector<CaptureCandidate> flagTwo = {{21, 2.0f, true, false}, {22, 1.0f, true, false}};
    Check(ElectCapturer(flagOne) == 0, "human opening the same flag makes all bots cover");
    Check(ElectCapturer(flagTwo) == 22, "a capture at another flag does not block this flag");
    flagOne.back().capturingSameFlag = false;
    Check(ElectCapturer(flagOne) == 11, "human capture interruption lets an eligible bot take over");
    flagOne.front().capturingSameFlag = true;
    Check(ElectCapturer(flagOne) == 0, "bot already opening the flag receives cover");
    flagOne.front().capturingSameFlag = false;
    flagOne.front().eligible = false;
    Check(ElectCapturer(flagOne) == 12, "interrupted ineligible capturer is replaced by another bot");

    Check(ElectCapturer({{11, 0.1f, false, false}, {12, 0.2f, false, false},
        {13, 0.3f, false, false}, {14, 4.0f, true, false}}) == 14,
        "dead controlled or otherwise ineligible closer candidates are excluded");
    Check(ElectCapturer({{11, 0.1f, false, false}, {12, 0.2f, false, false}}) == 0,
        "all ineligible candidates leave the flag unassigned");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    Check(ElectCapturer({{1, nan, true, false}, {2, infinity, true, false},
        {3, -infinity, true, false}, {4, -0.1f, true, false}, {5, 4.0f, true, false}}) == 5,
        "nonfinite and negative distances are never elected");
    Check(ElectCapturer({{1, nan, true, false}, {2, infinity, true, false},
        {3, -1.0f, true, false}}) == 0, "only malformed distances leave no capturer");
    Check(ElectCapturer({{1, 0.0f, true, false}, {2, 0.1f, true, false}}) == 1,
        "zero distance is a valid nearest candidate");
    Check(ElectCapturer({{0, 0.0f, true, false}, {1, 1.0f, true, false}}) == 1,
        "empty GUID cannot be elected as a real capturer");
    Check(ElectCapturer({{1, 1.0f, true, false}, {2, nan, false, true}}) == 0,
        "an existing same-flag capture takes precedence over candidate scoring");

    std::vector<CaptureCandidate> shuffled = {{1, 2.0f, true, false}, {2, 1.0f, true, false},
        {3, 1.0f, true, false}, {4, 0.1f, false, false}};
    auto byGuid = [](const CaptureCandidate& left, const CaptureCandidate& right)
    {
        return left.guid < right.guid;
    };
    do
    {
        Check(ElectCapturer(shuffled) == 2, "election is independent of candidate iteration order");
    }
    while (std::next_permutation(shuffled.begin(), shuffled.end(), byGuid));

    shuffled.back().capturingSameFlag = true;
    do
    {
        Check(ElectCapturer(shuffled) == 0, "existing human capture blocks election in every iteration order");
    }
    while (std::next_permutation(shuffled.begin(), shuffled.end(), byGuid));

    std::cout << checks << " Arathi capture policy checks passed\n";
    return EXIT_SUCCESS;
}
