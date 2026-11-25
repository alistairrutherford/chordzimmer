// Import a MIDI file (PLAN.md §6 Phase 9, the optional phase: "drop a .mid in: melody → Match,
// chords → timeline"). Plain C++, no JUCE; std::vector and std::string, for the message thread.
//
// The file is read by MidiFile.h (type 0 or 1, every track's notes together). Then:
//
//   chords   the notes grouped as Detect groups them live (ChordListener.h, ChordMonkey's
//            Phase 8), here with the whole file at hand: notes starting within 60 ms of a
//            group's first make one chord, and a rolled chord also gathers notes each within
//            120 ms of the one before while every note so far is held, up to half a second.
//            A group counts as a chord with three pitch classes or more, or a power chord (two,
//            a fifth apart); a melody note or an interval over a chord is ignored, and so is a
//            group's top note more than an octave above the next one down (a melody note
//            struck with the chord: it belongs to the melody). The same
//            chord again over the same bass (a rhythm, a comp) lengthens the last one. A chord
//            lasts until the next chord starts (a rest between two belongs to the first); the
//            last to its last note's release, rounded up to a whole beat (as Detect does: a
//            chord let go early still fills its beat). The silence before the first chord is
//            dropped.
//            Starts are rounded to a 1/16 grid from the first chord; every chord is at least a
//            1/16 and at most 16 bars (a longer one is cut there).
//   names    each chord is read by Detect.h's slotForHeard: ChordNamer over the bass, one of the
//            chord types in root position where it is one, else the tones as played; a bass
//            that's a chord tone is the inversion, one that isn't sits under the chord; the
//            register played is kept
//   sections the file's markers (meta-event 6: ChordZimmer's own Drag MIDI writes the section
//            names so) start sections named after them; without markers, one section named
//            after the file. A section holds at most maxSectionChords (then "Name 2" …), the
//            progression maxProgressionChords (the rest are dropped and counted)
//   drums    notes on MIDI channel 10 (General MIDI's drums) are left out before anything else,
//            so a song's drum track doesn't make chords or melody; they're counted
//   melody   the notes that aren't part of a chord, as a NoteSet for Match (beats each pitch
//            class sounds; the bass is the lowest); a file with no notes outside its chords
//            gives all of its notes
#pragma once

#include "../model/Detect.h"
#include "../model/Progression.h"
#include "../theory/Matcher.h"
#include "MidiFile.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace chordzimmer
{
    struct MidiImport
    {
        ChordProgression chords;          // empty if the file has no chords
        NoteSet melody;                   // empty if it has no notes
        int numNotes = 0, melodyNotes = 0, droppedChords = 0, drumNotes = 0;
        double beats = 0.0;               // the file's length (its last note's end)
        std::string error;                // why it couldn't be read ("" if it could)

        bool ok() const noexcept { return error.empty(); }
    };

    namespace midi_import_detail
    {
        struct Note { double start, end, seconds; int pitch; };
        constexpr int drumChannel = 9;    // MIDI channel 10, counted from 0

        inline int pcCount (unsigned m) noexcept
        {
            int n = 0;
            for (; m != 0; m &= m - 1)
                ++n;
            return n;
        }

        inline bool countsAsChord (unsigned m) noexcept
        {
            const int n = pcCount (m);
            if (n >= 3)
                return true;
            if (n != 2)
                return false;
            for (int pc = 0; pc < 12; ++pc)
                if (((m >> pc) & 1u) != 0 && ((m >> ((pc + 7) % 12)) & 1u) != 0)
                    return true;
            return false;
        }

        struct Group
        {
            double start = 0.0, end = 0.0;
            unsigned mask = 0;
            int lowest = 128;
            std::vector<size_t> notes;
        };
    }

    // The chords and melody of a read file. `key` and `scale` spell the names; `name` names the
    // section when the file has no markers.
    inline MidiImport importMidi (const MidiSong& song, int key, Scale scale, const std::string& name)
    {
        using namespace midi_import_detail;
        MidiImport out;
        const double ppq = song.ppq > 0 ? (double) song.ppq : 960.0;
        const double secondsPerBeat = 60.0 / (song.bpm > 0.0 ? song.bpm : 120.0);

        std::vector<Note> notes;
        for (const auto& n : song.notes)
            if ((n.channel & 15) == drumChannel)
                ++out.drumNotes;
            else
                notes.push_back ({ (double) n.start / ppq, (double) std::max (n.end, n.start + 1) / ppq, (double) n.start / ppq * secondsPerBeat, n.pitch });
        std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
        out.numNotes = (int) notes.size();
        for (const auto& n : notes)
            out.beats = std::max (out.beats, n.end);
        if (notes.empty())
            return out;

        // Group the onsets (ChordListener's rules).
        std::vector<Group> groups;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            bool joins = false;
            if (! groups.empty())
            {
                auto& g = groups.back();
                const double first = notes[g.notes.front()].seconds, last = notes[g.notes.back()].seconds;
                const bool allHeld = std::all_of (g.notes.begin(), g.notes.end(), [&] (size_t k) { return notes[k].end > n.start; });
                joins = n.seconds - first <= 0.06 || (allHeld && n.seconds - last <= 0.12 && n.seconds - first <= 0.5);
            }
            if (! joins)
                groups.push_back ({ n.start, n.end, 0, 128, {} });
            auto& g = groups.back();
            g.notes.push_back (i);
            g.mask |= 1u << (n.pitch % 12);
            g.lowest = std::min (g.lowest, n.pitch);
            g.end = std::max (g.end, n.end);
        }

        // A top note far above the rest (more than an octave) is the melody's, struck with the chord.
        std::vector<bool> topLine (notes.size(), false);
        for (auto& g : groups)
        {
            if (g.notes.size() < 3)
                continue;
            std::vector<size_t> byPitch = g.notes;
            std::sort (byPitch.begin(), byPitch.end(), [&] (size_t a, size_t b) { return notes[a].pitch < notes[b].pitch; });
            const size_t top = byPitch.back(), next = byPitch[byPitch.size() - 2];
            if (notes[top].pitch - notes[next].pitch <= 12)
                continue;
            unsigned rest = 0;
            for (size_t k : g.notes)
                if (k != top)
                    rest |= 1u << (notes[k].pitch % 12);
            if (! countsAsChord (rest))
                continue;
            topLine[top] = true;
            g.notes.erase (std::find (g.notes.begin(), g.notes.end(), top));
            g.mask = rest;
        }

        // The chords: counting groups, a repeat lengthening the one before.
        std::vector<Group> chords;
        std::vector<bool> inChord (notes.size(), false);
        for (const auto& g : groups)
        {
            if (! countsAsChord (g.mask))
                continue;
            for (size_t k : g.notes)
                inChord[k] = true;
            if (! chords.empty() && chords.back().mask == g.mask && chords.back().lowest % 12 == g.lowest % 12)
            {
                chords.back().end = std::max (chords.back().end, g.end);
                continue;
            }
            chords.push_back (g);
        }

        // The melody: the notes outside the chords (or all of them).
        for (size_t i = 0; i < notes.size(); ++i)
            inChord[i] = inChord[i] && ! topLine[i];
        bool anyOutside = std::find (inChord.begin(), inChord.end(), false) != inChord.end();
        int lowest = 128;
        for (size_t i = 0; i < notes.size(); ++i)
            if (! anyOutside || ! inChord[i])
            {
                out.melody.weight[(size_t) (notes[i].pitch % 12)] += notes[i].end - notes[i].start;
                lowest = std::min (lowest, notes[i].pitch);
                ++out.melodyNotes;
            }
        out.melody.bass = lowest < 128 ? lowest % 12 : -1;
        if (chords.empty())
            return out;

        // Times on the 1/16 grid from the first chord; each lasts to the next one's start.
        constexpr int grid = ticksPerBeat / 4;
        const double origin = chords.front().start;
        const auto toTicks = [&] (double beat) { return (long long) std::llround ((beat - origin) * ticksPerBeat / grid) * grid; };
        std::vector<long long> starts;
        for (const auto& c : chords)
            starts.push_back (toTicks (c.start));

        // Sections from the markers at or after the first chord (a marker before it starts the first).
        struct Cut { long long tick; std::string name; };
        std::vector<Cut> cuts;
        for (const auto& m : song.markers)
            cuts.push_back ({ std::max (0LL, toTicks ((double) m.tick / ppq)), m.text.empty() ? name : m.text });
        std::stable_sort (cuts.begin(), cuts.end(), [] (const Cut& a, const Cut& b) { return a.tick < b.tick; });
        if (cuts.empty() || cuts.front().tick > 0)
            cuts.insert (cuts.begin(), { 0, cuts.empty() ? name : cuts.front().name });

        size_t cut = 0;
        int partInSection = 1;
        std::string sectionName = cuts.front().name;
        for (size_t i = 0; i < chords.size(); ++i)
        {
            const long long start = starts[i];
            long long end = i + 1 < chords.size() ? starts[i + 1]
                                                  : (long long) std::ceil ((chords[i].end - origin) - 1.0e-6) * ticksPerBeat;
            const int ticks = (int) std::clamp (end - start, (long long) minChordTicks, (long long) maxChordTicks);
            if (end - start < minChordTicks && i + 1 < chords.size())
                continue;                 // two chords on one grid point: the later one stays

            bool newSection = out.chords.empty();
            while (cut + 1 < cuts.size() && cuts[cut + 1].tick <= start)
            {
                ++cut;
                newSection = true;
                sectionName = cuts[cut].name;
                partInSection = 1;
            }
            if (! newSection && (int) out.chords.sections.back().chords.size() >= maxSectionChords)
            {
                newSection = true;
                ++partInSection;
            }
            if (out.chords.numChords() >= maxProgressionChords || (newSection && out.chords.numSections() >= maxProgressionSections))
            {
                ++out.droppedChords;
                continue;
            }
            if (newSection)
            {
                ProgressionSection s;
                s.name = ChordProgression::sanitisedName (partInSection > 1 ? sectionName + " " + std::to_string (partInSection) : sectionName);
                if (s.name.empty())
                    s.name = "Imported";
                s.colour = (uint8_t) (out.chords.numSections() % numSectionColours);
                out.chords.sections.push_back (std::move (s));
            }
            const PatternSlot slot = slotForHeard (chords[i].mask, chords[i].lowest, key, scale, defaultSlotHalfBeats);
            out.chords.sections.back().chords.push_back (makeProgressionChord (slot.chord, ticks, slot.inversion, slot.octave));
        }
        return out;
    }

    // From the file's bytes: an error (and nothing else) if it isn't a MIDI file this reads.
    inline MidiImport importMidiFile (const uint8_t* data, size_t size, int key, Scale scale, const std::string& name)
    {
        MidiSong song;
        std::string error;
        if (data == nullptr || ! readMidiFile (data, size, song, error))
        {
            MidiImport out;
            out.error = error.empty() ? "not a MIDI file" : error;
            return out;
        }
        return importMidi (song, key, scale, name);
    }
}
