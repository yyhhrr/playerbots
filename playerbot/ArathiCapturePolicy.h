#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace ai
{
namespace arathi
{
    constexpr float CaptureThreatRadius = 40.0f;
    constexpr std::uint32_t CaptureRetryDelayMs = 2500;

    struct CaptureThreat
    {
        float distance = 0.0f;
        bool attackable = false;
        bool visible = false;
        bool lineOfSight = false;
        bool disabled = false;
        bool directAttack = false;
        bool spellTargetsBot = false;
    };

    inline bool IsImmediateCaptureThreat(const CaptureThreat& threat)
    {
        return std::isfinite(threat.distance) && threat.distance >= 0.0f &&
            threat.distance <= CaptureThreatRadius && threat.attackable &&
            threat.visible && threat.lineOfSight && !threat.disabled &&
            (threat.directAttack || threat.spellTargetsBot);
    }

    inline bool IsCaptureRetryHeld(std::uint32_t lastThreat, std::uint32_t now)
    {
        return lastThreat && std::uint32_t(now - lastThreat) < CaptureRetryDelayMs;
    }

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
