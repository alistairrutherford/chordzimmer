// Match and Chord-from-notes tests (PLAN.md §3.4, §7 test_matcher; Phase 5, G6 and G7): hand-made
// melodies with the chord each should match best, the scoring terms and tie rules, borrowing
// from neighbouring keys, the same melody in every key, the note window Listen collects
// (NoteCollector.h), and chords built from chosen notes and a root, with their names (ChordNamer:
// C7#9, Fm(maj7), C(b9) … for chords outside the palette).
#include "FakeHost.h"
#include "core/engine/NoteCollector.h"
#include "core/model/ChordFromNotes.h"
#include "core/model/TimelineLayout.h"

using namespace chordzimmer;
using testutil::check;

namespace
{
    // A melody: (pitch class, beats) pairs.
    NoteSet melody (std::initializer_list<std::pair<int, double>> notes)
    {
        NoteSet s;
        for (auto [pc, beats] : notes)
            s.weight[(size_t) (((pc % 12) + 12) % 12)] += beats;
        return s;
    }

    PaletteContext home (int key, Scale s)
    {
        PaletteContext c;
        c.paletteKey = c.homeKey = key;
        c.paletteScale = c.homeScale = s;
        return c;
    }

    std::string best (const NoteSet& notes, const PaletteContext& ctx, int around = 0)
    {
        MatchOptions o;
        o.fifthsAround = around;
        const auto m = matchPalette (notes, ctx, o);
        return m.empty() ? std::string ("-") : m.front().cell.name + " " + m.front().cell.numeral;
    }

    std::string ranking (const NoteSet& notes, const PaletteContext& ctx, int n, int around = 0)
    {
        MatchOptions o;
        o.fifthsAround = around;
        o.top = n;
        std::string s;
        for (const auto& m : matchPalette (notes, ctx, o))
            s += (s.empty() ? "" : " ") + m.cell.name;
        return s;
    }

    enum { C = 0, Db = 1, D = 2, Eb = 3, E = 4, F = 5, Gb = 6, G = 7, Ab = 8, A = 9, Bb = 10, B = 11 };
}

static void testMelodies()
{
    std::cout << "== melodies in C major ==\n";
    const auto c = home (0, Scale::Major);
    struct Case { const char* what; NoteSet notes; const char* expected; };
    const Case cases[] = {
        { "C E G, even", melody ({ { C, 1 }, { E, 1 }, { G, 1 } }), "C I" },
        { "D F A", melody ({ { D, 1 }, { F, 1 }, { A, 1 } }), "Dm ii" },
        { "E G B", melody ({ { E, 1 }, { G, 1 }, { B, 1 } }), "Em iii" },
        { "F A C", melody ({ { F, 1 }, { A, 1 }, { C, 1 } }), "F IV" },
        { "A C E, a long A", melody ({ { A, 2 }, { C, 1 }, { E, 1 } }), "Am vi" },
        { "G B D F", melody ({ { G, 1 }, { B, 1 }, { D, 1 }, { F, 1 } }), "G7 V7" },
        { "C long, D E G short (a passing D)", melody ({ { C, 2 }, { D, 0.5 }, { E, 1 }, { G, 0.5 } }), "C I" },
        { "F and A long, a passing G", melody ({ { F, 2 }, { A, 2 }, { G, 0.5 } }), "F IV" },
        { "G B D, a long B", melody ({ { G, 1 }, { B, 2 }, { D, 1 } }), "G V" },
        { "C F G: the F is a suspension", melody ({ { C, 1 }, { F, 1 }, { G, 1 } }), "Csus4 Isus4" },
        { "C G alone: the power chord before the triad", melody ({ { C, 1 }, { G, 1 } }), "C5 I5" },
        { "C E G D, even: add9", melody ({ { C, 1 }, { E, 1 }, { G, 1 }, { D, 1 } }), "Cadd9 Iadd9" },
        { "F long with E: maj7", melody ({ { F, 2 }, { E, 1 } }), "Fmaj7 IVmaj7" },
        { "a scale run C D E F G A B C, the Cs long", melody ({ { C, 3 }, { D, 0.5 }, { E, 0.5 }, { F, 0.5 }, { G, 1 }, { A, 0.5 }, { B, 0.5 } }), "C I" },
        { "B D F", melody ({ { B, 1 }, { D, 1 }, { F, 1 } }), "Bdim vii°" },
        { "D F A C", melody ({ { D, 1 }, { F, 1 }, { A, 1 }, { C, 1 } }), "Dm7 ii7" },
    };
    for (const auto& k : cases)
    {
        const auto got = best (k.notes, c);
        check (got == k.expected, std::string (k.what) + ": " + got + (got == k.expected ? "" : " (expected " + std::string (k.expected) + ")"));
    }

    std::cout << "== other homes ==\n";
    const auto am = home (A, Scale::NaturalMinor);
    check (best (melody ({ { A, 1 }, { C, 1 }, { E, 1 } }), am) == "Am i", "A minor: A C E is i");
    check (best (melody ({ { G, 1 }, { B, 1 }, { D, 1 } }), am) == "G VII", "A minor: G B D is VII");
    check (best (melody ({ { E, 1 }, { Ab, 1 }, { B, 1 } }), home (A, Scale::HarmonicMinor)) == "E V", "A harmonic minor: E G# B is V");
    check (best (melody ({ { D, 1 }, { F, 1 }, { A, 1 }, { B, 1 } }), home (D, Scale::Dorian)) == "Dm6 i6", "D Dorian: D F A B is i6");

    // The same melody in every key matches the same numeral.
    bool same = true;
    for (int k = 0; k < 12; ++k)
    {
        const auto ctx = home (k, Scale::Major);
        const auto shift = [k] (int pc) { return (pc + k) % 12; };
        MatchOptions o;
        const auto m1 = matchPalette (melody ({ { shift (G), 1 }, { shift (B), 1 }, { shift (D), 1 }, { shift (F), 1 } }), ctx, o);
        const auto m2 = matchPalette (melody ({ { shift (A), 2 }, { shift (C), 1 }, { shift (E), 1 } }), ctx, o);
        same = same && ! m1.empty() && m1.front().cell.numeral == "V7" && ! m2.empty() && m2.front().cell.numeral == "vi";
    }
    check (same, "in all 12 major keys, the V7 and vi melodies match V7 and vi");
}

static void testScoring()
{
    std::cout << "== scoring and ties ==\n";
    const auto c = home (0, Scale::Major);
    const unsigned cMaj = (1u << C) | (1u << E) | (1u << G);
    check (std::abs (matchScore (melody ({ { C, 1 } }), cMaj, C, Tier::Triads) - (1.5 - 0.01)) < 1e-12, "a root alone: 1.5 (less the tier)");
    check (std::abs (matchScore (melody ({ { E, 1 } }), cMaj, C, Tier::Triads) - (1.25 - 0.01)) < 1e-12, "a 3rd alone: 1.25");
    check (std::abs (matchScore (melody ({ { C, 1 }, { Db, 1 } }), cMaj, C, Tier::Other) - (0.75 - 0.375)) < 1e-12,
           "C and Db (a semitone above C) half and half: 0.75 - 0.75 x 0.5");
    check (std::abs (matchScore (melody ({ { C, 1 }, { D, 1 } }), cMaj, C, Tier::Other) - 0.75) < 1e-12, "a passing D (a tone above) counts nothing");
    const unsigned c7 = cMaj | (1u << Bb);
    check (std::abs (matchScore (melody ({ { C, 1 } }), c7, C, Tier::Other) - (1.5 - 0.15)) < 1e-12
               && std::abs (matchScore (melody ({ { C, 1 }, { Bb, 1 } }), c7, C, Tier::Other) - (0.75 + 0.5 - 0.15)) < 1e-12,
           "a 7th costs 0.15, played or not (and counts its weight when played)");
    check (std::abs (matchScore (melody ({ { C, 3 } }), cMaj, C, Tier::Other) - matchScore (melody ({ { C, 1 } }), cMaj, C, Tier::Other)) < 1e-12,
           "weights are normalised: a longer phrase scores the same");

    check (ranking (melody ({ { C, 1 }, { F, 1 }, { G, 1 } }), c, 2) == "Csus4 Fsus2",
           "Csus4 and Fsus2 (the same notes) tie: the earlier column first (" + ranking (melody ({ { C, 1 }, { F, 1 }, { G, 1 } }), c, 2) + ")");
    check (matchPalette (NoteSet {}, c).empty(), "no notes: no matches");
    const unsigned g7s9 = (1u << G) | (1u << B) | (1u << D) | (1u << F) | (1u << Bb);
    check (matchScore (melody ({ { Bb, 1 } }), g7s9, G, Tier::Altered) < matchScore (melody ({ { B, 1 } }), g7s9, G, Tier::Altered),
           "in G7#9 the 3rd is B (the major 3rd); the Bb is a #9, a colour tone");
    check (ranking (melody ({ { G, 1 }, { B, 1 }, { D, 1 }, { F, 1 } }), c, 3) == "G7 G Bdim",
           "G B D F: G7, then G, then Bdim (three of its notes), not G7#9 (" + ranking (melody ({ { G, 1 }, { B, 1 }, { D, 1 }, { F, 1 } }), c, 3) + ")");
    check (matchPalette (melody ({ { Db, 1 } }), c).empty() || matchPalette (melody ({ { Db, 1 } }), c).front().cell.pitchClasses & (1u << Db),
           "a lone Db: only chords holding it score above 0");
    const auto top = matchPalette (melody ({ { C, 1 }, { E, 1 }, { G, 1 } }), c);
    bool ranked = top.size() <= 8;
    for (size_t i = 0; i < top.size(); ++i)
        ranked = ranked && top[i].rank == (int) i + 1 && top[i].score > 0.0 && (i == 0 || top[i - 1].score >= top[i].score - 1e-9);
    check (ranked && top.size() == 8, "at most 8, ranked 1… in order of score, all positive");
}

static void testBorrowing()
{
    std::cout << "== borrowing (±2 fifths) ==\n";
    const auto c = home (0, Scale::Major);
    const auto bbMelody = melody ({ { Bb, 2 }, { D, 1 }, { F, 1 } });
    check (best (bbMelody, c, 0) != "Bb bVII" && best (bbMelody, c, 2) == "Bb bVII", "Bb D F (long Bb): with ±2 fifths, Bb from F major, bVII (" + best (bbMelody, c, 0) + " without)");
    MatchOptions o;
    o.fifthsAround = 2;
    const auto m = matchPalette (bbMelody, c, o);
    check (! m.empty() && m.front().fifths == -1 && m.front().paletteKey == F && ! m.front().cell.inKey, "it comes from the F palette (-1) and is out of key");
    check (best (melody ({ { C, 1 }, { E, 1 }, { G, 1 }, { Bb, 1 } }), c, 2) == "C7 I7", "C E G Bb with ±2: C7 (the F palette's V7, I7 from C)");
    const auto cTriad = matchPalette (melody ({ { C, 1 }, { E, 1 }, { G, 1 } }), c, o);
    int cs = 0;
    for (const auto& x : cTriad)
        cs += x.cell.name == "C" ? 1 : 0;
    check (! cTriad.empty() && cTriad.front().cell.name == "C" && cTriad.front().fifths == 0 && cs == 1,
           "C, in the C, F and G palettes, counts once, from the nearest (C)");
}

static void testCollector()
{
    std::cout << "== the notes Listen collects ==\n";
    NoteCollector col;
    col.reset();
    col.noteOn (60, 0.0);
    col.noteOff (60, 2.0);    // C, 2 beats
    col.noteOn (64, 2.0);
    col.noteOff (64, 3.0);    // E, 1
    col.noteOn (55, 3.0);     // G3 held from 3
    col.advanceTo (4.0);
    CollectedNotes snap;
    col.snapshot (snap);
    check (snap.count == 3 && snap.notes[2].held && snap.notes[2].end == 4.0 && snap.now == 4.0, "two notes done and one held (ending now)");
    auto set = noteSetFromCollected (snap, 64.0);
    check (set.weight[C] == 2.0 && set.weight[E] == 1.0 && set.weight[G] == 1.0 && set.bass == G, "weights are beats held; the bass is the lowest note (G3)");
    set = noteSetFromCollected (snap, 2.0);
    check (set.weight[C] == 0.0 && set.weight[E] == 1.0 && set.weight[G] == 1.0, "a 2-beat window: only the last two beats of playing");
    col.noteOff (55, 5.0);
    col.advanceTo (40.0);     // a long silence after
    col.snapshot (snap);
    set = noteSetFromCollected (snap, 2.0);
    check (set.weight[G] == 2.0 && set.weight[E] == 0.0, "silence doesn't slide the window away: it ends with the last note");
    check (best (noteSetFromCollected (snap, 64.0), home (0, Scale::Major)) == "C I", "and the phrase matches C");

    NoteCollector many;
    many.reset();
    for (int i = 0; i < 300; ++i)
    {
        many.noteOn (60 + i % 12, i);
        many.noteOff (60 + i % 12, i + 0.5);
    }
    many.noteOn (40, 300.0);
    many.snapshot (snap);
    check (snap.count == maxCollectedNotes && snap.notes[0].start == 300.0 - (maxCollectedNotes - 1) && snap.notes[(size_t) snap.count - 1].held,
           "past 256 notes the oldest go; the held one is kept");
    many.reset();
    many.snapshot (snap);
    check (snap.count == 0 && noteSetFromCollected (snap, 8.0).empty(), "reset empties it");
    check (many.takeChanged() && ! many.takeChanged(), "a change is reported once");
}

static void testChordFromNotes()
{
    std::cout << "== chord from notes ==\n";
    const auto label = [] (const ProgressionChord& ch) { const auto l = timelineChordLabel (ch, 0, Scale::Major); return l.name + " " + l.numeral; };
    const auto mask = [] (std::initializer_list<int> pcs) { unsigned m = 0; for (int p : pcs) m |= 1u << p; return m; };

    const auto grid = buildPalette (home (0, Scale::Major));
    check (chordFromNotes (mask ({ D, F, A }), D) == progressionChordFromCell (grid.row ("triad")->cells[1]), "D F A on D is the palette's Dm, note for note");
    check (chordFromNotes (mask ({ G, B, D, F }), G) == progressionChordFromCell (grid.row ("7")->cells[4]), "G B D F on G is the palette's G7");
    check (label (chordFromNotes (mask ({ C, E, G, Bb, Eb }), C)) == "C7#9 I7#9", "C E G Bb D#: C7#9 (" + label (chordFromNotes (mask ({ C, E, G, Bb, Eb }), C)) + ")");
    check (label (chordFromNotes (mask ({ F, Ab, C, E }), F)).rfind ("Fm(maj7)", 0) == 0, "F Ab C E: Fm(maj7) (" + label (chordFromNotes (mask ({ F, Ab, C, E }), F)) + ")");
    check (label (chordFromNotes (mask ({ C, Db, E, G }), C)).rfind ("C(b9)", 0) == 0, "C Db E G: C(b9), an alteration in brackets");
    check (label (chordFromNotes (mask ({ C, E, G }), C, D)).rfind ("Cadd9/D", 0) == 0, "C E G over a D bass: a slash chord (Cadd9/D)");
    check (label (chordFromNotes (mask ({ C, E, G }), C, E)).rfind ("C/E", 0) == 0, "C E G over E: C/E");
    check (chordFromNotes (mask ({ C, E, G }), C, C).bass == -1, "a bass on the root is no bass");
    check (chordFromNotes (mask ({ E, G }), C).pitchClasses() == mask ({ C, E, G }), "a root not among the notes is added");
    check (chordFromNotes (0, C).chord.empty() && chordFromNotes (mask ({ C }), -1).chord.empty(), "no notes or no root: no chord");
    check (suggestedRoot (mask ({ F, A, C, E })) == F && suggestedRoot (mask ({ C, E, G }), E) == C && suggestedRoot (0) == -1,
           "the suggested root: F for F A C E, C for C E G over E");

    // Every pitch-class set with every root in it: a chord on that root, with its notes (up to
    // 8), in the octave above the root, and a name.
    int bad = 0, sets = 0;
    for (unsigned m = 1; m < 4096; ++m)
        for (int r = 0; r < 12; ++r)
        {
            if (((m >> r) & 1u) == 0)
                continue;
            ++sets;
            const auto ch = chordFromNotes (m, r);
            int bits = 0;
            for (int i = 0; i < 12; ++i)
                bits += (int) ((m >> i) & 1u);
            const auto notes = progressionChordNotes (ch);
            const bool ok = ch.root() == r && (bits > maxWorkspaceTones || ch.pitchClasses() == m) && notes.size > 0
                         && notes.notes[0] == chordBaseNote + r && notes.notes[(size_t) notes.size - 1] < chordBaseNote + r + 12
                         && ! timelineChordLabel (ch, 0, Scale::Major).name.empty();
            bad += ok ? 0 : 1;
        }
    check (bad == 0, std::to_string (sets) + " sets and roots: each a named chord on its root within the octave");
}

static void testListenInTheEngine()
{
    std::cout << "== Listen through the engine ==\n";
    // A melody played into Listen with the host stopped: it passes through, no chords play, and
    // the notes come back with their lengths in beats (at the host's tempo).
    fakehost::Host h (48000.0, 120.0);
    h.playing = false;
    h.settings.collecting = true;
    const int notes[] = { 67, 71, 74, 77 };   // G B D F
    for (int i = 0; i < 4; ++i)
    {
        h.key (i, notes[i], true, 90);
        h.key (i + 0.9, notes[i], false);
    }
    h.run (h.at (5.0), 256);
    std::vector<int> ons;
    for (const auto& hit : h.hits)
        if (hit.on)
            ons.push_back (hit.note);
    check (ons == std::vector<int> { 67, 71, 74, 77 } && h.clean(), "the melody passes through unchanged, no chords");
    CollectedNotes c;
    h.engine->getCollected (c);
    bool lengths = c.count == 4;
    for (int i = 0; lengths && i < 4; ++i)
        lengths = c.notes[(size_t) i].pitch == notes[i] && std::abs (c.notes[(size_t) i].end - c.notes[(size_t) i].start - 0.9) < 0.01;
    check (lengths, "four notes collected, each 0.9 beats");
    check (best (noteSetFromCollected (c, 16.0), home (0, Scale::Major)) == "G7 V7", "and they match G7");

    h.engine->requestCollectedReset();
    h.run (h.pos + 256, 256);
    h.engine->getCollected (c);
    check (c.count == 0, "Clear (a reset request) empties it at the next block");

    h.key ((double) h.pos / h.spq() + 0.1, 60, true);
    h.run (h.pos + h.at (0.5), 256);
    h.settings.collecting = false;
    h.run (h.pos + 256, 256);
    h.settings.collecting = true;
    h.run (h.pos + 256, 256);
    h.engine->getCollected (c);
    check (c.count == 0 && h.clean(), "Listen off ends the held note; on again starts afresh");
}

int main()
{
    testMelodies();
    testScoring();
    testBorrowing();
    testCollector();
    testChordFromNotes();
    testListenInTheEngine();
    return testutil::finish ("test_matcher");
}
