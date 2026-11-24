// The MIDI engine: the key map (KeyMap.h), the chords (Chord Group, Progression, Key, Scale,
// Chord Octave), the effects (ChordFx.h: Stack, Strum, Velocity Algorithm, Hold, voicing and
// voice leading) on the Clock, the test pattern, and note bookkeeping through VoiceTracker.
// Plain C++, no JUCE, no allocation; the plug-in is a thin adapter around it, so every
// behaviour here is unit-testable. Forked from ChordMonkey (ChordMonkey's PLAN.md §3); its Bind
// zones and Keys-Lock are gone (PLAN.md §2).
//
//   white key     → the slot's chord: degree from the Progression, shape from the Chord Group,
//                   Key, Scale, Chord Octave, then the effect chain (one performance per key)
//   black key     → its borrowed chord (ChordGroups.h), the same way
//   Key octave    → Ignore: any octave plays the same; Follow: the key's octave from C at 48
//                   moves the chord (ChordFx.h)
//   Enable off    → input passes through unchanged
//   Analyse       → input passes through unchanged and the KeyListener guesses its key
//                   (ChordChimp's docs/KEY_DETECTION_PLAN.md); no chords, no test pattern
//   Detect        → Capture: as Analyse, and the ChordListener groups the input into chords on
//                   the beat grid (ChordListener.h; ChordMonkey's PLAN.md §3.6) for the plug-in
//                   to write into a pattern (core/model/Detect.h)
//   Play          → the progression's timeline plays (ProgressionPlayer.h: Sync with the host
//                   from the Start bar, or Preview on its own clock; Loop repeats the loop
//                   region), through the same effects, as performance 128 (PLAN.md §3.3)
//   Previews      → a chord auditioned from the editor (a palette cell or a timeline chord,
//                   PLAN.md §3.1) plays through the same effects as performance 129, until it is
//                   released or for a number of beats
//
// The timeline reaches the engine by setTimeline (a copy, on the audio thread). The sounding
// chord follows it and Chord Octave: if an edit leaves the chord under the playhead in place but
// changes its notes it is retuned, like a held key's chord; if it moves the chord lines, the
// player restrikes. The workspace (setWorkspace) is only used for the editor's slot preview
// until the timeline UI (Phase 4).
//
// The timeline and the preview play at Velocity peak (the fixed velocity, PLAN.md §3.6) unless a
// Velocity curve is chosen.
//
// Whenever the chords change while chord keys are held (a parameter), each held key whose chord
// changed is released and replayed.
//
// A block is processed in the Clock's segments (split at loop wraps). Within a segment, input
// and queued events run in sample order; at one sample, queued note-offs come first, then
// input (the host's, then the test pattern's, then the timeline's triggers), then queued
// note-ons. Previews start at the first sample of the block they arrive in.
#pragma once

#include "../theory/Progressions.h"
#include "ChordFx.h"
#include "ChordListener.h"
#include "Clock.h"
#include "KeyListener.h"
#include "KeyMap.h"
#include "MidiEvents.h"
#include "ProgressionPlayer.h"
#include "TestPattern.h"
#include "Transport.h"
#include "VoiceTracker.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace chordzimmer
{
    struct EngineSettings
    {
        bool enabled = true;
        int key = 0;
        Scale scale = Scale::Major;
        ChordGroup group = ChordGroup::Triads;
        Progression progression = Progression::Diatonic;
        int chordOctave = 0;
        bool followKeyOctave = false;     // Key octave: Follow (Phase 8)
        bool testPattern = false;
        bool analysing = false;           // Analyse is Listen or Use guess (key detection)
        bool capturing = false;           // Detect is Capture or Use result (Phase 8)
        bool play = false;                // Play: the timeline plays (Phase 3)
        SyncMode sync = SyncMode::Sync;   // Sync mode (Phase 3)
        bool loop = true;                 // Loop: the loop region repeats (Phase 3)
        int startBar = 1;                 // Start bar: Sync's bar 1 of the timeline (Phase 3)
        int pattern = 0;                  // Pattern: A-D (the editor's slot preview)
        int outputChannel = 0;            // 0..15
        ChordFxSettings fx;

        // Analyse or Detect: the input passes through and is listened to; no chords play.
        bool listening() const noexcept { return analysing || capturing; }
    };

    // A chord auditioned from the editor (PLAN.md §3.1): its notes at Chord Octave 0
    // (progressionChordNotes), held until a request with `on` false, or for `beats` if > 0.
    struct PreviewRequest
    {
        ChordNotes notes;
        int8_t root = 0;
        bool on = false;
        float beats = 0.0f;
    };

    class ChordEngine
    {
    public:
        ChordEngine() { refreshChords (0, nullptr); }

        // The timeline that Play plays (audio thread, before process()).
        void setTimeline (const Timeline& t) noexcept
        {
            timeline = t;
            timelineChanged = true;
        }

        // A preview to start or end at the next block's first sample (audio thread, before
        // process()). False if too many are waiting.
        bool queuePreview (const PreviewRequest& r) noexcept
        {
            if (numPreviews >= (int) previews.size())
                return false;
            previews[(size_t) numPreviews++] = r;
            return true;
        }

        // Preview's ⏮: the timeline's own clock goes back to the loop region's start (or the
        // top). Any thread.
        void requestRewind() noexcept { rewindRequested.store (true); }

        // The workspace the patterns come from (audio thread, before process()).
        void setWorkspace (const Workspace& w) noexcept
        {
            workspace = w;
            workspaceChanged = true;
        }

        // Processes one block. `in` must be sorted by sample. Appends to `out` in sample order.
        void process (const EngineSettings& s, const Transport& t, const MidiEvent* in, int numIn, int numSamples,
                      BlockEvents& out) noexcept
        {
            numSamples = numSamples > 0 ? numSamples : 0;
            secondsPerSample = t.sampleRate > 0.0 && std::isfinite (t.sampleRate) ? 1.0 / t.sampleRate : 0.0;
            clock.beginBlock (t, numSamples);
            performer.setSegment (0);

            if (resetRequested.exchange (false))
                releaseAll (0, out);
            if (heardResetRequested.exchange (false))
            {
                chordListener.reset();
                if (! s.analysing)
                    listener.reset();
            }

            if (wasPlaying && ! t.playing)
                releaseAll (0, out);   // transport stop: all notes off (Chordism's PLAN.md §5)
            if (! wasPlaying && t.playing)
                performer.transportStarted();
            wasPlaying = t.playing;

            if (clock.jumpedAtStart())
                performer.transportJump (clock.jumpDelta(), 0, out);

            applySettings (s, out);
            if (workspaceChanged)
            {
                workspaceChanged = false;
                SlotFlags changed {};
                refreshPatternChords (chordsPattern, &changed);
                performer.retune (changed, 0, out);
            }
            if (timelineChanged)
            {
                timelineChanged = false;
                const int sounding = player.currentChord();
                if (sounding >= 0 && player.expectedChord (timeline) == sounding)
                {
                    SlotFlags changed {};
                    setTimelineSlot (sounding, &changed);
                    performer.retune (changed, 0, out);
                }
            }
            if (rewindRequested.exchange (false))
                player.rewind();
            startPreviews (s, out);

            int i = 0;
            for (int seg = 0; seg < clock.numSegments(); ++seg)
            {
                const ClockSegment g = clock.segment (seg);
                const bool last = seg == clock.numSegments() - 1;
                performer.setSegment (seg);
                if (seg > 0)
                    performer.transportJump (clock.wrapDelta (seg), g.start, out);   // a loop wrap inside the block

                Transport segmentTransport = t;
                segmentTransport.ppq = g.ppq;
                injected.clear();
                pattern.generate (s.enabled && s.testPattern && ! s.listening(), segmentTransport, g.end - g.start, injected, g.start);
                triggers.clear();
                playOn = s.enabled && s.play && ! s.listening();
                player.generate (PlayerSettings { playOn, s.sync, s.loop, s.startBar }, timeline, clock, seg, g.end - g.start, triggers, g.start);
                if (previewEnd >= 0 && previewEnd < clock.blockStart() + g.end)
                {
                    const auto at = previewEnd - clock.blockStart();
                    triggers.insertSorted ({ (int) (at < g.start ? g.start : at), (int16_t) -1, TimelineTrigger::PreviewEnd });
                    previewEnd = -1;
                }

                int j = 0, k = 0, cursor = g.start, runs = 0;
                while (true)
                {
                    // The next key event of this segment: at one sample, input, then the test
                    // pattern's, then the pattern's triggers.
                    const bool haveIn = i < numIn && (last || clampSample (in[i].sample, numSamples) < g.end);
                    const bool haveInjected = j < injected.size();
                    const bool haveTrigger = k < triggers.size();
                    const int inSample = haveIn ? clampSample (in[i].sample, numSamples) : INT32_MAX;
                    const int injectedSample = haveInjected ? injected[j].sample : INT32_MAX;
                    const int triggerSample = haveTrigger ? triggers[k].sample : INT32_MAX;
                    const bool fromInput = haveIn && inSample <= injectedSample && inSample <= triggerSample;
                    const bool fromInjected = ! fromInput && haveInjected && injectedSample <= triggerSample;
                    const bool fromTrigger = ! fromInput && ! fromInjected && haveTrigger;
                    const MidiEvent* next = fromInput ? &in[i] : (fromInjected ? &injected[j] : nullptr);
                    const int keySample = fromInput ? inSample : (fromInjected ? injectedSample : (fromTrigger ? triggerSample : g.end));
                    const bool haveNext = next != nullptr || fromTrigger;

                    int at = 0;
                    uint8_t order = 0;
                    const bool due = runs < maxQueueRunsPerSegment
                                  && performer.peek (cursor, haveNext ? keySample + 1 : g.end, at, order);
                    if (due && (! haveNext || at < keySample || order < ChordPerformer::inputOrder))
                    {
                        ++runs;
                        cursor = at;
                        performer.runNext (out);
                        continue;
                    }
                    if (! haveNext)
                        break;
                    cursor = keySample > cursor ? keySample : cursor;
                    if (fromTrigger)
                        handleTrigger (triggers[k++], out);
                    else
                    {
                        (fromInput ? i : j)++;
                        handle (*next, numSamples, out);
                    }
                }
            }

            seconds += numSamples * secondsPerSample;
            listenBeats += numSamples / beatSamples();
            if (current.listening())
                listener.advance (seconds);
            if (current.capturing)
                chordListener.advanceTo (listenBeats);
        }

        // All notes off now (bypass, transport stop, all-notes-off, destruction).
        void releaseAll (int sample, BlockEvents& out) noexcept
        {
            performer.killAll (sample, out);
            voices.releaseAll (sample, out);
            held = {};
            player.reset();
            previewEnd = -1;
            listener.allUp (timeAt (sample));
            chordListener.allUp (beatAt (sample));
            refreshChords (sample, nullptr);
        }

        // The next process() starts by releasing everything. For places where no MIDI can be
        // sent (releaseResources, prepareToPlay); callable from any thread.
        void requestReset() noexcept { resetRequested.store (true); }

        const VoiceTracker& getVoices() const noexcept { return voices; }
        const ChordSet& getChords() const noexcept { return chords; }
        const ChordContext& getChordContext() const noexcept { return context; }
        int getLastChordSlot() const noexcept { return lastChordSlot; }
        int getPatternKey() const noexcept { return pattern.getHeldKey(); }
        const ChordPerformer& getPerformer() const noexcept { return performer; }
        const ProgressionPlayer& getPlayer() const noexcept { return player; }
        const Workspace& getWorkspace() const noexcept { return workspace; }
        const Timeline& getTimeline() const noexcept { return timeline; }

        // The timeline chord sounding (an index into the timeline), or -1, and the playhead
        // (timeline beats, -1 when not playing), for the editor.
        int getPlayingChord() const noexcept { return performer.isAlive (ChordPerformer::timelineKey) ? player.currentChord() : -1; }
        double getPlayhead() const noexcept { return player.playhead(); }
        bool isPreviewing() const noexcept { return performer.isAlive (ChordPerformer::previewKey); }

        const Clock& getClock() const noexcept { return clock; }

        // The key heard in Analyse mode (the last analysis's, until the next one starts).
        KeyGuess getKeyGuess() const noexcept { return listener.guess(); }

        // The chords heard in Detect's Capture (the last capture's, until the next one starts),
        // and whether they changed since the last call (audio thread).
        void getHeard (HeardChords& out) const noexcept { chordListener.heard (out); }
        bool takeHeardChanged() noexcept { return chordListener.takeChanged(); }

        // The next process() starts a new capture (and key guess, unless Analyse is on): for
        // the plug-in, once a result is used or a capture cancelled. Any thread.
        void requestHeardReset() noexcept { heardResetRequested.store (true); }

        // For the editor: input key `note` is down; a performance of key `note` is playing, held
        // or not.
        bool isKeyDown (int note) const noexcept { return note >= 0 && note < 128 && held[(size_t) note].down; }
        bool isPerforming (int note) const noexcept { return performer.isAlive (note); }

    private:
        struct HeldKey
        {
            bool down = false;
            uint8_t velocity = 0;
            uint8_t channel = 0;
            int8_t slot = -1;             // the chord slot it plays (0..19), or -1
            bool passThrough = false;     // owns a plain note: Enable off, Analyse, Detect
        };

        // Recomputes the chords. With `out`, every performance whose chord changed is replayed at
        // `sample` (ChordPerformer::retune); `changedOut` gets the slots whose chord changed.
        void refreshChords (int sample, BlockEvents* out, SlotFlags* changedOut = nullptr) noexcept
        {
            const ChordContext ctx { current.key, current.scale, current.chordOctave };
            const ChordSet next = makeChords (current.group, current.scale, progressionDegrees (current.progression));

            const bool ctxChanged = ctx.key != context.key || ctx.scale != context.scale || ctx.outputOctave != context.outputOctave;
            SlotFlags changed {};
            bool any = false;
            for (size_t n = 0; n < (size_t) numChordSlots; ++n)
            {
                changed[n] = ctxChanged || ! (next[n] == chords[n]);
                any |= changed[n];
            }
            chords = next;
            context = ctx;
            if (changedOut != nullptr)
                *changedOut = changed;

            if (out != nullptr && any)
                performer.retune (changed, sample, *out);
        }

        static bool sameNotes (const ChordNotes& a, const ChordNotes& b) noexcept
        {
            if (a.size != b.size)
                return false;
            for (int i = 0; i < a.size; ++i)
                if (a.notes[(size_t) i] != b.notes[(size_t) i])
                    return false;
            return true;
        }

        // Recomputes the pattern slots' notes from pattern `index` of the workspace (Chord Octave
        // applied), marking in `changed` (slots 12-19) those whose notes changed.
        void refreshPatternChords (int index, SlotFlags* changed) noexcept
        {
            chordsPattern = index;
            const Pattern& p = workspace.patterns[(size_t) index];
            for (int i = 0; i < numPatternSlots; ++i)
            {
                setExtraSlot (numChordSlots + i, slotNotes (p.slots[(size_t) i], current.chordOctave), p.slots[(size_t) i].chord.root, changed);
            }
        }

        // `notes` moved by `octaves`; notes outside 0..127 are dropped.
        static ChordNotes octaveMoved (const ChordNotes& notes, int octaves) noexcept
        {
            ChordNotes out;
            for (const uint8_t n : notes)
                if (n + 12 * octaves >= 0 && n + 12 * octaves <= 127)
                    out.notes[(size_t) out.size++] = (uint8_t) (n + 12 * octaves);
            return out;
        }

        // An extra slot's notes and root; marks it in `changed` if they changed.
        void setExtraSlot (int slot, const ChordNotes& notes, int root, SlotFlags* changed) noexcept
        {
            const auto i = (size_t) (slot - numChordSlots);
            if (changed != nullptr)
                (*changed)[(size_t) slot] = ! sameNotes (notes, extraChords[i]) || root != extraRoots[i];
            extraChords[i] = notes;
            extraRoots[i] = (int8_t) root;
        }

        void setTimelineSlot (int chord, SlotFlags* changed) noexcept
        {
            if (chord < 0 || chord >= timeline.count)
                return;
            const auto& c = timeline.chords[(size_t) chord];
            setExtraSlot (timelineSlot, octaveMoved (c.notes, current.chordOctave), c.root, changed);
        }

        int fixedVelocity() const noexcept { return current.fx.velocity.peak; }

        // A ProgressionPlayer trigger: the timeline's performance plays chord `t.chord`, or ends;
        // or a timed preview ends.
        void handleTrigger (const TimelineTrigger& t, BlockEvents& out) noexcept
        {
            if (t.kind == TimelineTrigger::PreviewEnd)
            {
                performer.end (ChordPerformer::previewKey, t.sample, out);
                return;
            }
            if (t.chord < 0 || t.chord >= timeline.count)
            {
                performer.end (ChordPerformer::timelineKey, t.sample, out);
                return;
            }
            setTimelineSlot (t.chord, nullptr);
            performer.press (ChordPerformer::timelineKey, timelineSlot, fixedVelocity(), t.sample, out);
        }

        // The previews queued for this block, in order, at its first sample.
        void startPreviews (const EngineSettings& s, BlockEvents& out) noexcept
        {
            const int n = numPreviews;
            numPreviews = 0;
            for (int i = 0; i < n; ++i)
            {
                const auto& r = previews[(size_t) i];
                previewEnd = -1;
                if (! r.on || ! s.enabled || s.listening() || r.notes.size == 0)
                {
                    performer.end (ChordPerformer::previewKey, 0, out);
                    continue;
                }
                previewNotes = r.notes;
                previewRoot = r.root;
                setExtraSlot (previewSlot, octaveMoved (previewNotes, current.chordOctave), previewRoot, nullptr);
                performer.press (ChordPerformer::previewKey, previewSlot, fixedVelocity(), 0, out);
                if (r.beats > 0.0f && std::isfinite (r.beats))
                    previewEnd = clock.blockStart() + (int64_t) std::llround (r.beats * clock.samplesPerQuarter());
            }
        }

        void applySettings (const EngineSettings& s, BlockEvents& out) noexcept
        {
            const EngineSettings old = current;
            current = s;
            // Key octave moves chords only when every key plays them.
            performer.setFollowKeyOctave (s.followKeyOctave);
            if (s.pattern != old.pattern || ! settingsSeen)
                currentPattern = s.pattern < 0 ? 0 : (s.pattern >= numPatterns ? numPatterns - 1 : s.pattern);
            settingsSeen = true;

            if (s.enabled != old.enabled || s.analysing != old.analysing || s.capturing != old.capturing)
            {
                releaseAll (0, out);
                if (s.listening() && ! old.listening())
                    listener.reset();     // each analysis starts afresh
                if (s.capturing && ! old.capturing)
                    chordListener.reset();   // and each capture
                performer.applySettings (s.fx, s.outputChannel, {}, 0, out);
                return;
            }

            // Held chords follow parameter changes, and the effects follow theirs. A Key octave
            // change moves every held chord; Chord Octave the pattern's too.
            SlotFlags changed {};
            refreshChords (0, nullptr, &changed);
            if (s.chordOctave != old.chordOctave)
            {
                refreshPatternChords (chordsPattern, &changed);
                setTimelineSlot (player.currentChord(), &changed);
                setExtraSlot (previewSlot, octaveMoved (previewNotes, s.chordOctave), previewRoot, &changed);
            }
            if (s.followKeyOctave != old.followKeyOctave)
                changed.fill (true);
            performer.applySettings (s.fx, s.outputChannel, changed, 0, out);
        }

        static int clampSample (int sample, int numSamples) noexcept
        {
            return sample < 0 ? 0 : (sample >= numSamples ? (numSamples > 0 ? numSamples - 1 : 0) : sample);
        }

        // Seconds since the engine started, at `sample` of this block (the KeyListener's clock).
        double timeAt (int sample) const noexcept { return seconds + sample * secondsPerSample; }

        // Beats since the engine started, at the host's tempo, counted whether or not it plays
        // (the ChordListener's clock: a looping clip or a stopped transport counts on).
        double beatSamples() const noexcept
        {
            const double spq = clock.samplesPerQuarter();
            return spq > 0.0 && std::isfinite (spq) ? spq : 22050.0;
        }
        double beatAt (int sample) const noexcept { return listenBeats + sample / beatSamples(); }

        void handle (const MidiEvent& e, int numSamples, BlockEvents& out) noexcept
        {
            const int sample = clampSample (e.sample, numSamples);

            if (e.isNoteOn())
            {
                const int n = e.note();
                auto& h = held[(size_t) n];
                if (h.down)
                {
                    performer.end (n, sample, out);   // retrigger: a held key ends, even with Hold
                    keyUp (n, sample, out);
                }

                h.down = true;
                h.velocity = (uint8_t) e.velocity();
                h.channel = (uint8_t) e.channel();
                h.passThrough = ! current.enabled || current.listening();
                h.slot = -1;

                if (h.passThrough)
                {
                    voices.start ((VoiceTracker::Source) n, h.channel, n, h.velocity, sample, out);
                    if (current.listening())
                        listener.noteOn (n, h.velocity, timeAt (sample));
                    if (current.capturing)
                        chordListener.noteOn (n, timeAt (sample), beatAt (sample));
                }
                else if (e.origin == MidiEvent::fromWorkspace)
                    pressPatternSlot (n, n - MidiEvent::workspaceKey, sample, out);
                else
                    pressChord (n, KeyMap::slotOf (n), sample, out);
            }
            else if (e.isNoteOff())
            {
                const int n = e.note();
                if (held[(size_t) n].down)
                    keyUp (n, sample, out);
            }
            else if (e.isAllNotesOff())
            {
                releaseAll (sample, out);
                out.push ({ sample, { e.data[0], e.data[1], e.data[2] }, e.size });
            }
            else
            {
                out.push ({ sample, { e.data[0], e.data[1], e.data[2] }, e.size });
            }
        }

        // Key `n` plays chord slot `slot` (0..19).
        void pressChord (int n, int slot, int sample, BlockEvents& out) noexcept
        {
            auto& h = held[(size_t) n];
            h.slot = (int8_t) slot;
            if (slot < numChordSlots)
                lastChordSlot = slot;
            performer.press (n, slot, h.velocity, sample, out);
        }

        // Key `n` plays slot `slot` of the current pattern while held (the editor's preview);
        // nothing for an empty slot.
        void pressPatternSlot (int n, int slot, int sample, BlockEvents& out) noexcept
        {
            if (slot < 0 || slot >= workspace.patterns[(size_t) currentPattern].size())
                return;
            if (chordsPattern != currentPattern)
            {
                SlotFlags changed {};
                refreshPatternChords (currentPattern, &changed);
                performer.retune (changed, sample, out);   // a chord still held from the old pattern
            }
            pressChord (n, numChordSlots + slot, sample, out);
        }

        void keyUp (int n, int sample, BlockEvents& out) noexcept
        {
            auto& h = held[(size_t) n];
            h.down = false;
            if (h.passThrough)
            {
                voices.release ((VoiceTracker::Source) n, sample, out);
                if (current.listening())
                    listener.noteOff (n, timeAt (sample));
                if (current.capturing)
                    chordListener.noteOff (n, beatAt (sample));
            }
            else if (h.slot >= 0)
                performer.release (n, sample, out);   // ends, or is held (Hold)
        }

        static constexpr int maxQueueRunsPerSegment = 1 << 16;

        EngineSettings current;
        VoiceTracker voices;
        Clock clock;
        TestPattern pattern;
        ChordSet chords {};
        ChordContext context;
        std::array<HeldKey, 128> held {};
        EventBuffer<256> injected;
        ProgressionPlayer player;
        TimelineTriggers<256> triggers;
        Timeline timeline;
        bool timelineChanged = false;
        std::array<PreviewRequest, 16> previews {};
        int numPreviews = 0;
        ChordNotes previewNotes {};       // the preview's chord at Chord Octave 0
        int8_t previewRoot = 0;
        int64_t previewEnd = -1;          // a timed preview's end (absolute sample), or -1
        std::atomic<bool> rewindRequested { false };
        Workspace workspace;
        ExtraChords extraChords {};
        ExtraRoots extraRoots {};
        int chordsPattern = 0;            // the pattern patternChords holds
        bool workspaceChanged = false;
        int currentPattern = 0;           // the Pattern parameter
        bool settingsSeen = false;
        bool playOn = false;              // this block: Play, Enable on, not analysing
        int lastChordSlot = -1;
        bool wasPlaying = false;
        KeyListener listener;
        ChordListener chordListener;
        double seconds = 0.0, secondsPerSample = 1.0 / 44100.0, listenBeats = 0.0;
        std::atomic<bool> resetRequested { false }, heardResetRequested { false };
        ChordPerformer performer { voices, clock, chords, extraChords, extraRoots, context };
    };
}
