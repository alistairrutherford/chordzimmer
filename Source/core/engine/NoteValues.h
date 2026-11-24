// Note values for Strum (Chordism's PLAN.md §4.5).
//
// The order of each list is the order of a host parameter's choices: only ever append.
// Lengths are in quarter notes (PPQ). Plain C++.
#pragma once

#include <cmath>

namespace chordzimmer
{
    // Strum Rate: the time between strummed tones while the host plays.
    constexpr int numStrumRates = 7;

    inline const char* strumRateName (int i) noexcept
    {
        static constexpr const char* names[numStrumRates] = { "1/128", "1/64T", "1/64", "1/32T", "1/32", "1/16T", "1/16" };
        return names[i >= 0 && i < numStrumRates ? i : 0];
    }

    inline double strumRatePpq (int i) noexcept
    {
        static constexpr double ppq[numStrumRates] = { 1.0 / 32.0, 1.0 / 24.0, 1.0 / 16.0, 1.0 / 12.0, 0.125, 1.0 / 6.0, 0.25 };
        return ppq[i >= 0 && i < numStrumRates ? i : 0];
    }
}
