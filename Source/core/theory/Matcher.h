// Match against notes (PLAN.md §3.4, G6; Phase 5): rank the palette's chords by how well they fit
// a set of notes. Plain C++, no JUCE; for the message thread and tests.
//
// The notes are a NoteSet: a weight per pitch class (the beats each was held, from Listen; or 1
// each, from the note picker). Weights are normalised to sum to 1, so a score doesn't depend on
// how long the phrase was. A chord scores (PLAN.md §3.4)
//
//     Σ weight of the notes that are chord tones, × 1.5 for its root and × 1.25 for its 3rd
//   − 0.75 × Σ weight of the notes that clash (a semitone above a chord tone)
//   − 0.15 for each colour tone the chord has (anything but root, 3rd and 5th), played or not
//   − 0.01 × its tier (a lower tier first between near-equals)
//
// Notes that are neither (passing tones a tone away) count nothing. A colour tone pays for
// itself only when it's held for more than 15 % of the phrase, so a triad that holds the long
// notes wins over a 13th chord that holds every passing note too; a 7th, 9th or 6th the phrase
// dwells on wins over the plain triad.
//
// Candidates: every existing cell of the palette (its key and mode), and with `fifthsAround`
// also the palettes up to that many fifths either side (borrowed keys, same mode). A chord found
// in several palettes counts once, as found nearest the palette. Ties (scores within 1e-9):
// in the home key before borrowed (out of it), then the lower tier, then the nearer palette, then
// column and row order. Only chords with a positive score are returned, best first, at most
// `top`.
#pragma once

#include "CircleOfFifths.h"
#include "PaletteBuilder.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace chordzimmer
{
    struct NoteSet
    {
        std::array<double, 12> weight {};   // per pitch class, ≥ 0
        int bass = -1;                      // the lowest note's pitch class, if known

        double total() const noexcept
        {
            double t = 0.0;
            for (double w : weight)
                t += w > 0.0 ? w : 0.0;
            return t;
        }
        bool empty() const noexcept { return total() <= 0.0; }
        unsigned mask() const noexcept
        {
            unsigned m = 0;
            for (int pc = 0; pc < 12; ++pc)
                if (weight[(size_t) pc] > 0.0)
                    m |= 1u << pc;
            return m;
        }

        // The pitch classes in `mask`, 1 each (the note picker).
        static NoteSet fromMask (unsigned mask) noexcept
        {
            NoteSet s;
            for (int pc = 0; pc < 12; ++pc)
                s.weight[(size_t) pc] = ((mask >> pc) & 1u) != 0 ? 1.0 : 0.0;
            return s;
        }
    };

    struct MatchOptions
    {
        int fifthsAround = 0;               // also the palettes up to this many fifths away (PLAN.md: ±2)
        int top = 8;
        double rootWeight = 1.5, thirdWeight = 1.25, clashPenalty = 0.75, colourCost = 0.15, tierCost = 0.01;
    };

    struct ChordMatch
    {
        PaletteCell cell;                   // the chord, as its palette builds it (numeral relative to home)
        int fifths = 0;                     // the palette it came from: fifths from the given palette's key
        int paletteKey = 0;
        double score = 0.0;
        int rank = 0;                       // 1 = best
    };

    namespace matcher_detail
    {
        inline int mod12 (int v) noexcept { return ((v % 12) + 12) % 12; }
    }

    // One chord's score against `notes` (PLAN.md §3.4; see above).
    inline double matchScore (const NoteSet& notes, unsigned chordPcs, int root, Tier tier, const MatchOptions& o = {}) noexcept
    {
        using matcher_detail::mod12;
        const double total = notes.total();
        if (total <= 0.0 || chordPcs == 0)
            return 0.0;
        root = mod12 (root);
        const auto inChord = [chordPcs] (int pc) { return ((chordPcs >> mod12 (pc)) & 1u) != 0; };
        // The 3rd: the major 3rd if the chord has one (then a minor 3rd is a #9), else the minor.
        const auto isThird = [&] (int pc)
        {
            const int i = mod12 (pc - root);
            return inChord (pc) && (i == 4 || (i == 3 && ! inChord (root + 4)));
        };

        double score = 0.0;
        for (int pc = 0; pc < 12; ++pc)
        {
            const double w = notes.weight[(size_t) pc] > 0.0 ? notes.weight[(size_t) pc] / total : 0.0;
            if (w <= 0.0)
                continue;
            if (inChord (pc))
                score += w * (pc == root ? o.rootWeight : (isThird (pc) ? o.thirdWeight : 1.0));
            else if (inChord (pc - 1))
                score -= o.clashPenalty * w;
        }

        // Colour tones, played or not.
        for (int pc = 0; pc < 12; ++pc)
        {
            if (! inChord (pc))
                continue;
            const int i = mod12 (pc - root);
            const bool core = i == 0 || isThird (pc) || i == 6 || i == 7 || i == 8;
            if (! core)
                score -= o.colourCost;
        }
        return score - o.tierCost * (double) (int) tier;
    }

    // The best matches among the palette's chords (and, with fifthsAround, its neighbours').
    inline std::vector<ChordMatch> matchPalette (const NoteSet& notes, const PaletteContext& ctx, const MatchOptions& o = {})
    {
        std::vector<ChordMatch> all;
        if (notes.empty())
            return all;
        const int around = std::clamp (o.fifthsAround, 0, maxFifths);
        for (int f = -around; f <= around; ++f)
        {
            PaletteContext c = ctx;
            c.paletteKey = keyAtFifths (ctx.paletteKey, f);
            const PaletteGrid grid = buildPalette (c);
            for (const auto& row : grid.rows)
                for (const auto& cell : row.cells)
                    if (cell.exists)
                    {
                        const double s = matchScore (notes, cell.pitchClasses, cell.root, row.tier, o);
                        if (s > 0.0)
                            all.push_back ({ cell, f, c.paletteKey, s, 0 });
                    }
        }

        std::stable_sort (all.begin(), all.end(), [] (const ChordMatch& a, const ChordMatch& b)
        {
            if (std::abs (a.score - b.score) > 1.0e-9)
                return a.score > b.score;
            if (a.cell.inKey != b.cell.inKey)
                return a.cell.inKey;
            const Tier ta = tierVariants[a.cell.variant].tier, tb = tierVariants[b.cell.variant].tier;
            if (ta != tb)
                return (int) ta < (int) tb;
            if (std::abs (a.fifths) != std::abs (b.fifths))
                return std::abs (a.fifths) < std::abs (b.fifths);
            if (a.fifths != b.fifths)
                return a.fifths < b.fifths;
            if (a.cell.degree != b.cell.degree)
                return a.cell.degree < b.cell.degree;
            return a.cell.variant < b.cell.variant;
        });

        // The same chord (pitch classes and root) from several rows or palettes: the first.
        std::vector<ChordMatch> out;
        for (const auto& m : all)
        {
            const bool seen = std::any_of (out.begin(), out.end(), [&] (const ChordMatch& x)
                                           { return x.cell.pitchClasses == m.cell.pitchClasses && x.cell.root == m.cell.root; });
            if (seen)
                continue;
            out.push_back (m);
            out.back().rank = (int) out.size();
            if ((int) out.size() >= std::max (1, o.top))
                break;
        }
        return out;
    }
}
