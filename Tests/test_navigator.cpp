// Key navigator tests (PLAN.md §3.2, G3, §7): the circle of fifths (12 steps either way return
// home, the distance from home), Home, the scale families and their parallel-mode lists, and
// in-key flags and numerals that are always relative to the home key.
#include "TestUtil.h"
#include "core/theory/CircleOfFifths.h"
#include "core/theory/PaletteBuilder.h"
#include <set>

using namespace chordzimmer;
using testutil::check;

static std::string row (const PaletteGrid& g, const char* label, bool numerals)
{
    std::string s;
    for (const auto& c : g.row (label)->cells)
        s += (s.empty() ? "" : " ") + (! c.exists ? std::string ("-") : (numerals ? c.numeral : c.name) + (c.inKey ? "" : "~"));
    return s;
}

static void testSteps()
{
    std::cout << "== circle of fifths ==\n";
    CircleOfFifths nav;
    std::string keys, dists;
    for (int i = 0; i < 12; ++i)
    {
        nav.step (+1);
        keys += std::string (i ? " " : "") + std::to_string (nav.paletteKey());
        dists += std::string (i ? " " : "") + std::to_string (nav.distance());
    }
    check (keys == "7 2 9 4 11 6 1 8 3 10 5 0", "up from C: G D A E B F# Db Ab Eb Bb F C (" + keys + ")");
    check (dists == "1 2 3 4 5 6 -5 -4 -3 -2 -1 0", "distance from home: +1 … +6, then -5 … 0 (" + dists + ")");

    dists.clear();
    for (int i = 0; i < 12; ++i)
    {
        nav.step (-1);
        dists += std::string (i ? " " : "") + std::to_string (nav.distance());
    }
    check (dists == "-1 -2 -3 -4 -5 -6 5 4 3 2 1 0" && nav.paletteKey() == 0, "down from C: -1 … -6 (Gb), then +5 … 0 (" + dists + ")");

    bool allHome = true, allVisited = true, distOk = true;
    for (int home = 0; home < numKeys; ++home)
        for (int dir : { -1, +1 })
        {
            CircleOfFifths n;
            n.setHome (home, Scale::Major);
            std::set<int> seen;
            for (int i = 0; i < 12; ++i)
            {
                n.step (dir);
                seen.insert (n.paletteKey());
                distOk &= (n.distance() == 0) == (i == 11) && n.distance() >= -6 && n.distance() <= 6;
                distOk &= fifthsBetween (home, n.paletteKey()) == (n.distance() == -6 ? 6 : n.distance());
                distOk &= keyAtFifths (home, n.distance()) == n.paletteKey();
            }
            allHome &= n.paletteKey() == home && n.distance() == 0 && n.atHome();
            allVisited &= seen.size() == 12;
        }
    check (allHome, "12 steps either way return home, from every key");
    check (allVisited, "the 12 steps visit all 12 keys");
    check (distOk, "the distance is ±1 … ±6 and agrees with fifthsBetween / keyAtFifths");
    check (fifthsBetween (0, 5) == -1 && fifthsBetween (0, 7) == 1 && fifthsBetween (9, 4) == 1 && fifthsBetween (0, 6) == 6,
           "fifthsBetween: C→F -1, C→G +1, A→E +1, C→F# +6");

    nav.step (+1);
    nav.step (+1);
    nav.setMode (Scale::Dorian);
    check (nav.paletteKey() == 2 && nav.paletteScale == Scale::Dorian && ! nav.atHome(), "two fifths up and a mode change: D Dorian");
    nav.step (-1);
    check (nav.paletteKey() == 7 && nav.paletteScale == Scale::Dorian, "◀ keeps the mode: G Dorian");
    nav.goHome();
    check (nav.atHome() && nav.paletteKey() == 0 && nav.paletteScale == Scale::Major, "⌂ restores the home key and mode");

    nav.setHome (9, Scale::NaturalMinor);
    check (nav.paletteKey() == 9 && nav.paletteScale == Scale::NaturalMinor, "a palette at home follows a new home (A minor)");
    nav.step (+1);
    nav.setHome (2, Scale::Major);
    check (nav.paletteKey() == 9 && nav.distance() == 1 && nav.paletteScale == Scale::NaturalMinor,
           "a palette away from home keeps its distance and mode (one fifth above D: A minor)");
}

static void testFamilies()
{
    std::cout << "== scale families ==\n";
    // Every scale is in exactly one family.
    std::vector<int> count ((size_t) numScales, 0);
    for (int f = 0; f < numModeFamilies; ++f)
    {
        const auto& info = modeFamilyInfo ((ModeFamily) f);
        for (int m = 0; m < info.numModes; ++m)
            ++count[(size_t) info.modes[(size_t) m]];
    }
    bool once = true;
    for (int c : count)
        once &= c == 1;
    check (once, "every one of the " + std::to_string (numScales) + " scales is in exactly one family");

    // Mode m of a family is its first mode rotated to start on degree m.
    bool rotations = true;
    for (int f = 0; f < numModeFamilies; ++f)
    {
        const auto& info = modeFamilyInfo ((ModeFamily) f);
        const auto& parent = scaleInfo (info.modes[0]).semitones;
        for (int m = 0; m < info.numModes; ++m)
        {
            const auto& mode = scaleInfo (info.modes[(size_t) m]).semitones;
            for (int d = 0; d < 7; ++d)
                rotations &= mode[(size_t) d] == ((parent[(size_t) ((m + d) % 7)] - parent[(size_t) m]) + 12) % 12;
            const auto sm = scaleMode (info.modes[(size_t) m]);
            rotations &= sm.family == (ModeFamily) f && sm.mode == m;
        }
    }
    check (rotations, "mode m of each family is mode 1 rotated to degree m; scaleMode() finds it");

    const auto names = [] (Scale s) {
        std::string out;
        for (auto m : parallelModes (s))
            out += (out.empty() ? "" : ", ") + std::string (scaleInfo (m).name);
        return out;
    };
    check (names (Scale::Phrygian) == "Major, Dorian, Phrygian, Lydian, Mixolydian, Natural Minor, Locrian",
           "major modes: " + names (Scale::Phrygian));
    check (names (Scale::PhrygianDominant) == "Harmonic Minor, Locrian Natural 6, Ionian #5, Dorian #4, Phrygian Dominant, Lydian #2, Altered bb7",
           "harmonic-minor modes: " + names (Scale::PhrygianDominant));
    check (names (Scale::Altered) == "Melodic Minor, Dorian b2, Lydian Augmented, Lydian Dominant, Mixolydian b6, Locrian Natural 2, Altered",
           "melodic-minor modes: " + names (Scale::Altered));
    check (names (Scale::HungarianMinor) == "Hungarian Minor" && names (Scale::DoubleHarmonic) == "Double Harmonic"
               && names (Scale::HarmonicMajor) == "Harmonic Major",
           "Hungarian minor, Double harmonic and Harmonic major are single-mode families");

    bool contains = true;
    for (int s = 0; s < numScales; ++s)
        contains &= parallelModes ((Scale) s).contains ((Scale) s)
                    && parallelModes ((Scale) s).size == modeFamilyInfo (modeFamily ((Scale) s)).numModes;
    check (contains, "each scale's parallel modes include itself");

    CircleOfFifths nav;
    nav.setHome (9, Scale::HarmonicMinor);
    check (nav.modes().size == 7 && nav.modes().modes[4] == Scale::PhrygianDominant, "the mode menu offers the palette scale's family");

    // The 13 inherited scales keep their indices (the Scale parameter saves the index).
    check ((int) Scale::DoubleHarmonic == 12 && (int) Scale::LocrianNatural6 == 13 && (int) Scale::Altered == numScales - 1 && numScales == 24,
           "the inherited 13 scales keep their indices; the 11 new modes follow");
}

static void testBorrowing()
{
    std::cout << "== borrowing: numerals and in-key against home ==\n";
    // Home C major, palette C minor (the parallel Aeolian).
    const PaletteGrid minor = buildPalette ({ 0, Scale::NaturalMinor, 0, Scale::Major });
    check (row (minor, "triad", true) == "i~ ii°~ bIII~ iv~ v~ bVI~ bVII~", "C minor from C major: " + row (minor, "triad", true));
    check (row (minor, "triad", false) == "Cm~ Ddim~ Eb~ Fm~ Gm~ Ab~ Bb~", "names spelled in C minor: " + row (minor, "triad", false));
    check (row (minor, "5", false) == "C5 - Eb5~ F5 G5 Ab5~ Bb5~",
           "power chords on C, F, G stay in key; none on D (its 5th is Ab): " + row (minor, "5", false));
    check (row (minor, "sus4", false) == "Csus4 - Ebsus4~ Fsus4~ Gsus4 - Bbsus4~",
           "sus4 in C minor: Csus4 and Gsus4 stay in C major (" + row (minor, "sus4", false) + ")");

    // One fifth up (G major) and one down (F major).
    const PaletteGrid g = buildPalette ({ 7, Scale::Major, 0, Scale::Major });
    check (row (g, "triad", true) == "V vi vii~ I II~ iii #iv°~", "G major from C: " + row (g, "triad", true));
    const PaletteGrid f = buildPalette ({ 5, Scale::Major, 0, Scale::Major });
    check (row (f, "triad", true) == "IV v~ vi bVII~ I ii iii°~", "F major from C: " + row (f, "triad", true));

    // Further out: Eb major (-3) and E major (+4) from C.
    const PaletteGrid eb = buildPalette ({ 3, Scale::Major, 0, Scale::Major });
    check (row (eb, "triad", true) == "bIII~ iv~ v~ bVI~ bVII~ i~ ii°~", "Eb major from C: " + row (eb, "triad", true));
    const PaletteGrid e = buildPalette ({ 4, Scale::Major, 0, Scale::Major });
    check (row (e, "triad", true) == "III~ #iv~ #v~ VI~ VII~ #i~ #ii°~", "E major from C: " + row (e, "triad", true));

    // Home A minor, palette A harmonic minor: the raised 7th is the only difference.
    const PaletteGrid ahm = buildPalette ({ 9, Scale::HarmonicMinor, 9, Scale::NaturalMinor });
    check (row (ahm, "triad", true) == "i ii° III+~ iv V~ VI #vii°~", "A harmonic minor from A minor: " + row (ahm, "triad", true));

    // In-key is always against home, for every palette key and mode, from several homes.
    bool ok = true, stackedHomeOk = true;
    for (auto home : { std::pair<int, Scale> { 0, Scale::Major }, { 9, Scale::NaturalMinor }, { 2, Scale::Dorian }, { 4, Scale::HarmonicMinor } })
    {
        const unsigned homePcs = scalePitchClasses (home.first, home.second);
        CircleOfFifths nav;
        nav.setHome (home.first, home.second);
        for (int i = 0; i < 12; ++i)
        {
            nav.step (+1);
            for (int s = 0; s < numScales; ++s)
            {
                nav.setMode ((Scale) s);
                const PaletteGrid grid = buildPalette ({ nav.paletteKey(), nav.paletteScale, nav.homeKey, nav.homeScale });
                for (const auto& r : grid.rows)
                    for (const auto& c : r.cells)
                        if (c.exists)
                        {
                            unsigned pcs = 0;
                            for (int k = 0; k < c.numNotes; ++k)
                                pcs |= 1u << (c.notes[(size_t) k] % 12);
                            ok &= c.inKey == ((pcs & ~homePcs) == 0) && c.outOfKey == (pcs & ~homePcs);
                            // A numeral's root carries an accidental exactly when it is outside home.
                            const bool accidental = c.numeral[0] == 'b' || c.numeral[0] == '#';
                            ok &= accidental == (((homePcs >> c.root) & 1u) == 0);
                        }
                if (nav.distance() == 0 && nav.paletteScale == home.second)
                    for (const auto& r : grid.rows)
                        for (const auto& c : r.cells)
                            stackedHomeOk &= ! c.exists || tierVariants[c.variant].shape != 0 || c.inKey;
            }
        }
    }
    check (ok, "every cell of every palette key and mode: in-key and out-of-key tones are against home; the numeral's accidental marks a root outside home");
    check (stackedHomeOk, "back home, every stacked cell is in key");
}

int main()
{
    testSteps();
    testFamilies();
    testBorrowing();
    return testutil::finish ("test_navigator");
}
