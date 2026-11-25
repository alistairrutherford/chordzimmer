// Builds the chord palette (PLAN.md §3.1): 7 degree columns of the palette's key and mode × the
// tier rows of PaletteTiers.h. Each cell carries its notes, root, name, Roman numeral, family
// and in-key flag. Plain C++, no JUCE; std::string, so for the UI and tests, not the audio
// thread.
//
// Two keys are involved (PLAN.md §3.2): the **palette** key and mode, which the navigator moves
// around the circle of fifths and through the parallel modes, and the **home** key and scale (the
// Key and Scale parameters). Cells are built in the palette's scale; their numerals and in-key
// flags are relative to home, so a borrowed column reads bVII or iv and a borrowed chord is
// hollow unless every note is in the home scale.
#pragma once

#include "ChordNamer.h"
#include "PaletteTiers.h"
#include <array>
#include <string>

namespace chordzimmer
{
    // A chord's colour family (PLAN.md §3.1, §5.1), from its 3rd and 5th, not its name: a major
    // 3rd and perfect 5th is major (C, C7, C7#9), a minor 3rd and perfect 5th without a major
    // 3rd is minor (Cm, Cm(maj7)), anything else is other (power, sus, dim, aug, m7b5).
    // Replaces ui::familyOf in Phase 4.
    enum class ChordFamily : uint8_t { Major, Minor, Other };

    // `mask`: bit pc for each pitch class; `rootPc`: the chord's root.
    inline ChordFamily chordFamily (unsigned mask, int rootPc) noexcept
    {
        rootPc = ((rootPc % 12) + 12) % 12;
        const auto has = [&] (int iv) { return ((mask >> ((rootPc + iv) % 12)) & 1u) != 0; };
        if (has (4) && has (7))
            return ChordFamily::Major;
        if (has (3) && has (7))
            return ChordFamily::Minor;
        return ChordFamily::Other;
    }

    // The pitch classes of a key and scale (bit pc).
    inline unsigned scalePitchClasses (int key, Scale scale) noexcept
    {
        unsigned m = 0;
        for (auto st : scaleInfo (scale).semitones)
            m |= 1u << (((key + st) % 12 + 12) % 12);
        return m;
    }

    struct PaletteContext
    {
        int paletteKey = 0;              // 0..11, C = 0
        Scale paletteScale = Scale::Major;
        int homeKey = 0;
        Scale homeScale = Scale::Major;
        int fifths = 0;                  // the palette key's steps from home: only its spelling's tie
    };

    constexpr int numPaletteColumns = 7;
    constexpr int maxPaletteNotes = 8;

    struct PaletteCell
    {
        bool exists = false;
        int degree = 0;                  // 0..6, the column
        int variant = -1;                // index into tierVariants (the row)
        std::array<uint8_t, maxPaletteNotes> notes {};   // MIDI notes, ascending, the root lowest
        int numNotes = 0;
        int root = -1;                   // pitch class
        unsigned pitchClasses = 0;       // bit pc
        unsigned outOfKey = 0;           // the pitch classes not in the home scale
        bool inKey = false;              // filled: every pitch class is in the home scale
        ChordFamily family = ChordFamily::Other;
        std::string name;                // ChordNamer, spelled in the palette key ("Dm7", "Bb")
        std::string numeral;             // relative to home ("ii7", "bVII", "viiø7")
    };

    struct PaletteColumn
    {
        int degree = 0;
        int root = 0;                    // pitch class
        std::string rootName;
        std::string numeral;             // the column header: the triad's numeral relative to home
    };

    struct PaletteRow
    {
        int variant = 0;
        Tier tier = Tier::Other;
        const char* label = "";
        std::array<PaletteCell, numPaletteColumns> cells {};
    };

    struct PaletteGrid
    {
        PaletteContext context;
        std::array<PaletteColumn, numPaletteColumns> columns {};
        std::array<PaletteRow, numTierVariants> rows {};   // bottom (tier 0) to top

        const PaletteRow* row (const std::string& label) const noexcept
        {
            for (const auto& r : rows)
                if (label == r.label)
                    return &r;
            return nullptr;
        }
    };

    namespace palette
    {
        // The numeral's root relative to home: a degree of the home scale and an accidental. A
        // root in the home scale takes its degree as is. Any other root keeps its letter in
        // the palette's spelling (F# in G major is a raised IV from C, Bb in F major a lowered
        // VII), falling back to the lowered degree above.
        struct NumeralRoot
        {
            int degree = 0;
            int accidental = 0;
        };

        inline NumeralRoot numeralRoot (int rootPc, const Spelling& paletteSpelling, const PaletteContext& ctx) noexcept
        {
            const auto& home = scaleInfo (ctx.homeScale).semitones;
            const int r = ((rootPc - ctx.homeKey) % 12 + 12) % 12;
            for (int d = 0; d < 7; ++d)
                if (home[(size_t) d] == r)
                    return { d, 0 };

            const Spelling homeSpelling = makeSpelling (ctx.homeKey, ctx.homeScale);
            const int homeLetter = homeSpelling.letter[(size_t) (((ctx.homeKey % 12) + 12) % 12)];
            const int d = (paletteSpelling.letter[(size_t) rootPc] - homeLetter + 7) % 7;
            const int acc = naming::wrap (r - home[(size_t) d]);
            if (acc >= -2 && acc <= 2)
                return { d, acc };
            for (int a = 0; a < 7; ++a)          // a guard: the letters can't give an accidental this large
                if (home[(size_t) a] > r)
                    return { a, r - home[(size_t) a] };
            return { 0, r - 12 };
        }

        inline bool startsWith (const std::string& s, const char* p) { return s.rfind (p, 0) == 0; }

        // The numeral's suffix from the chord name's quality: a minor chord's "m" is dropped (its
        // numeral is lower case), "dim" is "°", "m7b5" is "ø7", "aug" and "#5" are "+".
        inline std::string numeralSuffix (const std::string& q, unsigned rel)
        {
            const auto has = [rel] (int i) { return ((rel >> i) & 1u) != 0; };
            const bool minorThird = has (3) && ! has (4);
            if (minorThird && has (6) && ! has (7))
            {
                if (startsWith (q, "dim7")) return "°7" + q.substr (4);
                if (startsWith (q, "dim"))  return "°" + q.substr (3);
                if (startsWith (q, "m7b5")) return "ø7" + q.substr (4);
                return startsWith (q, "m") ? q.substr (1) : q;
            }
            if (has (4) && has (8) && ! has (7))
            {
                if (startsWith (q, "aug")) return "+" + q.substr (3);
                const auto pos = q.find ("#5");
                return pos == std::string::npos ? q : "+" + q.substr (0, pos) + q.substr (pos + 2);
            }
            if (minorThird && startsWith (q, "m") && ! startsWith (q, "maj"))
                return q.substr (1);
            return q;
        }

        inline std::string romanFor (const NumeralRoot& nr, bool lowerCase)
        {
            static constexpr const char* upper[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
            static constexpr const char* lower[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
            const int a = nr.accidental;
            std::string s ((size_t) (a < 0 ? -a : a), a < 0 ? 'b' : '#');
            return s + (lowerCase ? lower : upper)[nr.degree];
        }
    }

    // One cell: tier variant `variant` on degree `degree` of the palette's key and mode.
    inline PaletteCell buildPaletteCell (const PaletteContext& ctx, int variant, int degree, const Spelling& paletteSpelling)
    {
        PaletteCell cell;
        cell.degree = degree;
        cell.variant = variant;
        const TierVariant& v = tierVariants[variant >= 0 && variant < numTierVariants ? variant : 0];

        const int key = ((ctx.paletteKey % 12) + 12) % 12;
        const int rootNote = chordBaseNote + key + scaleOffset (ctx.paletteScale, degree);
        const int rootPc = rootNote % 12;

        // 1. Stack the scale steps and test the result.
        std::array<int, maxPaletteNotes> stack {};
        int n = 0;
        unsigned rel = 0;
        for (int i = 0; i < 13 && n < maxPaletteNotes; ++i)
            if ((v.steps >> i) & 1u)
            {
                const int note = chordBaseNote + key + scaleOffset (ctx.paletteScale, degree + i);
                stack[(size_t) n++] = note;
                rel |= 1u << ((note - rootNote) % 12);
            }
        bool accepted = false;
        for (auto a : v.accept)
            accepted |= a != 0 && a == rel;
        if (! accepted)
            return cell;

        // 2. The notes: the stack, or the variant's fixed shape on the root.
        if (v.shape != 0)
        {
            n = 0;
            rel = 0;
            for (int i = 0; i < 32 && n < maxPaletteNotes; ++i)
                if ((v.shape >> i) & 1u)
                {
                    stack[(size_t) n++] = rootNote + i;
                    rel |= 1u << (i % 12);
                }
        }

        cell.exists = true;
        cell.root = rootPc;
        cell.numNotes = n;
        for (int i = 0; i < n; ++i)
        {
            cell.notes[(size_t) i] = (uint8_t) stack[(size_t) i];
            cell.pitchClasses |= 1u << (stack[(size_t) i] % 12);
        }

        const unsigned home = scalePitchClasses (ctx.homeKey, ctx.homeScale);
        cell.outOfKey = cell.pitchClasses & ~home;
        cell.inKey = cell.outOfKey == 0;
        cell.family = chordFamily (cell.pitchClasses, rootPc);

        const ChordName name = nameChord (cell.pitchClasses, rootPc, paletteSpelling, rootPc);
        cell.name = name.text();
        const bool lowerCase = ((rel >> 3) & 1u) != 0 && ((rel >> 4) & 1u) == 0;
        cell.numeral = palette::romanFor (palette::numeralRoot (rootPc, paletteSpelling, ctx), lowerCase)
                     + (name.root == rootPc && name.bass == rootPc ? palette::numeralSuffix (name.quality, rel) : std::string());
        return cell;
    }

    // The whole palette (PLAN.md §3.1).
    inline PaletteGrid buildPalette (const PaletteContext& ctx)
    {
        PaletteGrid grid;
        grid.context = ctx;
        const Spelling sp = makeSpelling (ctx.paletteKey, ctx.paletteScale, ctx.fifths);

        for (int v = 0; v < numTierVariants; ++v)
        {
            auto& row = grid.rows[(size_t) v];
            row.variant = v;
            row.tier = tierVariants[v].tier;
            row.label = tierVariants[v].label;
            for (int d = 0; d < numPaletteColumns; ++d)
                row.cells[(size_t) d] = buildPaletteCell (ctx, v, d, sp);
        }

        const PaletteRow* triads = grid.row ("triad");
        for (int d = 0; d < numPaletteColumns; ++d)
        {
            auto& col = grid.columns[(size_t) d];
            col.degree = d;
            col.root = (((ctx.paletteKey + scaleOffset (ctx.paletteScale, d)) % 12) + 12) % 12;
            col.rootName = sp.name (col.root);
            const PaletteCell& triad = triads->cells[(size_t) d];
            col.numeral = triad.exists ? triad.numeral : palette::romanFor (palette::numeralRoot (col.root, sp, ctx), false);
        }
        return grid;
    }
}
