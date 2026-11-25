// Palette tests (PLAN.md §3.1, G2, §7): hand-written grids for C major, A minor, A harmonic
// minor and D Dorian (palette = home), cell by cell: name, notes (lowest first), colour family,
// filled or hollow, and the empty cells; plus numerals, and invariants over every key and scale.
#include "TestUtil.h"
#include "core/theory/PaletteBuilder.h"
#include <sstream>
#include <vector>

using namespace chordzimmer;
using testutil::check;

namespace
{
    int pcOf (const std::string& n)
    {
        static constexpr int natural[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A..G
        int pc = natural[n[0] - 'A'];
        for (size_t i = 1; i < n.size(); ++i)
            pc += n[i] == '#' ? 1 : (n[i] == 'b' ? -1 : 0);
        return (pc + 12) % 12;
    }

    std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (' '), b = s.find_last_not_of (' ');
        return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
    }

    std::vector<std::string> split (const std::string& s, char sep)
    {
        std::vector<std::string> out;
        std::string part;
        std::istringstream in (s);
        while (std::getline (in, part, sep))
            out.push_back (trim (part));
        return out;
    }

    // One expected row: its label, then 7 cells separated by '|'. A cell is "-" (empty) or
    // "Name:F=notes": F is the family (M major, m minor, o other), followed by '~' if the cell is
    // hollow (out of the home key); the notes are listed lowest first.
    struct Expected
    {
        const char* label;
        const char* cells;
    };

    std::string describe (const PaletteCell& c)
    {
        if (! c.exists)
            return "-";
        std::string s = c.name + ":" + (c.family == ChordFamily::Major ? "M" : c.family == ChordFamily::Minor ? "m" : "o")
                      + (c.inKey ? "" : "~") + "=";
        static constexpr const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < c.numNotes; ++i)
            s += std::string (i ? " " : "") + names[c.notes[(size_t) i] % 12];
        return s;
    }

    // Compares a cell with its expected text; the notes by pitch class, in order, and rising.
    bool matches (const PaletteCell& c, const std::string& want)
    {
        if (want == "-")
            return ! c.exists;
        if (! c.exists)
            return false;
        const auto colon = want.find (':'), eq = want.find ('=');
        const std::string name = want.substr (0, colon), flags = want.substr (colon + 1, eq - colon - 1);
        const auto notes = split (want.substr (eq + 1), ' ');
        const ChordFamily fam = flags[0] == 'M' ? ChordFamily::Major : flags[0] == 'm' ? ChordFamily::Minor : ChordFamily::Other;
        const bool hollow = flags.find ('~') != std::string::npos;
        bool ok = c.name == name && c.family == fam && c.inKey == ! hollow && c.numNotes == (int) notes.size();
        for (int i = 0; ok && i < c.numNotes; ++i)
        {
            ok &= c.notes[(size_t) i] % 12 == pcOf (notes[(size_t) i]);
            ok &= i == 0 || c.notes[(size_t) i] > c.notes[(size_t) i - 1];
        }
        return ok && c.root == pcOf (notes[0]);
    }

    void checkGrid (const char* title, const PaletteContext& ctx, const std::vector<Expected>& rows)
    {
        std::cout << "== " << title << " ==\n";
        const PaletteGrid g = buildPalette (ctx);
        check ((int) rows.size() == numTierVariants, std::string (title) + ": the table lists every row");
        for (size_t r = 0; r < rows.size() && r < (size_t) numTierVariants; ++r)
        {
            const auto& row = g.rows[r];
            const auto want = split (rows[r].cells, '|');
            bool ok = std::string (row.label) == rows[r].label && want.size() == (size_t) numPaletteColumns;
            std::string got;
            for (int d = 0; d < numPaletteColumns; ++d)
            {
                got += (d ? " | " : "") + describe (row.cells[(size_t) d]);
                ok &= d < (int) want.size() && matches (row.cells[(size_t) d], want[(size_t) d]);
            }
            check (ok, std::string (title) + ", row " + rows[r].label + (ok ? "" : "\n          got:  " + got + "\n          want: " + rows[r].cells));
        }
    }

    std::string numerals (const PaletteGrid& g, const char* label)
    {
        std::string s;
        for (const auto& c : g.row (label)->cells)
            s += (s.empty() ? "" : " ") + (c.exists ? c.numeral : std::string ("-"));
        return s;
    }

    std::string headers (const PaletteGrid& g)
    {
        std::string s;
        for (const auto& c : g.columns)
            s += (s.empty() ? "" : " ") + c.numeral;
        return s;
    }

    void checkNumerals (const PaletteGrid& g, const char* label, const std::string& want, const std::string& what)
    {
        const std::string got = numerals (g, label);
        check (got == want, what + ": " + want + (got == want ? "" : "  (got " + got + ")"));
    }
}

static void testCMajor()
{
    checkGrid ("C major", { 0, Scale::Major, 0, Scale::Major }, {
        { "5",     "C5:o=C G | D5:o=D A | E5:o=E B | F5:o=F C | G5:o=G D | A5:o=A E | -" },
        { "sus2",  "Csus2:o=C D G | Dsus2:o=D E A | - | Fsus2:o=F G C | Gsus2:o=G A D | Asus2:o=A B E | -" },
        { "sus4",  "Csus4:o=C F G | Dsus4:o=D G A | Esus4:o=E A B | - | Gsus4:o=G C D | Asus4:o=A D E | -" },
        { "triad", "C:M=C E G | Dm:m=D F A | Em:m=E G B | F:M=F A C | G:M=G B D | Am:m=A C E | Bdim:o=B D F" },
        { "6",     "C6:M=C E G A | Dm6:m=D F A B | - | F6:M=F A C D | G6:M=G B D E | - | -" },
        { "7",     "- | Dm7:m=D F A C | Em7:m=E G B D | - | G7:M=G B D F | Am7:m=A C E G | Bm7b5:o=B D F A" },
        { "maj7",  "Cmaj7:M=C E G B | - | - | Fmaj7:M=F A C E | - | - | -" },
        { "add9",  "Cadd9:M=C E G D | Dmadd9:m=D F A E | - | Fadd9:M=F A C G | Gadd9:M=G B D A | Amadd9:m=A C E B | -" },
        { "7sus4", "- | D7sus4:o=D G A C | E7sus4:o=E A B D | - | G7sus4:o=G C D F | A7sus4:o=A D E G | -" },
        { "9",     "Cmaj9:M=C E G B D | Dm9:m=D F A C E | - | Fmaj9:M=F A C E G | G9:M=G B D F A | Am9:m=A C E G B | -" },
        { "11",    "- | Dm11:m=D F A C E G | - | - | G11:M=G B D F A C | Am11:m=A C E G B D | -" },
        { "13",    "Cmaj13:M=C E G B D A | Dm13:m=D F A C E B | - | Fmaj13:M=F A C E G D | G13:M=G B D F A E | - | -" },
        { "6/9",   "C6/9:M=C E G A D | Dm6/9:m=D F A B E | - | F6/9:M=F A C D G | G6/9:M=G B D E A | - | -" },
        { "7b9",   "C7b9:M~=C E G Bb Db | D7b9:M~=D F# A C Eb | E7b9:M~=E G# B D F | F7b9:M~=F A C Eb Gb | G7b9:M~=G B D F Ab | A7b9:M~=A C# E G Bb | -" },
        { "7#9",   "C7#9:M~=C E G Bb D# | D7#9:M~=D F# A C F | E7#9:M~=E G# B D G | F7#9:M~=F A C Eb G# | G7#9:M~=G B D F A# | A7#9:M~=A C# E G C | -" },
        { "aug",   "Caug:o~=C E G# | - | - | Faug:o~=F A C# | Gaug:o~=G B D# | - | -" },
        { "dim",   "- | Ddim:o~=D F Ab | Edim:o~=E G Bb | - | - | Adim:o~=A C Eb | -" },
    });

    const PaletteGrid g = buildPalette ({ 0, Scale::Major, 0, Scale::Major });
    check (headers (g) == "I ii iii IV V vi vii°", "C major column headers: I ii iii IV V vi vii° (got " + headers (g) + ")");
    checkNumerals (g, "7", "- ii7 iii7 - V7 vi7 viiø7", "C major sevenths");
    checkNumerals (g, "maj7", "Imaj7 - - IVmaj7 - - -", "C major maj7");
    checkNumerals (g, "5", "I5 II5 III5 IV5 V5 VI5 -", "power chords are upper case");
    checkNumerals (g, "6/9", "I6/9 ii6/9 - IV6/9 V6/9 - -", "6/9 chords");
    checkNumerals (g, "7b9", "I7b9 II7b9 III7b9 IV7b9 V7b9 VI7b9 -", "7b9 on every degree with a perfect 5th (secondary dominants)");
    checkNumerals (g, "aug", "I+ - - IV+ V+ - -", "aug raises the 5th of a major triad");
    checkNumerals (g, "dim", "- ii° iii° - - vi° -", "dim lowers the 5th of a minor triad");

    // MIDI notes: the palette key's tonic is in the chord octave (C3 = 48), stacked upwards.
    const auto notes = [&g] (const char* row, int d) {
        const auto& c = g.row (row)->cells[(size_t) d];
        return std::vector<int> (c.notes.begin(), c.notes.begin() + c.numNotes);
    };
    check (notes ("triad", 0) == std::vector<int> { 48, 52, 55 }, "C = 48 52 55");
    check (notes ("triad", 6) == std::vector<int> { 59, 62, 65 }, "Bdim = 59 62 65");
    check (notes ("13", 4) == std::vector<int> { 55, 59, 62, 65, 69, 76 }, "G13 = G B D F A E, the 13th two octaves up (no 11th)");
    check (notes ("7b9", 4) == std::vector<int> { 55, 59, 62, 65, 68 }, "G7b9 = G B D F Ab, the b9 above the 7th");
    check (notes ("6/9", 0) == std::vector<int> { 48, 52, 55, 57, 62 }, "C6/9 = C E G A D");
    const auto& g7b9 = g.row ("7b9")->cells[4];
    check (g7b9.outOfKey == (1u << 8), "G7b9's only out-of-key tone is Ab");
}

static void testAMinor()
{
    checkGrid ("A minor", { 9, Scale::NaturalMinor, 9, Scale::NaturalMinor }, {
        { "5",     "A5:o=A E | - | C5:o=C G | D5:o=D A | E5:o=E B | F5:o=F C | G5:o=G D" },
        { "sus2",  "Asus2:o=A B E | - | Csus2:o=C D G | Dsus2:o=D E A | - | Fsus2:o=F G C | Gsus2:o=G A D" },
        { "sus4",  "Asus4:o=A D E | - | Csus4:o=C F G | Dsus4:o=D G A | Esus4:o=E A B | - | Gsus4:o=G C D" },
        { "triad", "Am:m=A C E | Bdim:o=B D F | C:M=C E G | Dm:m=D F A | Em:m=E G B | F:M=F A C | G:M=G B D" },
        { "6",     "- | - | C6:M=C E G A | Dm6:m=D F A B | - | F6:M=F A C D | G6:M=G B D E" },
        { "7",     "Am7:m=A C E G | Bm7b5:o=B D F A | - | Dm7:m=D F A C | Em7:m=E G B D | - | G7:M=G B D F" },
        { "maj7",  "- | - | Cmaj7:M=C E G B | - | - | Fmaj7:M=F A C E | -" },
        { "add9",  "Amadd9:m=A C E B | - | Cadd9:M=C E G D | Dmadd9:m=D F A E | - | Fadd9:M=F A C G | Gadd9:M=G B D A" },
        { "7sus4", "A7sus4:o=A D E G | - | - | D7sus4:o=D G A C | E7sus4:o=E A B D | - | G7sus4:o=G C D F" },
        { "9",     "Am9:m=A C E G B | - | Cmaj9:M=C E G B D | Dm9:m=D F A C E | - | Fmaj9:M=F A C E G | G9:M=G B D F A" },
        { "11",    "Am11:m=A C E G B D | - | - | Dm11:m=D F A C E G | - | - | G11:M=G B D F A C" },
        { "13",    "- | - | Cmaj13:M=C E G B D A | Dm13:m=D F A C E B | - | Fmaj13:M=F A C E G D | G13:M=G B D F A E" },
        { "6/9",   "- | - | C6/9:M=C E G A D | Dm6/9:m=D F A B E | - | F6/9:M=F A C D G | G6/9:M=G B D E A" },
        { "7b9",   "A7b9:M~=A C# E G Bb | - | C7b9:M~=C E G Bb Db | D7b9:M~=D F# A C Eb | E7b9:M~=E G# B D F | F7b9:M~=F A C Eb Gb | G7b9:M~=G B D F Ab" },
        { "7#9",   "A7#9:M~=A C# E G C | - | C7#9:M~=C E G Bb D# | D7#9:M~=D F# A C F | E7#9:M~=E G# B D G | F7#9:M~=F A C Eb G# | G7#9:M~=G B D F A#" },
        { "aug",   "- | - | Caug:o~=C E G# | - | - | Faug:o~=F A C# | Gaug:o~=G B D#" },
        { "dim",   "Adim:o~=A C Eb | - | - | Ddim:o~=D F Ab | Edim:o~=E G Bb | - | -" },
    });

    const PaletteGrid g = buildPalette ({ 9, Scale::NaturalMinor, 9, Scale::NaturalMinor });
    check (headers (g) == "i ii° III iv v VI VII", "A minor headers are relative to the home scale, not to major: i ii° III iv v VI VII (got " + headers (g) + ")");
    checkNumerals (g, "7", "i7 iiø7 - iv7 v7 - VII7", "A minor sevenths");
    check (g.row ("triad")->cells[4].name == "Em", "natural minor's v stays minor (no default raised 3rd in the palette)");
    check (g.columns[0].rootName == "A" && g.columns[0].root == 9 && g.row ("triad")->cells[0].notes[0] == 57, "A minor starts on A3 (57)");
}

static void testAHarmonicMinor()
{
    checkGrid ("A harmonic minor", { 9, Scale::HarmonicMinor, 9, Scale::HarmonicMinor }, {
        { "5",     "A5:o=A E | - | - | D5:o=D A | E5:o=E B | F5:o=F C | -" },
        { "sus2",  "Asus2:o=A B E | - | - | Dsus2:o=D E A | - | - | -" },
        { "sus4",  "Asus4:o=A D E | - | - | - | Esus4:o=E A B | - | -" },
        { "triad", "Am:m=A C E | Bdim:o=B D F | Caug:o=C E G# | Dm:m=D F A | E:M=E G# B | F:M=F A C | G#dim:o=G# B D" },
        { "6",     "- | - | - | Dm6:m=D F A B | - | F6:M=F A C D | -" },
        { "7",     "- | Bm7b5:o=B D F A | - | Dm7:m=D F A C | E7:M=E G# B D | - | G#dim7:o=G# B D F" },
        { "maj7",  "Am(maj7):m=A C E G# | - | Cmaj7#5:o=C E G# B | - | - | Fmaj7:M=F A C E | -" },
        { "add9",  "Amadd9:m=A C E B | - | - | Dmadd9:m=D F A E | - | - | -" },
        { "7sus4", "- | - | - | - | E7sus4:o=E A B D | - | -" },
        { "9",     "- | - | - | Dm9:m=D F A C E | - | - | -" },
        { "11",    "- | - | - | - | - | - | -" },
        { "13",    "- | - | - | Dm13:m=D F A C E B | - | - | -" },
        { "6/9",   "- | - | - | Dm6/9:m=D F A B E | - | - | -" },
        { "7b9",   "A7b9:M~=A C# E G Bb | - | - | D7b9:M~=D F# A C Eb | E7b9:M=E G# B D F | F7b9:M~=F A C Eb Gb | -" },
        { "7#9",   "A7#9:M~=A C# E G C | - | - | D7#9:M~=D F# A C F | E7#9:M~=E G# B D G | F7#9:M~=F A C Eb G# | -" },
        { "aug",   "- | - | - | - | Eaug:o=E G# C | Faug:o~=F A C# | -" },
        { "dim",   "Adim:o~=A C Eb | - | - | Ddim:o=D F G# | - | - | -" },
    });

    const PaletteGrid g = buildPalette ({ 9, Scale::HarmonicMinor, 9, Scale::HarmonicMinor });
    check (headers (g) == "i ii° III+ iv V VI vii°", "A harmonic minor headers: i ii° III+ iv V VI vii° (got " + headers (g) + ")");
    checkNumerals (g, "7", "- iiø7 - iv7 V7 - vii°7", "A harmonic minor sevenths");
    checkNumerals (g, "maj7", "i(maj7) - III+maj7 - - VImaj7 -", "major-7th chords, whatever their triad, share the maj7 row");
    check (g.columns[6].rootName == "G#", "the leading tone is spelled G#");
}

static void testDDorian()
{
    checkGrid ("D Dorian", { 2, Scale::Dorian, 2, Scale::Dorian }, {
        { "5",     "D5:o=D A | E5:o=E B | F5:o=F C | G5:o=G D | A5:o=A E | - | C5:o=C G" },
        { "sus2",  "Dsus2:o=D E A | - | Fsus2:o=F G C | Gsus2:o=G A D | Asus2:o=A B E | - | Csus2:o=C D G" },
        { "sus4",  "Dsus4:o=D G A | Esus4:o=E A B | - | Gsus4:o=G C D | Asus4:o=A D E | - | Csus4:o=C F G" },
        { "triad", "Dm:m=D F A | Em:m=E G B | F:M=F A C | G:M=G B D | Am:m=A C E | Bdim:o=B D F | C:M=C E G" },
        { "6",     "Dm6:m=D F A B | - | F6:M=F A C D | G6:M=G B D E | - | - | C6:M=C E G A" },
        { "7",     "Dm7:m=D F A C | Em7:m=E G B D | - | G7:M=G B D F | Am7:m=A C E G | Bm7b5:o=B D F A | -" },
        { "maj7",  "- | - | Fmaj7:M=F A C E | - | - | - | Cmaj7:M=C E G B" },
        { "add9",  "Dmadd9:m=D F A E | - | Fadd9:M=F A C G | Gadd9:M=G B D A | Amadd9:m=A C E B | - | Cadd9:M=C E G D" },
        { "7sus4", "D7sus4:o=D G A C | E7sus4:o=E A B D | - | G7sus4:o=G C D F | A7sus4:o=A D E G | - | -" },
        { "9",     "Dm9:m=D F A C E | - | Fmaj9:M=F A C E G | G9:M=G B D F A | Am9:m=A C E G B | - | Cmaj9:M=C E G B D" },
        { "11",    "Dm11:m=D F A C E G | - | - | G11:M=G B D F A C | Am11:m=A C E G B D | - | -" },
        { "13",    "Dm13:m=D F A C E B | - | Fmaj13:M=F A C E G D | G13:M=G B D F A E | - | - | Cmaj13:M=C E G B D A" },
        { "6/9",   "Dm6/9:m=D F A B E | - | F6/9:M=F A C D G | G6/9:M=G B D E A | - | - | C6/9:M=C E G A D" },
        { "7b9",   "D7b9:M~=D F# A C Eb | E7b9:M~=E G# B D F | F7b9:M~=F A C Eb Gb | G7b9:M~=G B D F Ab | A7b9:M~=A C# E G Bb | - | C7b9:M~=C E G Bb Db" },
        { "7#9",   "D7#9:M~=D F# A C F | E7#9:M~=E G# B D G | F7#9:M~=F A C Eb G# | G7#9:M~=G B D F A# | A7#9:M~=A C# E G C | - | C7#9:M~=C E G Bb D#" },
        { "aug",   "- | - | Faug:o~=F A C# | Gaug:o~=G B D# | - | - | Caug:o~=C E G#" },
        { "dim",   "Ddim:o~=D F Ab | Edim:o~=E G Bb | - | - | Adim:o~=A C Eb | - | -" },
    });

    const PaletteGrid g = buildPalette ({ 2, Scale::Dorian, 2, Scale::Dorian });
    check (headers (g) == "i ii III IV v vi° VII", "D Dorian headers: i ii III IV v vi° VII (got " + headers (g) + ")");
    check (g.row ("triad")->cells[6].notes[0] == 60, "D Dorian's C column is C4 (the stack rises from D3)");
}

static void testExoticModes()
{
    std::cout << "== other modes ==\n";
    const PaletteGrid pd = buildPalette ({ 0, Scale::PhrygianDominant, 0, Scale::PhrygianDominant });
    std::string triads;
    for (const auto& c : pd.row ("triad")->cells)
        triads += (triads.empty() ? "" : " ") + c.name;
    check (triads == "C Db Edim Fm Gdim Abaug Bbm", "C Phrygian dominant triads: " + triads);
    check (headers (pd) == "I II iii° iv v° VI+ vii", "C Phrygian dominant headers relative to itself: " + headers (pd));

    // A diatonic triad that is none of maj/min/dim/aug leaves the triad cell empty: degree 4
    // of Hungarian minor (F# G# C over A: a diminished 3rd).
    const PaletteGrid hm = buildPalette ({ 9, Scale::HungarianMinor, 9, Scale::HungarianMinor });
    check (! hm.row ("triad")->cells[3].exists && hm.columns[3].numeral == "IV",
           "A Hungarian minor: the stack on D# is D# F A (a diminished 3rd), so the triad cell is empty; header IV");
}

// Over every key and scale (palette = home): stacked tiers are always in key; every cell is
// named on its own root without a slash; the family follows the 3rd and 5th; notes rise from
// the root; the triad row is full for the three 7-mode families.
static void testInvariants()
{
    std::cout << "== invariants, 12 keys x " << numScales << " scales ==\n";
    bool inKey = true, named = true, rising = true, family = true, triadsFull = true;
    std::string firstBad;
    for (int key = 0; key < numKeys; ++key)
        for (int s = 0; s < numScales; ++s)
        {
            const PaletteGrid g = buildPalette ({ key, (Scale) s, key, (Scale) s });
            const unsigned home = scalePitchClasses (key, (Scale) s);
            for (const auto& row : g.rows)
                for (const auto& c : row.cells)
                {
                    if (! c.exists)
                    {
                        triadsFull &= ! (std::string (row.label) == "triad" && modeFamily ((Scale) s) <= ModeFamily::MelodicMinor);
                        continue;
                    }
                    const bool stacked = tierVariants[c.variant].shape == 0;
                    inKey &= ! stacked || c.inKey;
                    inKey &= c.inKey == ((c.pitchClasses & ~home) == 0);
                    std::string bare = c.name;
                    if (const auto p69 = bare.find ("6/9"); p69 != std::string::npos)
                        bare.erase (p69, 3);
                    const bool ok = bare.find ('/') == std::string::npos;
                    const bool rootOk = c.name.rfind (makeSpelling (key, (Scale) s).name (c.root), 0) == 0;
                    if ((! ok || ! rootOk) && firstBad.empty())
                        firstBad = std::string (keyName (key)) + " " + scaleInfo ((Scale) s).name + ", row " + row.label + ": " + c.name;
                    named &= ok && rootOk;
                    for (int i = 1; i < c.numNotes; ++i)
                        rising &= c.notes[(size_t) i] > c.notes[(size_t) i - 1];
                    rising &= c.notes[0] % 12 == c.root;
                    const unsigned rel = ((c.pitchClasses >> c.root) | (c.pitchClasses << (12 - c.root))) & 0xfffu;
                    const auto has = [rel] (int i) { return ((rel >> i) & 1u) != 0; };
                    family &= c.family == (has (4) && has (7) ? ChordFamily::Major : has (3) && has (7) ? ChordFamily::Minor : ChordFamily::Other);
                }
        }
    check (inKey, "stacked cells (tiers 0-4) are always in key when the palette is home; the flag matches the notes");
    check (named, "every cell is named on its own root, spelled in the key, with no slash" + (firstBad.empty() ? std::string() : " (first: " + firstBad + ")"));
    check (rising, "notes rise from the root");
    check (family, "family follows the 3rd and 5th");
    check (triadsFull, "the triad row is full in every mode of the major, harmonic-minor and melodic-minor families");
}

// The navigator's steps from home break the tonic's tie (the six-sharp/six-flat keys): walked
// down from C the palette reads Gb, walked up F#; at home the old rule stays (PLAN.md §3.2).
static void testFlatSideSpelling()
{
    check (makeSpelling (6, Scale::Major).name (6) == "F#", "F# major at home stays F#");
    check (makeSpelling (6, Scale::Major, -6).name (6) == "Gb" && makeSpelling (6, Scale::Major, -6).name (11) == "Cb",
           "six fifths down: Gb major, its 4th Cb");
    check (makeSpelling (6, Scale::Major, 6).name (6) == "F#", "six fifths up: F#");
    check (makeSpelling (3, Scale::NaturalMinor, 6).name (3) == "D#" && makeSpelling (3, Scale::NaturalMinor).name (3) == "Eb",
           "Eb minor / D# minor: the lean wins only the tie");
    check (makeSpelling (1, Scale::Major, 1).name (1) == "Db", "no tie, no change: Db major, not C#, whatever the lean");
    PaletteContext down { 6, Scale::Major, 0, Scale::Major, -6 };
    const auto g = buildPalette (down);
    check (g.row ("triad")->cells[0].name == "Gb" && g.row ("triad")->cells[3].name == "Cb" && g.columns[0].rootName == "Gb",
           "the palette six fifths down from C: Gb, Cb, header Gb (got " + g.row ("triad")->cells[0].name + ", " + g.row ("triad")->cells[3].name + ")");
}

int main()
{
    testCMajor();
    testAMinor();
    testAHarmonicMinor();
    testDDorian();
    testExoticModes();
    testInvariants();
    testFlatSideSpelling();
    return testutil::finish ("test_palette");
}
