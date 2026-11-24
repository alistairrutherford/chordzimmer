// The effect chain and its scheduler (ChordChimp's PLAN.md §3.3; ChordMonkey's PLAN.md §3.4),
// cut down for ChordZimmer (PLAN.md §2, §3.6: block chords, no Style, Arp, Repeat or Step seq):
//
//   trigger → chord slot → Voicing → voice leading → Octave → Stack → key octave → Bass →
//           Strum → Velocity Algorithm → Hold → out
//
// Voicing and Bass (Voicings.h): the chord is voiced before voice leading; the bass (the chord's
// root, and its fifth, an octave below the lowest note) goes under the finished voicing.
//
// Key octave (ChordChimp Phase 8): with Follow, a performance's tones move by its key's octave
// from the home octave (C at MIDI 48), after voice leading and Stack, so Smooth leads from the
// last chord as written and the whole voicing moves; tones outside 0..127 are dropped.
//
// Voice leading (VoiceLeading.h): with Smooth, a chord takes the inversion and octave nearest
// the chord played last (pressed or retuned), before Stack. The last chord is forgotten when
// the transport starts and when everything ends, so a take from the top always voices the same.
//
// Extra chord slots (ExtraChords), whose notes the engine computes: 12-19 are the current
// workspace pattern's eight slots (the editor's slot preview, until the timeline UI of Phase 4),
// 20 the timeline chord playing and 21 the preview chord (PLAN.md §3.3, §3.1, Phase 3).
// Performance 128 (timelineKey) plays the timeline, pressed and ended by the ProgressionPlayer's
// triggers; performance 129 (previewKey) plays previews. The key octave never moves either.
//
// Each chord key that goes down starts a *performance* (one per key, so 128, and the
// pattern's). A performance plays its chord as one strike for as long as the chord is held: all
// the tones at once, or one after another with Strum, ended together. Velocity Algorithm sets
// each note-on's velocity from its place in the bar. Hold keeps a performance going after its
// key is released (Hold.h).
//
// Note bookkeeping: performance k owns two VoiceTracker sources, 128 + 2k and 129 + 2k, and
// uses them in turn, one per strike, so each note-off ends exactly the notes its strike
// started, whatever changed meanwhile. Ending a performance releases both.
//
// Scheduling: what happens later (the next strummed tone, a held chord's bar end) waits in a
// fixed-size EventQueue, in musical time (or in samples, for Strum while stopped). A tempo
// change needs nothing (musical events find their sample in the block they fall in); on a loop
// wrap or locate, `transportJump` moves the strum tones with the music.
//
// Plain C++, no JUCE, no allocation.
#pragma once

#include "../theory/ChordModel.h"
#include "../theory/ChordGroups.h"
#include "../theory/VoiceLeading.h"
#include "../theory/Voicings.h"
#include "../model/Workspace.h"
#include "Clock.h"
#include "EventQueue.h"
#include "Hold.h"
#include "KeyMap.h"
#include "MidiEvents.h"
#include "Stack.h"
#include "NoteValues.h"
#include "Strum.h"
#include "VelocityAlgorithm.h"
#include "VoiceTracker.h"
#include <array>
#include <cmath>
#include <limits>

namespace chordzimmer
{
    // The chord slots a performance can play: the palette's twelve, the workspace pattern's
    // eight, the timeline's and the preview's.
    constexpr int numExtraSlots = numPatternSlots + 2;
    constexpr int timelineSlot = numChordSlots + numPatternSlots;   // 20
    constexpr int previewSlot = timelineSlot + 1;                   // 21
    constexpr int numPerformSlots = numChordSlots + numExtraSlots;
    using SlotFlags = std::array<bool, numPerformSlots>;

    // The notes of the extra slots (inversion, octave and Chord Octave applied), and their roots
    // (pitch classes, for Bass).
    using ExtraChords = std::array<ChordNotes, numExtraSlots>;
    using ExtraRoots = std::array<int8_t, numExtraSlots>;

    struct ChordFxSettings
    {
        StackMode stack = StackMode::Off;
        StrumMode strum = StrumMode::Off;
        int strumRate = 2;                // index into the strum rates (1/64)
        VelocitySettings velocity;
        HoldMode hold = HoldMode::Off;
        VoiceLeadingMode voiceLeading = VoiceLeadingMode::Off;
        VoicingMode voicing = VoicingMode::Close;
        BassMode bass = BassMode::Off;
    };

    class ChordPerformer
    {
    public:
        static constexpr int numPerformances = 130;
        static constexpr int timelineKey = 128;   // the timeline's performance (Phase 3)
        static constexpr int previewKey = 129;    // previews (Phase 3)
        static constexpr int firstSource = 128;   // sources 0..127 are input keys

        static VoiceTracker::Source sourceOf (int key, int which) noexcept
        {
            return (VoiceTracker::Source) (firstSource + 2 * key + (which & 1));
        }

        enum EventType : uint8_t { StrumNote, HoldEnd };

        // Ties at one sample: note-offs, then input (key presses and releases), then note-ons.
        static constexpr uint8_t endOrder = 0, inputOrder = 1, startOrder = 2;

        ChordPerformer (VoiceTracker& v, Clock& c, const ChordSet& chordSet, const ExtraChords& extraChordSet,
                        const ExtraRoots& extraRootSet, const ChordContext& ctx) noexcept
            : voices (v), clock (c), chords (chordSet), extraChords (extraChordSet), extraRoots (extraRootSet), context (ctx) {}

        VoiceTracker::Source source (int key, int which) const noexcept { return sourceOf (key, which); }

        // Forgets every performance without sending anything (the voices were released or
        // cleared already), and reseeds Random strum.
        void reset() noexcept
        {
            for (auto& p : perfs)
                if (p.alive)
                    forget (p);
            queue.clear();
            alive = 0;
            strummer.reset();
            haveLed = false;
        }

        // The block segment (Clock) that the following calls' samples are in.
        void setSegment (int seg) noexcept { segment = seg; }

        // Key octave (Phase 8): Follow moves each performance by its key's octave. A change takes
        // effect when the chords are next retuned (ChordEngine marks every slot changed).
        void setFollowKeyOctave (bool follow) noexcept { followKeyOctave = follow; }

        // Settings for this block, at `sample` (the block start); `changed` marks the chord
        // slots whose chord changed with them. Changed chords (or a voicing change) restrike.
        void applySettings (const ChordFxSettings& s, int channel, const SlotFlags& changed,
                            int sample, BlockEvents& o) noexcept
        {
            out = &o;
            const ChordFxSettings old = fx;
            fx = s;
            outChannel = channel;
            if (alive == 0)
                return;

            if (old.hold != HoldMode::Off && s.hold == HoldMode::Off)
                endLatched (sample, -1);

            SlotFlags slots = changed;
            if (old.stack != s.stack || old.voiceLeading != s.voiceLeading || old.voicing != s.voicing || old.bass != s.bass)
                slots.fill (true);
            retuneWhere ([&] (int, const Perf& p) noexcept
            {
                return p.slot >= 0 && p.slot < numPerformSlots && slots[(size_t) p.slot];
            }, sample);
        }

        // The transport started (stopped → playing): Random strum starts from its seed again, and
        // voice leading from the chord as written.
        void transportStarted() noexcept
        {
            strummer.reset();
            haveLed = false;
        }

        // Musical time moved by `delta` at `sample` (the start of the current segment): a loop
        // wrap, a locate, or the start of playback. Queued strum tones move with the music; a
        // held chord's bar end goes to the next bar line.
        void transportJump (double delta, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            if (alive == 0)
                return;
            const double now = ppqAt (sample);
            queue.forEach ([&] (TimedEvent& e) noexcept
            {
                if (e.base != TimedEvent::Base::Musical)
                    return;
                if (e.type == HoldEnd)
                    e.ppq = clock.isPlaying() ? clock.nextBarLine (now) : e.ppq + delta;
                else
                    e.ppq += delta;
            });
            // A strum's later tones are placed from its start: that moves too. (ChordMonkey kept
            // the old start, so a strum across a wrap or locate lost or bunched its last tones.)
            for (auto& p : perfs)
                if (p.alive && p.strikeOpen && p.strumMusical)
                    p.strumStartPpq += delta;
        }

        // Chord key `key` went down: plays chord slot `slot`, `octave` octaves up.
        void press (int key, int slot, int velocity, int sample, BlockEvents& o, int octave = 0) noexcept
        {
            out = &o;
            if (! validKey (key))
                return;
            const bool stopped = ! clock.isPlaying();
            const bool wasLatched = perfs[(size_t) key].alive && perfs[(size_t) key].latched;

            endLatched (sample, -1);                // a new selection ends the held chords
            if (perfs[(size_t) key].alive)
                endPerformance (key, sample);
            if (stopped && wasLatched)
                return;                             // stopped: pressing a held chord's key ends it

            if (stopped && alive == 0)
                clock.anchor (sample);              // the bar (Velocity's grid) starts here while stopped

            Perf& p = perfs[(size_t) key];
            p.alive = true;
            p.keyDown = true;
            p.latched = false;
            p.slot = slot;
            p.octave = (int8_t) octave;
            p.velocity = (uint8_t) (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
            p.notes = take (key, slot);
            ++alive;
            beginStrike (key, sample);
        }

        // Chord key `key` went up: the performance ends, or is held (Hold).
        void release (int key, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            if (! validKey (key))
                return;
            Perf& p = perfs[(size_t) key];
            if (! p.alive || ! p.keyDown)
                return;
            p.keyDown = false;
            if (fx.hold == HoldMode::Off)
            {
                endPerformance (key, sample);
                return;
            }
            p.latched = true;
            if (fx.hold == HoldMode::Bar && clock.isPlaying())
                pushAt (clock.nextBarLine (ppqAt (sample)), HoldEnd, endOrder, key, p.life, 0);
        }

        // Ends key `key`'s performance now, held or not.
        void end (int key, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            if (validKey (key) && perfs[(size_t) key].alive)
                endPerformance (key, sample);
        }

        void killAll (int sample, BlockEvents& o) noexcept
        {
            out = &o;
            for (int k = 0; k < numPerformances; ++k)
                if (perfs[(size_t) k].alive)
                    endPerformance (k, sample);
            haveLed = false;
        }

        // The chords changed (parameters): every performance whose slot is marked and whose
        // tones changed plays the new tones. A sounding strike is restruck at `sample` (all
        // releases first, so a shared pitch restrikes).
        void retune (const SlotFlags& changed, int sample, BlockEvents& o) noexcept
        {
            out = &o;
            retuneWhere ([&] (int, const Perf& p) noexcept
            {
                return p.slot >= 0 && p.slot < numPerformSlots && changed[(size_t) p.slot];
            }, sample);
        }

        // The next queued event due in the current segment before block sample `limit` (and at
        // least `floor`): true with its sample and tie order. Run it with runNext().
        bool peek (int floor, int limit, int& at, uint8_t& order) noexcept
        {
            pending = queue.earliestDue (clock, segment, floor, limit, pendingAt);
            if (pending < 0)
                return false;
            at = pendingAt;
            order = queue[pending].order;
            return true;
        }

        void runNext (BlockEvents& o) noexcept
        {
            out = &o;
            if (pending < 0 || pending >= queue.size())
                return;
            const TimedEvent e = queue.take (pending);
            pending = -1;
            const int s = pendingAt;
            Perf& p = perfs[(size_t) e.owner];
            if (! p.alive)
                return;

            switch (e.type)
            {
                case StrumNote:
                    if (e.tag == p.gen && p.strikeOpen && e.arg == p.strumGen)
                        playStrumNote (e.owner, s);
                    break;
                case HoldEnd:
                    if (e.tag == p.life && p.latched && fx.hold == HoldMode::Bar)
                        endPerformance (e.owner, s);
                    break;
                default:
                    break;
            }
        }

        bool isAlive (int key) const noexcept { return validKey (key) && perfs[(size_t) key].alive; }
        bool isLatched (int key) const noexcept { return isAlive (key) && perfs[(size_t) key].latched; }
        int aliveCount() const noexcept { return alive; }
        int queued() const noexcept { return queue.size(); }
        const ChordFxSettings& settings() const noexcept { return fx; }

        // The tones key `key`'s performance plays now (ascending).
        VoicedNotes notesOf (int key) const noexcept { return isAlive (key) ? perfs[(size_t) key].notes : VoicedNotes {}; }

    private:
        struct Perf
        {
            bool alive = false, keyDown = false, latched = false;
            uint32_t gen = 0;             // the strike's events: bumped when it restarts or ends
            uint32_t life = 0;            // the performance: bumped when it ends
            int slot = 0;
            int8_t octave = 0;            // octaves up
            uint8_t velocity = 100;
            VoicedNotes notes;

            uint8_t src = 0;              // which of the two sources the current strike uses
            bool strikeOpen = false;
            uint32_t strumGen = 0;
            bool strumMusical = true;
            double strumStartPpq = 0.0, strumStepPpq = 0.0, strumStepSamples = 0.0;
            int64_t strumStartAbs = 0;
            int strumCount = 0, strumNext = 0;
            std::array<uint8_t, VoicedNotes::capacity> order {};
        };

        static bool validKey (int key) noexcept { return key >= 0 && key < numPerformances; }

        double ppqAt (double sample) const noexcept { return clock.ppqAt (segment, sample); }

        // The tones key `key`'s performance of `slot` plays; `base` gets the chord's voicing
        // before Stack and the key octave (led from the last chord with Smooth voice leading).
        VoicedNotes voiced (int key, int slot, ChordNotes* base = nullptr) const noexcept
        {
            if (slot < 0 || slot >= numPerformSlots)
                return {};
            ChordNotes notes = slot < numChordSlots ? chordNotes (chords[(size_t) slot], context)
                                                    : extraChords[(size_t) (slot - numChordSlots)];
            int lowestWritten = 128;      // the chord's bass as written (its notes aren't in pitch order)
            for (const uint8_t n : notes)
                lowestWritten = n < lowestWritten ? n : lowestWritten;
            const int bassPc = lowestWritten < 128 ? lowestWritten % 12 : -1;
            notes = applyVoicing (notes, fx.voicing);
            if (fx.voiceLeading == VoiceLeadingMode::Smooth)
                notes = leadVoicing (notes, haveLed ? &led : nullptr, context);
            if (base != nullptr)
                *base = notes;
            const VoicedNotes stacked = stackNotes (notes, fx.stack);
            const int shift = (followKeyOctave && key < firstSource ? 12 * (key / 12 - KeyMap::homeC / 12) : 0)
                            + (validKey (key) ? 12 * perfs[(size_t) key].octave : 0);
            VoicedNotes moved;
            for (const uint8_t n : stacked)
                if (n + shift >= 0 && n + shift <= 127)
                    moved.notes[(size_t) moved.size++] = (uint8_t) (n + shift);
            return withBass (moved, rootOf (slot), bassPc);
        }

        // The root pitch class of chord slot `slot`.
        int rootOf (int slot) const noexcept
        {
            if (slot >= numChordSlots)
                return extraRoots[(size_t) (slot - numChordSlots)];
            const auto& c = chords[(size_t) slot];
            return (((context.key + scaleOffset (context.scale, c.degree) + c.steps[0].accidental) % 12) + 12) % 12;
        }

        // `v` (ascending) with Bass's notes under it: the written bass `bassPc` (-1: the root).
        VoicedNotes withBass (const VoicedNotes& v, int rootPc, int bassPc = -1) const noexcept
        {
            int bass[2] {};
            const int nb = v.size > 0 ? bassNotes (fx.bass, v[0], rootPc, bass, bassPc) : 0;
            if (nb == 0)
                return v;
            VoicedNotes withIt;
            for (int i = 0; i < nb; ++i)
                withIt.notes[(size_t) withIt.size++] = (uint8_t) bass[i];
            for (const uint8_t n : v)
                if (withIt.size < VoicedNotes::capacity)
                    withIt.notes[(size_t) withIt.size++] = n;
            return withIt;
        }

        // voiced(), for a performance that now plays it: its chord is the one the next is led from.
        VoicedNotes take (int key, int slot) noexcept
        {
            ChordNotes base;
            const VoicedNotes v = voiced (key, slot, &base);
            if (slot >= 0 && slot < numPerformSlots)
            {
                led = base;
                haveLed = true;
            }
            return v;
        }

        template <class Pred>
        void retuneWhere (Pred marked, int sample) noexcept
        {
            if (alive == 0)
                return;
            std::array<bool, numPerformances> restrike {};
            for (int k = 0; k < numPerformances; ++k)
            {
                Perf& p = perfs[(size_t) k];
                if (! p.alive || ! marked (k, (const Perf&) p))
                    continue;
                ChordNotes base;
                const VoicedNotes next = voiced (k, p.slot, &base);
                if (next == p.notes)
                    continue;
                p.notes = next;
                led = base;
                haveLed = true;
                if (p.strikeOpen)
                {
                    voices.release (source (k, p.src), sample, *out);
                    restrike[(size_t) k] = true;
                }
            }
            for (int k = 0; k < numPerformances; ++k)
                if (restrike[(size_t) k])
                    strum (k, sample);
        }

        // The velocity for a note-on at `sample`. The bar position is read a sample on, so a note
        // on a grid line counts as on it even when rounding put it early: by up to half a sample
        // for the event, and for a strum tone up to half a sample more for its strike's start.
        int velocityAt (const Perf& p, int sample) const noexcept
        {
            return fx.velocity.apply (p.velocity, clock.barFraction (ppqAt (sample + 1.0)));
        }

        void pushAt (double ppq, uint8_t type, uint8_t order, int key, uint32_t tag, uint32_t arg) noexcept
        {
            if (std::isfinite (ppq))
                queue.push (TimedEvent::musical (ppq, type, order, (uint16_t) key, tag, arg));
        }

        void endPerformance (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            voices.release (source (key, 0), sample, *out);
            voices.release (source (key, 1), sample, *out);
            forget (p);
            queue.removeIf ([key] (const TimedEvent& e) noexcept { return e.owner == key; });
            --alive;
        }

        void forget (Perf& p) noexcept
        {
            p.alive = p.keyDown = p.latched = false;
            p.strikeOpen = false;
            ++p.gen;
            ++p.life;
        }

        // Ends every held (released) performance, except key `keep`.
        void endLatched (int sample, int keep) noexcept
        {
            for (int k = 0; k < numPerformances; ++k)
                if (k != keep && perfs[(size_t) k].alive && perfs[(size_t) k].latched)
                    endPerformance (k, sample);
        }

        //======================================================================
        // Strikes

        // A strike from `sample` until the performance ends.
        void beginStrike (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            closeStrike (key, sample);
            p.src ^= 1;
            p.strikeOpen = true;
            strum (key, sample);
        }

        // Starts the strike's tones: all now, or the first now and the rest queued (Strum).
        void strum (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            ++p.strumGen;
            drop (key, StrumNote);

            const int n = p.notes.size;
            if (fx.strum == StrumMode::Off || n < 2)
            {
                const int vel = velocityAt (p, sample);
                for (const auto note : p.notes)
                    voices.start (source (key, p.src), outChannel, note, vel, sample, *out);
                return;
            }

            strummer.order (fx.strum, n, p.order.data());
            p.strumCount = n;
            p.strumNext = 0;
            p.strumMusical = clock.isPlaying();
            p.strumStartPpq = ppqAt (sample);
            p.strumStartAbs = clock.blockStart() + sample;
            p.strumStepPpq = strumRatePpq (fx.strumRate);
            p.strumStepSamples = p.strumStepPpq * clock.samplesPerQuarter();   // while stopped: the clock's tempo
            playStrumNote (key, sample);
        }

        void playStrumNote (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            if (p.strumNext >= p.strumCount)
                return;
            const int i = p.order[(size_t) p.strumNext++];
            if (i < p.notes.size)
                voices.start (source (key, p.src), outChannel, p.notes[i], velocityAt (p, sample), sample, *out);

            if (p.strumNext >= p.strumCount)
                return;
            const double k = p.strumNext;
            if (p.strumMusical)
                pushAt (p.strumStartPpq + k * p.strumStepPpq, StrumNote, startOrder, key, p.gen, p.strumGen);
            else
                queue.push (TimedEvent::atSample (p.strumStartAbs + (int64_t) std::llround (k * p.strumStepSamples), StrumNote,
                                                  startOrder, (uint16_t) key, p.gen, p.strumGen));
        }

        void closeStrike (int key, int sample) noexcept
        {
            Perf& p = perfs[(size_t) key];
            if (! p.strikeOpen)
                return;
            voices.release (source (key, p.src), sample, *out);
            p.strikeOpen = false;
            ++p.strumGen;
            drop (key, StrumNote);
        }

        // Drops `key`'s queued events of one type (stale ones, so the queue stays short).
        void drop (int key, uint8_t type) noexcept
        {
            queue.removeIf ([key, type] (const TimedEvent& e) noexcept { return e.owner == key && e.type == type; });
        }

        VoiceTracker& voices;
        Clock& clock;
        const ChordSet& chords;
        const ExtraChords& extraChords;
        const ExtraRoots& extraRoots;
        const ChordContext& context;

        ChordFxSettings fx;
        int outChannel = 0;
        int segment = 0;
        BlockEvents* out = nullptr;
        std::array<Perf, numPerformances> perfs {};
        int alive = 0;
        Strummer strummer;
        EventQueue<1024> queue;           // ≤ 2 events per performance (strum tone, hold)
        int pending = -1, pendingAt = 0;
        ChordNotes led {};                // voice leading: the chord played last, before Stack
        bool haveLed = false;
        bool followKeyOctave = false;     // Key octave: Follow
    };
}
