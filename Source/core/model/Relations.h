// Chord Relations (PLAN.md §3.5, Phase 6; our own design: Klimper's manual doesn't describe its
// button). Plain C++, no JUCE; std::vector and std::string, for the message thread and tests.
// (In core/model rather than PLAN.md §4.1's core/theory: it builds on Substitutes.h and the
// timeline's chords, which live here.)
//
// For a *focus* chord (the palette cell or timeline chord last chosen), every palette chord gets:
//
//   function     dominant    a major or dominant-7th chord a 5th above the focus: it leads to it
//                            ("V/vi", "V7/vi"; just "V" or "V7" when the focus is the home tonic)
//                subdominant a major (without a b7) or minor chord a 4th above ("IV/vi", "iv"…)
//                relative    the relative minor of a major focus (a minor 3rd below), the relative
//                            major (without a b7) of a minor one ("rel")
//                A dominant-7th chord is read only as a dominant or a tritone sub: D7b9 a 4th
//                above Am leads to G, it isn't Am's subdominant.
//                tritone sub a dominant 7th a semitone above the focus (it leads to it like V/x
//                            does), or, for a dominant focus, the 7th a tritone away that stands
//                            in for it ("sub")
//   common       the pitch classes it shares with the focus
//   distance     the voice-leading distance: the fewest semitones the voices move from one to
//                the other (every note of the larger chord moves to a note of the smaller,
//                each of those used at least once; up or down, the shorter way)
//
// Next ▸ (nextChords): five follow-ups for the focus from a small transition table of our own,
// by the focus's degree in the home scale (one table for homes with a major 3rd, one for a
// minor 3rd), as the home palette's triads; a secondary dominant (a non-diatonic major or
// dominant chord whose root a 5th down is in the home scale) suggests that target first;
// other borrowed chords suggest I (i), IV (iv), V and the rest of the tonic's row.
//
// Replace with ▸ (replacementsFor): Substitutes.h's list (a 3rd below and above, parallel, sus,
// the 9th, the 7th, tritone sub), then the home palette's chords sharing two or more notes with
// it, nearest first by voice-leading distance; each once, at most `limit`.
#pragma once

#include "Substitutes.h"
#include "TimelineLayout.h"
#include <algorithm>
#include <array>
#include <climits>
#include <string>
#include <vector>

namespace chordzimmer
{
    namespace relations_detail
    {
        inline int mod12 (int v) noexcept { return ((v % 12) + 12) % 12; }
        inline bool has (unsigned pcs, int pc) noexcept { return ((pcs >> mod12 (pc)) & 1u) != 0; }
        inline int countBits (unsigned m) noexcept
        {
            int n = 0;
            for (int i = 0; i < 12; ++i)
                n += (int) ((m >> i) & 1u);
            return n;
        }
        // The chord's intervals above its root.
        inline unsigned rel (unsigned pcs, int root) noexcept
        {
            unsigned r = 0;
            for (int pc = 0; pc < 12; ++pc)
                if (has (pcs, pc))
                    r |= 1u << mod12 (pc - root);
            return r;
        }
        inline bool majorQuality (unsigned r) noexcept { return has (r, 4) && has (r, 7) && ! has (r, 3); }
        inline bool minorQuality (unsigned r) noexcept { return has (r, 3) && has (r, 7) && ! has (r, 4); }
        inline bool dominantQuality (unsigned r) noexcept { return has (r, 4) && has (r, 10) && ! has (r, 11); }
        inline bool dominantOrMajor (unsigned r) noexcept { return (majorQuality (r) || dominantQuality (r)) && ! has (r, 11); }

        // The Roman numeral of a chord without its quality suffix ("ii7" → "ii", "bVII" → "bVII").
        inline std::string bareNumeral (const std::string& n)
        {
            size_t i = 0;
            while (i < n.size() && (n[i] == 'b' || n[i] == '#'))
                ++i;
            while (i < n.size() && (n[i] == 'I' || n[i] == 'V' || n[i] == 'i' || n[i] == 'v'))
                ++i;
            return n.substr (0, i);
        }
    }

    // A chord as relations see it: its pitch classes and root.
    struct ChordId
    {
        unsigned pcs = 0;
        int root = -1;

        bool valid() const noexcept { return pcs != 0 && root >= 0; }
        static ChordId of (const PaletteCell& c) noexcept { return c.exists ? ChordId { c.pitchClasses, c.root } : ChordId {}; }
        static ChordId of (const ProgressionChord& c) noexcept { return c.chord.empty() ? ChordId {} : ChordId { c.pitchClasses(), c.root() }; }
        bool operator== (const ChordId& o) const noexcept { return pcs == o.pcs && root == o.root; }
    };

    // The voice-leading distance between two pitch-class sets (see above); 0 for the same set,
    // -1 if either is empty.
    inline int voiceLeadingDistance (unsigned a, unsigned b)
    {
        using namespace relations_detail;
        std::vector<int> x, y;
        for (int pc = 0; pc < 12; ++pc)
        {
            if (has (a, pc)) x.push_back (pc);
            if (has (b, pc)) y.push_back (pc);
        }
        if (x.empty() || y.empty())
            return -1;
        if (x.size() < y.size())
            std::swap (x, y);                       // x: the larger; y: the smaller, all covered
        const size_t m = y.size();
        const auto step = [] (int p, int q) { const int d = mod12 (p - q); return std::min (d, 12 - d); };
        // best[mask]: the least cost of the notes of x so far, covering the notes of y in mask.
        std::vector<int> best ((size_t) 1 << m, INT_MAX);
        best[0] = 0;
        for (int p : x)
        {
            std::vector<int> next ((size_t) 1 << m, INT_MAX);
            for (size_t mask = 0; mask < best.size(); ++mask)
                if (best[mask] != INT_MAX)
                    for (size_t j = 0; j < m; ++j)
                    {
                        const size_t nm = mask | ((size_t) 1 << j);
                        next[nm] = std::min (next[nm], best[mask] + step (p, y[j]));
                    }
            best = std::move (next);
        }
        return best.back();
    }

    struct Relation
    {
        enum Flag : uint8_t { Dominant = 1, Subdominant = 2, Relative = 4, TritoneSub = 8 };
        uint8_t flags = 0;
        int common = 0;                    // shared pitch classes
        int distance = -1;                 // voice-leading distance (semitones)
        std::string label;                 // the function, as the badge reads ("V/vi", "rel"…), or ""

        bool related() const noexcept { return flags != 0 || common >= 2; }
    };

    // How `other` relates to `focus`. `focusNumeral` is the focus's numeral in the home key
    // (palette or timelineChordLabel), for the labels; `focusIsTonic` drops the "/x".
    inline Relation relationTo (const ChordId& focus, const ChordId& other, const std::string& focusNumeral, bool focusIsTonic)
    {
        using namespace relations_detail;
        Relation r;
        if (! focus.valid() || ! other.valid())
            return r;
        r.common = countBits (focus.pcs & other.pcs);
        r.distance = voiceLeadingDistance (focus.pcs, other.pcs);
        if (focus == other)
            return r;

        const unsigned f = rel (focus.pcs, focus.root), o = rel (other.pcs, other.root);
        const int up = mod12 (other.root - focus.root);
        const std::string of = focusIsTonic ? std::string() : "/" + bareNumeral (focusNumeral);
        const bool seventh = has (o, 10);
        if (up == 7 && dominantOrMajor (o))
        {
            r.flags |= Relation::Dominant;
            r.label = (seventh ? "V7" : "V") + of;
        }
        else if (up == 5 && ((majorQuality (o) && ! seventh) || minorQuality (o)))
        {
            r.flags |= Relation::Subdominant;
            r.label = (minorQuality (o) ? "iv" : "IV") + of;
        }
        if ((majorQuality (f) && minorQuality (o) && up == 9) || (minorQuality (f) && majorQuality (o) && ! seventh && up == 3))
        {
            r.flags |= Relation::Relative;
            if (r.label.empty())
                r.label = "rel";
        }
        if (dominantQuality (o) && ((up == 1 && ! dominantQuality (f)) || (up == 6 && dominantQuality (f))))
        {
            r.flags |= Relation::TritoneSub;
            if (r.label.empty())
                r.label = "sub";
        }
        return r;
    }

    // Each cell of `grid` against `focus` (empty relations for empty cells).
    inline std::array<std::array<Relation, numPaletteColumns>, numTierVariants> paletteRelations (const PaletteGrid& grid, const ChordId& focus,
                                                                                                   const std::string& focusNumeral, bool focusIsTonic)
    {
        std::array<std::array<Relation, numPaletteColumns>, numTierVariants> out {};
        for (size_t v = 0; v < grid.rows.size(); ++v)
            for (size_t d = 0; d < (size_t) numPaletteColumns; ++d)
                out[v][d] = relationTo (focus, ChordId::of (grid.rows[v].cells[d]), focusNumeral, focusIsTonic);
        return out;
    }

    //==========================================================================
    // Next ▸

    namespace relations_detail
    {
        // Our transition table: from a degree (0..6) of the home scale, five degrees to go to,
        // most usual first. Built from common-practice tendencies (PLAN.md §3.5), not copied.
        constexpr int majorNext[7][5] = {
            { 3, 4, 5, 1, 2 },        // I    → IV V vi ii iii
            { 4, 6, 3, 0, 5 },        // ii   → V vii° IV I vi
            { 5, 3, 1, 0, 4 },        // iii  → vi IV ii I V
            { 4, 0, 1, 6, 5 },        // IV   → V I ii vii° vi
            { 0, 5, 3, 2, 1 },        // V    → I vi IV iii ii
            { 1, 3, 4, 2, 0 },        // vi   → ii IV V iii I
            { 0, 2, 5, 4, 3 },        // vii° → I iii vi V IV
        };
        constexpr int minorNext[7][5] = {
            { 3, 5, 6, 4, 2 },        // i    → iv VI VII v III
            { 4, 0, 6, 3, 5 },        // ii°  → v i VII iv VI
            { 5, 3, 6, 0, 1 },        // III  → VI iv VII i ii°
            { 4, 0, 6, 1, 5 },        // iv   → v i VII ii° VI
            { 0, 5, 3, 2, 6 },        // v    → i VI iv III VII
            { 3, 6, 1, 0, 2 },        // VI   → iv VII ii° i III
            { 2, 0, 5, 3, 4 },        // VII  → III i VI iv v
        };
    }

    // Up to `count` palette cells to follow `focus` (the home palette's triads; the dominant
    // degree as its 7th chord when the palette has one).
    inline std::vector<PaletteCell> nextChords (const ChordId& focus, int homeKey, Scale homeScale, int count = 5)
    {
        using namespace relations_detail;
        std::vector<PaletteCell> out;
        if (! focus.valid())
            return out;
        PaletteContext ctx;
        ctx.paletteKey = ctx.homeKey = mod12 (homeKey);
        ctx.paletteScale = ctx.homeScale = homeScale;
        const PaletteGrid grid = buildPalette (ctx);
        const PaletteRow* triads = grid.row ("triad");
        const PaletteRow* sevenths = grid.row ("7");
        const bool minorHome = scaleInfo (homeScale).semitones[2] == 3;

        int degree = -1;
        for (int d = 0; d < 7; ++d)
            if (grid.columns[(size_t) d].root == mod12 (focus.root))
                degree = d;

        const auto add = [&] (int d)
        {
            if (d < 0 || d >= 7 || (int) out.size() >= count)
                return;
            const PaletteCell* c = &triads->cells[(size_t) d];
            if (d == 4 && sevenths != nullptr && sevenths->cells[(size_t) d].exists
                && dominantQuality (rel (sevenths->cells[(size_t) d].pitchClasses, sevenths->cells[(size_t) d].root)))
                c = &sevenths->cells[(size_t) d];
            if (! c->exists || (c->pitchClasses == focus.pcs && c->root == focus.root))
                return;
            for (const auto& x : out)
                if (x.pitchClasses == c->pitchClasses && x.root == c->root)
                    return;
            out.push_back (*c);
        };

        const unsigned f = rel (focus.pcs, focus.root);
        const bool diatonicChord = degree >= 0 && (focus.pcs & ~scalePitchClasses (ctx.homeKey, homeScale)) == 0;
        if (! diatonicChord)
        {
            // A secondary dominant first resolves to its target.
            if (dominantOrMajor (f))
                for (int d = 0; d < 7; ++d)
                    if (grid.columns[(size_t) d].root == mod12 (focus.root + 5))
                        add (d);
            for (int d : { 0, 3, 4, 5, 1, 2, 6 })
                add (d);
            return out;
        }
        for (int d : (minorHome ? minorNext : majorNext)[degree])
            add (d);
        return out;
    }

    //==========================================================================
    // Replace with ▸

    struct Replacement
    {
        WorkspaceChord chord;
        std::string reason;
    };

    inline std::vector<Replacement> replacementsFor (const ProgressionChord& c, int homeKey, Scale homeScale, size_t limit = 12)
    {
        std::vector<Replacement> out;
        if (c.chord.empty())
            return out;
        const auto seen = [&] (const WorkspaceChord& w)
        {
            if (w.root == c.chord.root && w.relativeMask() == c.chord.relativeMask())
                return true;
            return std::any_of (out.begin(), out.end(), [&] (const Replacement& r)
                                { return r.chord.root == w.root && r.chord.relativeMask() == w.relativeMask(); });
        };
        for (const auto& s : substitutesFor (c.chord, homeKey, homeScale))
            if (! seen (s.chord) && out.size() < limit)
                out.push_back ({ s.chord, s.reason });

        // The home palette's chords sharing two or more notes, nearest first.
        PaletteContext ctx;
        ctx.paletteKey = ctx.homeKey = relations_detail::mod12 (homeKey);
        ctx.paletteScale = ctx.homeScale = homeScale;
        const PaletteGrid grid = buildPalette (ctx);
        struct Near { int distance, common; PaletteCell cell; };
        std::vector<Near> near;
        const unsigned pcs = c.pitchClasses();
        for (const auto& row : grid.rows)
            for (const auto& cell : row.cells)
                if (cell.exists)
                {
                    const int common = relations_detail::countBits (pcs & cell.pitchClasses);
                    if (common >= 2)
                        near.push_back ({ voiceLeadingDistance (pcs, cell.pitchClasses), common, cell });
                }
        std::stable_sort (near.begin(), near.end(), [] (const Near& a, const Near& b)
        {
            return a.distance != b.distance ? a.distance < b.distance : a.common > b.common;
        });
        for (const auto& n : near)
        {
            if (out.size() >= limit)
                break;
            const WorkspaceChord w = progressionChordFromCell (n.cell).chord;
            if (seen (w))
                continue;
            out.push_back ({ w, std::to_string (n.common) + " notes in common, " + std::to_string (n.distance)
                                    + (n.distance == 1 ? " semitone" : " semitones") + " apart" });
        }
        return out;
    }
}
