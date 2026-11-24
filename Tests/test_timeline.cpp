// Timeline gesture tests (PLAN.md §6 Phase 4: "scripted drag tests on the model"; §8: every
// gesture is a Progression edit, tested without the UI). Each test lays the progression out as
// the panel does (TimelineLayout.h), then scripts the mouse: where it lands (hitTest, dropPoint),
// and what a drag does to the progression (resize, scale a section, move, Alt-duplicate, a
// palette drop), through an UndoList as the processor runs them. Also: the selections clicks
// and ← → give, the insertion marker, the chord labels, zoom, and the `placed` selection the
// edits report.
//
// Layout used throughout: 10 px per beat from x = 100, 14 px gaps, so with song() below
//   Verse  [C 4][G 4][Am 4][F 4]   x 100 … 260
//   Chorus [F 4][G 4]              x 274 … 354
//   Bridge [Dm 2][Em 2]            x 368 … 408
#include "TestUtil.h"
#include "core/model/TimelineLayout.h"
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

    std::string chordText (const ProgressionChord& c)
    {
        static const char* names[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        std::string s = names[c.root()];
        if ((c.chord.relativeMask() & (1u << 3)) != 0)
            s += "m";
        if (c.ticks % ticksPerBeat != 0)
            return s + std::to_string (c.ticks) + "t";
        return s + std::to_string (c.ticks / ticksPerBeat);
    }

    // "Verse[C4 G4] Chorus[Am2]"
    std::string layout (const ChordProgression& p)
    {
        std::string s;
        for (const auto& sec : p.sections)
        {
            s += (s.empty() ? "" : " ") + sec.name + "[";
            for (size_t i = 0; i < sec.chords.size(); ++i)
                s += (i ? " " : "") + chordText (sec.chords[i]);
            s += "]";
        }
        return s;
    }

    std::string selText (const Selection& sel)
    {
        std::string s;
        for (int x : sel.sections)
            s += (s.empty() ? "" : " ") + std::string ("S") + std::to_string (x);
        for (auto c : sel.chords)
            s += (s.empty() ? "" : " ") + std::to_string (c.section) + "." + std::to_string (c.index);
        return s.empty() ? "-" : s;
    }

    std::string pointText (InsertPoint at)
    {
        return at.kind == InsertPoint::Kind::NewSection ? "new@" + std::to_string (at.section)
                                                        : "in" + std::to_string (at.section) + "@" + std::to_string (at.index);
    }

    ProgressionSection section (std::string name, int colour, std::vector<ProgressionChord> chords)
    {
        ProgressionSection s;
        s.name = std::move (name);
        s.colour = (uint8_t) colour;
        s.chords = std::move (chords);
        return s;
    }

    ChordProgression song()
    {
        ChordProgression p;
        p.sections.push_back (section ("Verse", 0, { C, G, Am, F }));
        p.sections.push_back (section ("Chorus", 1, { F, G }));
        p.sections.push_back (section ("Bridge", 2, { withTicks (Dm, 192), withTicks (Em, 192) }));
        return p;
    }

    TimelineGeometry geometry()
    {
        TimelineGeometry g;
        g.pixelsPerBeat = 10.0;
        g.origin = 100.0;
        g.gap = 14.0;
        g.grab = 5.0;
        return g;
    }

    // A drag as the panel commits it: one labelled step on an UndoList, which undo reverses
    // exactly.
    template <class Edit>
    std::string drag (const std::string& what, const ChordProgression& start, Edit edit, bool expectChange = true)
    {
        UndoList u (start);
        const bool did = u.perform (what, edit);
        if (did)
        {
            const ChordProgression after = u.current();
            check (u.undo() && u.current() == start && u.redo() && u.current() == after, what + ": undo and redo are exact");
        }
        check (did == expectChange, what + (expectChange ? ": changes the progression" : ": changes nothing"));
        return layout (u.current());
    }
}

static void testLayout()
{
    std::cout << "== layout ==\n";
    const auto p = song();
    const auto l = TimelineLayout::make (p, geometry());
    check (l.numSections() == 3 && l.sections[0].x0 == 100.0 && l.sections[0].x1 == 260.0 && l.sections[1].x0 == 274.0
               && l.sections[1].x1 == 354.0 && l.sections[2].x0 == 368.0 && l.sections[2].x1 == 408.0,
           "sections side by side, 10 px a beat, 14 px gaps");
    check (l.chords[0][1].x0 == 140.0 && l.chords[0][1].x1 == 180.0 && l.chords[2][1].x0 == 388.0 && l.chords[2][1].x1 == 408.0,
           "chords within their sections");
    check (l.end() == 408.0 && l.width() == 308.0, "the end and the width (gaps included)");
    check (l.xOfTick (0) == 100.0 && l.xOfTick (16 * ticksPerBeat) == 274.0 && l.xOfTick (16 * ticksPerBeat, true) == 260.0
               && l.xOfBeat (17.5) == 289.0 && l.xOfTick (28 * ticksPerBeat, true) == 408.0,
           "a tick's x: a boundary is the later section's start, or with atEnd the earlier one's end");

    const auto empty = TimelineLayout::make ({}, geometry());
    check (empty.numSections() == 0 && empty.end() == 100.0 && empty.xOfTick (0) == 100.0 && pointText (dropPoint (empty, 300.0)) == "new@0",
           "an empty progression: anywhere drops a new section");
}

static void testHits()
{
    std::cout << "== hit testing ==\n";
    const auto l = TimelineLayout::make (song(), geometry());
    using K = TimelineHit::Kind;
    const auto chordHit = [&] (double x) { return hitTest (l, x, TimelineRow::Chords); };
    const auto tabHit = [&] (double x) { return hitTest (l, x, TimelineRow::Sections); };

    check (chordHit (120.0) == TimelineHit { K::Chord, 0, { 0, 0 } }, "the middle of C is C");
    check (chordHit (137.0) == TimelineHit { K::ChordEdge, 0, { 0, 0 } } && chordHit (142.0) == TimelineHit { K::ChordEdge, 0, { 0, 0 } },
           "within 5 px either side of the C|G boundary is C's right edge");
    check (chordHit (146.0) == TimelineHit { K::Chord, 0, { 0, 1 } }, "6 px into G is G");
    check (chordHit (258.0) == TimelineHit { K::ChordEdge, 0, { 0, 3 } } && chordHit (266.0).kind == K::None,
           "the last chord's edge reaches half the gap; past it, nothing");
    check (chordHit (404.0) == TimelineHit { K::ChordEdge, 2, { 2, 1 } } && chordHit (400.0) == TimelineHit { K::Chord, 2, { 2, 1 } },
           "a 2-beat chord (20 px): its edge zone is the last 5 px (and its body the rest)");
    check (chordHit (50.0).kind == K::None && chordHit (500.0).kind == K::None, "before and after the progression: nothing");

    check (tabHit (200.0) == TimelineHit { K::Section, 0, {} } && tabHit (257.0) == TimelineHit { K::SectionEdge, 0, {} }
               && tabHit (380.0) == TimelineHit { K::Section, 2, {} } && tabHit (267.0).kind == K::None,
           "the tab row: a section, its right edge, a gap");

    // A 1/16 chord at 10 px a beat is 2.5 px: its body keeps 70 % for clicks.
    ChordProgression tiny;
    tiny.sections.push_back (section ("T", 0, { withTicks (C, minChordTicks), G }));
    const auto lt = TimelineLayout::make (tiny, geometry());
    check (hitTest (lt, 100.5, TimelineRow::Chords) == TimelineHit { K::Chord, 0, { 0, 0 } }
               && hitTest (lt, 102.4, TimelineRow::Chords) == TimelineHit { K::ChordEdge, 0, { 0, 0 } },
           "a 1/16 chord: most of it is its body, its last 0.75 px its edge");
}

static void testDropPoints()
{
    std::cout << "== drop points ==\n";
    const auto l = TimelineLayout::make (song(), geometry());
    const auto at = [&] (double x) { return pointText (dropPoint (l, x)); };
    check (at (90.0) == "new@0", "left of the first section: a new section first");
    check (at (110.0) == "in0@0" && at (125.0) == "in0@1" && at (159.0) == "in0@1" && at (161.0) == "in0@2" && at (255.0) == "in0@4",
           "inside a section: before the first chord whose middle is right of the drop");
    check (at (265.0) == "new@1" && at (360.0) == "new@2", "in a gap: a new section between");
    check (at (500.0) == "new@3", "past the end: a new section last (empty space)");

    check (insertX (l, InsertPoint::newSection (0)) == 93.0 && insertX (l, InsertPoint::newSection (1)) == 267.0
               && insertX (l, InsertPoint::newSection (3)) == 415.0 && insertX (l, InsertPoint::inSection (0, 2)) == 180.0
               && insertX (l, InsertPoint::inSection (1, 2)) == 354.0,
           "where a point is drawn: mid-gap, or the chord boundary");
}

static void testResizeDrags()
{
    std::cout << "== resize drags ==\n";
    const auto p = song();
    const auto l = TimelineLayout::make (p, geometry());

    // 203 - 140 = 63 px = 6.3 beats, snapped to the 1/8 grid: 6.5 beats (624 ticks).
    check (drag ("G's edge to x 203 (1/8 snap)", p, [&] (ChordProgression& q) { return resizeChordToX (q, l, { 0, 1 }, 203.0); })
               == "Verse[C4 G624t Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "G's edge dragged 23 px right: 6.3 beats, snapped to 6.5");
    check (drag ("G's edge to x 203 (bar snap)", p, [&] (ChordProgression& q) { return resizeChordToX (q, l, { 0, 1 }, 203.0, Snap::Bar); })
               == "Verse[C4 G8 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "with Bar snap: 2 bars");
    check (drag ("G's edge to x 203 (3/4 bars)", p, [&] (ChordProgression& q) { return resizeChordToX (q, l, { 0, 1 }, 203.0, Snap::Bar, 3); })
               == "Verse[C4 G6 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "with Bar snap in 3/4: 2 bars of 3");
    check (drag ("G's edge to x 203 (no snap)", p, [&] (ChordProgression& q) { return resizeChordToX (q, l, { 0, 1 }, 203.0, Snap::Off); })
               == "Verse[C4 G605t Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "with snap off: 6.3 beats to the tick (605)");
    check (drag ("Em's edge left past its start", p, [&] (ChordProgression& q) { return resizeChordToX (q, l, { 2, 1 }, 300.0); })
               == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em48t]",
           "dragged left past its start: the shortest on the grid (1/8)");
    check (drag ("C's edge far right", p, [&] (ChordProgression& q) { return resizeChordToX (q, l, { 0, 0 }, 1.0e6); })
               == "Verse[C64 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "dragged far right: 16 bars at most");
    drag ("C's edge back where it was", p, [&] (ChordProgression& q) { return resizeChordToX (q, l, { 0, 0 }, 141.0); }, false);

    check (drag ("Verse's edge from 260 to 180", p, [&] (ChordProgression& q) { return resizeSectionToX (q, l, 0, 180.0); })
               == "Verse[C2 G2 Am2 F2] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "the Verse's tab edge dragged to half: every chord halves");
    check (drag ("Bridge's edge from 408 to 448", p, [&] (ChordProgression& q) { return resizeSectionToX (q, l, 2, 448.0); })
               == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm4 Em4]",
           "the Bridge's edge dragged to double: every chord doubles");
    check (drag ("Chorus's edge far left", p, [&] (ChordProgression& q) { return resizeSectionToX (q, l, 1, 0.0); })
               == "Verse[C4 G4 Am4 F4] Chorus[F48t G48t] Bridge[Dm2 Em2]",
           "dragged far left: each chord one grid step");
    ChordProgression q = p;
    check (! resizeChordToX (q, l, { 5, 0 }, 300.0) && ! resizeSectionToX (q, l, 7, 300.0) && q == p, "an edge that isn't there: nothing");
}

static void testMoveDrags()
{
    std::cout << "== move and Alt-duplicate drags ==\n";
    const auto p = song();
    const auto l = TimelineLayout::make (p, geometry());
    const auto moveTo = [&] (const Selection& sel, double x, bool copy, Selection* placed = nullptr)
    {
        return [=] (ChordProgression& q) { return dropSelection (q, l, sel, x, copy, placed); };
    };

    check (drag ("G dragged into the Chorus, before its G", p, moveTo (Selection::chord (0, 1), 330.0, false))
               == "Verse[C4 Am4 F4] Chorus[F4 G4 G4] Bridge[Dm2 Em2]",
           "drag G (Verse) onto the first half of the Chorus's G: moved in before it");
    check (drag ("G dragged to the gap after the Verse", p, moveTo (Selection::chord (0, 1), 266.0, false))
               == "Verse[C4 Am4 F4] Verse[G4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "drag G to the gap: a new section between, named after its own");
    check (drag ("Bridge dragged to the start", p, moveTo (Selection::section (2), 95.0, false))
               == "Bridge[Dm2 Em2] Verse[C4 G4 Am4 F4] Chorus[F4 G4]",
           "drag the Bridge's tab left of the Verse: moved first");
    check (drag ("Verse dragged past the end", p, moveTo (Selection::section (0), 600.0, false))
               == "Chorus[F4 G4] Bridge[Dm2 Em2] Verse[C4 G4 Am4 F4]",
           "drag the Verse to empty space: moved last");
    drag ("C dropped on itself", p, moveTo (Selection::chord (0, 0), 105.0, false), false);
    drag ("the Verse dropped into itself", p, moveTo (Selection::section (0), 150.0, false), false);

    check (drag ("Alt-drag the Chorus between Verse and Chorus", p, moveTo (Selection::section (1), 267.0, true))
               == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "Alt-drag the Chorus into the gap before itself: a copy in place, between the sections");
    check (drag ("Alt-drag Am into the Bridge", p, moveTo (Selection::chord (0, 2), 400.0, true))
               == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2 Am4]",
           "Alt-drag Am to the Bridge's end: a copy there, the Verse unchanged");
    Selection two;
    two.chords = { { 0, 0 }, { 1, 1 } };
    check (drag ("Alt-drag C and the Chorus's G after the Bridge", p, moveTo (two, 450.0, true))
               == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2] Verse[C4] Chorus[G4]",
           "Alt-drag two chords from two sections past the end: a section each");
    check (drag ("Alt-drag the Verse onto its own C", p, moveTo (Selection::section (0), 105.0, true))
               == "Verse[C4 G4 Am4 F4 C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "Alt-drag a section into itself: its chords go in at the point");

    // Where the moved or copied chords landed (the panel selects them).
    Selection placed;
    ChordProgression q = p;
    check (dropSelection (q, l, Selection::chord (0, 1), 330.0, false, &placed) && selText (placed) == "1.1",
           "a moved chord is reported where it landed (the Chorus's second)");
    q = p;
    check (dropSelection (q, l, Selection::section (2), 95.0, false, &placed) && selText (placed) == "S0", "a moved section: its new place (first)");
    q = p;
    check (dropSelection (q, l, two, 450.0, true, &placed) && selText (placed) == "S3 S4", "two copies past the end: the two new sections");
    q = p;
    check (dropSelection (q, l, two, 300.0, false, &placed) && selText (placed) == "1.1 1.2" && layout (q) == "Verse[G4 Am4 F4] Chorus[F4 C4 G4] Bridge[Dm2 Em2]",
           "C and the Chorus's G moved into the Chorus after its F: placed at 1.1 and 1.2");
}

static void testPaletteDrops()
{
    std::cout << "== palette drops ==\n";
    const auto p = song();
    const auto l = TimelineLayout::make (p, geometry());
    // Drop the C major palette's ii (Dm) cell.
    PaletteContext ctx;
    const auto grid = buildPalette (ctx);
    const auto dm = progressionChordFromCell (grid.row ("triad")->cells[1]);
    const auto dropAt = [&] (double x, Selection* placed = nullptr)
    {
        return [=] (ChordProgression& q) { return q.insertChord (dropPoint (l, x), dm, placed); };
    };
    check (drag ("Dm dropped on the Verse's G (first half)", p, dropAt (150.0)) == "Verse[C4 Dm4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "a palette chord dropped on the first half of G: in before it");
    check (drag ("Dm dropped between Verse and Chorus", p, dropAt (265.0)) == "Verse[C4 G4 Am4 F4] Section 1[Dm4] Chorus[F4 G4] Bridge[Dm2 Em2]",
           "dropped in the gap: a new section between");
    check (drag ("Dm dropped on empty space", p, dropAt (700.0)) == "Verse[C4 G4 Am4 F4] Chorus[F4 G4] Bridge[Dm2 Em2] Section 1[Dm4]",
           "dropped on empty space: a new section last");
    ChordProgression empty;
    Selection placed;
    check (empty.insertChord (dropPoint (TimelineLayout::make (empty, geometry()), 300.0), dm, &placed)
               && layout (empty) == "Section 1[Dm4]" && selText (placed) == "S0",
           "the first drop on an empty timeline makes Section 1, reported as placed");
    ChordProgression q = p;
    check (q.insertChord (dropPoint (l, 150.0), dm, &placed) && selText (placed) == "0.1", "a drop into a section: the new chord is placed");
    check (! q.insertChord (dropPoint (l, 150.0), ProgressionChord {}, &placed) && selText (placed) == "0.1", "an empty chord isn't dropped; placed is left alone");
}

static void testSelection()
{
    std::cout << "== clicks, marker, arrows ==\n";
    const auto p = song();
    const auto l = TimelineLayout::make (p, geometry());
    const auto click = [&] (const Selection& cur, double x, TimelineRow row, bool toggle) { return selectItem (p, cur, hitTest (l, x, row), toggle); };

    Selection s = click ({}, 150.0, TimelineRow::Chords, false);
    check (selText (s) == "0.1", "click G: G");
    s = click (s, 290.0, TimelineRow::Chords, true);
    check (selText (s) == "0.1 1.0", "Shift-click the Chorus's F: both");
    s = click (s, 150.0, TimelineRow::Chords, true);
    check (selText (s) == "1.0", "Shift-click G again: just F");
    s = click (s, 380.0, TimelineRow::Sections, true);
    check (selText (s) == "S2 1.0", "Shift-click the Bridge's tab: F and the Bridge");
    s = click (s, 400.0, TimelineRow::Chords, true);
    check (selText (s) == "1.0 2.0", "Shift-click Em inside the selected Bridge: the Bridge gives way to its Dm");
    s = click (s, 120.0, TimelineRow::Chords, false);
    check (selText (s) == "0.0", "a plain click: C alone");
    check (selText (click (s, 266.0, TimelineRow::Chords, false)) == "-" && selText (click (s, 266.0, TimelineRow::Chords, true)) == "0.0",
           "a click on a gap clears; with Shift it keeps the selection");
    check (isSelected (Selection::section (1), hitTest (l, 290.0, TimelineRow::Chords)) && ! isSelected (Selection::chord (0, 0), hitTest (l, 150.0, TimelineRow::Chords)),
           "a chord in a selected section counts as selected");

    check (pointText (markerAfter (p, {})) == "new@3", "the marker with nothing selected: the end");
    check (pointText (markerAfter (p, Selection::chord (0, 1))) == "in0@2", "after G: in the Verse after it");
    Selection mixed;
    mixed.sections = { 0 };
    mixed.chords = { { 1, 0 } };
    check (pointText (markerAfter (p, mixed)) == "in1@1" && pointText (markerAfter (p, Selection::section (1))) == "new@2",
           "after a chord past a section: after the chord; after a section: a new section after it");
    check (pointText (clampInsertPoint (p, InsertPoint::inSection (5, 2))) == "new@3" && pointText (clampInsertPoint (p, InsertPoint::inSection (2, 9))) == "in2@2"
               && pointText (clampInsertPoint (p, InsertPoint::newSection (9))) == "new@3",
           "a marker left outside the progression (an undo) is pulled back in");

    check (selText (stepSelection (p, {}, +1)) == "0.0" && selText (stepSelection (p, {}, -1)) == "2.1", "→ from nothing: the first chord; ← the last");
    check (selText (stepSelection (p, Selection::chord (0, 3), +1)) == "1.0" && selText (stepSelection (p, Selection::chord (1, 0), -1)) == "0.3",
           "→ and ← cross section boundaries");
    check (selText (stepSelection (p, Selection::chord (2, 1), +1)) == "2.1" && selText (stepSelection (p, Selection::chord (0, 0), -1)) == "0.0",
           "→ at the last chord and ← at the first stay");
    check (selText (stepSelection (p, Selection::section (0), +1)) == "S1" && selText (stepSelection (p, Selection::section (0), -1)) == "S0",
           "with a section selected: the next section");
    check (chordAtOrder (p, 5) == ChordRef { 1, 1 } && orderOf (p, { 2, 1 }) == 7 && chordAtOrder (p, 8) == ChordRef {} && orderOf (p, { 3, 0 }) == -1,
           "chords in order (as the player counts them) and back");
}

static void testLabelsAndZoom()
{
    std::cout << "== labels and zoom ==\n";
    const auto label = [] (const ProgressionChord& c, int key, Scale s)
    {
        const auto l = timelineChordLabel (c, key, s);
        return l.name + " " + l.numeral;
    };
    check (label (C, 0, Scale::Major) == "C I" && label (Am, 0, Scale::Major) == "Am vi" && label (Dm, 0, Scale::Major) == "Dm ii",
           "in C major: C I, Am vi, Dm ii");
    check (label (chord (10, { 0, 4, 7 }), 0, Scale::Major) == "Bb bVII", "Bb in C major: bVII");
    check (label (C, 9, Scale::NaturalMinor) == "C III" && label (chord (4, { 0, 4, 7 }), 9, Scale::NaturalMinor) == "E V",
           "in A minor: C is III, E is V (relative to the home scale, as the palette)");
    auto inv = C;
    inv.inversion = 1;
    auto bass = Am;
    bass.bass = 7;
    check (label (inv, 0, Scale::Major) == "C/E I" && label (bass, 0, Scale::Major).find ("/G vi") != std::string::npos,
           "an inversion or a bass: a slash (C/E I, " + label (bass, 0, Scale::Major) + ")");
    check (label (chord (7, { 0, 4, 7, 10 }), 0, Scale::Major) == "G7 V7" && label (chord (11, { 0, 3, 6, 10 }), 0, Scale::Major) == "Bm7b5 viiø7",
           "sevenths: G7 V7, Bm7b5 viiø7");
    check (timelineChordLabel (ProgressionChord {}, 0, Scale::Major).name.empty(), "an empty chord has no label");

    const auto g = geometry();
    const auto z = zoomAround (g, 2.0, 180.0);
    check (z.pixelsPerBeat == 20.0 && z.origin == 20.0, "zoom ×2 around x 180: the beat under it stays (beat 8)");
    check (zoomAround (g, 100.0, 0.0).pixelsPerBeat == TimelineGeometry::maxPixelsPerBeat && zoomAround (g, 0.01, 0.0).pixelsPerBeat == TimelineGeometry::minPixelsPerBeat,
           "zoom stays within its limits");
}

static void testRandomGestures()
{
    std::cout << "== random gestures ==\n";
    // 3000 random clicks, drags and drops at random x: every result keeps the invariants (a
    // section has a chord, durations within limits, the text round-trips), and undoing all of
    // them gives the start back.
    std::mt19937 rng (4);
    const auto p0 = song();
    UndoList u (p0);
    int changed = 0, bad = 0;
    Selection sel;
    const ProgressionChord pool[] = { C, G, Am, F, Dm, Em };
    for (int n = 0; n < 3000; ++n)
    {
        TimelineGeometry g = geometry();
        g.pixelsPerBeat = std::uniform_real_distribution<double> (6.0, 60.0) (rng);
        const auto l = TimelineLayout::make (u.current(), g);
        const double x = std::uniform_real_distribution<double> (50.0, l.end() + 80.0) (rng);
        const double x2 = std::uniform_real_distribution<double> (50.0, l.end() + 80.0) (rng);
        const auto row = rng() % 2 ? TimelineRow::Chords : TimelineRow::Sections;
        const auto hit = hitTest (l, x, row);
        const Snap snap = (Snap) (rng() % 6);
        bool did = false;
        switch (rng() % 5)
        {
            case 0: sel = selectItem (u.current(), sel, hit, rng() % 2); break;
            case 1:
                if (hit.kind == TimelineHit::Kind::ChordEdge || hit.kind == TimelineHit::Kind::Chord)
                    did = u.perform ("resize", [&] (ChordProgression& q) { return resizeChordToX (q, l, hit.chord, x2, snap); });
                else if (hit.section >= 0)
                    did = u.perform ("scale", [&] (ChordProgression& q) { return resizeSectionToX (q, l, hit.section, x2, snap); });
                break;
            case 2:
            case 3:
            {
                Selection placed;
                const bool copy = rng() % 2;
                did = u.perform ("drag", [&] (ChordProgression& q) { return dropSelection (q, l, sel, x2, copy, &placed); });
                if (did)
                    sel = placed;
                break;
            }
            default:
                did = u.perform ("drop", [&] (ChordProgression& q) { return q.insertChord (dropPoint (l, x2), pool[rng() % 6]); });
                break;
        }
        sel = u.current().normalised (sel);
        changed += did ? 1 : 0;
        const auto& q = u.current();
        ChordProgression back;
        bool ok = progressionFromText (progressionToText (q), back) && back == q;
        for (const auto& s : q.sections)
        {
            ok = ok && ! s.chords.empty();
            for (const auto& c : s.chords)
                ok = ok && c.ticks >= minChordTicks && c.ticks <= maxChordTicks;
        }
        bad += ok ? 0 : 1;
    }
    check (changed > 500 && bad == 0, std::to_string (changed) + " of 3000 gestures changed the progression; every result is valid and round-trips");
    while (u.undo()) {}
    // The history is bounded (100 steps): undo goes back as far as it keeps.
    check (u.undoDepth() == 0 && (changed > UndoList::defaultLimit || u.current() == p0), "undo goes back to the oldest step kept");
}

int main()
{
    testLayout();
    testHits();
    testDropPoints();
    testResizeDrags();
    testMoveDrags();
    testPaletteDrops();
    testSelection();
    testLabelsAndZoom();
    testRandomGestures();
    return testutil::finish ("test_timeline");
}
