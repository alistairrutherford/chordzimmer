// The progression timeline's model (PLAN.md §3.3): one progression, a list of **sections**, each a
// named, coloured list of **chords** with durations. Plain C++, no JUCE; std::vector and
// std::string, so for the message thread (the editor, the state), not the audio thread. Phase 3
// gives the player a flat, fixed-size copy through a SnapshotExchange. The type is ChordProgression:
// theory/Progressions.h already has an (inherited) enum Progression, the white keys' degree order.
//
// A chord is a WorkspaceChord (Workspace.h: a root pitch class and its tones in semitones above
// C2 + root, so the notes keep their voicing) plus a bass note, an inversion, an octave and a
// duration. Palette cells (PaletteBuilder.h) become chords with progressionChordFromCell, which
// keeps the cell's notes exactly. Positions aren't stored: a chord starts where the one before it
// ends, and a section where the one before it ends.
//
// Durations are in ticks, ticksPerBeat to a beat, so every snap (1/16 … 1 bar) and a later
// triplet grid are whole numbers. A section always has at least one chord: an edit that empties
// one removes it.
//
// Every edit is a method returning true if it changed the progression; UndoList.h records them.
// Edits that address chords take ChordRefs and Selections in the current order, and drops and
// pastes take an InsertPoint (the insertion marker): a point inside a section (before chord
// `index`) or a new section (before section `section`; numSections() is the end, "empty space").
//
// The saved text (progressionToText / progressionFromText) is one line of ASCII, versioned:
//
//   czp1 S:<colour>:<name> C:<root>:<tone>.<tone>…:<ticks>:<inversion>:<octave>:<bass> … S:… C:…
//   czp1 S:0:Verse C:0:0.4.7:384:0:0:-1 C:7:0.4.7:384:0:0:-1 S:1:Chorus%201 C:9:0.3.7:192:1:0:4
//
// Names are percent-encoded (anything but A-Z a-z 0-9 - _ . ~), so the line has no spaces or
// colons inside a field and stays ASCII in the state XML. Reading is strict: any malformed token,
// value out of range, unknown version or broken invariant rejects the whole text.
#pragma once

#include "../theory/PaletteBuilder.h"
#include "Workspace.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace chordzimmer
{
    constexpr int ticksPerBeat = 96;
    constexpr int defaultChordTicks = 4 * ticksPerBeat;     // a bar of 4/4 (PLAN.md §3.3)
    constexpr int minChordTicks = ticksPerBeat / 4;         // a 1/16
    constexpr int maxChordTicks = 64 * ticksPerBeat;        // 16 bars of 4/4
    constexpr int maxProgressionSections = 64;
    constexpr int maxSectionChords = 64;
    constexpr int maxProgressionChords = 256;
    constexpr int numSectionColours = 8;                    // the 8 hues of PLAN.md §5.1
    constexpr int maxSectionNameBytes = 48;                 // UTF-8
    constexpr int maxChordInversion = maxSlotInversion;
    constexpr int minChordOctave = minSlotOctave, maxChordOctave = maxSlotOctave;

    //==========================================================================
    // Snap (PLAN.md §3.3): 1/16 … 1 bar, default 1/8; Off keeps any whole tick.

    enum class Snap : uint8_t { Off, Sixteenth, Eighth, Quarter, Half, Bar };
    constexpr Snap defaultSnap = Snap::Eighth;

    // The grid in ticks. `beatsPerBar` (1 … 16) only matters for Bar.
    inline int snapUnitTicks (Snap s, int beatsPerBar = 4) noexcept
    {
        switch (s)
        {
            case Snap::Off:       return 1;
            case Snap::Sixteenth: return ticksPerBeat / 4;
            case Snap::Eighth:    return ticksPerBeat / 2;
            case Snap::Quarter:   return ticksPerBeat;
            case Snap::Half:      return ticksPerBeat * 2;
            case Snap::Bar:       return ticksPerBeat * std::clamp (beatsPerBar, 1, 16);
        }
        return ticksPerBeat / 2;
    }

    namespace progression_detail
    {
        constexpr int mod12 (int n) noexcept { return ((n % 12) + 12) % 12; }

        // The shortest and longest durations on a grid of `unit` ticks.
        inline int minTicksOn (int unit) noexcept { return (minChordTicks + unit - 1) / unit * unit; }
        inline int maxTicksOn (int unit) noexcept { return std::max (minTicksOn (unit), maxChordTicks / unit * unit); }

        // `ticks` to the nearest multiple of `unit` (halves round up).
        inline long long roundTo (long long ticks, int unit) noexcept
        {
            return ticks < 0 ? 0 : (ticks + unit / 2) / unit * unit;
        }
    }

    // A dragged duration snapped: the nearest grid multiple, at least one grid step (and 1/16)
    // and at most maxChordTicks. Snapping is relative to the chord's start, which is the grid
    // itself while everything before it is on the grid (every snapped edit keeps it there).
    inline int snapDuration (int ticks, Snap s, int beatsPerBar = 4) noexcept
    {
        using namespace progression_detail;
        const int unit = snapUnitTicks (s, beatsPerBar);
        return (int) std::clamp (roundTo (ticks, unit), (long long) minTicksOn (unit), (long long) maxTicksOn (unit));
    }

    //==========================================================================
    // Chords.

    struct ProgressionChord
    {
        WorkspaceChord chord;             // root + tones: the notes (Workspace.h)
        int8_t bass = -1;                 // a pitch class played below the chord; -1: none
        int8_t inversion = 0;             // 0 … maxChordInversion
        int8_t octave = 0;                // minChordOctave … maxChordOctave
        int32_t ticks = defaultChordTicks;

        int root() const noexcept { return chord.root; }
        unsigned pitchClasses() const noexcept { return chord.pitchClassMask(); }   // the chord's, without the bass
        double beats() const noexcept { return ticks / (double) ticksPerBeat; }

        bool operator== (const ProgressionChord& o) const noexcept
        {
            return chord == o.chord && bass == o.bass && inversion == o.inversion && octave == o.octave && ticks == o.ticks;
        }
        bool operator!= (const ProgressionChord& o) const noexcept { return ! (*this == o); }
    };

    inline ProgressionChord makeProgressionChord (const WorkspaceChord& c, int ticks = defaultChordTicks, int inversion = 0,
                                                  int octave = 0, int bass = -1) noexcept
    {
        ProgressionChord p;
        p.chord = c;
        p.ticks = std::clamp (ticks, minChordTicks, maxChordTicks);
        p.inversion = (int8_t) std::clamp (inversion, 0, maxChordInversion);
        p.octave = (int8_t) std::clamp (octave, minChordOctave, maxChordOctave);
        p.bass = (int8_t) (bass < 0 ? -1 : progression_detail::mod12 (bass));
        return p;
    }

    // A palette cell as a chord with the same notes (the cell's root at or below its lowest
    // note). An empty chord for a cell that doesn't exist.
    inline ProgressionChord progressionChordFromCell (const PaletteCell& cell, int ticks = defaultChordTicks) noexcept
    {
        using progression_detail::mod12;
        if (! cell.exists || cell.numNotes <= 0)
            return {};
        const int lowest = cell.notes[0] - chordBaseNote;
        const int rootFromC = lowest - mod12 (lowest - cell.root);
        std::array<int, maxPaletteNotes> rel {};
        const int n = std::min (cell.numNotes, maxPaletteNotes);
        for (int i = 0; i < n; ++i)
            rel[(size_t) i] = cell.notes[(size_t) i] - chordBaseNote - rootFromC;
        return makeProgressionChord (makeWorkspaceChord (rootFromC, rel.data(), n), ticks);
    }

    // The notes a chord plays, lowest first: its voicing with inversion and octave (as a
    // workspace slot, Workspace.h's slotNotes), then the bass, if set and not already the lowest
    // note's pitch class, as the nearest note of that pitch class below.
    inline ChordNotes progressionChordNotes (const ProgressionChord& c, int outputOctave = 0) noexcept
    {
        using progression_detail::mod12;
        ChordNotes n = slotNotes (makePatternSlot (c.chord, defaultSlotHalfBeats, c.inversion, c.octave), outputOctave);
        if (c.bass < 0 || n.size == 0 || n.size >= maxChordNotes || mod12 (n.notes[0]) == c.bass)
            return n;
        const int below = n.notes[0] - 1 - mod12 (n.notes[0] - 1 - c.bass);
        if (below < 0)
            return n;
        for (int i = n.size; i > 0; --i)
            n.notes[(size_t) i] = n.notes[(size_t) i - 1];
        n.notes[0] = (uint8_t) below;
        ++n.size;
        return n;
    }

    //==========================================================================
    // Sections, references, selections.

    struct ProgressionSection
    {
        std::string name;
        uint8_t colour = 0;               // 0 … numSectionColours-1
        std::vector<ProgressionChord> chords;

        int ticks() const noexcept
        {
            int t = 0;
            for (const auto& c : chords)
                t += c.ticks;
            return t;
        }

        bool operator== (const ProgressionSection& o) const
        {
            return name == o.name && colour == o.colour && chords == o.chords;
        }
        bool operator!= (const ProgressionSection& o) const { return ! (*this == o); }
    };

    struct ChordRef
    {
        int section = -1, index = -1;

        bool operator== (const ChordRef& o) const noexcept { return section == o.section && index == o.index; }
        bool operator!= (const ChordRef& o) const noexcept { return ! (*this == o); }
        bool operator< (const ChordRef& o) const noexcept { return section != o.section ? section < o.section : index < o.index; }
    };

    // The insertion marker, or where a drop lands.
    struct InsertPoint
    {
        enum class Kind : uint8_t { InSection, NewSection };
        Kind kind = Kind::NewSection;
        int section = 0;                  // InSection: the section; NewSection: the section it goes before
        int index = 0;                    // InSection: the chord it goes before (0 … size)

        static InsertPoint inSection (int s, int i) noexcept { return { Kind::InSection, s, i }; }
        static InsertPoint newSection (int before) noexcept { return { Kind::NewSection, before, 0 }; }

        bool operator== (const InsertPoint& o) const noexcept { return kind == o.kind && section == o.section && index == o.index; }
    };

    // Shift/Cmd-click multi-select: whole sections and single chords. A chord inside a selected
    // section counts as part of that section.
    struct Selection
    {
        std::vector<ChordRef> chords;
        std::vector<int> sections;

        bool empty() const noexcept { return chords.empty() && sections.empty(); }
        static Selection chord (int s, int i) { Selection sel; sel.chords.push_back ({ s, i }); return sel; }
        static Selection section (int s) { Selection sel; sel.sections.push_back (s); return sel; }
    };

    // What Copy takes and Paste and Duplicate put back: parts in progression order, each a whole
    // selected section or the selected chords of one section (with that section's name and
    // colour). Pasted into a section, the parts' chords go in at the point, in order; pasted as a
    // new section, each part becomes a section of its own.
    struct ProgressionClip
    {
        std::vector<ProgressionSection> parts;

        bool empty() const noexcept { return parts.empty(); }
        int numChords() const noexcept
        {
            int n = 0;
            for (const auto& p : parts)
                n += (int) p.chords.size();
            return n;
        }
    };

    struct TickRange
    {
        int start = 0, end = 0;           // [start, end)

        int length() const noexcept { return end - start; }
        bool empty() const noexcept { return end <= start; }
        double startBeats() const noexcept { return start / (double) ticksPerBeat; }
        double endBeats() const noexcept { return end / (double) ticksPerBeat; }
        bool operator== (const TickRange& o) const noexcept { return start == o.start && end == o.end; }
    };

    //==========================================================================

    struct ChordProgression
    {
        std::vector<ProgressionSection> sections;

        bool operator== (const ChordProgression& o) const { return sections == o.sections; }
        bool operator!= (const ChordProgression& o) const { return ! (*this == o); }

        //======================================================================
        // Queries.

        bool empty() const noexcept { return sections.empty(); }
        int numSections() const noexcept { return (int) sections.size(); }

        int numChords() const noexcept
        {
            int n = 0;
            for (const auto& s : sections)
                n += (int) s.chords.size();
            return n;
        }

        bool validSection (int s) const noexcept { return s >= 0 && s < numSections(); }
        bool valid (ChordRef r) const noexcept
        {
            return validSection (r.section) && r.index >= 0 && r.index < (int) sections[(size_t) r.section].chords.size();
        }

        const ProgressionChord* chord (ChordRef r) const noexcept
        {
            return valid (r) ? &sections[(size_t) r.section].chords[(size_t) r.index] : nullptr;
        }

        int totalTicks() const noexcept
        {
            int t = 0;
            for (const auto& s : sections)
                t += s.ticks();
            return t;
        }
        double totalBeats() const noexcept { return totalTicks() / (double) ticksPerBeat; }

        // Where section `s` starts (s == numSections(): the end).
        int sectionStart (int s) const noexcept
        {
            int t = 0;
            for (int i = 0; i < std::min (s, numSections()); ++i)
                t += sections[(size_t) i].ticks();
            return t;
        }

        TickRange sectionRange (int s) const noexcept
        {
            if (! validSection (s))
                return {};
            const int start = sectionStart (s);
            return { start, start + sections[(size_t) s].ticks() };
        }

        TickRange chordRange (ChordRef r) const noexcept
        {
            if (! valid (r))
                return {};
            int t = sectionStart (r.section);
            const auto& ch = sections[(size_t) r.section].chords;
            for (int i = 0; i < r.index; ++i)
                t += ch[(size_t) i].ticks;
            return { t, t + ch[(size_t) r.index].ticks };
        }

        // The chord sounding at `tick` (0 … totalTicks()-1); an invalid ref outside.
        ChordRef chordAt (long long tick) const noexcept
        {
            if (tick < 0)
                return {};
            long long t = 0;
            for (int s = 0; s < numSections(); ++s)
                for (int i = 0; i < (int) sections[(size_t) s].chords.size(); ++i)
                {
                    t += sections[(size_t) s].chords[(size_t) i].ticks;
                    if (tick < t)
                        return { s, i };
                }
            return {};
        }
        ChordRef chordAtBeat (double beat) const noexcept
        {
            return beat < 0.0 ? ChordRef {} : chordAt ((long long) (beat * ticksPerBeat + 1.0e-6));
        }

        // The span from the first selected item's start to the last one's end (the loop region
        // and the export, PLAN.md §3.3, §3.7); nothing selected is the whole progression.
        // Invalid references are ignored.
        TickRange range (const Selection& sel) const noexcept
        {
            TickRange r { 0, 0 };
            bool any = false;
            const auto add = [&] (TickRange x)
            {
                if (x.empty())
                    return;
                r = any ? TickRange { std::min (r.start, x.start), std::max (r.end, x.end) } : x;
                any = true;
            };
            for (int s : sel.sections)
                add (sectionRange (s));
            for (auto c : sel.chords)
                add (chordRange (c));
            return any ? r : TickRange { 0, totalTicks() };
        }

        // `sel` sorted, without duplicates, invalid references or chords of selected sections.
        Selection normalised (const Selection& sel) const
        {
            Selection out;
            for (int s : sel.sections)
                if (validSection (s))
                    out.sections.push_back (s);
            std::sort (out.sections.begin(), out.sections.end());
            out.sections.erase (std::unique (out.sections.begin(), out.sections.end()), out.sections.end());
            for (auto c : sel.chords)
                if (valid (c) && ! std::binary_search (out.sections.begin(), out.sections.end(), c.section))
                    out.chords.push_back (c);
            std::sort (out.chords.begin(), out.chords.end());
            out.chords.erase (std::unique (out.chords.begin(), out.chords.end()), out.chords.end());
            return out;
        }

        // A section name not yet used: "Section 1", "Section 2", …
        std::string defaultSectionName() const
        {
            for (int n = 1;; ++n)
            {
                const std::string name = "Section " + std::to_string (n);
                if (std::none_of (sections.begin(), sections.end(), [&] (const auto& s) { return s.name == name; }))
                    return name;
            }
        }

        // A colour for a new section before section `before`: the section count's hue, moved on
        // until it differs from both neighbours.
        uint8_t defaultSectionColour (int before) const noexcept
        {
            int c = numSections() % numSectionColours;
            const int prev = before > 0 && before <= numSections() ? sections[(size_t) before - 1].colour : -1;
            const int next = before >= 0 && before < numSections() ? sections[(size_t) before].colour : -1;
            while (c == prev || c == next)
                c = (c + 1) % numSectionColours;
            return (uint8_t) c;
        }

        //======================================================================
        // Edits (PLAN.md §3.3). Each returns true if the progression changed. One that would break
        // a limit (maxProgressionSections, maxSectionChords, maxProgressionChords) changes
        // nothing and returns false.

        // Every edit that puts chords in (insertChord, insert, move, duplicate, duplicateInPlace)
        // can report where they landed in `placed` (Phase 4: the editor selects them): the new
        // chords when they went into a section, or the new sections. Left alone on failure.

        // Drop a palette chord: on empty space or between two sections (NewSection) it makes a
        // section with a default name and colour; on a section (InSection) it goes in at the point.
        bool insertChord (InsertPoint at, const ProgressionChord& c, Selection* placed = nullptr)
        {
            if (c.chord.empty())
                return false;
            ProgressionClip clip;
            ProgressionSection part;
            if (at.kind == InsertPoint::Kind::NewSection)
            {
                part.name = defaultSectionName();
                part.colour = defaultSectionColour (at.section);
            }
            part.chords.push_back (c);
            clip.parts.push_back (std::move (part));
            return insert (at, clip, placed);
        }

        // Paste (at the insertion marker) and the second half of Duplicate.
        bool insert (InsertPoint at, const ProgressionClip& clip, Selection* placed = nullptr)
        {
            const int adding = clip.numChords();
            if (adding == 0 || numChords() + adding > maxProgressionChords)
                return false;
            for (const auto& p : clip.parts)
                for (const auto& c : p.chords)
                    if (c.chord.empty() || c.ticks < minChordTicks || c.ticks > maxChordTicks)
                        return false;

            if (at.kind == InsertPoint::Kind::InSection)
            {
                if (! validSection (at.section))
                    return false;
                auto& ch = sections[(size_t) at.section].chords;
                if (at.index < 0 || at.index > (int) ch.size() || (int) ch.size() + adding > maxSectionChords)
                    return false;
                std::vector<ProgressionChord> flat;
                flat.reserve ((size_t) adding);
                for (const auto& p : clip.parts)
                    flat.insert (flat.end(), p.chords.begin(), p.chords.end());
                ch.insert (ch.begin() + at.index, flat.begin(), flat.end());
                if (placed != nullptr)
                {
                    *placed = {};
                    for (int i = 0; i < adding; ++i)
                        placed->chords.push_back ({ at.section, at.index + i });
                }
                return true;
            }

            if (at.section < 0 || at.section > numSections())
                return false;
            std::vector<ProgressionSection> made;
            for (const auto& p : clip.parts)
            {
                if (p.chords.empty())
                    continue;
                if ((int) p.chords.size() > maxSectionChords)
                    return false;
                ProgressionSection s = p;
                s.name = sanitisedName (s.name);
                s.colour = (uint8_t) (s.colour % numSectionColours);
                made.push_back (std::move (s));
            }
            if (numSections() + (int) made.size() > maxProgressionSections)
                return false;
            sections.insert (sections.begin() + at.section, made.begin(), made.end());
            if (placed != nullptr)
            {
                *placed = {};
                for (int i = 0; i < (int) made.size(); ++i)
                    placed->sections.push_back (at.section + i);
            }
            return true;
        }

        // Drag a chord's right edge: its duration becomes `ticks` snapped (snapDuration).
        bool resizeChord (ChordRef r, int ticks, Snap snap = defaultSnap, int beatsPerBar = 4)
        {
            if (! valid (r))
                return false;
            auto& c = sections[(size_t) r.section].chords[(size_t) r.index];
            const int t = snapDuration (ticks, snap, beatsPerBar);
            if (t == c.ticks)
                return false;
            c.ticks = t;
            return true;
        }

        // Drag a section's right edge: its length becomes `ticks` snapped, and its chords scale
        // with it. Each chord boundary moves in proportion and snaps to the grid (relative to
        // the section's start); every chord keeps at least one grid step (and 1/16) and at most
        // maxChordTicks, so the length is clamped to what n chords can hold.
        bool resizeSection (int s, int ticks, Snap snap = defaultSnap, int beatsPerBar = 4)
        {
            using namespace progression_detail;
            if (! validSection (s))
                return false;
            auto& ch = sections[(size_t) s].chords;
            const long long n = (long long) ch.size();
            const int unit = snapUnitTicks (snap, beatsPerBar);
            const long long lo = minTicksOn (unit), hi = maxTicksOn (unit);
            const long long target = std::clamp (roundTo (ticks, unit), n * lo, n * hi);
            const long long original = sections[(size_t) s].ticks();

            std::vector<int> durations ((size_t) n);
            long long cumulative = 0, prev = 0;
            for (long long i = 0; i < n; ++i)
            {
                cumulative += ch[(size_t) i].ticks;
                long long b = target;
                if (i < n - 1)
                {
                    const long long left = n - 1 - i;   // chords still to place after this one
                    b = roundTo ((cumulative * target * 2 + original) / (original * 2), unit);
                    b = std::clamp (b, std::max (prev + lo, target - left * hi), std::min (prev + hi, target - left * lo));
                }
                durations[(size_t) i] = (int) (b - prev);
                prev = b;
            }

            bool changed = false;
            for (size_t i = 0; i < ch.size(); ++i)
            {
                changed = changed || ch[i].ticks != durations[i];
                ch[i].ticks = durations[i];
            }
            return changed;
        }

        // Copy (Cmd-C): the selection as a clip, in progression order.
        ProgressionClip copy (const Selection& selection) const
        {
            const Selection sel = normalised (selection);
            ProgressionClip clip;
            size_t c = 0;
            for (int s = 0; s < numSections(); ++s)
            {
                const auto& sec = sections[(size_t) s];
                if (std::binary_search (sel.sections.begin(), sel.sections.end(), s))
                {
                    clip.parts.push_back (sec);
                    continue;
                }
                ProgressionSection part;
                part.name = sec.name;
                part.colour = sec.colour;
                for (; c < sel.chords.size() && sel.chords[c].section == s; ++c)
                    part.chords.push_back (sec.chords[(size_t) sel.chords[c].index]);
                if (! part.chords.empty())
                    clip.parts.push_back (std::move (part));
            }
            return clip;
        }

        // Delete: the selected sections and chords; a section left without chords goes too.
        bool remove (const Selection& selection)
        {
            const Selection sel = normalised (selection);
            if (sel.empty())
                return false;
            removeNormalised (sel);
            return true;
        }

        bool removeSection (int s) { return remove (Selection::section (s)); }
        bool removeChord (ChordRef r) { return remove (Selection { { r }, {} }); }

        // Drag to move: the selection is taken out and put in at `to` (positions as they are
        // before the move). Dropping into a section the move empties puts the chords back in a
        // section where it was; dropping into a section that is itself being moved fails.
        bool move (const Selection& selection, InsertPoint to, Selection* placed = nullptr)
        {
            const Selection sel = normalised (selection);
            if (sel.empty())
                return false;
            const auto removed = removedSections (sel);
            const auto removedBefore = [&] (int k)
            {
                int n = 0;
                for (int i = 0; i < std::min (k, numSections()); ++i)
                    n += removed[(size_t) i] ? 1 : 0;
                return n;
            };

            InsertPoint target = to;
            if (to.kind == InsertPoint::Kind::InSection)
            {
                if (! validSection (to.section) || to.index < 0 || to.index > (int) sections[(size_t) to.section].chords.size()
                    || std::binary_search (sel.sections.begin(), sel.sections.end(), to.section))
                    return false;
                if (removed[(size_t) to.section])
                    target = InsertPoint::newSection (to.section - removedBefore (to.section));
                else
                {
                    int i = to.index;
                    for (auto c : sel.chords)
                        if (c.section == to.section && c.index < to.index)
                            --i;
                    target = InsertPoint::inSection (to.section - removedBefore (to.section), i);
                }
            }
            else
            {
                if (to.section < 0 || to.section > numSections())
                    return false;
                target = InsertPoint::newSection (to.section - removedBefore (to.section));
            }

            ChordProgression next = *this;
            const ProgressionClip clip = copy (sel);
            next.removeNormalised (sel);
            Selection where;
            if (! next.insert (target, clip, &where) || next == *this)
                return false;
            *this = std::move (next);
            if (placed != nullptr)
                *placed = std::move (where);
            return true;
        }

        // Alt/Option-drag: a copy of the selection goes in at `to`, anywhere, including between
        // sections (as new sections).
        bool duplicate (const Selection& selection, InsertPoint to, Selection* placed = nullptr) { return insert (to, copy (selection), placed); }

        // Duplicate in place: the copy goes right after the selection's last item. After whole
        // sections (the last item is a section) the copies are new sections; after chords they
        // go into the last chord's section.
        bool duplicateInPlace (const Selection& selection, Selection* placed = nullptr)
        {
            const Selection sel = normalised (selection);
            if (sel.empty())
                return false;
            const int lastSection = sel.sections.empty() ? -1 : sel.sections.back();
            const ChordRef lastChord = sel.chords.empty() ? ChordRef {} : sel.chords.back();
            if (lastSection >= lastChord.section)
                return insert (InsertPoint::newSection (lastSection + 1), copy (sel), placed);
            return insert (InsertPoint::inSection (lastChord.section, lastChord.index + 1), copy (sel), placed);
        }

        // Right-click a chord (PLAN.md §3.3).
        bool setInversion (ChordRef r, int inversion) { return setChordField (r, &ProgressionChord::inversion, std::clamp (inversion, 0, maxChordInversion)); }
        bool setOctave (ChordRef r, int octave) { return setChordField (r, &ProgressionChord::octave, std::clamp (octave, minChordOctave, maxChordOctave)); }
        bool setBass (ChordRef r, int pitchClass) { return setChordField (r, &ProgressionChord::bass, pitchClass < 0 ? -1 : progression_detail::mod12 (pitchClass)); }

        // Replace with ▸ (§3.5) and Chord-from-notes Set (§3.4): new notes, the same duration,
        // inversion, octave and bass.
        bool replaceChord (ChordRef r, const WorkspaceChord& c)
        {
            if (! valid (r) || c.empty())
                return false;
            auto& dest = sections[(size_t) r.section].chords[(size_t) r.index].chord;
            if (dest == c)
                return false;
            dest = c;
            return true;
        }

        // Rename a section. The name is trimmed, stripped of control characters and cut to
        // maxSectionNameBytes (at a character boundary); an empty result is refused.
        bool renameSection (int s, const std::string& name)
        {
            if (! validSection (s))
                return false;
            const std::string clean = sanitisedName (name);
            if (clean.empty() || clean == sections[(size_t) s].name)
                return false;
            sections[(size_t) s].name = clean;
            return true;
        }

        bool setSectionColour (int s, int colour)
        {
            if (! validSection (s) || colour < 0 || colour >= numSectionColours || sections[(size_t) s].colour == colour)
                return false;
            sections[(size_t) s].colour = (uint8_t) colour;
            return true;
        }

        bool clear()
        {
            if (sections.empty())
                return false;
            sections.clear();
            return true;
        }

        // A name as renameSection stores it (UTF-8 kept whole; invalid bytes dropped).
        static std::string sanitisedName (const std::string& name)
        {
            std::string out;
            for (size_t i = 0; i < name.size();)
            {
                const auto b = (unsigned char) name[i];
                const size_t len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
                bool ok = len > 0 && i + len <= name.size();
                for (size_t k = 1; ok && k < len; ++k)
                    ok = ((unsigned char) name[i + k] >> 6) == 0x2;
                if (! ok)
                {
                    ++i;
                    continue;
                }
                if (len == 1 && (b < 0x20 || b == 0x7F))
                {
                    out += ' ';
                    ++i;
                    continue;
                }
                if (out.size() + len > (size_t) maxSectionNameBytes)
                    break;
                out.append (name, i, len);
                i += len;
            }
            const auto first = out.find_first_not_of (' ');
            if (first == std::string::npos)
                return {};
            return out.substr (first, out.find_last_not_of (' ') - first + 1);
        }

    private:
        template <class Field>
        bool setChordField (ChordRef r, Field ProgressionChord::* field, int value)
        {
            if (! valid (r))
                return false;
            auto& c = sections[(size_t) r.section].chords[(size_t) r.index];
            if ((int) (c.*field) == value)
                return false;
            c.*field = (Field) value;
            return true;
        }

        // For each section: whether removing `sel` (normalised) removes it.
        std::vector<bool> removedSections (const Selection& sel) const
        {
            std::vector<bool> removed ((size_t) numSections(), false);
            std::vector<int> picked ((size_t) numSections(), 0);
            for (auto c : sel.chords)
                ++picked[(size_t) c.section];
            for (int s = 0; s < numSections(); ++s)
                removed[(size_t) s] = std::binary_search (sel.sections.begin(), sel.sections.end(), s)
                                   || picked[(size_t) s] == (int) sections[(size_t) s].chords.size();
            return removed;
        }

        void removeNormalised (const Selection& sel)
        {
            for (auto it = sel.chords.rbegin(); it != sel.chords.rend(); ++it)
            {
                auto& ch = sections[(size_t) it->section].chords;
                ch.erase (ch.begin() + it->index);
            }
            for (int s = numSections() - 1; s >= 0; --s)
                if (sections[(size_t) s].chords.empty() || std::binary_search (sel.sections.begin(), sel.sections.end(), s))
                    sections.erase (sections.begin() + s);
        }
    };

    // A workspace pattern as a one-section progression with the same chords and lengths (the
    // workspace's playback and drag-out until the timeline UI replaces it, Phase 4).
    inline ChordProgression progressionFromPattern (const Pattern& pattern, const std::string& name = "Pattern")
    {
        ChordProgression p;
        ProgressionSection s;
        s.name = name;
        for (int i = 0, n = pattern.size(); i < n; ++i)
        {
            const auto& slot = pattern.slots[(size_t) i];
            s.chords.push_back (makeProgressionChord (slot.chord, slot.halfBeats * ticksPerBeat / 2, slot.inversion, slot.octave));
        }
        if (! s.chords.empty())
            p.sections.push_back (std::move (s));
        return p;
    }

    //==========================================================================
    // The saved text (the plug-in state and presets). Message thread.

    constexpr const char* progressionTextVersion = "czp1";

    namespace progression_detail
    {
        inline bool unreserved (unsigned char ch) noexcept
        {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        }

        inline std::string percentEncode (const std::string& s)
        {
            static constexpr char hex[] = "0123456789ABCDEF";
            std::string out;
            for (char c : s)
            {
                const auto ch = (unsigned char) c;
                if (unreserved (ch))
                    out += c;
                else
                {
                    out += '%';
                    out += hex[ch >> 4];
                    out += hex[ch & 15];
                }
            }
            return out;
        }

        inline int hexValue (char c) noexcept
        {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        }

        inline bool percentDecode (const std::string& s, std::string& out)
        {
            out.clear();
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] != '%')
                {
                    if (! unreserved ((unsigned char) s[i]))
                        return false;
                    out += s[i];
                    continue;
                }
                if (i + 2 >= s.size())
                    return false;
                const int hi = hexValue (s[i + 1]), lo = hexValue (s[i + 2]);
                if (hi < 0 || lo < 0)
                    return false;
                out += (char) (hi * 16 + lo);
                i += 2;
            }
            return true;
        }

        inline bool inRange (int v, int lo, int hi) noexcept { return v >= lo && v <= hi; }
    }

    inline std::string progressionToText (const ChordProgression& p)
    {
        std::string o = progressionTextVersion;
        for (const auto& s : p.sections)
        {
            o += " S:" + std::to_string ((int) s.colour) + ':' + progression_detail::percentEncode (s.name);
            for (const auto& c : s.chords)
            {
                o += " C:" + std::to_string ((int) c.chord.root) + ':';
                for (int t = 0; t < c.chord.size; ++t)
                    o += (t ? "." : "") + std::to_string ((int) c.chord.tones[(size_t) t]);
                o += ':' + std::to_string (c.ticks) + ':' + std::to_string ((int) c.inversion) + ':' + std::to_string ((int) c.octave)
                   + ':' + std::to_string ((int) c.bass);
            }
        }
        return o;
    }

    // Reads progressionToText's form. False, with `out` untouched, for anything else: another
    // version, a malformed token, a value out of range, a chord before any section, a section
    // without chords, a name that isn't valid UTF-8 or is too long, or a limit exceeded.
    inline bool progressionFromText (const std::string& text, ChordProgression& out)
    {
        using namespace progression_detail;
        using workspace_detail::split;
        using workspace_detail::toInt;
        constexpr size_t maxTextBytes = 64 * 1024;
        if (text.size() > maxTextBytes)
            return false;

        const auto tokens = split (text, ' ');
        if (tokens.empty() || tokens[0] != progressionTextVersion)
            return false;

        ChordProgression p;
        int chords = 0;
        for (size_t t = 1; t < tokens.size(); ++t)
        {
            const auto& tok = tokens[t];
            if (tok.rfind ("S:", 0) == 0)
            {
                const auto f = split (tok, ':');
                int colour = 0;
                ProgressionSection s;
                if (f.size() != 3 || ! toInt (f[1], colour) || ! inRange (colour, 0, numSectionColours - 1)
                    || ! percentDecode (f[2], s.name) || ChordProgression::sanitisedName (s.name) != s.name)
                    return false;
                if ((! p.sections.empty() && p.sections.back().chords.empty()) || p.numSections() == maxProgressionSections)
                    return false;
                s.colour = (uint8_t) colour;
                p.sections.push_back (std::move (s));
            }
            else if (tok.rfind ("C:", 0) == 0)
            {
                const auto f = split (tok, ':');
                int root = 0, ticks = 0, inv = 0, oct = 0, bass = 0;
                if (f.size() != 7 || ! toInt (f[1], root) || ! toInt (f[3], ticks) || ! toInt (f[4], inv) || ! toInt (f[5], oct)
                    || ! toInt (f[6], bass))
                    return false;
                if (! inRange (root, 0, 11) || ! inRange (ticks, minChordTicks, maxChordTicks) || ! inRange (inv, 0, maxChordInversion)
                    || ! inRange (oct, minChordOctave, maxChordOctave) || ! inRange (bass, -1, 11))
                    return false;

                const auto toneFields = split (f[2], '.');
                if (toneFields.empty() || toneFields.size() > (size_t) maxWorkspaceTones)
                    return false;
                std::array<int, maxWorkspaceTones> tones {};
                for (size_t i = 0; i < toneFields.size(); ++i)
                    if (! toInt (toneFields[i], tones[i]) || ! inRange (tones[i], -60, 72) || (i > 0 && tones[i] <= tones[i - 1]))
                        return false;

                if (p.sections.empty() || (int) p.sections.back().chords.size() == maxSectionChords || chords == maxProgressionChords)
                    return false;
                const auto wc = makeWorkspaceChord (root, tones.data(), (int) toneFields.size());
                p.sections.back().chords.push_back (makeProgressionChord (wc, ticks, inv, oct, bass));
                ++chords;
            }
            else
                return false;
        }
        if (! p.sections.empty() && p.sections.back().chords.empty())
            return false;
        out = std::move (p);
        return true;
    }
}
