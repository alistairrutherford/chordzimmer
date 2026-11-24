// A progression rendered as MIDI (ChordMonkey's PLAN.md §3.6, Phase 6; PLAN.md §3.7): the drag-out.
// The progression played once offline through a ChordEngine of its own (the same code that
// plays it in the host), with Play on in Sync from bar 1 and a fake transport from song position
// 0, then stopped at its end, so the clip is exactly what the host would hear: voicing, bass,
// strum and velocity included. With `chordsOnly`, Strum is left out: each chord starts together
// and is held for its length.
//
// renderSelection (PLAN.md §3.7, Phase 7: the timeline's drag-out) renders what is selected: the
// chords and sections of the selection, back to back in progression order (copy's parts), or
// the whole progression with nothing selected; each section's name is a marker at its start,
// and the clip is named after its sections and the key (exportName: "Verse-Chorus_C"), or after
// its chord when it is one chord ("Dm7_C").
//
// renderPattern renders one workspace pattern as a one-section progression
// (progressionFromPattern); since Phase 4 only the tests use it.
//
// Plain C++, no JUCE. Allocates (the engine is large): for the message thread.
#pragma once

#include "../engine/ChordEngine.h"
#include "../model/TimelineLayout.h"
#include "MidiFile.h"
#include <memory>
#include <vector>

namespace chordzimmer
{
    inline MidiSong renderProgression (const ChordProgression& p, EngineSettings s, double bpm, int numerator = 4,
                                       int denominator = 4, bool chordsOnly = false, double sampleRate = 48000.0, int ppq = 960)
    {
        MidiSong song;
        song.ppq = ppq;
        song.bpm = bpm > 0.0 ? bpm : 120.0;
        song.numerator = numerator;
        song.denominator = denominator;
        song.name = "ChordZimmer";
        if (p.empty())
            return song;

        s.enabled = true;
        s.analysing = false;
        s.capturing = false;
        s.testPattern = false;
        s.play = true;
        s.sync = SyncMode::Sync;
        s.loop = true;
        s.startBar = 1;
        if (chordsOnly)
        {
            s.fx.strum = StrumMode::Off;
        }

        auto engine = std::make_unique<ChordEngine>();
        const auto timeline = std::make_unique<Timeline> (makeTimeline (p));
        engine->setTimeline (*timeline);
        const double beats = p.totalBeats();
        const double spb = sampleRate * 60.0 / song.bpm;
        const auto total = (int64_t) std::llround (beats * spb);
        constexpr int block = 512;

        struct Event { double beat; MidiEvent e; };
        std::vector<Event> events;
        auto out = std::make_unique<BlockEvents>();
        int64_t pos = 0;
        for (bool playing = true;; playing = false)
        {
            do
            {
                const int n = playing ? (int) std::min<int64_t> (block, total - pos) : block;
                Transport t { sampleRate, playing, (double) pos / spb, song.bpm, numerator, denominator };
                out->clear();
                engine->process (s, t, nullptr, 0, n, *out);
                for (const auto& e : *out)
                    if (e.isNoteOn() || e.isNoteOff())
                        events.push_back ({ (double) (pos + e.sample) / spb, e });
                pos += n;
            } while (playing && pos < total);
            if (! playing)
                break;
        }

        // Note-ons and note-offs paired per channel and pitch, oldest first; cut at the loop's end.
        const auto tick = [&] (double beat) { return (int64_t) std::llround (beat * ppq); };
        const int64_t last = tick (beats);
        std::vector<std::vector<size_t>> open (16 * 128);
        for (const auto& ev : events)
        {
            const size_t key = (size_t) ev.e.channel() * 128 + (size_t) ev.e.note();
            if (ev.e.isNoteOn())
            {
                open[key].push_back (song.notes.size());
                song.notes.push_back ({ tick (ev.beat), -1, (uint8_t) ev.e.note(), (uint8_t) ev.e.velocity(), (uint8_t) ev.e.channel() });
            }
            else if (! open[key].empty())
            {
                auto& note = song.notes[open[key].front()];
                open[key].erase (open[key].begin());
                note.end = std::min (tick (ev.beat), last);
            }
        }
        std::vector<MidiNote> kept;
        for (auto note : song.notes)
        {
            if (note.start >= last)
                continue;
            if (note.end < 0)
                note.end = last;
            if (note.end <= note.start)
                note.end = note.start + 1;
            kept.push_back (note);
        }
        std::stable_sort (kept.begin(), kept.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        song.notes = std::move (kept);
        return song;
    }

    //==========================================================================
    // The drag-out of a selection (PLAN.md §3.7).

    // The progression a drag-out holds: the selection's parts as sections, or all of `p`.
    inline ChordProgression exportProgression (const ChordProgression& p, const Selection& sel)
    {
        const Selection n = p.normalised (sel);
        if (n.empty())
            return p;
        ChordProgression out;
        out.sections = p.copy (n).parts;
        return out;
    }

    // "Verse-Chorus_C", "Intro-Verse-Chorus-Bridge+2_Am", "Dm7_C": the sections in order (a name
    // repeated back to back once; at most four, then how many more), or the chord when there is
    // just one, then the key (with "m" for a scale with a minor 3rd). No '/' (a slash chord's
    // bass follows "over").
    inline std::string exportName (const ChordProgression& clip, int key, Scale scale)
    {
        std::string base;
        if (clip.numChords() == 1)
            base = timelineChordLabel (clip.sections[0].chords[0], key, scale).name;
        else
        {
            std::vector<std::string> names;
            for (const auto& s : clip.sections)
                if (names.empty() || names.back() != s.name)
                    names.push_back (s.name);
            for (size_t i = 0; i < names.size() && i < 4; ++i)
                base += (i ? "-" : "") + names[i];
            if (names.size() > 4)
                base += "+" + std::to_string (names.size() - 4);
        }
        if (base.empty())
            base = "ChordZimmer";
        std::string clean;
        for (char c : base)
            clean += c == '/' ? std::string (" over ") : std::string (1, c);
        const int k = ((key % 12) + 12) % 12;
        const bool minor = scaleInfo (scale).semitones[2] == 3;
        return clean + "_" + makeSpelling (k, scale).name (k) + (minor ? "m" : "");
    }

    inline MidiSong renderSelection (const ChordProgression& p, const Selection& sel, const EngineSettings& s, double bpm, int numerator = 4,
                                     int denominator = 4, bool chordsOnly = false, double sampleRate = 48000.0, int ppq = 960)
    {
        const ChordProgression clip = exportProgression (p, sel);
        MidiSong song = renderProgression (clip, s, bpm, numerator, denominator, chordsOnly, sampleRate, ppq);
        for (int i = 0; i < clip.numSections(); ++i)
            song.markers.push_back ({ (int64_t) clip.sectionStart (i) * ppq / ticksPerBeat, clip.sections[(size_t) i].name });
        if (! clip.empty())
            song.name = exportName (clip, s.key, s.scale);
        return song;
    }

    inline MidiSong renderPattern (const Workspace& ws, int index, EngineSettings s, double bpm, int numerator = 4,
                                   int denominator = 4, bool chordsOnly = false, double sampleRate = 48000.0, int ppq = 960)
    {
        if (index < 0 || index >= numPatterns)
            return renderProgression ({}, s, bpm, numerator, denominator, chordsOnly, sampleRate, ppq);
        return renderProgression (progressionFromPattern (ws.patterns[(size_t) index]), s, bpm, numerator, denominator, chordsOnly,
                                  sampleRate, ppq);
    }
}
