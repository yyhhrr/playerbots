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
