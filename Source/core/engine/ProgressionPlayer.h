// Timeline playback (PLAN.md §3.3, §4, Phase 3): the progression plays in time, chord by chord,
// through the effect chain. Reworked from ChordMonkey's PatternPlayer (ChordMonkey's PLAN.md
// §3.2). Plain C++, no JUCE, no allocation in the player.
//
// The audio thread doesn't see the ChordProgression (vectors, strings): the message thread
// flattens it into a Timeline (makeTimeline), a fixed-size, trivially copyable list of chords
// with their start, length and notes, and the loop region, and hands it over through a
// SnapshotExchange.
//
// Two clocks (PLAN.md §3.3, the Sync mode parameter):
//
//   Sync     follows the host. The timeline is locked to the song from the Start bar: with Loop
//            on, the loop region repeats (timeline position = region start + (song position −
//            start) mod the region's length, before the start bar too); with Loop off, the
//            progression plays once from the start bar and is silent outside it. Nothing plays
//            while the host is stopped. A locate or a host loop lands where the timeline would be.
//   Preview  runs on its own at the host's tempo (the last one it gave while stopped), whether or
//            not the host plays, from where it last stopped (rewind: the region's start, or the
//            top). With Loop on it repeats the region; with Loop off it stops at the end, and the
//            next Play starts from the top.
//
// The loop region is the selection's range (Progression.h's range(): the selected chords and
// sections, or everything), clamped into the progression.
//
// The player decides *when* a chord starts and emits triggers; the engine plays each through the
// effect chain as one performance. A trigger restrikes:
//
//   - at every chord start (two equal chords in a row are two strikes; a one-chord loop
//     restrikes each time round), and at each wrap of the loop region,
//   - at the start of playback, and wherever the playhead jumps (a locate, a host loop wrap),
//   - when the chord under the playhead changes otherwise (an edit moved the chord lines),
//
// and a stop trigger ends the chord: Play off, the host stopping (Sync), the end of the
// progression (Loop off) or an empty progression.
//
// Like PatternPlayer, a Sync segment that ends at a host loop wrap stops short of the loop end
// in musical time: a chord line there belongs after the wrap.
#pragma once

#include "../model/Progression.h"
#include "Clock.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace chordzimmer
{
    // The order of the Sync mode parameter's choices: only ever append.
    enum class SyncMode : uint8_t { Sync, Preview, Count };
    constexpr int numSyncModes = (int) SyncMode::Count;

    inline const char* syncModeName (SyncMode m) noexcept
    {
        static constexpr const char* names[numSyncModes] = { "Sync", "Preview" };
        const int i = (int) m;
        return names[i >= 0 && i < numSyncModes ? i : 0];
    }

    constexpr int maxStartBar = 999;

    //==========================================================================

    struct TimelineChord
    {
        int32_t start = 0, ticks = 0;     // Progression.h's ticks (ticksPerBeat to a beat)
        ChordNotes notes;                 // progressionChordNotes at Chord Octave 0
        int8_t root = 0;                  // pitch class (for Bass)
        int16_t section = 0;

        double startBeats() const noexcept { return start / (double) ticksPerBeat; }
        double endBeats() const noexcept { return (start + ticks) / (double) ticksPerBeat; }
    };

    struct Timeline
    {
        static constexpr double eps = 1.0e-7;   // beats: a hair before a line counts as on it

        std::array<TimelineChord, maxProgressionChords> chords {};
        int count = 0;
        int32_t total = 0;                // ticks
        int32_t loopStart = 0, loopEnd = 0;   // the loop region [start, end), ticks

        bool empty() const noexcept { return count == 0 || total <= 0; }
        double lengthBeats() const noexcept { return total / (double) ticksPerBeat; }
        double loopStartBeats() const noexcept { return loopStart / (double) ticksPerBeat; }
        double loopEndBeats() const noexcept { return loopEnd / (double) ticksPerBeat; }
        double loopBeats() const noexcept { return (loopEnd - loopStart) / (double) ticksPerBeat; }

        // The chord at timeline position `beats` (0 … length), or -1 outside.
        int chordAt (double beats) const noexcept
        {
            if (empty() || beats < -eps || beats >= lengthBeats() - eps)
                return -1;
            int lo = 0, hi = count - 1;   // the last chord starting at or before beats (+ eps)
            while (lo < hi)
            {
                const int mid = (lo + hi + 1) / 2;
                if (chords[(size_t) mid].startBeats() <= beats + eps)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            return lo;
        }

        // The first chord line strictly after `beats` (+ eps): the next chord's start, or the
        // end; HUGE_VAL past the end.
        double nextLine (double beats) const noexcept
        {
            if (empty() || beats >= lengthBeats() - eps)
                return HUGE_VAL;
            if (beats < -eps)
                return 0.0;
            const int i = chordAt (beats);
            return i + 1 < count ? chords[(size_t) i + 1].startBeats() : lengthBeats();
        }

        // Whether `beats` is on a chord line (a chord's start), within eps.
        bool onLine (double beats) const noexcept
        {
            const int i = chordAt (beats);
            return i >= 0 && std::abs (chords[(size_t) i].startBeats() - beats) <= eps;
        }

        bool operator== (const Timeline& o) const noexcept
        {
            if (count != o.count || total != o.total || loopStart != o.loopStart || loopEnd != o.loopEnd)
                return false;
            for (int i = 0; i < count; ++i)
            {
                const auto& a = chords[(size_t) i];
                const auto& b = o.chords[(size_t) i];
                if (a.start != b.start || a.ticks != b.ticks || a.root != b.root || a.section != b.section || a.notes.size != b.notes.size)
                    return false;
                for (int n = 0; n < a.notes.size; ++n)
                    if (a.notes.notes[(size_t) n] != b.notes.notes[(size_t) n])
                        return false;
            }
            return true;
        }
        bool operator!= (const Timeline& o) const noexcept { return ! (*this == o); }
    };

    // The progression flattened for the audio thread, with `loop` (the selection) as its loop
    // region. Message thread (the progression is).
    inline Timeline makeTimeline (const ChordProgression& p, const Selection& loop = {})
    {
        Timeline t;
        int32_t at = 0;
        for (int s = 0; s < p.numSections(); ++s)
            for (const auto& c : p.sections[(size_t) s].chords)
            {
                if (t.count == maxProgressionChords)
                    break;
                auto& d = t.chords[(size_t) t.count++];
                d.start = at;
                d.ticks = c.ticks;
                d.notes = progressionChordNotes (c);
                d.root = (int8_t) c.root();
                d.section = (int16_t) s;
                at += c.ticks;
            }
        t.total = at;
        const TickRange r = p.range (loop);
        t.loopStart = std::clamp (r.start, 0, t.total);
        t.loopEnd = std::clamp (r.end, t.loopStart, t.total);
        if (t.loopEnd <= t.loopStart)
        {
            t.loopStart = 0;
            t.loopEnd = t.total;
        }
        return t;
    }

    //==========================================================================

    struct TimelineTrigger
    {
        enum Kind : uint8_t { Chord, PreviewEnd };
        int sample = 0;
        int16_t chord = -1;               // Chord: the timeline chord, or -1: stop
        uint8_t kind = Chord;
    };

    template <int Capacity>
    struct TimelineTriggers
    {
        static constexpr int capacity = Capacity;
        std::array<TimelineTrigger, Capacity> items {};
        int count = 0;

        void clear() noexcept { count = 0; }
        int size() const noexcept { return count; }
        const TimelineTrigger& operator[] (int i) const noexcept { return items[(size_t) i]; }
        bool push (const TimelineTrigger& t) noexcept
        {
            if (count >= Capacity)
                return false;
            items[(size_t) count++] = t;
            return true;
        }

        // Inserts `t` after every trigger at or before its sample.
        bool insertSorted (const TimelineTrigger& t) noexcept
        {
            if (count >= Capacity)
                return false;
            int i = count;
            while (i > 0 && items[(size_t) i - 1].sample > t.sample)
            {
                items[(size_t) i] = items[(size_t) i - 1];
                --i;
            }
            items[(size_t) i] = t;
            ++count;
            return true;
        }
    };

    struct PlayerSettings
    {
        bool on = false;                  // Play is on and the engine may play
        SyncMode sync = SyncMode::Sync;
        bool loop = true;
        int startBar = 1;                 // Sync: the bar the timeline starts on (1 …)
    };

    class ProgressionPlayer
    {
    public:
        static constexpr double eps = Timeline::eps;

        // This segment's triggers, in sample order. For a segment of a block (Clock.h): `seg` its
        // index, `numSamples` its length, `offset` its first sample.
        template <class Out>
        void generate (const PlayerSettings& s, const Timeline& tl, const Clock& clock, int seg, int numSamples, Out& out,
                       int offset = 0) noexcept
        {
            const ClockSegment& g = clock.segment (seg);
            spq = clock.samplesPerQuarter();
            const bool preview = s.sync == SyncMode::Preview;
            const bool active = s.on && ! tl.empty() && (preview || clock.isPlaying());

            if (preview != inPreview)
            {
                inPreview = preview;
                restart = true;           // a change of clock restrikes
                haveLastEnd = false;
            }

            if (! active)
            {
                stop (offset, out);
                haveLastEnd = false;
                if (! s.on)
                    playedOn = false;
                position = preview && ! finished && ! tl.empty() ? previewPos : -1.0;
                return;
            }

            if (preview)
                generatePreview (s, tl, numSamples, out, offset);
            else
                generateSync (s, tl, clock, g, numSamples, out, offset);
        }

        // Everything ended outside the player (all notes off, a reset): the chord under the
        // playhead plays again from the next segment.
        void reset() noexcept
        {
            current = -1;
            restart = true;
        }

        // Preview: back to the loop region's start (with Loop) or the top, from the next segment.
        void rewind() noexcept
        {
            rewindPending = true;
            restart = true;
        }

        // The chord the player sounds (-1: none), and the timeline position (beats) at the end of
        // the last segment (-1: not playing), for the editor.
        int currentChord() const noexcept { return current; }
        double playhead() const noexcept { return position; }

        // The chord under the playhead if `tl` replaced the timeline now (-1: none); when it is
        // the one sounding, an edit only retunes it.
        int expectedChord (const Timeline& tl) const noexcept { return position < 0.0 ? -1 : tl.chordAt (position); }

    private:
        static double mod (double x, double len) noexcept
        {
            double r = std::fmod (x, len);
            if (r < 0.0)
                r += len;
            if (r > len - eps)
                r = 0.0;                  // within eps of the end: the next time round
            return r;
        }

        int sampleOf (double beatsFromStart, int numSamples, int offset) const noexcept
        {
            int s = (int) std::llround (beatsFromStart * spq);
            return offset + (s < 0 ? 0 : (s >= numSamples ? (numSamples > 0 ? numSamples - 1 : 0) : s));
        }

        template <class Out>
        void emit (int sample, int chord, Out& out) noexcept
        {
            if (chord < 0)
            {
                stop (sample, out);
                return;
            }
            if (out.push ({ sample, (int16_t) chord, TimelineTrigger::Chord }))
                current = chord;
        }

        template <class Out>
        void stop (int sample, Out& out) noexcept
        {
            if (current >= 0 && out.push ({ sample, (int16_t) -1, TimelineTrigger::Chord }))
                current = -1;
        }

        //======================================================================
        // Sync: timeline position as a function of song position u (ppq).

        struct SyncMap
        {
            const Timeline& tl;
            bool loop;
            double start;                 // song position of the timeline's 0

            double rel (double u) const noexcept { return u - start; }

            // The timeline position at u, or a negative value where nothing plays.
            double at (double u) const noexcept
            {
                if (loop)
                    return tl.loopStartBeats() + mod (rel (u), tl.loopBeats());
                const double r = rel (u);
                return r < -eps || r >= tl.lengthBeats() - eps ? -1.0 : (r < 0.0 ? 0.0 : r);
            }

            int chordAt (double u) const noexcept
            {
                const double p = at (u);
                return p < 0.0 ? -1 : tl.chordAt (p);
            }

            // The first line (a chord start, a region wrap, the start or the end) strictly after u.
            double nextEvent (double u) const noexcept
            {
                if (loop)
                {
                    const double p = at (u);
                    const double line = std::min (tl.nextLine (p), tl.loopEndBeats());
                    return u + (line - p);
                }
                const double r = rel (u);
                if (r < -eps)
                    return start;
                if (r >= tl.lengthBeats() - eps)
                    return HUGE_VAL;
                return start + tl.nextLine (r < 0.0 ? 0.0 : r);
            }

            bool onLine (double u) const noexcept { return nextEvent (u - 2.0 * eps) <= u + eps; }
        };

        template <class Out>
        void generateSync (const PlayerSettings& s, const Timeline& tl, const Clock& clock, const ClockSegment& g, int numSamples,
                           Out& out, int offset) noexcept
        {
            const int bar = s.startBar < 1 ? 1 : (s.startBar > maxStartBar ? maxStartBar : s.startBar);
            const SyncMap map { tl, s.loop, (bar - 1) * clock.barLength() };
            const double u0 = g.ppq;
            double uEnd = u0 + numSamples / spq;
            if (g.endPpq < uEnd)
                uEnd = g.endPpq;

            const bool jumped = haveLastEnd && std::abs (u0 - lastEnd) * spq > 0.5;
            const int want = map.chordAt (u0);
            if (restart || jumped || map.onLine (u0) || want != current)
                emit (offset, want, out);
            restart = false;

            double u = u0;
            for (int guard = 0; guard < 4096; ++guard)
            {
                const double next = map.nextEvent (u);
                if (next == HUGE_VAL || next >= uEnd - eps)
                    break;
                emit (sampleOf (next - u0, numSamples, offset), map.chordAt (next), out);
                u = next;
            }

            lastEnd = uEnd;
            haveLastEnd = true;
            position = map.at (u0 + numSamples / spq);
        }

        //======================================================================
        // Preview: the player's own position, advanced at the host's tempo.

        template <class Out>
        void generatePreview (const PlayerSettings& s, const Timeline& tl, int numSamples, Out& out, int offset) noexcept
        {
            const double from = s.loop ? tl.loopStartBeats() : 0.0;
            // Playing on from the last segment to the very end (Loop off): finished, as if the
            // end had fallen inside it.
            if (playedOn && ! s.loop && ! finished && ! rewindPending && ! restart && previewPos >= tl.lengthBeats() - eps)
            {
                stop (offset, out);
                finished = true;
            }
            const bool startingOver = finished && (! playedOn || s.loop);   // Play again, or Loop turned on
            const bool outside = previewPos < -eps || previewPos >= tl.lengthBeats() - eps
                              || (s.loop && (previewPos < tl.loopStartBeats() - eps || previewPos >= tl.loopEndBeats() - eps));
            if (rewindPending || startingOver || (! finished && outside))
            {
                previewPos = from;
                finished = false;
                restart = true;
            }
            rewindPending = false;
            playedOn = true;
            haveLastEnd = false;

            if (finished)
            {
                stop (offset, out);
                position = -1.0;
                return;
            }

            const double span = numSamples / spq;
            const int want = tl.chordAt (previewPos);
            if (restart || tl.onLine (previewPos) || want != current)
                emit (offset, want, out);
            restart = false;

            double t = 0.0;               // beats into the segment
            for (int guard = 0; guard < 4096; ++guard)
            {
                const double line = s.loop ? std::min (tl.nextLine (previewPos), tl.loopEndBeats()) : tl.nextLine (previewPos);
                const double d = line - previewPos;
                if (line == HUGE_VAL || t + d >= span - eps)
                {
                    previewPos += span - t;
                    break;
                }
                t += d;
                previewPos = line;
                if (s.loop && previewPos >= tl.loopEndBeats() - eps)
                    previewPos = tl.loopStartBeats();
                if (! s.loop && previewPos >= tl.lengthBeats() - eps)
                {
                    stop (sampleOf (t, numSamples, offset), out);
                    finished = true;
                    break;
                }
                emit (sampleOf (t, numSamples, offset), tl.chordAt (previewPos), out);
            }
            position = finished ? -1.0 : previewPos;
        }

        double spq = 22050.0;
        int current = -1;                 // the chord sounding, or -1
        bool restart = true;
        double lastEnd = 0.0;             // Sync: song position at the end of the last segment
        bool haveLastEnd = false;
        bool inPreview = false;
        double previewPos = 0.0;          // Preview: the timeline position (beats)
        bool finished = false;            // Preview, Loop off: played to the end
        bool playedOn = false;            // Preview: Play stayed on since it last played
        bool rewindPending = false;
        double position = -1.0;
    };
}
