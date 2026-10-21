#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
namespace arathi
{
    struct CaptureCandidate
    {
        std::uint64_t guid = 0;
        float distance = 0.0f;
        bool eligible = false;
        bool capturingSameFlag = false;
    };

    // The caller supplies candidates for one flag only. A human already opening
    // it needs cover just like a bot, even though humans cannot be assigned tasks.
    inline std::uint64_t ElectCapturer(const std::vector<CaptureCandidate>& candidates)
    {
        const CaptureCandidate* chosen = nullptr;
        for (const CaptureCandidate& candidate : candidates)
        {
            if (candidate.capturingSameFlag)
                return 0;

            if (!candidate.eligible || !candidate.guid ||
                !std::isfinite(candidate.distance) || candidate.distance < 0.0f)
                continue;

            if (!chosen || candidate.distance < chosen->distance ||
                (candidate.distance == chosen->distance && candidate.guid < chosen->guid))
                chosen = &candidate;
        }

        return chosen ? chosen->guid : 0;
    }
}
}
