// Keys and seven-tone scales (Chordism's PLAN.md §4.2), grouped into families of parallel
// modes (PLAN.md §3.2). Plain C++, no JUCE.
//
// The order of `Scale` and `Key` is the order of the host parameters' choices, which Live
// saves by index: only ever append new entries.
#pragma once

#include <array>
#include <cstdint>

namespace chordzimmer
{
    constexpr int numKeys = 12;

    // Spelled the way the Key menu shows them. Index 0 = C.
    inline const char* keyName (int key) noexcept
    {
        static constexpr const char* names[numKeys] = { "C", "C#/Db", "D", "Eb", "E", "F",
                                                        "F#/Gb", "G", "Ab", "A", "Bb", "B" };
        return names[((key % numKeys) + numKeys) % numKeys];
    }

    enum class Scale : uint8_t
    {
        Major,
        NaturalMinor,
        HarmonicMinor,
        MelodicMinor,       // ascending form
        Dorian,
        Phrygian,
        Lydian,
        Mixolydian,
        Locrian,
        HarmonicMajor,
        PhrygianDominant,
        HungarianMinor,
        DoubleHarmonic,
        // Added in Phase 1 (PLAN.md §3.2): the rest of the harmonic-minor modes …
        LocrianNatural6,    // mode 2 of harmonic minor
        IonianSharp5,       // mode 3
        DorianSharp4,       // mode 4 (mode 5 is PhrygianDominant)
        LydianSharp2,       // mode 6
        AlteredDoubleFlat7, // mode 7 ("ultralocrian")
        // … and the modes of melodic minor.
        DorianFlat2,        // mode 2
        LydianAugmented,    // mode 3
        LydianDominant,     // mode 4
        MixolydianFlat6,    // mode 5
        LocrianNatural2,    // mode 6
        Altered,            // mode 7 ("super Locrian")
        Count
    };

    constexpr int numScales = (int) Scale::Count;

    using ScaleSteps = std::array<int8_t, 7>;   // semitones above the tonic, degrees 1..7

    struct ScaleInfo
    {
        const char* name;
        ScaleSteps semitones;
    };

    inline const ScaleInfo& scaleInfo (Scale s) noexcept
    {
        static constexpr ScaleInfo table[numScales] = {
            { "Major",             { 0, 2, 4, 5, 7, 9, 11 } },
            { "Natural Minor",     { 0, 2, 3, 5, 7, 8, 10 } },
            { "Harmonic Minor",    { 0, 2, 3, 5, 7, 8, 11 } },
            { "Melodic Minor",     { 0, 2, 3, 5, 7, 9, 11 } },
            { "Dorian",            { 0, 2, 3, 5, 7, 9, 10 } },
            { "Phrygian",          { 0, 1, 3, 5, 7, 8, 10 } },
            { "Lydian",            { 0, 2, 4, 6, 7, 9, 11 } },
            { "Mixolydian",        { 0, 2, 4, 5, 7, 9, 10 } },
            { "Locrian",           { 0, 1, 3, 5, 6, 8, 10 } },
            { "Harmonic Major",    { 0, 2, 4, 5, 7, 8, 11 } },
            { "Phrygian Dominant", { 0, 1, 4, 5, 7, 8, 10 } },
            { "Hungarian Minor",   { 0, 2, 3, 6, 7, 8, 11 } },
            { "Double Harmonic",   { 0, 1, 4, 5, 7, 8, 11 } },
            { "Locrian Natural 6", { 0, 1, 3, 5, 6, 9, 10 } },
            { "Ionian #5",         { 0, 2, 4, 5, 8, 9, 11 } },
            { "Dorian #4",         { 0, 2, 3, 6, 7, 9, 10 } },
            { "Lydian #2",         { 0, 3, 4, 6, 7, 9, 11 } },
            { "Altered bb7",       { 0, 1, 3, 4, 6, 8, 9 } },
            { "Dorian b2",         { 0, 1, 3, 5, 7, 9, 10 } },
            { "Lydian Augmented",  { 0, 2, 4, 6, 8, 9, 11 } },
            { "Lydian Dominant",   { 0, 2, 4, 6, 7, 9, 10 } },
            { "Mixolydian b6",     { 0, 2, 4, 5, 7, 8, 10 } },
            { "Locrian Natural 2", { 0, 2, 3, 5, 6, 8, 10 } },
            { "Altered",           { 0, 1, 3, 4, 6, 8, 10 } },
        };
        const int i = (int) s;
        return table[i >= 0 && i < numScales ? i : 0];
    }

    // Semitones from the tonic to scale index `i` (0 = tonic, 7 = the tonic an octave up,
    // negative = below the tonic).
    inline int scaleOffset (Scale s, int i) noexcept
    {
        const int octave = i >= 0 ? i / 7 : -((6 - i) / 7);
        return scaleInfo (s).semitones[(size_t) (i - 7 * octave)] + 12 * octave;
    }

    // Default accidentals (§4.2): per scale, per degree, per chord step, applied when a Chord
    // Group fills the editor. Only natural minor needs one: its V chord takes the leading tone
    // as its 3rd, so V and V7 come out major and dominant without editing.
    inline int defaultAccidental (Scale s, int degree, int step) noexcept
    {
        if (s == Scale::NaturalMinor && degree == 4 && step == 1)
            return +1;
        return 0;
    }

    // Scale families (PLAN.md §3.2): the modes that share one set of intervals, in mode order
    // (mode 1 first). A palette's mode menu lists the family of its current scale on the same
    // tonic. Harmonic major, Hungarian minor and Double harmonic are single-mode families: their
    // other modes aren't scales of ours.
    enum class ModeFamily : uint8_t
    {
        Major,              // the church modes
        HarmonicMinor,
        MelodicMinor,
        HarmonicMajor,
        HungarianMinor,
        DoubleHarmonic,
        Count
    };

    constexpr int numModeFamilies = (int) ModeFamily::Count;

    struct ModeFamilyInfo
    {
        const char* name;
        std::array<Scale, 7> modes;   // the first `numModes` are used
        int numModes;
    };

    inline const ModeFamilyInfo& modeFamilyInfo (ModeFamily f) noexcept
    {
        using S = Scale;
        static constexpr ModeFamilyInfo table[numModeFamilies] = {
            { "Major modes", { S::Major, S::Dorian, S::Phrygian, S::Lydian, S::Mixolydian, S::NaturalMinor, S::Locrian }, 7 },
            { "Harmonic minor modes", { S::HarmonicMinor, S::LocrianNatural6, S::IonianSharp5, S::DorianSharp4,
                                        S::PhrygianDominant, S::LydianSharp2, S::AlteredDoubleFlat7 }, 7 },
            { "Melodic minor modes", { S::MelodicMinor, S::DorianFlat2, S::LydianAugmented, S::LydianDominant,
                                       S::MixolydianFlat6, S::LocrianNatural2, S::Altered }, 7 },
            { "Harmonic major", { S::HarmonicMajor }, 1 },
            { "Hungarian minor", { S::HungarianMinor }, 1 },
            { "Double harmonic", { S::DoubleHarmonic }, 1 },
        };
        const int i = (int) f;
        return table[i >= 0 && i < numModeFamilies ? i : 0];
    }

    // The family a scale belongs to, and its mode number in it (0 = mode 1).
    struct ScaleMode
    {
        ModeFamily family = ModeFamily::Major;
        int mode = 0;
    };

    inline ScaleMode scaleMode (Scale s) noexcept
    {
        for (int f = 0; f < numModeFamilies; ++f)
        {
            const auto& info = modeFamilyInfo ((ModeFamily) f);
            for (int m = 0; m < info.numModes; ++m)
                if (info.modes[(size_t) m] == s)
                    return { (ModeFamily) f, m };
        }
        return {};
    }

    inline ModeFamily modeFamily (Scale s) noexcept { return scaleMode (s).family; }

    // The parallel modes of a scale (PLAN.md §3.2): every mode of its family, in mode order,
    // the scale itself included. On the same tonic they are what the palette's mode menu offers.
    struct ParallelModes
    {
        std::array<Scale, 7> modes {};
        int size = 0;

        const Scale* begin() const noexcept { return modes.data(); }
        const Scale* end() const noexcept { return modes.data() + size; }
        bool contains (Scale s) const noexcept
        {
            for (auto m : *this)
                if (m == s)
                    return true;
            return false;
        }
    };

    inline ParallelModes parallelModes (Scale s) noexcept
    {
        const auto& info = modeFamilyInfo (modeFamily (s));
        ParallelModes out;
        for (int m = 0; m < info.numModes; ++m)
            out.modes[(size_t) out.size++] = info.modes[(size_t) m];
        return out;
    }
}
