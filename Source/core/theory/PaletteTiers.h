// The palette's complexity tiers (PLAN.md §3.1) as data. Plain C++, no JUCE.
//
// The palette has one row per variant, bottom to top, grouped into six tiers. Each row has a
// cell on every degree of the palette's key and mode; a cell is empty when its variant doesn't
// exist on that degree. Every variant is the same for every mode: what a mode shows follows from
// these rules, so a row can be tuned here without code (PLAN.md §8).
//
// A variant is built in two steps:
//
//   1. **Stack** the scale steps in `steps` above the degree (bit i = i scale steps up: 0 = the
//      root, 2 = the 3rd, 4 = the 5th … 12 = the 13th). The chord is these scale tones.
//   2. It **exists** if the stacked chord's pitch classes, relative to its root, equal one of
//      the masks in `accept`. So the "6" row shows C6 and Dm6 in C major but nothing on E
//      (E G B C is not a 6th chord).
//
// A variant with a `shape` (the Altered tier) uses the stack only as the test: when it passes,
// the cell plays `shape` (bit n = n semitones above the root) instead, whatever the scale. So
// "7b9" stands on every degree with a perfect 5th, as a secondary dominant would, and is
// usually out of key.
#pragma once

#include <cstdint>

namespace chordzimmer
{
    namespace palette
    {
        // Bit masks from a list of small numbers: scale steps, pitch classes or semitones.
        template <typename... T>
        constexpr uint32_t bits (T... b) noexcept { return ((1u << b) | ...); }
    }

    enum class Tier : uint8_t
    {
        Other,               // non-triadic: power, sus
        Triads,
        SixthsSevenths,
        Colour,              // maj7, add9, 7sus4
        Extended,            // 9, 11, 13, 6/9
        Altered,             // non-diatonic options, usually out of key
        Count
    };

    constexpr int numTiers = (int) Tier::Count;

    inline const char* tierName (Tier t) noexcept
    {
        static constexpr const char* names[numTiers] = { "Other", "Triads", "Sixths & sevenths", "Colour", "Extended", "Altered" };
        const int i = (int) t;
        return names[i >= 0 && i < numTiers ? i : 0];
    }

    constexpr int maxAccept = 6;

    struct TierVariant
    {
        Tier tier;
        const char* label;                 // the row's label, e.g. "sus2"
        uint32_t steps;                    // scale steps above the degree, bit 0 = the root
        uint16_t accept[maxAccept];        // pitch-class masks the stack must match (0 = unused)
        uint32_t shape;                    // 0, or the fixed chord this variant plays (semitones)
    };

    namespace palette
    {
        // Chord shapes as pitch-class masks above the root.
        constexpr uint16_t pcs (uint32_t m) noexcept { return (uint16_t) m; }
        constexpr uint16_t power   = pcs (bits (0, 7));
        constexpr uint16_t sus2    = pcs (bits (0, 2, 7));
        constexpr uint16_t sus4    = pcs (bits (0, 5, 7));
        constexpr uint16_t major   = pcs (bits (0, 4, 7));
        constexpr uint16_t minor   = pcs (bits (0, 3, 7));
        constexpr uint16_t dim     = pcs (bits (0, 3, 6));
        constexpr uint16_t aug     = pcs (bits (0, 4, 8));
        constexpr uint16_t six     = pcs (bits (0, 4, 7, 9));
        constexpr uint16_t minSix  = pcs (bits (0, 3, 7, 9));
        constexpr uint16_t dom7    = pcs (bits (0, 4, 7, 10));
        constexpr uint16_t min7    = pcs (bits (0, 3, 7, 10));
        constexpr uint16_t halfDim = pcs (bits (0, 3, 6, 10));
        constexpr uint16_t dim7    = pcs (bits (0, 3, 6, 9));
        constexpr uint16_t dom7b5  = pcs (bits (0, 4, 6, 10));
        constexpr uint16_t dom7s5  = pcs (bits (0, 4, 8, 10));
        constexpr uint16_t maj7    = pcs (bits (0, 4, 7, 11));
        constexpr uint16_t minMaj7 = pcs (bits (0, 3, 7, 11));
        constexpr uint16_t maj7s5  = pcs (bits (0, 4, 8, 11));
        constexpr uint16_t add9    = pcs (bits (0, 2, 4, 7));
        constexpr uint16_t minAdd9 = pcs (bits (0, 2, 3, 7));
        constexpr uint16_t sus47   = pcs (bits (0, 5, 7, 10));
        constexpr uint16_t dom9    = pcs (bits (0, 2, 4, 7, 10));
        constexpr uint16_t min9    = pcs (bits (0, 2, 3, 7, 10));
        constexpr uint16_t maj9    = pcs (bits (0, 2, 4, 7, 11));
        constexpr uint16_t dom11   = pcs (bits (0, 2, 4, 5, 7, 10));
        constexpr uint16_t min11   = pcs (bits (0, 2, 3, 5, 7, 10));
        constexpr uint16_t dom13   = pcs (bits (0, 2, 4, 7, 9, 10));
        constexpr uint16_t min13   = pcs (bits (0, 2, 3, 7, 9, 10));
        constexpr uint16_t maj13   = pcs (bits (0, 2, 4, 7, 9, 11));
        constexpr uint16_t sixNine = pcs (bits (0, 2, 4, 7, 9));
        constexpr uint16_t min69   = pcs (bits (0, 2, 3, 7, 9));
    }

    // The rows, bottom (tier 0) to top. A diatonic 7th chord sits in exactly one row: "7" if its
    // 7th is minor or diminished, "maj7" if it is major (Imaj7, i(maj7), III+maj7).
    inline constexpr TierVariant tierVariants[] = {
        // Tier 0, Other.
        { Tier::Other,          "5",     palette::bits (0, 4),             { palette::power }, 0 },
        { Tier::Other,          "sus2",  palette::bits (0, 1, 4),          { palette::sus2 }, 0 },
        { Tier::Other,          "sus4",  palette::bits (0, 3, 4),          { palette::sus4 }, 0 },
        // Tier 1, Triads: the diatonic triad.
        { Tier::Triads,         "triad", palette::bits (0, 2, 4),          { palette::major, palette::minor, palette::dim, palette::aug }, 0 },
        // Tier 2, Sixths & sevenths.
        { Tier::SixthsSevenths, "6",     palette::bits (0, 2, 4, 5),       { palette::six, palette::minSix }, 0 },
        { Tier::SixthsSevenths, "7",     palette::bits (0, 2, 4, 6),       { palette::dom7, palette::min7, palette::halfDim, palette::dim7,
                                                                             palette::dom7b5, palette::dom7s5 }, 0 },
        // Tier 3, Colour.
        { Tier::Colour,         "maj7",  palette::bits (0, 2, 4, 6),       { palette::maj7, palette::minMaj7, palette::maj7s5 }, 0 },
        { Tier::Colour,         "add9",  palette::bits (0, 2, 4, 8),       { palette::add9, palette::minAdd9 }, 0 },
        { Tier::Colour,         "7sus4", palette::bits (0, 3, 4, 6),       { palette::sus47 }, 0 },
        // Tier 4, Extended.
        { Tier::Extended,       "9",     palette::bits (0, 2, 4, 6, 8),    { palette::dom9, palette::min9, palette::maj9 }, 0 },
        { Tier::Extended,       "11",    palette::bits (0, 2, 4, 6, 8, 10), { palette::dom11, palette::min11 }, 0 },
        { Tier::Extended,       "13",    palette::bits (0, 2, 4, 6, 8, 12), { palette::dom13, palette::min13, palette::maj13 }, 0 },
        { Tier::Extended,       "6/9",   palette::bits (0, 2, 4, 5, 8),    { palette::sixNine, palette::min69 }, 0 },
        // Tier 5, Altered: fixed shapes on the degree's root. 7b9 and 7#9 need a perfect 5th on
        // the degree; aug raises the 5th of a major triad, dim lowers the 5th of a minor one (so
        // neither repeats a diatonic aug or dim triad from tier 1).
        { Tier::Altered,        "7b9",   palette::bits (0, 4),             { palette::power }, palette::bits (0, 4, 7, 10, 13) },
        { Tier::Altered,        "7#9",   palette::bits (0, 4),             { palette::power }, palette::bits (0, 4, 7, 10, 15) },
        { Tier::Altered,        "aug",   palette::bits (0, 2, 4),          { palette::major }, palette::bits (0, 4, 8) },
        { Tier::Altered,        "dim",   palette::bits (0, 2, 4),          { palette::minor }, palette::bits (0, 3, 6) },
    };

    constexpr int numTierVariants = (int) (sizeof (tierVariants) / sizeof (tierVariants[0]));
}
