// Follow (1.2): where the palette shows the chord that's playing, so the editor can bring it on
// screen. The chord is a set of pitch classes over a root (the timeline's playing chord, or the
// notes sounding from a keyboard). The search goes round the circle of fifths from where the
// palette is now, nearest first, keeping its mode (PLAN.md §3.2: ◀ ▶):
//
//   exact   a cell with the same pitch classes and root: the first found
//   close   else, in the nearest palette that has one, a cell on the same root with the same 3rd
//           (minor, major or none): the one there that shares most notes and adds or misses
//           fewest, then the simpler row (an imported Em11 shows as Em7 or Em9). The key moves
//           for a chord that's exactly in another palette, not for a nearer likeness
//
// Plain C++, no JUCE; header-only, for the message thread and tests.
#pragma once

#include "CircleOfFifths.h"
#include "PaletteBuilder.h"
#include <cstdlib>
#include <vector>

namespace chordzimmer
{
    struct FollowSpot
    {
        bool found = false, exact = false;
        int fifths = 0;                   // the navigator's steps from home for the palette holding it
        int variant = -1, degree = -1;    // the cell: its row and column
        unsigned pitchClasses = 0;        // the cell's notes and root (to light it)
        int root = -1;
    };

    namespace follow_detail
    {
        inline int bits (unsigned m) noexcept
        {
            int n = 0;
            for (; m != 0; m &= m - 1)
                ++n;
            return n;
        }

        // The 3rd above `root` in `pcs`: 1 minor, 2 major, 0 neither.
        inline int third (unsigned pcs, int root) noexcept
        {
            if (((pcs >> ((root + 3) % 12)) & 1u) != 0)
                return 1;
            return ((pcs >> ((root + 4) % 12)) & 1u) != 0 ? 2 : 0;
        }
    }

    // The palettes the search visits: the navigator's own first, then a fifth either side, two …
    // (-6 … +6 from home, as the navigator reaches them).
    inline std::vector<int> followOrder (int fifths)
    {
        std::vector<int> order { fifths };
        for (int d = 1; d <= 2 * maxFifths; ++d)
            for (int f : { fifths - d, fifths + d })
                if (f >= -maxFifths && f <= maxFifths)
                    order.push_back (f);
        return order;
    }

    inline FollowSpot followSpot (unsigned pcs, int root, const CircleOfFifths& nav)
    {
        using namespace follow_detail;
        FollowSpot close;
        if (pcs == 0 || root < 0 || root > 11)
            return close;
        int bestScore = 0;
        bool closeSettled = false;       // a close one found in a nearer palette
        for (int f : followOrder (nav.fifths))
        {
            PaletteContext ctx;
            ctx.paletteKey = keyAtFifths (nav.homeKey, f);
            ctx.paletteScale = nav.paletteScale;
            ctx.homeKey = nav.homeKey;
            ctx.homeScale = nav.homeScale;
            ctx.fifths = f;
            const auto grid = buildPalette (ctx);
            for (int v = 0; v < numTierVariants; ++v)
                for (int d = 0; d < numPaletteColumns; ++d)
                {
                    const auto& cell = grid.rows[(size_t) v].cells[(size_t) d];
                    if (! cell.exists || cell.root != root)
                        continue;
                    if (cell.pitchClasses == pcs)
                        return { true, true, f, v, d, cell.pitchClasses, cell.root };
                    if (closeSettled || third (cell.pitchClasses, root) != third (pcs, root))
                        continue;
                    const int score = 2 * bits (cell.pitchClasses & pcs) - bits (cell.pitchClasses & ~pcs) - bits (pcs & ~cell.pitchClasses);
                    if (! close.found || score > bestScore)
                    {
                        close = { true, false, f, v, d, cell.pitchClasses, cell.root };
                        bestScore = score;
                    }
                }
            closeSettled = close.found;
        }
        return close;
    }
}
