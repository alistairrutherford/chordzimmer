// ChordProgression model tests (PLAN.md §3.3, §7 test_progression; Phase 2, G4): palette chords as
// timeline chords, every edit and its undo/redo, snap, insert between sections, duplicate in
// place, paste at the insertion marker, the queries the loop and export use, and the saved text:
// round trip (hand-written and random) and garbage rejected without a crash.
//
// Durations are in ticks (96 to a beat); layouts are written as Name[chord beats …].
#include "TestUtil.h"
#include "core/model/UndoList.h"
#include <random>

using namespace chordzimmer;
using testutil::check;

namespace
{
    ProgressionChord chord (int root, std::vector<int> tones, int ticks = defaultChordTicks)
    {
        return makeProgressionChord (makeWorkspaceChord (root, tones.data(), (int) tones.size()), ticks);
    }

    const ProgressionChord C = chord (0, { 0, 4, 7 }), G = chord (7, { 0, 4, 7 }), Am = chord (9, { 0, 3, 7 }),
                           F = chord (5, { 0, 4, 7 }), Dm = chord (2, { 0, 3, 7 }), Em = chord (4, { 0, 3, 7 });

    ProgressionChord withTicks (ProgressionChord c, int ticks) { c.ticks = ticks; return c; }

    std::string chordLabel (const ProgressionChord& c)
    {
        static const char* names[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        std::string s = names[c.root()];
        if ((c.chord.relativeMask() & (1u << 3)) != 0)
            s += "m";
        std::string beats = std::to_string (c.ticks / ticksPerBeat);
        if (c.ticks % ticksPerBeat != 0)
            beats = std::to_string (c.ticks) + "t";
        return s + beats;
    }

    // "Verse[C4 G4] Chorus[Am2]"
    std::string layout (const ChordProgression& p)
    {
        std::string s;
        for (const auto& sec : p.sections)
        {
            s += (s.empty() ? "" : " ") + sec.name + "[";
            for (size_t i = 0; i < sec.chords.size(); ++i)
                s += (i ? " " : "") + chordLabel (sec.chords[i]);
            s += "]";
        }
        return s;
    }

    ProgressionSection section (std::string name, int colour, std::vector<ProgressionChord> chords)
    {
        ProgressionSection s;
        s.name = std::move (name);
        s.colour = (uint8_t) colour;
        s.chords = std::move (chords);
        return s;
    }

    // Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]
    ChordProgression song()
    {
        ChordProgression p;
        p.sections.push_back (section ("Verse", 0, { C, G, Am, F }));
        p.sections.push_back (section ("Chorus", 1, { F, G }));
        p.sections.push_back (section ("Bridge", 2, { withTicks (Dm, 192), withTicks (Em, 192) }));
        return p;
    }

    // `edit` through an UndoList from `start`: it changes the progression to `expected`; undo
    // gives `start` back exactly, redo `expected`.
    template <class Edit>
    void checkUndoable (const std::string& what, const ChordProgression& start, Edit edit, const std::string& expected)
    {
        UndoList u (start);
        const bool did = u.perform (what, edit);
        const ChordProgression after = u.current();
        check (did && layout (after) == expected, what + ": " + layout (after) + (did ? "" : " (refused)"));
        check (u.undoLabel() == what && ! u.canRedo(), what + ": one undo step, labelled");
        check (u.undo() && u.current() == start && ! u.canUndo(), what + ": undo restores the start exactly");
        check (u.redoLabel() == what && u.redo() && u.current() == after, what + ": redo restores the edit exactly");
    }
}

static void testSnap()
{
    std::cout << "== snap ==\n";
    check (snapUnitTicks (Snap::Sixteenth) == 24 && snapUnitTicks (Snap::Eighth) == 48 && snapUnitTicks (Snap::Quarter) == 96
               && snapUnitTicks (Snap::Half) == 192 && snapUnitTicks (Snap::Bar) == 384 && snapUnitTicks (Snap::Bar, 3) == 288
               && snapUnitTicks (Snap::Off) == 1,
           "grid: 1/16 = 24, 1/8 = 48, 1/4 = 96, 1/2 = 192, bar = 384 (288 in 3/4), off = 1 tick");
    check (defaultSnap == Snap::Eighth, "the default snap is 1/8 (as Klimper)");
    check (snapDuration (100, Snap::Eighth) == 96 && snapDuration (70, Snap::Eighth) == 48 && snapDuration (72, Snap::Eighth) == 96,
           "1/8: 100 → 96, 70 → 48, 72 → 96 (halves round up)");
    check (snapDuration (0, Snap::Eighth) == 48 && snapDuration (-500, Snap::Eighth) == 48, "1/8: never shorter than one step");
    check (snapDuration (100000, Snap::Eighth) == maxChordTicks, "never longer than 16 bars");
    check (snapDuration (10, Snap::Sixteenth) == 24 && snapDuration (110, Snap::Sixteenth) == 120, "1/16: 10 → 24, 110 → 120");
    check (snapDuration (30, Snap::Off) == 30 && snapDuration (5, Snap::Off) == minChordTicks, "off: any tick, at least a 1/16");
    check (snapDuration (500, Snap::Bar, 3) == 576 && snapDuration (100000, Snap::Bar, 3) == 21 * 288,
           "bar in 3/4: 500 → 2 bars; the longest whole number of bars under 16 bars of 4/4");
}

static void testChords()
{
    std::cout << "== chords: palette cells and notes ==\n";
    bool notesOk = true, pcsOk = true;
    int cells = 0;
    for (int key = 0; key < 12; ++key)
        for (Scale scale : { Scale::Major, Scale::NaturalMinor, Scale::HarmonicMinor, Scale::Dorian })
        {
            PaletteContext ctx;
            ctx.paletteKey = ctx.homeKey = key;
            ctx.paletteScale = ctx.homeScale = scale;
            const auto grid = buildPalette (ctx);
            for (const auto& row : grid.rows)
                for (const auto& cell : row.cells)
                {
                    if (! cell.exists)
                        continue;
                    ++cells;
                    const auto c = progressionChordFromCell (cell);
                    const auto n = progressionChordNotes (c);
                    notesOk = notesOk && n.size == cell.numNotes && std::equal (n.begin(), n.end(), cell.notes.begin());
                    pcsOk = pcsOk && c.pitchClasses() == cell.pitchClasses && c.root() == cell.root && c.ticks == defaultChordTicks;
                }
        }
    check (cells > 1000 && notesOk, "every palette cell (12 keys × 4 scales) plays the same notes as a timeline chord");
    check (pcsOk, "… with the same pitch classes and root, a bar long");
    check (progressionChordFromCell (PaletteCell {}).chord.empty(), "an empty cell gives an empty chord");

    const auto notes = [] (const ProgressionChord& c) { const auto n = progressionChordNotes (c); return std::vector<int> (n.begin(), n.end()); };
    auto c = C;
    check (notes (c) == std::vector<int> { 48, 52, 55 }, "C: 48 52 55");
    c.bass = 4;
    check (notes (c) == std::vector<int> { 40, 48, 52, 55 }, "C/E: E below (40 48 52 55)");
    c.bass = 0;
    check (notes (c) == std::vector<int> { 48, 52, 55 }, "C/C: the bass is already the lowest note");
    c.bass = 7;
    c.inversion = 1;
    check (notes (c) == std::vector<int> { 43, 52, 55, 60 }, "C first inversion over G: 43 52 55 60");
    c.octave = 1;
    check (notes (c) == std::vector<int> { 55, 64, 67, 72 }, "… an octave up: 55 64 67 72");
    const auto clamped = makeProgressionChord (C.chord, 5, 9, -9, 14);
    check (clamped.ticks == minChordTicks && clamped.inversion == maxChordInversion && clamped.octave == minChordOctave && clamped.bass == 2,
           "makeProgressionChord clamps duration, inversion and octave, and wraps the bass");
}

static void testInsert()
{
    std::cout << "== insert ==\n";
    ChordProgression p;
    check (p.insertChord (InsertPoint::newSection (0), C) && layout (p) == "Section 1[C4]", "a chord on empty space makes a section");
    check (p.insertChord (InsertPoint::inSection (0, 1), G) && layout (p) == "Section 1[C4 G4]", "on a section, at its end");
    check (p.insertChord (InsertPoint::inSection (0, 1), Am) && layout (p) == "Section 1[C4 Am4 G4]", "on a section, at a point");
    check (p.insertChord (InsertPoint::newSection (p.numSections()), F) && layout (p) == "Section 1[C4 Am4 G4] Section 2[F4]",
           "on empty space after the last section: a new section");
    check (p.insertChord (InsertPoint::newSection (1), Dm) && layout (p) == "Section 1[C4 Am4 G4] Section 3[Dm4] Section 2[F4]",
           "between two sections: a new section there");
    check (p.sections[1].colour != p.sections[0].colour && p.sections[1].colour != p.sections[2].colour,
           "a new section's colour differs from both neighbours");
    check (p.insertChord (InsertPoint::newSection (0), Em) && p.sections[0].name == "Section 4", "default names count on past those in use");

    const auto before = p;
    check (! p.insertChord (InsertPoint::inSection (9, 0), C) && ! p.insertChord (InsertPoint::inSection (0, 9), C)
               && ! p.insertChord (InsertPoint::inSection (0, -1), C) && ! p.insertChord (InsertPoint::newSection (-1), C)
               && ! p.insertChord (InsertPoint::newSection (9), C) && ! p.insertChord (InsertPoint::newSection (0), ProgressionChord {}),
           "out-of-range points and empty chords are refused");
    check (p == before, "… and change nothing");

    ChordProgression full;
    for (int i = 0; i < maxProgressionSections; ++i)
        full.insertChord (InsertPoint::newSection (i), C);
    check (full.numSections() == maxProgressionSections && ! full.insertChord (InsertPoint::newSection (0), C),
           "no more than maxProgressionSections sections");
    ChordProgression longSection;
    for (int i = 0; i < maxSectionChords; ++i)
        longSection.insertChord (i == 0 ? InsertPoint::newSection (0) : InsertPoint::inSection (0, i), C);
    check (! longSection.insertChord (InsertPoint::inSection (0, 0), C) && longSection.insertChord (InsertPoint::newSection (1), C),
           "no more than maxSectionChords chords in a section; a new section still fits");
}

static void testResize()
{
    std::cout << "== resize ==\n";
    auto p = song();
    check (p.resizeChord ({ 0, 0 }, 100) && p.sections[0].chords[0].ticks == 96, "a chord's edge to 100 ticks snaps to 96 (1/8)");
    check (p.chordRange ({ 0, 1 }) == TickRange { 96, 480 }, "the chords after it move up");
    check (! p.resizeChord ({ 0, 0 }, 90), "snapping to the same length changes nothing");
    check (p.resizeChord ({ 0, 0 }, 110, Snap::Sixteenth) && p.sections[0].chords[0].ticks == 120, "1/16: 110 → 120");
    check (p.resizeChord ({ 0, 0 }, 1000, Snap::Bar) && p.sections[0].chords[0].ticks == 1152, "bar: 1000 → 3 bars");
    check (! p.resizeChord ({ 5, 0 }, 96), "an invalid chord is refused");

    p = song();
    check (p.resizeSection (1, 384) && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[F2 G2] Bridge[Dm2 Em2]",
           "a section of two bars to one: both chords halve");
    check (p.resizeSection (1, 288) && p.sections[1].chords[0].ticks == 144 && p.sections[1].chords[1].ticks == 144, "to 3 beats: 1½ each");
    p = song();
    p.sections[0].chords = { C, withTicks (G, 192), withTicks (Am, 192) };   // 4 + 2 + 2
    check (p.resizeSection (0, 576) && layout (p).rfind ("Verse[C3 G144t Am144t]", 0) == 0, "4+2+2 beats to 6: 3 + 1½ + 1½");
    check (p.resizeSection (0, 10, Snap::Bar) && layout (p).rfind ("Verse[C4 G4 Am4]", 0) == 0, "shorter than one step each: one bar each (bar snap)");
    check (p.resizeSection (0, 100, Snap::Off) && p.sections[0].chords[0].ticks + p.sections[0].chords[1].ticks + p.sections[0].chords[2].ticks == 100
               && p.sections[0].chords[0].ticks >= minChordTicks && p.sections[0].chords[2].ticks >= minChordTicks,
           "snap off: 100 ticks exactly, every chord at least a 1/16");
    check (p.resizeSection (0, 100000) && p.sections[0].ticks() == 3 * maxChordTicks, "longer than n × 16 bars is clamped");
    check (p.sectionRange (1).start == 3 * maxChordTicks, "the next section starts at the new end");
    check (! p.resizeSection (7, 384), "an invalid section is refused");
}

static void testMove()
{
    std::cout << "== move ==\n";
    auto p = song();
    check (p.move (Selection::chord (0, 0), InsertPoint::inSection (0, 3)) && layout (p) == "Verse[G4 Am4 C4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "a chord later in its section (positions as before the move)");
    p = song();
    check (p.move (Selection::chord (0, 3), InsertPoint::inSection (0, 0)) && layout (p) == "Verse[F4 C4 G4 Am4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "a chord earlier in its section");
    p = song();
    check (p.move (Selection::chord (0, 1), InsertPoint::inSection (2, 1)) && layout (p) == "Verse[C4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 G4 Em2]",
           "a chord into another section");
    p = song();
    check (p.move (Selection::chord (0, 1), InsertPoint::newSection (2)) && layout (p) == "Verse[C4 Am4 F4] Chorus[F4 G4] Verse[G4] Bridge[Dm2 Em2]",
           "a chord between sections: a new section (with its section's name)");
    p = song();
    check (p.move (Selection::section (2), InsertPoint::newSection (0)) && layout (p) == "Bridge[Dm2 Em2] Verse[C4 G4 Am4 F4] Chorus[F4 G4]",
           "a whole section to the front");
    p = song();
    check (p.move (Selection::section (0), InsertPoint::newSection (3)) && layout (p) == "Chorus[F4 G4] Bridge[Dm2 Em2] Verse[C4 G4 Am4 F4]",
           "a whole section to the end");
    p = song();
    check (p.move (Selection::section (0), InsertPoint::inSection (1, 1)) && layout (p) == "Chorus[F4 C4 G4 Am4 F4 G4] Bridge[Dm2 Em2]",
           "a section dropped into another: its chords go in at the point");
    p = song();
    Selection both;
    both.chords = { { 1, 0 }, { 1, 1 } };
    check (! p.move (both, InsertPoint::inSection (1, 1)) && p == song(), "all of a section's chords dropped inside it: put back, no change");
    both.chords.push_back ({ 0, 0 });
    check (p.move (both, InsertPoint::inSection (1, 1)) && layout (p) == "Verse[G4 Am4 F4] Verse[C4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "… with a chord from elsewhere: the emptied section's place takes them as new sections");
    p = song();
    Selection spread;
    spread.chords = { { 0, 0 }, { 2, 1 } };
    check (p.move (spread, InsertPoint::inSection (1, 2)) && layout (p) == "Verse[G4 Am4 F4] Chorus[F4 G4 C4 Em2] Bridge[Dm2]",
           "chords from two sections into a third, in order");
    p = song();
    const auto before = p;
    check (! p.move (Selection::section (1), InsertPoint::inSection (1, 0)) && p == before, "into the section being moved: refused");
    check (! p.move (Selection::chord (0, 1), InsertPoint::inSection (0, 1)) && ! p.move (Selection::chord (0, 1), InsertPoint::inSection (0, 2))
               && p == before,
           "dropping a chord where it already is changes nothing");
    check (! p.move (Selection {}, InsertPoint::newSection (0)) && ! p.move (Selection::chord (0, 0), InsertPoint::newSection (9)),
           "an empty selection or a bad target is refused");
}

static void testDuplicate()
{
    std::cout << "== duplicate ==\n";
    auto p = song();
    check (p.duplicate (Selection::chord (0, 1), InsertPoint::inSection (1, 2)) && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[F4 G4 G4] Bridge[Dm2 Em2]",
           "Alt-drag a chord into another section");
    p = song();
    check (p.duplicate (Selection::section (1), InsertPoint::newSection (1))
               && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "Alt-drag a section in place, between sections (Klimper reviewers' complaint)");
    check (p.sections[1] == p.sections[2], "… an exact copy, name and colour too");
    p = song();
    check (p.duplicate (Selection::section (2), InsertPoint::newSection (0)) && layout (p) == "Bridge[Dm2 Em2] Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "Alt-drag a section to the front");
    p = song();
    check (p.duplicateInPlace (Selection::chord (0, 1)) && layout (p) == "Verse[C4 G4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "duplicate a chord in place: right after it");
    p = song();
    Selection two;
    two.chords = { { 0, 2 }, { 0, 1 } };
    check (p.duplicateInPlace (two) && layout (p) == "Verse[C4 G4 Am4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "two chords in place: after the later one, in order");
    p = song();
    check (p.duplicateInPlace (Selection::section (0)) && layout (p) == "Verse[C4 G4 Am4 F4] Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "a section in place: a new section right after it");
    p = song();
    Selection mixed;
    mixed.sections = { 2 };
    mixed.chords = { { 0, 0 } };
    check (p.duplicateInPlace (mixed) && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2] Verse[C4] Bridge[Dm2 Em2]",
           "a chord and a later section: new sections after the section");
    check (! p.duplicateInPlace (Selection {}), "nothing selected: nothing to duplicate");
}

static void testRemove()
{
    std::cout << "== multi-select delete ==\n";
    auto p = song();
    Selection sel;
    sel.chords = { { 0, 1 }, { 0, 3 }, { 2, 0 }, { 0, 1 }, { 9, 9 } };
    check (p.remove (sel) && layout (p) == "Verse[C4 Am4] Chorus[F4 G4] Bridge[Em2]", "chords from two sections (duplicates and bad refs ignored)");
    p = song();
    sel = {};
    sel.sections = { 1 };
    sel.chords = { { 1, 0 }, { 2, 0 }, { 2, 1 } };
    check (p.remove (sel) && layout (p) == "Verse[C4 G4 Am4 F4]", "a section, and every chord of another: both sections go");
    check (p.removeSection (0) && p.empty(), "delete the last section: empty");
    check (! p.remove (Selection::chord (0, 0)) && ! p.removeSection (0), "nothing left to delete");
    p = song();
    check (p.removeChord ({ 2, 1 }) && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2]", "one chord");
}

static void testCopyPaste()
{
    std::cout << "== copy and paste at the insertion marker ==\n";
    auto p = song();
    Selection sel;
    sel.chords = { { 0, 2 }, { 0, 0 } };
    const auto clip = p.copy (sel);
    check (clip.parts.size() == 1 && clip.numChords() == 2 && clip.parts[0].chords[0] == C && clip.parts[0].chords[1] == Am,
           "copy two chords: in progression order");
    check (p.insert (InsertPoint::inSection (1, 1), clip) && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[F4 C4 Am4 G4] Bridge[Dm2 Em2]",
           "paste at a marker inside a section: there, not at the end");
    check (p.insert (InsertPoint::inSection (0, 0), clip) && layout (p).rfind ("Verse[C4 Am4 C4 G4", 0) == 0, "paste at the start of a section");
    p = song();
    check (p.insert (InsertPoint::newSection (1), clip) && layout (p) == "Verse[C4 G4 Am4 F4] Verse[C4 Am4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "paste at a marker between sections: a new section there");
    p = song();
    Selection secs;
    secs.sections = { 2, 0 };
    const auto two = p.copy (secs);
    check (two.parts.size() == 2 && two.parts[0].name == "Verse" && two.parts[1].name == "Bridge", "copy two sections");
    check (p.insert (InsertPoint::newSection (2), two)
               && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Verse[C4 G4 Am4 F4] Bridge[Dm2 Em2] Bridge[Dm2 Em2]",
           "paste sections between sections: each a section");
    p = song();
    check (p.insert (InsertPoint::inSection (1, 0), two) && layout (p) == "Verse[C4 G4 Am4 F4] Chorus[C4 G4 Am4 F4 Dm2 Em2 F4 G4] Bridge[Dm2 Em2]",
           "paste sections into a section: their chords at the marker");
    check (! p.insert (InsertPoint::newSection (0), ProgressionClip {}) && p.copy (Selection {}).empty(), "an empty clip pastes nothing");
}

static void testChordEdits()
{
    std::cout << "== inversion, octave, bass, replace, rename, colour ==\n";
    auto p = song();
    check (p.setInversion ({ 0, 0 }, 2) && p.sections[0].chords[0].inversion == 2, "inversion 2");
    check (! p.setInversion ({ 0, 0 }, 2) && p.setInversion ({ 0, 0 }, 9) && p.sections[0].chords[0].inversion == maxChordInversion,
           "the same inversion changes nothing; past the last is clamped");
    check (p.setOctave ({ 0, 1 }, -1) && p.sections[0].chords[1].octave == -1 && p.setOctave ({ 0, 1 }, 7) && p.sections[0].chords[1].octave == maxChordOctave,
           "octave, clamped");
    check (p.setBass ({ 0, 2 }, 16) && p.sections[0].chords[2].bass == 4 && p.setBass ({ 0, 2 }, -5) && p.sections[0].chords[2].bass == -1,
           "bass wraps to a pitch class; negative clears it");
    check (p.replaceChord ({ 1, 0 }, Dm.chord) && layout (p).find ("Chorus[Dm4 G4]") != std::string::npos, "replace a chord, keeping its length");
    check (! p.replaceChord ({ 1, 0 }, WorkspaceChord {}) && ! p.setOctave ({ 9, 0 }, 1), "an empty chord or a bad ref is refused");

    check (p.renameSection (1, "  Chorus 2 ") && p.sections[1].name == "Chorus 2", "rename, trimmed");
    check (! p.renameSection (1, "   ") && ! p.renameSection (1, "Chorus 2") && ! p.renameSection (5, "X"), "an empty name, the same name or a bad section: refused");
    check (p.renameSection (1, "A\tB\nC") && p.sections[1].name == "A B C", "control characters become spaces");
    std::string accents;
    for (int i = 0; i < 30; ++i)
        accents += "\xC3\xA9";   // é
    check (p.renameSection (1, accents) && p.sections[1].name.size() == 48 && p.sections[1].name == accents.substr (0, 48),
           "a long UTF-8 name is cut at a character boundary (48 bytes)");
    check (p.renameSection (1, "Ok\xC3") && p.sections[1].name == "Ok", "a broken UTF-8 sequence is dropped");
    check (p.setSectionColour (1, 5) && p.sections[1].colour == 5 && ! p.setSectionColour (1, 8) && ! p.setSectionColour (1, 5), "colour 0 … 7");
    check (p.clear() && p.empty() && ! p.clear(), "clear");
}

static void testQueries()
{
    std::cout << "== queries: length, chord at a beat, selection range ==\n";
    const auto p = song();
    check (p.totalTicks() == 28 * ticksPerBeat && p.totalBeats() == 28.0 && p.numChords() == 8,
           "length: 16 + 8 + 4 = 28 beats, 8 chords");
    check (p.chordAtBeat (0) == ChordRef { 0, 0 } && p.chordAtBeat (3.99) == ChordRef { 0, 0 } && p.chordAtBeat (4) == ChordRef { 0, 1 }
               && p.chordAtBeat (16) == ChordRef { 1, 0 } && p.chordAtBeat (25.5) == ChordRef { 2, 0 } && p.chordAtBeat (26) == ChordRef { 2, 1 },
           "chord at a beat, on and just before the boundaries");
    check (! p.valid (p.chordAtBeat (28)) && ! p.valid (p.chordAtBeat (-1)) && ! p.valid (ChordProgression {}.chordAt (0)), "outside: none");
    check (p.range (Selection {}) == TickRange { 0, 28 * 96 }, "nothing selected: the whole progression");
    check (p.range (Selection::chord (0, 2)) == TickRange { 8 * 96, 12 * 96 }, "one chord");
    check (p.range (Selection::section (1)) == TickRange { 16 * 96, 24 * 96 } && p.range (Selection::section (1)).startBeats() == 16.0,
           "one section");
    Selection sel;
    sel.chords = { { 2, 0 }, { 0, 1 } };
    check (p.range (sel) == TickRange { 4 * 96, 26 * 96 }, "chords in two sections: first start to last end");
    check (p.range (Selection::chord (7, 7)) == TickRange { 0, 28 * 96 }, "only invalid refs: the whole progression");
    check (p.sectionStart (2) == 24 * 96 && p.sectionStart (3) == 28 * 96 && p.chordRange ({ 1, 1 }) == TickRange { 20 * 96, 24 * 96 },
           "section starts and chord ranges");
}

static void testUndo()
{
    std::cout << "== undo/redo of every edit ==\n";
    const auto s = song();
    checkUndoable ("Insert on empty space", ChordProgression {}, [] (ChordProgression& p) { return p.insertChord (InsertPoint::newSection (0), C); }, "Section 1[C4]");
    checkUndoable ("Insert in a section", s, [] (ChordProgression& p) { return p.insertChord (InsertPoint::inSection (1, 1), Am); },
                   "Verse[C4 G4 Am4 F4] Chorus[F4 Am4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Insert between sections", s, [] (ChordProgression& p) { return p.insertChord (InsertPoint::newSection (1), Em); },
                   "Verse[C4 G4 Am4 F4] Section 1[Em4] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Resize chord", s, [] (ChordProgression& p) { return p.resizeChord ({ 0, 0 }, 200); },
                   "Verse[C2 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Resize section", s, [] (ChordProgression& p) { return p.resizeSection (0, 8 * 96); },
                   "Verse[C2 G2 Am2 F2] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Move", s, [] (ChordProgression& p) { return p.move (Selection::section (2), InsertPoint::newSection (1)); },
                   "Verse[C4 G4 Am4 F4] Bridge[Dm2 Em2] Chorus[F4 G4]");
    checkUndoable ("Duplicate", s, [] (ChordProgression& p) { return p.duplicate (Selection::chord (2, 0), InsertPoint::inSection (0, 0)); },
                   "Verse[Dm2 C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Duplicate in place", s, [] (ChordProgression& p) { return p.duplicateInPlace (Selection::section (1)); },
                   "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Delete", s, [] (ChordProgression& p) { Selection d; d.chords = { { 0, 0 }, { 2, 1 } }; return p.remove (d); },
                   "Verse[G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2]");
    checkUndoable ("Paste", s, [] (ChordProgression& p) { return p.insert (InsertPoint::inSection (0, 2), p.copy (Selection::section (2))); },
                   "Verse[C4 G4 Dm2 Em2 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Inversion", s, [] (ChordProgression& p) { return p.setInversion ({ 0, 0 }, 1); }, layout (s));
    checkUndoable ("Octave", s, [] (ChordProgression& p) { return p.setOctave ({ 0, 0 }, 1); }, layout (s));
    checkUndoable ("Bass", s, [] (ChordProgression& p) { return p.setBass ({ 0, 0 }, 4); }, layout (s));
    checkUndoable ("Replace", s, [] (ChordProgression& p) { return p.replaceChord ({ 0, 0 }, Em.chord); },
                   "Verse[Em4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Rename", s, [] (ChordProgression& p) { return p.renameSection (0, "Intro"); }, "Intro[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]");
    checkUndoable ("Colour", s, [] (ChordProgression& p) { return p.setSectionColour (0, 7); }, layout (s));
    checkUndoable ("Delete section", s, [] (ChordProgression& p) { return p.removeSection (1); }, "Verse[C4 G4 Am4 F4] Bridge[Dm2 Em2]");

    UndoList u (s);
    check (! u.perform ("No-op", [] (ChordProgression& p) { return p.setInversion ({ 0, 0 }, 0); }) && ! u.canUndo(), "an edit that changes nothing adds no step");
    check (! u.perform ("Refused", [] (ChordProgression& p) { p.sections.clear(); return false; }) && u.current() == s,
           "an edit that returns false leaves no trace, even if it touched its copy");
    u.perform ("A", [] (ChordProgression& p) { return p.renameSection (0, "A"); });
    u.perform ("B", [] (ChordProgression& p) { return p.renameSection (0, "B"); });
    u.undo();
    check (u.canRedo() && u.redoLabel() == "B", "after undo, redo is available");
    u.perform ("C", [] (ChordProgression& p) { return p.renameSection (0, "C"); });
    check (! u.canRedo() && u.undoDepth() == 2 && u.current().sections[0].name == "C", "a new edit drops the redo steps");
    check (u.undo() && u.undo() && ! u.undo() && u.current() == s && u.redoDepth() == 2, "undo to the start, then no further");
    check (u.redo() && u.redo() && ! u.redo() && u.current().sections[0].name == "C", "redo to the end, then no further");

    UndoList bounded (s, 5);
    for (int i = 0; i < 12; ++i)
        bounded.perform ("Rename " + std::to_string (i), [i] (ChordProgression& p) { return p.renameSection (0, "V" + std::to_string (i)); });
    check (bounded.undoDepth() == 5 && bounded.limit() == 5, "bounded: 12 edits keep 5 undo steps");
    while (bounded.undo()) {}
    check (bounded.current().sections[0].name == "V6", "… the oldest kept state is the one before the 5 newest edits");
    bounded.reset (ChordProgression {});
    check (bounded.current().empty() && ! bounded.canUndo() && ! bounded.canRedo(), "reset: a new start with no history");

    // Many random edits, all undone and redone exactly.
    std::mt19937 rng (7);
    UndoList r (s);
    std::vector<ChordProgression> states { s };
    const auto pick = [&] (int n) { return n <= 0 ? 0 : (int) (rng() % (unsigned) n); };
    const auto anyChord = [&] (const ChordProgression& p) { const int sec = pick (p.numSections()); return ChordRef { sec, p.validSection (sec) ? pick ((int) p.sections[(size_t) sec].chords.size()) : 0 }; };
    const auto anyPoint = [&] (const ChordProgression& p)
    {
        if (p.empty() || pick (3) == 0)
            return InsertPoint::newSection (pick (p.numSections() + 1));
        const int sec = pick (p.numSections());
        return InsertPoint::inSection (sec, pick ((int) p.sections[(size_t) sec].chords.size() + 1));
    };
    int performed = 0;
    for (int i = 0; i < 400; ++i)
    {
        const int kind = pick (9);
        const bool did = r.perform ("random", [&] (ChordProgression& p)
        {
            switch (kind)
            {
                case 0: return p.insertChord (anyPoint (p), chord (pick (12), { 0, 3 + pick (2), 7 }, 48 * (1 + pick (8))));
                case 1: return p.resizeChord (anyChord (p), pick (900), Snap (pick (6)));
                case 2: return p.resizeSection (pick (p.numSections()), pick (3000), Snap (pick (6)));
                case 3: return p.move (Selection::chord (anyChord (p).section, anyChord (p).index), anyPoint (p));
                case 4: return p.duplicate (Selection::section (pick (p.numSections())), anyPoint (p));
                case 5: return p.duplicateInPlace (Selection::chord (anyChord (p).section, 0));
                case 6: return p.numChords() > 6 && p.remove (Selection::chord (anyChord (p).section, anyChord (p).index));
                case 7: return p.setBass (anyChord (p), pick (13) - 1) || p.setInversion (anyChord (p), pick (4));
                default: return p.renameSection (pick (p.numSections()), "S" + std::to_string (pick (50)));
            }
        });
        if (did)
        {
            states.push_back (r.current());
            ++performed;
        }
    }
    const int depth = r.undoDepth();
    bool exact = depth == std::min (performed, UndoList::defaultLimit);
    for (int k = 0; k < depth; ++k)
        exact = exact && r.undo() && r.current() == states[states.size() - 2 - (size_t) k];
    for (int k = depth; k > 0; --k)
        exact = exact && r.redo() && r.current() == states[states.size() - (size_t) k];
    check (performed > 150 && exact, "400 random edits (" + std::to_string (performed) + " changed it): every kept step undoes and redoes exactly");
}

static void testText()
{
    std::cout << "== the saved text ==\n";
    auto p = song();
    p.sections[1].name = "Chorus 1: big!";
    p.sections[1].chords[0].inversion = 1;
    p.sections[1].chords[0].bass = 4;
    p.sections[1].chords[1].octave = -2;
    const std::string text = progressionToText (p);
    check (text == "czp1 S:0:Verse C:0:0.4.7:384:0:0:-1 C:7:0.4.7:384:0:0:-1 C:9:0.3.7:384:0:0:-1 C:5:0.4.7:384:0:0:-1"
                   " S:1:Chorus%201%3A%20big%21 C:5:0.4.7:384:1:0:4 C:7:0.4.7:384:0:-2:-1 S:2:Bridge C:2:0.3.7:192:0:0:-1 C:4:0.3.7:192:0:0:-1",
           "the text form, written out");
    ChordProgression back;
    check (progressionFromText (text, back) && back == p, "reads back equal");
    check (progressionToText (ChordProgression {}) == "czp1" && progressionFromText ("czp1", back) && back.empty(), "the empty progression");

    ChordProgression unicode;
    unicode.sections.push_back (section ("Refrain \xC3\xA9t\xC3\xA9 \xE2\x99\xAB", 3, { chord (11, { -1, 3, 6, 10, 14, 17 }, 24) }));
    check (progressionFromText (progressionToText (unicode), back) && back == unicode, "a UTF-8 name, a tone below the root, a six-note voicing, a 1/16: round trip");
    const auto ut = progressionToText (unicode);
    check (std::all_of (ut.begin(), ut.end(), [] (char ch) { return (ch > 0x20 && ch < 0x7F) || ch == ' '; })
               && std::none_of (ut.begin(), ut.end(), [] (char ch) { return ch == '"' || ch == '<' || ch == '>' || ch == '&'; }),
           "the text is plain printable ASCII, nothing XML escapes");

    // Random progressions.
    std::mt19937 rng (2026);
    const auto pick = [&] (int lo, int hi) { return lo + (int) (rng() % (unsigned) (hi - lo + 1)); };
    bool allOk = true;
    for (int iter = 0; iter < 500; ++iter)
    {
        ChordProgression r;
        const int nSec = pick (0, 12);
        for (int s = 0; s < nSec; ++s)
        {
            std::string name;
            for (int k = pick (0, 12); k > 0; --k)
            {
                const int kind = pick (0, 4);
                name += kind == 0 ? std::string ("\xC3\xA9") : kind == 1 ? std::string (1, (char) pick (0x21, 0x7E)) : std::string (1, (char) pick ('a', 'z'));
            }
            name = ChordProgression::sanitisedName (name);
            ProgressionSection sec = section (name, pick (0, 7), {});
            for (int c = pick (1, 8); c > 0; --c)
            {
                std::vector<int> tones;
                for (int t = pick (1, 8), v = pick (-12, 12); t > 0; --t, v += pick (1, 7))
                    tones.push_back (v);
                sec.chords.push_back (makeProgressionChord (makeWorkspaceChord (pick (0, 11), tones.data(), (int) tones.size()),
                                                            pick (minChordTicks, maxChordTicks), pick (0, 3), pick (-2, 2), pick (-1, 11)));
            }
            r.sections.push_back (std::move (sec));
        }
        ChordProgression b;
        const auto t = progressionToText (r);
        allOk = allOk && progressionFromText (t, b) && b == r && progressionToText (b) == t;
    }
    check (allOk, "500 random progressions: text → progression → the same text");

    // Garbage.
    const auto rejects = [] (const std::string& g)
    {
        ChordProgression out = song();
        const bool ok = progressionFromText (g, out);
        return ! ok && out == song();
    };
    const std::string chordTok = " C:0:0.4.7:384:0:0:-1";
    const std::vector<std::string> garbage {
        "", " ", "czp", "czp2", "czp0 S:0:A" + chordTok, "CZP1", "czp1 ", " czp1", "czp1  S:0:A" + chordTok, "czp1 X:1",
        "czp1" + chordTok,                                    // a chord before any section
        "czp1 S:0:A", "czp1 S:0:A S:1:B" + chordTok,          // a section without chords
        "czp1 S:8:A" + chordTok, "czp1 S:-1:A" + chordTok, "czp1 S:x:A" + chordTok, "czp1 S:0" + chordTok, "czp1 S:0:A:B" + chordTok,
        "czp1 S:0:A%" + chordTok, "czp1 S:0:A%2" + chordTok, "czp1 S:0:A%ZZ" + chordTok, "czp1 S:0:A%C3" + chordTok,   // bad escapes, broken UTF-8
        "czp1 S:0:%0A" + chordTok, "czp1 S:0:%20A" + chordTok, "czp1 S:0:A!" + chordTok, "czp1 S:0:" + std::string (49, 'x') + chordTok,
        "czp1 S:0:A C:12:0.4.7:384:0:0:-1", "czp1 S:0:A C:-1:0.4.7:384:0:0:-1", "czp1 S:0:A C:0:0.4.7:23:0:0:-1",
        "czp1 S:0:A C:0:0.4.7:6145:0:0:-1", "czp1 S:0:A C:0:0.4.7:384:4:0:-1", "czp1 S:0:A C:0:0.4.7:384:0:3:-1",
        "czp1 S:0:A C:0:0.4.7:384:0:0:12", "czp1 S:0:A C:0:0.4.7:384:0:0:-2", "czp1 S:0:A C:0:0.7.4:384:0:0:-1",
        "czp1 S:0:A C:0:0.4.4:384:0:0:-1", "czp1 S:0:A C:0::384:0:0:-1", "czp1 S:0:A C:0:0..7:384:0:0:-1",
        "czp1 S:0:A C:0:0.1.2.3.4.5.6.7.8:384:0:0:-1", "czp1 S:0:A C:0:0.4.99:384:0:0:-1", "czp1 S:0:A C:0:0.4.7:384:0:0",
        "czp1 S:0:A C:0:0.4.7:384:0:0:-1:5", "czp1 S:0:A C:0:0.4.7:3840000000:0:0:-1", "czp1 S:0:A C:0:0.4.7:+:0:0:-1",
        "czp1 S:0:A C:0:0.4.7:384:0:0:-1 ", "czp1 S:0:A C:0:0.4.7:384:0:0:-1\n", std::string ("czp1\0 S:0:A", 11) + chordTok,
    };
    bool allRejected = true;
    for (const auto& g : garbage)
        if (! rejects (g))
        {
            allRejected = false;
            check (false, "accepted garbage: \"" + g + "\"");
        }
    check (allRejected, std::to_string (garbage.size()) + " malformed texts rejected, leaving the target untouched");

    std::string tooMany = "czp1";
    for (int s = 0; s <= maxProgressionSections; ++s)
        tooMany += " S:0:A" + chordTok;
    std::string tooLong = "czp1 S:0:A";
    for (int c = 0; c <= maxSectionChords; ++c)
        tooLong += chordTok;
    std::string tooManyChords = "czp1";
    for (int s = 0; s < 5; ++s)
    {
        tooManyChords += " S:0:A";
        for (int c = 0; c < 60; ++c)
            tooManyChords += chordTok;
    }
    check (rejects (tooMany) && rejects (tooLong) && rejects (tooManyChords) && rejects ("czp1 S:0:A" + chordTok + std::string (70000, 'x')),
           "limits: sections, chords per section, chords in all, text size");

    // Mutated and random input: never a crash; anything accepted reads back as itself.
    std::mt19937 fz (99);
    const std::string valid = text;
    int accepted = 0;
    bool consistent = true;
    for (int i = 0; i < 20000; ++i)
    {
        std::string g = valid;
        const int mode = (int) (fz() % 4);
        if (mode == 0)
            for (int k = 1 + (int) (fz() % 4); k > 0; --k)
                g[fz() % g.size()] = (char) (fz() % 256);
        else if (mode == 1)
            g = g.substr (0, fz() % g.size());
        else if (mode == 2)
            g.insert (fz() % g.size(), 1, ":. %-0123456789SC"[fz() % 17]);
        else
        {
            g.assign (fz() % 80, ' ');
            for (auto& ch : g)
                ch = (char) (fz() % 256);
            if (fz() % 2)
                g = "czp1 " + g;
        }
        ChordProgression out;
        if (progressionFromText (g, out))
        {
            ++accepted;
            ChordProgression again;
            consistent = consistent && progressionFromText (progressionToText (out), again) && again == out;
        }
    }
    check (consistent, "20000 mutated or random texts: no crash; the " + std::to_string (accepted) + " accepted read back as themselves");
}

int main()
{
    testSnap();
    testChords();
    testInsert();
    testResize();
    testMove();
    testDuplicate();
    testRemove();
    testCopyPaste();
    testChordEdits();
    testQueries();
    testUndo();
    testText();
    return testutil::finish ("test_progression");
}
