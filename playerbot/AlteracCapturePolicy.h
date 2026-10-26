#pragma once

#include "ArathiCapturePolicy.h"

namespace ai::alterac
{
    constexpr std::uint32_t CaptureSpellId = 21651;
    using arathi::CaptureThreatRadius;
    using arathi::CaptureRetryDelayMs;
    using arathi::CaptureThreat;
    using arathi::IsImmediateCaptureThreat;
    using arathi::CaptureCandidate;
    using arathi::ElectCapturer;

    inline bool IsCaptureRetryHeld(bool armed, std::uint32_t lastThreat, std::uint32_t now)
    {
        return armed && std::uint32_t(now - lastThreat) < CaptureRetryDelayMs;
    }
}
