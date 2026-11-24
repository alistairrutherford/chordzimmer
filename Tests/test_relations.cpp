// Chord Relations tests (PLAN.md §3.5, §6 Phase 6: "relations for C major / A minor checked by
// hand"). Every expectation below was worked out by hand, not read back from the code: the
// palette chords that are a dominant, subdominant, relative or tritone substitute of a focus
// chord, with their badges; common tones; voice-leading distances; the Next ▸ suggestions; and
// the Replace with ▸ list.
#include "TestUtil.h"
#include "core/model/Relations.h"
#include "core/theory/CircleOfFifths.h"

using namespace chordzimmer;
using testutil::check;

namespace
{
    PaletteContext home (int key, Scale s, int fifths = 0, Scale palette = Scale::Count)
    {
        PaletteContext c;
        c.homeKey = key;
        c.homeScale = s;
        c.paletteKey = keyAtFifths (key, fifths);
        c.paletteScale = palette == Scale::Count ? s : palette;
        return c;
    }

    unsigned mask (std::initializer_list<int> pcs)
    {
        unsigned m = 0;
        for (int p : pcs)
            m |= 1u << (((p % 12) + 12) % 12);
        return m;
    }

    // "name:label" for the cells with a function badge in the rows worked out by hand (triad, 7,
    // 7b9, 7#9: the chords whose function is clear), in row and column order.
    std::string badges (const PaletteGrid& grid, const ChordId& focus, const std::string& numeral, bool tonic)
    {
        const auto rel = paletteRelations (grid, focus, numeral, tonic);
        std::string s;
        for (size_t v = 0; v < grid.rows.size(); ++v)
        {
            const std::string row = grid.rows[v].label;
            if (row != "triad" && row != "7" && row != "7b9" && row != "7#9")
                continue;
            for (size_t d = 0; d < 7; ++d)
                if (! rel[v][d].label.empty())
                    s += (s.empty() ? "" : " ") + grid.rows[v].cells[d].name + ":" + rel[v][d].label;
        }
        return s;
    }

    std::string names (const std::vector<PaletteCell>& cells)
    {
        std::string s;
        for (const auto& c : cells)
            s += (s.empty() ? "" : " ") + c.name;
        return s;
    }

    const PaletteCell& cell (const PaletteGrid& g, const char* row, int degree) { return g.row (row)->cells[(size_t) degree]; }

    enum { C = 0, Db = 1, D = 2, Eb = 3, E = 4, F = 5, Gb = 6, G = 7, Ab = 8, A = 9, Bb = 10, B = 11 };
}

static void testDistance()
{
    std::cout << "== voice-leading distance ==\n";
    const unsigned c = mask ({ C, E, G });
    check (voiceLeadingDistance (c, c) == 0, "C to C: 0");
    check (voiceLeadingDistance (c, mask ({ A, C, E })) == 2, "C to Am: G moves up a tone to A (2)");
    check (voiceLeadingDistance (c, mask ({ E, G, B })) == 1, "C to Em: C moves down a semitone to B (1)");
    check (voiceLeadingDistance (c, mask ({ F, A, C })) == 3, "C to F: E up to F (1), G up to A (2): 3");
    check (voiceLeadingDistance (c, mask ({ G, B, D })) == 3, "C to G: C down to B (1), E down to D (2): 3");
    check (voiceLeadingDistance (c, mask ({ D, F, A })) == 5, "C to Dm: C→D 2, E→F 1, G→A 2: 5");
    check (voiceLeadingDistance (mask ({ G, B, D, F }), c) == 4, "G7 to C: B→C 1, D→C 2 (or E), F→E 1, G stays: 4");
    check (voiceLeadingDistance (mask ({ G, B, D, F }), mask ({ Db, F, Ab, B })) == 2, "G7 to Db7: G→Ab 1, D→Db 1, B and F stay: 2");
    check (voiceLeadingDistance (c, mask ({ Gb })) == 9 && voiceLeadingDistance (0, c) == -1,
           "C to a lone F#: all three move to it, C 6, E 2, G 1 = 9; nothing to compare: -1");
}

static void testCMajor()
{
    std::cout << "== C major, by hand ==\n";
    const auto g = buildPalette (home (C, Scale::Major));
    const auto cMaj = ChordId::of (cell (g, "triad", 0));

    // Focus C (I): F is IV, G V, Am its relative; in the 7 row G7 is V7 and Am7 the relative;
    // G7b9 and G7#9 (the Altered rows on V) are dominants; F7b9 (a dominant on IV) and A7b9 are
    // neither IV nor the relative; Db7, the tritone sub, isn't in C major.
    check (badges (g, cMaj, "I", true) == "F:IV G:V Am:rel G7:V7 Am7:rel G7b9:V7 G7#9:V7", "focus C: " + badges (g, cMaj, "I", true));
    const auto rel = paletteRelations (g, cMaj, "I", true);
    check (rel[3][5].common == 2 && rel[3][5].distance == 2 && rel[3][2].common == 2 && rel[3][2].distance == 1 && rel[3][1].common == 0,
           "Am shares 2 notes, 2 semitones away; Em 2 notes, 1 away; Dm none");
    check (rel[3][0].common == 3 && rel[3][0].distance == 0 && rel[3][0].label.empty(), "C itself: 3 in common, 0 away, no badge");
    check (rel[3][2].related() && ! rel[3][1].related(), "Em (2 in common) is related; Dm (none, no function) isn't");

    // Focus Am (vi): C its relative, Dm and Dm7 iv/vi, E7b9 and E7#9 (Altered rows on iii) V7/vi;
    // Em is minor, not a dominant; C7b9 and D7b9 are dominants of other chords.
    const auto am = ChordId::of (cell (g, "triad", 5));
    check (badges (g, am, "vi", false) == "C:rel Dm:iv/vi Dm7:iv/vi E7b9:V7/vi E7#9:V7/vi", "focus Am: " + badges (g, am, "vi", false));

    // Focus G7 (V7): C is IV/V, Em and Em7 the relative of its G triad, D7b9 and D7#9 V7/V; its
    // tritone sub Db7 isn't in C major.
    const auto g7 = ChordId::of (cell (g, "7", 4));
    check (badges (g, g7, "V7", false) == "C:IV/V Em:rel Em7:rel D7b9:V7/V D7#9:V7/V", "focus G7: " + badges (g, g7, "V7", false));

    // Borrowed palettes, against focus C. F major (a fifth flat): F IV, Am and Am7 relative,
    // G7b9 and G7#9 (on F major's ii) V7; Bb is IV of F, not of C.
    const auto f = buildPalette (home (C, Scale::Major, -1));
    check (badges (f, cMaj, "I", true) == "F:IV Am:rel Am7:rel G7b9:V7 G7#9:V7", "F major's palette against C: " + badges (f, cMaj, "I", true));
    // Ab major (four fifths flat): Db7b9 and Db7#9 on its IV are dominants a semitone above C:
    // C's tritone subs; Fm (its vi) is iv of C.
    const auto ab = buildPalette (home (C, Scale::Major, -4));
    const auto abBadges = badges (ab, cMaj, "I", true);
    check (abBadges.find ("Db7b9:sub") != std::string::npos && abBadges.find ("Db7#9:sub") != std::string::npos && abBadges.find ("Fm:iv") != std::string::npos,
           "Ab major's palette against C: Db7b9 and Db7#9 are tritone subs, Fm is iv (" + abBadges + ")");
    // A dominant focus: G7's tritone sub is Db7 itself, a tritone away (Db7b9 in Ab major).
    check (badges (ab, g7, "V7", false).find ("Db7b9:sub") != std::string::npos, "against G7 too: Db7b9 stands in for it (" + badges (ab, g7, "V7", false) + ")");
}

static void testAMinor()
{
    std::cout << "== A minor, by hand ==\n";
    const auto g = buildPalette (home (A, Scale::NaturalMinor));
    const auto am = ChordId::of (cell (g, "triad", 0));
    // Focus Am (i): C its relative, Dm and Dm7 iv, E7b9 and E7#9 (Altered on v) V7; Em is minor.
    check (badges (g, am, "i", true) == "C:rel Dm:iv Dm7:iv E7b9:V7 E7#9:V7", "focus Am: " + badges (g, am, "i", true));

    const auto h = buildPalette (home (A, Scale::HarmonicMinor));
    const auto amH = ChordId::of (cell (h, "triad", 0));
    const auto hb = badges (h, amH, "i", true);
    check (hb.find ("E:V") != std::string::npos && hb.find ("E7:V7") != std::string::npos, "A harmonic minor: E and E7 are V and V7 of Am (" + hb + ")");

    // Focus C (III): Am its relative, F IV/III, G V/III, Am7 relative, G7 V7/III, G7b9 and G7#9
    // V7/III; F7b9 is a dominant, not IV.
    const auto c = ChordId::of (cell (g, "triad", 2));
    check (badges (g, c, "III", false) == "Am:rel F:IV/III G:V/III Am7:rel G7:V7/III G7b9:V7/III G7#9:V7/III",
           "focus C in A minor: " + badges (g, c, "III", false));
}

static void testNext()
{
    std::cout << "== Next ▸ ==\n";
    const auto g = buildPalette (home (C, Scale::Major));
    check (names (nextChords (ChordId::of (cell (g, "triad", 0)), C, Scale::Major)) == "F G7 Am Dm Em", "after C: F G7 Am Dm Em (IV V vi ii iii)");
    check (names (nextChords (ChordId::of (cell (g, "triad", 4)), C, Scale::Major)) == "C Am F Em Dm", "after G: C Am F Em Dm");
    check (names (nextChords (ChordId::of (cell (g, "triad", 1)), C, Scale::Major)) == "G7 Bdim F C Am", "after Dm: G7 Bdim F C Am");
    check (names (nextChords (ChordId::of (cell (g, "7", 4)), C, Scale::Major)) == "C Am F Em Dm", "after G7 (V7): the same as after V");
    // A secondary dominant: E7 (V7/vi) goes to Am first.
    check (names (nextChords ({ mask ({ E, Ab, B, D }), E }, C, Scale::Major)) == "Am C F G7 Dm", "after E7 (V7/vi): Am first, then C F G7 Dm");
    // A borrowed chord that isn't a dominant: Bb (bVII) → C F G7 Am Dm.
    check (names (nextChords ({ mask ({ Bb, D, F }), Bb }, C, Scale::Major)) == "C F G7 Am Dm", "after Bb (bVII): back to C F G7 Am Dm");

    const auto a = buildPalette (home (A, Scale::NaturalMinor));
    check (names (nextChords (ChordId::of (cell (a, "triad", 0)), A, Scale::NaturalMinor)) == "Dm F G Em C", "after Am in A minor: Dm F G Em C (iv VI VII v III)");
    check (names (nextChords (ChordId::of (cell (a, "triad", 3)), A, Scale::NaturalMinor)) == "Em Am G Bdim F", "after Dm (iv): Em Am G Bdim F");
    check (nextChords ({}, C, Scale::Major).empty(), "no focus: nothing");
}

static void testReplace()
{
    std::cout << "== Replace with ▸ ==\n";
    const auto g = buildPalette (home (C, Scale::Major));
    const auto c = progressionChordFromCell (cell (g, "triad", 0));
    const auto list = replacementsFor (c, C, Scale::Major);
    std::string s;
    for (size_t i = 0; i < list.size() && i < 9; ++i)
        s += (s.empty() ? "" : " | ") + nameNotes (slotNotes (makePatternSlot (list[i].chord)), makeSpelling (0, Scale::Major), list[i].chord.root).text()
           + " " + list[i].reason;
    // Substitutes.h's list for C in C major, then the nearest chords sharing two notes.
    check (s.rfind ("Am a 3rd below | Em a 3rd above | Cm parallel minor | Csus2 sus2 | Csus4 sus4 | Cadd9 add the 9th | Cmaj7 add the 7th", 0) == 0,
           "C: the substitutes first (" + s.substr (0, 120) + "…)");
    bool nearOk = list.size() == 12;
    int last = -1;
    for (size_t i = 7; nearOk && i < list.size(); ++i)
    {
        const int d = std::stoi (list[i].reason.substr (list[i].reason.find (", ") + 2));
        nearOk = list[i].reason.find ("notes in common") != std::string::npos && d >= last;
        last = d;
    }
    check (nearOk, "then palette chords with 2+ notes in common, nearest first, 12 in all");
    bool unique = true;
    for (size_t i = 0; i < list.size(); ++i)
        for (size_t j = i + 1; j < list.size(); ++j)
            unique = unique && ! (list[i].chord.root == list[j].chord.root && list[i].chord.relativeMask() == list[j].chord.relativeMask());
    check (unique, "each chord once, and never C itself");
}

int main()
{
    testDistance();
    testCMajor();
    testAMinor();
    testNext();
    testReplace();
    return testutil::finish ("test_relations");
}
