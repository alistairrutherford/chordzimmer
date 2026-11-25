// The timeline's geometry and gestures (PLAN.md §3.3, §5; Phase 4). Plain C++, no JUCE, so every
// gesture is a Progression edit tested without a UI (PLAN.md §8: "the UI only maps mouse to
// commands"; Tests/test_timeline.cpp drives them as scripted drags).
//
// Layout: sections side by side, left to right, each `pixelsPerBeat` wide per beat, with a
// fixed `gap` between two sections (the drop zone for a new section, to the
// left or right of a section). A section has two rows: its tab (name; drag its right edge to scale
// the section) and its chords (drag a chord's right edge to change its duration). x is in the
// panel's pixels; `origin` is where the progression starts, after scrolling.
//
// Gestures (the panel calls these with the mouse's x):
//   hitTest        what is under the mouse in a row: a chord, a chord's right edge, a section's
//                  tab or its right edge
//   dropPoint      where a drop lands (an InsertPoint): into a section before the chord whose
//                  middle is right of x, or a new section in a gap, before the first or after
//                  the last
//   resize…ToX     an edge dragged to x, snapped (Progression's resizeChord / resizeSection)
//   dropSelection  the selection dragged to x: moved, or with Alt copied (duplicate)
//   selectItem     a click: one item; Shift or Cmd adds or removes it
//   markerAfter    the insertion marker after the selection (where Paste goes)
//   stepSelection  ← →: the chord (or section) before or after
#pragma once

#include "Progression.h"
#include <cmath>
#include <vector>

namespace chordzimmer
{
    struct TimelineGeometry
    {
        static constexpr double minPixelsPerBeat = 6.0, maxPixelsPerBeat = 96.0, defaultPixelsPerBeat = 24.0;

        double pixelsPerBeat = defaultPixelsPerBeat;
        double origin = 0.0;              // x of the progression's start
        double gap = 14.0;                // between two sections
        double grab = 5.0;                // how near (either side) a right edge grabs it

        double pixelsPerTick() const noexcept { return pixelsPerBeat / ticksPerBeat; }
    };

    struct TimelineSpan
    {
        double x0 = 0.0, x1 = 0.0;

        double width() const noexcept { return x1 - x0; }
        double centre() const noexcept { return (x0 + x1) * 0.5; }
        bool contains (double x) const noexcept { return x >= x0 && x < x1; }
    };

    struct TimelineLayout
    {
        TimelineGeometry geometry;
        std::vector<TimelineSpan> sections;
        std::vector<std::vector<TimelineSpan>> chords;
        std::vector<int> sectionTicks;    // where each section starts, in ticks

        static TimelineLayout make (const ChordProgression& p, const TimelineGeometry& g)
        {
            TimelineLayout l;
            l.geometry = g;
            const double ppt = g.pixelsPerTick();
            int tick = 0;
            for (int s = 0; s < p.numSections(); ++s)
            {
                const auto& sec = p.sections[(size_t) s];
                const double x0 = g.origin + tick * ppt + s * g.gap;
                l.sectionTicks.push_back (tick);
                std::vector<TimelineSpan> row;
                double x = x0;
                for (const auto& c : sec.chords)
                {
                    row.push_back ({ x, x + c.ticks * ppt });
                    x += c.ticks * ppt;
                }
                l.sections.push_back ({ x0, x });
                l.chords.push_back (std::move (row));
                tick += sec.ticks();
            }
            return l;
        }

        int numSections() const noexcept { return (int) sections.size(); }

        // Where the progression ends (its start when empty).
        double end() const noexcept { return sections.empty() ? geometry.origin : sections.back().x1; }
        double width() const noexcept { return end() - geometry.origin; }

        // The x of a tick in the progression. A tick on a boundary between two sections is the
        // later one's start, or with `atEnd` the earlier one's end (for the end of a range).
        double xOfTick (long long tick, bool atEnd = false) const noexcept
        {
            if (sections.empty())
                return geometry.origin;
            const double ppt = geometry.pixelsPerTick();
            int s = 0;
            for (int i = 1; i < numSections(); ++i)
                if (tick > sectionTicks[(size_t) i] || (! atEnd && tick == sectionTicks[(size_t) i]))
                    s = i;
            return sections[(size_t) s].x0 + (double) (tick - sectionTicks[(size_t) s]) * ppt;
        }

        double xOfBeat (double beat) const noexcept
        {
            return xOfTick ((long long) std::floor (beat * ticksPerBeat)) + (beat * ticksPerBeat - std::floor (beat * ticksPerBeat)) * geometry.pixelsPerTick();
        }

        const TimelineSpan* chordSpan (ChordRef r) const noexcept
        {
            if (r.section < 0 || r.section >= numSections() || r.index < 0 || r.index >= (int) chords[(size_t) r.section].size())
                return nullptr;
            return &chords[(size_t) r.section][(size_t) r.index];
        }
    };

    //==========================================================================
    // Hit testing.

    enum class TimelineRow : uint8_t { Sections, Chords };

    struct TimelineHit
    {
        enum class Kind : uint8_t { None, Chord, ChordEdge, Section, SectionEdge };
        Kind kind = Kind::None;
        int section = -1;
        ChordRef chord;

        bool isEdge() const noexcept { return kind == Kind::ChordEdge || kind == Kind::SectionEdge; }
        bool operator== (const TimelineHit& o) const noexcept { return kind == o.kind && section == o.section && chord == o.chord; }
    };

    namespace timeline_detail
    {
        // The span of x that grabs the right edge of `span`, whose neighbour to the right is
        // `nextWidth` wide (0: a gap). Narrow items keep most of their body for clicks.
        inline bool onEdge (const TimelineSpan& span, double nextWidth, double x, const TimelineGeometry& g) noexcept
        {
            const double inside = std::min (g.grab, span.width() * 0.3);
            const double outside = nextWidth > 0.0 ? std::min (g.grab, nextWidth * 0.3) : std::min (g.grab, g.gap * 0.5);
            return x >= span.x1 - inside && x <= span.x1 + outside;
        }
    }

    inline TimelineHit hitTest (const TimelineLayout& l, double x, TimelineRow row) noexcept
    {
        using timeline_detail::onEdge;
        TimelineHit hit;
        const auto& g = l.geometry;
        for (int s = 0; s < l.numSections(); ++s)
        {
            if (row == TimelineRow::Sections)
            {
                if (onEdge (l.sections[(size_t) s], 0.0, x, g))
                    return { TimelineHit::Kind::SectionEdge, s, {} };
                if (l.sections[(size_t) s].contains (x))
                    hit = { TimelineHit::Kind::Section, s, {} };
                continue;
            }
            const auto& ch = l.chords[(size_t) s];
            for (int i = 0; i < (int) ch.size(); ++i)
            {
                const double next = i + 1 < (int) ch.size() ? ch[(size_t) i + 1].width() : 0.0;
                if (onEdge (ch[(size_t) i], next, x, g))
                    return { TimelineHit::Kind::ChordEdge, s, { s, i } };
                if (ch[(size_t) i].contains (x))
                    hit = { TimelineHit::Kind::Chord, s, { s, i } };
            }
        }
        return hit;
    }

    //==========================================================================
    // Drops.

    // Where a drop at x lands: into a section (before the first chord whose middle is right of
    // x), or a new section before the first, in a gap, or after the last ("empty space").
    inline InsertPoint dropPoint (const TimelineLayout& l, double x) noexcept
    {
        for (int s = 0; s < l.numSections(); ++s)
        {
            if (x < l.sections[(size_t) s].x0)
                return InsertPoint::newSection (s);
            if (x < l.sections[(size_t) s].x1)
            {
                const auto& ch = l.chords[(size_t) s];
                int i = 0;
                while (i < (int) ch.size() && x >= ch[(size_t) i].centre())
                    ++i;
                return InsertPoint::inSection (s, i);
            }
        }
        return InsertPoint::newSection (l.numSections());
    }

    // The x the panel draws an insert point at (the marker, a drop's indicator).
    inline double insertX (const TimelineLayout& l, InsertPoint at) noexcept
    {
        const int n = l.numSections();
        if (n == 0)
            return l.geometry.origin;
        if (at.kind == InsertPoint::Kind::InSection && at.section >= 0 && at.section < n)
        {
            const auto& ch = l.chords[(size_t) at.section];
            return at.index < (int) ch.size() ? ch[(size_t) std::max (0, at.index)].x0 : l.sections[(size_t) at.section].x1;
        }
        const int s = std::clamp (at.section, 0, n);
        if (s == 0)
            return l.sections.front().x0 - l.geometry.gap * 0.5;
        if (s == n)
            return l.sections.back().x1 + l.geometry.gap * 0.5;
        return (l.sections[(size_t) s - 1].x1 + l.sections[(size_t) s].x0) * 0.5;
    }

    //==========================================================================
    // Edits from the mouse.

    // A chord's right edge dragged to x: its duration from its start to x, snapped.
    inline bool resizeChordToX (ChordProgression& p, const TimelineLayout& l, ChordRef r, double x, Snap snap = defaultSnap, int beatsPerBar = 4)
    {
        const auto* span = l.chordSpan (r);
        if (span == nullptr || ! p.valid (r))
            return false;
        const double ticks = std::clamp ((x - span->x0) / l.geometry.pixelsPerTick(), 0.0, (double) maxChordTicks);
        return p.resizeChord (r, (int) std::lround (ticks), snap, beatsPerBar);
    }

    // A section's right edge dragged to x: its chords scale to the new length (resizeSection).
    inline bool resizeSectionToX (ChordProgression& p, const TimelineLayout& l, int s, double x, Snap snap = defaultSnap, int beatsPerBar = 4)
    {
        if (s < 0 || s >= l.numSections() || ! p.validSection (s))
            return false;
        const double limit = (double) maxChordTicks * maxSectionChords;
        const double ticks = std::clamp ((x - l.sections[(size_t) s].x0) / l.geometry.pixelsPerTick(), 0.0, limit);
        return p.resizeSection (s, (int) std::lround (ticks), snap, beatsPerBar);
    }

    // The selection dragged and dropped at x: moved there, or (Alt) copied there.
    inline bool dropSelection (ChordProgression& p, const TimelineLayout& l, const Selection& sel, double x, bool copy,
                               Selection* placed = nullptr)
    {
        const InsertPoint at = dropPoint (l, x);
        return copy ? p.duplicate (sel, at, placed) : p.move (sel, at, placed);
    }

    //==========================================================================
    // Selection.

    inline bool isSectionSelected (const Selection& sel, int s) noexcept
    {
        return std::find (sel.sections.begin(), sel.sections.end(), s) != sel.sections.end();
    }

    // A chord is selected on its own or with its section.
    inline bool isChordSelected (const Selection& sel, ChordRef r) noexcept
    {
        return isSectionSelected (sel, r.section) || std::find (sel.chords.begin(), sel.chords.end(), r) != sel.chords.end();
    }

    inline bool isSelected (const Selection& sel, const TimelineHit& hit) noexcept
    {
        switch (hit.kind)
        {
            case TimelineHit::Kind::Chord:
            case TimelineHit::Kind::ChordEdge:    return isChordSelected (sel, hit.chord);
            case TimelineHit::Kind::Section:
            case TimelineHit::Kind::SectionEdge:  return isSectionSelected (sel, hit.section);
            case TimelineHit::Kind::None:         break;
        }
        return false;
    }

    // A click on `hit`: that item alone, or with `toggle` (Shift or Cmd) added to or taken out of
    // the selection. A click on nothing clears it (without toggle).
    inline Selection selectItem (const ChordProgression& p, const Selection& current, const TimelineHit& hit, bool toggle)
    {
        const bool section = hit.kind == TimelineHit::Kind::Section || hit.kind == TimelineHit::Kind::SectionEdge;
        const bool chord = hit.kind == TimelineHit::Kind::Chord || hit.kind == TimelineHit::Kind::ChordEdge;
        if (! section && ! chord)
            return toggle ? p.normalised (current) : Selection {};
        if (! toggle)
            return section ? Selection::section (hit.section) : Selection::chord (hit.chord.section, hit.chord.index);

        Selection out = p.normalised (current);
        if (section)
        {
            auto it = std::find (out.sections.begin(), out.sections.end(), hit.section);
            if (it != out.sections.end())
                out.sections.erase (it);
            else
                out.sections.push_back (hit.section);
        }
        else if (isSectionSelected (out, hit.chord.section))
        {
            // A chord of a selected section: the section's other chords stay selected.
            out.sections.erase (std::find (out.sections.begin(), out.sections.end(), hit.chord.section));
            for (int i = 0; i < (int) p.sections[(size_t) hit.chord.section].chords.size(); ++i)
                if (i != hit.chord.index)
                    out.chords.push_back ({ hit.chord.section, i });
        }
        else
        {
            auto it = std::find (out.chords.begin(), out.chords.end(), hit.chord);
            if (it != out.chords.end())
                out.chords.erase (it);
            else
                out.chords.push_back (hit.chord);
        }
        return p.normalised (out);
    }

    // The insertion marker after the selection's last item (PLAN.md §3.3: Paste goes there): a
    // new section after a section, a point in the section after a chord, the end with nothing.
    inline InsertPoint markerAfter (const ChordProgression& p, const Selection& selection)
    {
        const Selection sel = p.normalised (selection);
        if (sel.empty())
            return InsertPoint::newSection (p.numSections());
        const int lastSection = sel.sections.empty() ? -1 : sel.sections.back();
        const ChordRef lastChord = sel.chords.empty() ? ChordRef {} : sel.chords.back();
        if (lastSection >= lastChord.section)
            return InsertPoint::newSection (lastSection + 1);
        return InsertPoint::inSection (lastChord.section, lastChord.index + 1);
    }

    // An insert point still inside `p` (after an edit or undo moved things): clamped.
    inline InsertPoint clampInsertPoint (const ChordProgression& p, InsertPoint at) noexcept
    {
        if (at.kind == InsertPoint::Kind::InSection)
        {
            if (! p.validSection (at.section))
                return InsertPoint::newSection (p.numSections());
            return InsertPoint::inSection (at.section, std::clamp (at.index, 0, (int) p.sections[(size_t) at.section].chords.size()));
        }
        return InsertPoint::newSection (std::clamp (at.section, 0, p.numSections()));
    }

    // The chords in order: chord n of the progression (as ProgressionPlayer counts them).
    inline ChordRef chordAtOrder (const ChordProgression& p, int n) noexcept
    {
        if (n < 0)
            return {};
        for (int s = 0; s < p.numSections(); ++s)
        {
            const int size = (int) p.sections[(size_t) s].chords.size();
            if (n < size)
                return { s, n };
            n -= size;
        }
        return {};
    }

    inline int orderOf (const ChordProgression& p, ChordRef r) noexcept
    {
        if (! p.valid (r))
            return -1;
        int n = 0;
        for (int s = 0; s < r.section; ++s)
            n += (int) p.sections[(size_t) s].chords.size();
        return n + r.index;
    }

    // ← → (PLAN.md §5): the chord after (or before) the last selected one; with sections
    // selected (and no chord after them), the next section. From nothing, the first or last chord.
    inline Selection stepSelection (const ChordProgression& p, const Selection& selection, int direction)
    {
        const int total = p.numChords();
        if (total == 0)
            return {};
        const Selection sel = p.normalised (selection);
        const int dir = direction < 0 ? -1 : 1;
        if (sel.empty())
        {
            const ChordRef r = chordAtOrder (p, dir > 0 ? 0 : total - 1);
            return Selection::chord (r.section, r.index);
        }
        const int lastSection = sel.sections.empty() ? -1 : (dir > 0 ? sel.sections.back() : sel.sections.front());
        const ChordRef chord = sel.chords.empty() ? ChordRef {} : (dir > 0 ? sel.chords.back() : sel.chords.front());
        const bool bySection = chord.section < 0 || (dir > 0 ? lastSection >= chord.section : (lastSection >= 0 && lastSection <= chord.section));
        if (bySection)
            return Selection::section (std::clamp (lastSection + dir, 0, p.numSections() - 1));
        const ChordRef r = chordAtOrder (p, std::clamp (orderOf (p, chord) + dir, 0, total - 1));
        return Selection::chord (r.section, r.index);
    }

    //==========================================================================
    // Zoom (Cmd +/-, Alt+Shift+wheel, PLAN.md §5): a new scale with the beat under `anchorX`
    // kept in place (gaps aside), within the limits.
    inline TimelineGeometry zoomAround (const TimelineGeometry& g, double factor, double anchorX) noexcept
    {
        TimelineGeometry z = g;
        z.pixelsPerBeat = std::clamp (g.pixelsPerBeat * factor, TimelineGeometry::minPixelsPerBeat, TimelineGeometry::maxPixelsPerBeat);
        z.origin = anchorX - (anchorX - g.origin) * (z.pixelsPerBeat / g.pixelsPerBeat);
        return z;
    }

    //==========================================================================
    // A timeline chord's name and numeral relative to the home key (PLAN.md §3.3: "the current
    // chord name and numeral"), as the palette writes them (PaletteBuilder.h): spelled in the
    // home key, the numeral relative to the home scale, a slash bass for an inversion or bass.
    struct TimelineChordLabel
    {
        std::string name, numeral;
    };

    inline TimelineChordLabel timelineChordLabel (const ProgressionChord& c, int homeKey, Scale homeScale)
    {
        TimelineChordLabel label;
        const ChordNotes notes = progressionChordNotes (c);
        if (notes.size == 0)
            return label;
        PaletteContext ctx;
        ctx.paletteKey = ctx.homeKey = ((homeKey % 12) + 12) % 12;
        ctx.paletteScale = ctx.homeScale = homeScale;
        const Spelling sp = makeSpelling (ctx.homeKey, homeScale);
        const int root = ((c.root() % 12) + 12) % 12;
        const unsigned pcs = c.pitchClasses();
        const ChordName name = nameChord (pcs, notes.notes[0] % 12, sp, root);
        label.name = name.text();
        unsigned rel = 0;
        for (int pc = 0; pc < 12; ++pc)
            if ((pcs >> pc) & 1u)
                rel |= 1u << (((pc - root) % 12 + 12) % 12);
        const bool lowerCase = ((rel >> 3) & 1u) != 0 && ((rel >> 4) & 1u) == 0;
        label.numeral = palette::romanFor (palette::numeralRoot (root, sp, ctx), lowerCase)
                      + (name.root == root ? palette::numeralSuffix (name.quality, rel) : std::string());
        return label;
    }
}
