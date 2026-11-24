// Listen (PLAN.md §3.4, Phase 5; ChordMonkey's ChordListener reworked): the notes played into
// ChordZimmer, with their lengths, for Match and Chord-from-notes. Plain C++, no JUCE, no
// allocation: fixed arrays, called on the audio thread.
//
// Time is in beats of listening (the engine's listenBeats: the host's tempo, counted while the
// engine runs, whether or not the host plays), so a looping clip or a stopped transport counts
// on. A note is kept from its note-on to its note-off; the last maxCollectedNotes are kept.
//
// The message thread gets a CollectedNotes snapshot (trivially copyable) and turns it into a
// NoteSet with noteSetFromCollected: the last N beats *of playing* (ending at the end of the
// last note, or now while one is held), so stopping to look doesn't let the phrase slide away.
#pragma once

#include "../theory/Matcher.h"
#include <array>
#include <cstdint>

namespace chordzimmer
{
    struct CollectedNote
    {
        double start = 0.0, end = 0.0;    // beats of listening; a held note ends at `now`
        uint8_t pitch = 0;
        bool held = false;
    };

    inline constexpr int maxCollectedNotes = 256;

    struct CollectedNotes
    {
        std::array<CollectedNote, maxCollectedNotes> notes {};   // oldest first
        int count = 0;
        double now = 0.0;
    };

    class NoteCollector
    {
    public:
        void reset() noexcept
        {
            numDone = 0;
            next = 0;
            heldSince.fill (-1.0);
            changed = true;
        }

        void noteOn (int pitch, double beat) noexcept
        {
            if (pitch < 0 || pitch > 127)
                return;
            if (heldSince[(size_t) pitch] >= 0.0)
                noteOff (pitch, beat);    // retriggered
            heldSince[(size_t) pitch] = beat;
            now = beat > now ? beat : now;
            changed = true;
        }

        void noteOff (int pitch, double beat) noexcept
        {
            if (pitch < 0 || pitch > 127 || heldSince[(size_t) pitch] < 0.0)
                return;
            done[(size_t) next] = { heldSince[(size_t) pitch], beat, (uint8_t) pitch, false };
            next = (next + 1) % maxCollectedNotes;
            numDone = numDone < maxCollectedNotes ? numDone + 1 : maxCollectedNotes;
            heldSince[(size_t) pitch] = -1.0;
            now = beat > now ? beat : now;
            changed = true;
        }

        void allUp (double beat) noexcept
        {
            for (int p = 0; p < 128; ++p)
                noteOff (p, beat);
        }

        // The clock moved on: held notes grow.
        void advanceTo (double beat) noexcept
        {
            now = beat;
            for (double s : heldSince)
                if (s >= 0.0)
                {
                    changed = true;
                    break;
                }
        }

        // Whether anything changed since the last call (the processor publishes then).
        bool takeChanged() noexcept
        {
            const bool c = changed;
            changed = false;
            return c;
        }

        // Oldest first: the completed notes, then the held ones (ending now). If there are more
        // than maxCollectedNotes in all, the oldest completed ones are left out.
        void snapshot (CollectedNotes& out) const noexcept
        {
            int numHeld = 0;
            for (double s : heldSince)
                numHeld += s >= 0.0 ? 1 : 0;
            const int keep = numDone < maxCollectedNotes - numHeld ? numDone : maxCollectedNotes - numHeld;
            out.count = 0;
            out.now = now;
            for (int i = numDone - keep; i < numDone; ++i)
                out.notes[(size_t) out.count++] = done[(size_t) ((next - numDone + i + 2 * maxCollectedNotes) % maxCollectedNotes)];
            for (int p = 0; p < 128 && out.count < maxCollectedNotes; ++p)
                if (heldSince[(size_t) p] >= 0.0)
                    out.notes[(size_t) out.count++] = { heldSince[(size_t) p], now, (uint8_t) p, true };
        }

    private:
        std::array<CollectedNote, maxCollectedNotes> done {};
        int numDone = 0, next = 0;
        std::array<double, 128> heldSince = [] { std::array<double, 128> a {}; a.fill (-1.0); return a; }();
        double now = 0.0;
        bool changed = false;
    };

    // The notes as a NoteSet: each pitch class's beats within the last `windowBeats` of playing
    // (ending at the last note's end, or now while one is held); the bass is the lowest note
    // sounding in that window.
    inline NoteSet noteSetFromCollected (const CollectedNotes& c, double windowBeats) noexcept
    {
        NoteSet s;
        double end = -1.0;
        for (int i = 0; i < c.count; ++i)
            end = c.notes[(size_t) i].end > end ? c.notes[(size_t) i].end : end;
        if (end < 0.0)
            return s;
        const double start = end - (windowBeats > 0.0 ? windowBeats : 0.0);
        int lowest = 128;
        for (int i = 0; i < c.count; ++i)
        {
            const auto& n = c.notes[(size_t) i];
            const double a = n.start > start ? n.start : start, b = n.end;
            if (b <= a && ! (n.start >= start && n.end == n.start))
                continue;
            s.weight[(size_t) (n.pitch % 12)] += b > a ? b - a : 1.0e-3;   // a zero-length note still counts a little
            lowest = n.pitch < lowest ? n.pitch : lowest;
        }
        s.bass = lowest < 128 ? lowest % 12 : -1;
        return s;
    }
}
