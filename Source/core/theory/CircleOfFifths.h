// The key navigator (PLAN.md §3.2): the palette key steps around the circle of fifths from the
// home key, the palette mode switches among the parallel modes of its family, and Home returns.
// Plain C++, no JUCE.
//
// The palette position is UI state saved with the plug-in, not a parameter. It is kept relative
// to home (a number of fifths), so changing the Key parameter carries the palette along.
#pragma once

#include "Scales.h"

namespace chordzimmer
{
    constexpr int maxFifths = 6;

    // The key `fifths` fifths above `key` (negative = below): +1 from C is G, -1 is F.
    constexpr int keyAtFifths (int key, int fifths) noexcept
    {
        return (((key + 7 * fifths) % 12) + 12) % 12;
    }

    // The shortest number of fifths from `from` to `to`, -5..+6 (+6 for the tritone).
    constexpr int fifthsBetween (int from, int to) noexcept
    {
        const int n = ((((to - from) * 7) % 12) + 12) % 12;   // 7 is its own inverse mod 12
        return n > maxFifths ? n - 12 : n;
    }

    struct CircleOfFifths
    {
        int homeKey = 0;
        Scale homeScale = Scale::Major;
        int fifths = 0;                    // the palette key's distance from home, -6..+6
        Scale paletteScale = Scale::Major;

        int paletteKey() const noexcept { return keyAtFifths (homeKey, fifths); }

        // Steps from home (±1 … ±6); 0 at the home key, whatever the mode. The tritone is +6
        // when reached sharpwards and -6 flatwards.
        int distance() const noexcept { return fifths; }

        bool atHome() const noexcept { return fifths == 0 && paletteScale == homeScale; }

        // ◀ ▶: a fifth down (-1) or up (+1), keeping the mode. Past ±6 it wraps round to ∓5, so
        // 12 steps either way come back home.
        void step (int direction) noexcept
        {
            fifths += direction > 0 ? 1 : (direction < 0 ? -1 : 0);
            if (fifths > maxFifths)
                fifths -= 12;
            else if (fifths < -maxFifths)
                fifths += 12;
        }

        // ⌂: back to the home key and mode.
        void goHome() noexcept
        {
            fifths = 0;
            paletteScale = homeScale;
        }

        // mode ▾: any scale (the menu offers parallelModes()); the palette key stays.
        void setMode (Scale s) noexcept { paletteScale = s; }

        // The modes the mode menu offers: the palette scale's family on the palette key.
        ParallelModes modes() const noexcept { return parallelModes (paletteScale); }

        // The Key or Scale parameter changed. A palette at home follows; one that is away keeps
        // its mode and its distance in fifths.
        void setHome (int key, Scale scale) noexcept
        {
            const bool wasHome = atHome();
            homeKey = ((key % 12) + 12) % 12;
            homeScale = scale;
            if (wasHome)
                paletteScale = scale;
        }
    };
}
