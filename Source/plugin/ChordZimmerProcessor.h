// The plug-in: a thin JUCE adapter around ChordEngine (Source/core/engine).
//
// Two builds from ChordZimmer.jucer (ChordChimp's PLAN.md §4.3): the VST3 instrument and the Standalone app.
// Generated MIDI goes to the host's MIDI output (in Live: another track's "MIDI From"). The AU
// builds and the CoreMIDI virtual port were removed on 2026-10-20.
//
// Presets (PresetManager) load on the message thread, and the factory presets are also the
// plug-in's programs (program 0 = Init), which a host may change from another thread: that load
// waits for the timer.
//
// An ExportRecorder keeps what ChordZimmer sends while the host plays, for the Export drag-out
// (ChordChimp's PLAN.md §11, Phase 6).
//
// Key detection (ChordChimp's docs/KEY_DETECTION_PLAN.md): in Analyse mode the engine guesses the input's
// key; the guess is published as one packed atomic (keyGuess). Using it (applyGuess) sets Key
// and Scale on the message thread: from the editor's click, or from the timer when the host has
// set Analyse to Use guess. Analyse isn't saved with the state.
//
// Detect (ChordMonkey's PLAN.md §3.6, Phase 8): in Capture the engine also groups the input into chords
// (ChordListener.h), published to the message thread (heardChords); Use result (applyDetect)
// writes them into the current pattern and sets Key and Scale to the guess. Not saved either.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/ChordEngine.h"
#include "../core/engine/SnapshotExchange.h"
#include "../core/midiio/ExportRecorder.h"
#include "../core/model/Progression.h"
#include "../core/model/UndoList.h"
#include "../core/model/Workspace.h"
#include "../core/midiio/PatternRender.h"
#include "Parameters.h"
#include "PresetManager.h"
#include <atomic>

namespace chordzimmer
{
    // What the chord keys play now, for the editor (published by the audio thread).
    struct ChordDisplay
    {
        ChordSet chords {};
        ChordContext context;
        int lastSlot = -1;

        bool operator== (const ChordDisplay& o) const noexcept
        {
            return chords == o.chords && context.key == o.context.key && context.scale == o.context.scale
                && context.outputOctave == o.context.outputOctave && lastSlot == o.lastSlot;
        }
    };

    class ChordZimmerProcessor : public juce::AudioProcessor,
                              private juce::Timer,
                              private juce::ValueTree::Listener
    {
    public:
        ChordZimmerProcessor();
        ~ChordZimmerProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        void reset() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;
        using AudioProcessor::processBlockBypassed;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        // The factory presets (Init first).
        int getNumPrograms() override;
        int getCurrentProgram() override { return currentProgram.load(); }
        void setCurrentProgram (int) override;
        const juce::String getProgramName (int) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState state;

        // Message thread, one reader (the editor).
        const ChordDisplay& readDisplay() noexcept { return display.read(); }

        // The engine settings the parameters give now (any thread; lock-free reads).
        EngineSettings currentSettings() const noexcept { return snapshot.engineSettings(); }

        // On-screen keys (Phase 5): the editor's keyboards play through the engine like host
        // MIDI input. Message thread only; lock-free, read by the next audio block. `palette`:
        // the chord pads (as the keyboard: KeyMap.h).
        void pushUiNote (int note, int velocity, bool on, bool palette = false) noexcept;

        // The workspace's preview (Phase 6): slot `slot` of the current pattern plays while held.
        void pushSlotPreview (int slot, bool on) noexcept;

        // Key and note states for the editor (written by the audio thread each block): an
        // output note sounds; an input key is down; a key's performance plays (held or latched).
        bool isNoteSounding (int note) const noexcept { return testBit (sounding, note); }
        bool isKeyDown (int note) const noexcept { return testBit (keysDown, note); }
        bool isPerforming (int note) const noexcept { return testBit (performing, note); }

        // Editor preferences saved with the plug-in (page shown, keyboard shown). Message thread.
        juce::ValueTree uiState { "UI" };

        // The workspace (ChordMonkey's PLAN.md §3.2): four patterns of chords. Not parameters; saved with the
        // state. Any thread except the audio thread (a lock); the audio thread gets each new
        // workspace through a SnapshotExchange. A change the host should save marks the state
        // changed.
        Workspace getWorkspace() const;
        void setWorkspace (const Workspace&);

        // The workspace as the state and presets save it, and back (patterns missing or
        // unreadable come back empty).
        static juce::ValueTree workspaceToTree (const Workspace&);
        static Workspace workspaceFromTree (const juce::ValueTree&);

        // The progression (PLAN.md §3.3): sections of chords. Not parameters; saved with the state
        // and in presets, next to the workspace. Any thread except the audio thread (a lock); the
        // audio thread plays it as a Timeline (ProgressionPlayer.h) handed over through a
        // SnapshotExchange whenever it or the loop selection changes. A change the host should
        // save marks the state changed.
        ChordProgression getProgression() const;

        // Replaces the progression as a load (a restore, a preset, the test rig): the undo
        // history starts again from it.
        void setProgression (const ChordProgression&);

        // The editor's edits (Phase 4, PLAN.md §3.3 "all undoable"): `edit` runs on the current
        // progression as one undo step named `label` (UndoList.h); true if it changed anything.
        // Undo and redo step through the history (100 steps), which a restore or a preset with a
        // progression resets. Not saved. Message thread (any thread but the audio thread).
        bool editProgression (const std::string& label, const std::function<bool (ChordProgression&)>& edit);
        bool undoProgression();
        bool redoProgression();
        struct UndoState
        {
            bool canUndo = false, canRedo = false;
            std::string undoLabel, redoLabel;
        };
        UndoState undoState() const;

        // Goes up by one whenever the progression changes (an edit, undo, a load), so the editor
        // copies it only then.
        uint32_t progressionVersion() const noexcept { return progressionChanges.load(); }

        // The selection the loop region follows (PLAN.md §3.3: the selected chords and sections,
        // else everything). Editor state: not saved.
        Selection getLoopSelection() const;
        void setLoopSelection (const Selection&);

        // Transport (PLAN.md §3.3): ⏮ for Preview's own clock (Sync follows the host). Any thread.
        void rewind() noexcept { engine.requestRewind(); }

        // Previews (PLAN.md §3.1; message thread, lock-free): `chord` sounds through the effects
        // from the next block, until endPreview, or for `beats` if > 0. A new preview replaces
        // the last.
        void previewChord (const ProgressionChord& chord, double beats = 0.0) noexcept;
        void endPreview() noexcept;

        // The progression as the state and presets save it (progressionToText in a PROGRESSION
        // child), and back: missing or unreadable text comes back empty.
        static juce::ValueTree progressionToTree (const ChordProgression&);
        static ChordProgression progressionFromTree (const juce::ValueTree&);

        //==========================================================================
        // Presets (message thread). applyPreset sets every preset parameter (the preset's value,
        // or the default if it has none), replaces the workspace and the progression if the
        // preset has them, and ends every note. presetState holds both too.
        PresetManager presets { *this };
        void applyPreset (const juce::ValueTree& preset, const juce::String& category, const juce::String& name);
        juce::ValueTree presetState();
        const juce::String& getPresetName() const noexcept { return presetName; }
        const juce::String& getPresetCategory() const noexcept { return presetCategory; }
        void setPresetIdentity (const juce::String& category, const juce::String& name);

        // Export (message thread): the last ≤ 32 completed bars, written to a temporary .mid
        // for a drag. An invalid File (and `error`) if there's nothing to export yet.
        TakeInfo exportInfo() const noexcept { return recorder.info(); }
        juce::File writeExport (juce::String& error);

        // The timeline's drag-out (PLAN.md §3.7, Phase 7; PatternRender.h's renderSelection): the
        // selection's chords and sections (everything when nothing is selected) rendered with
        // the current settings at the host's last tempo and meter (120 BPM, 4/4 before it has
        // played), with section names as markers, as a temporary .mid named after them and the
        // key ("Verse-Chorus_C.mid"); `chordsOnly` leaves the strum out. An invalid File (and
        // `error`) for an empty progression. (ChordMonkey's pattern drag-out went with the
        // workspace panel in Phase 4.)
        juce::File writeSelectionExport (const Selection&, bool chordsOnly, juce::String& error);

        // While the Test pattern is on, each finished take is also written here (for checks in
        // Live through the bridge).
        static juce::File testTakeFile();

        // Key detection. The guess (written by the audio thread each block; any thread reads it).
        // applyGuess (message thread): with a guess, Key = its tonic, Scale = scaleForGuess and
        // Analyse = Off, as one set of gestures; without one, Analyse = Listen.
        KeyGuess readKeyGuess() const noexcept { return KeyGuess::unpack (keyGuess.load (std::memory_order_relaxed)); }
        params::AnalyseMode analyseMode() const noexcept { return snapshot.analyseMode(); }
        void applyGuess();

        // While the Test pattern is on, the guess is also written here when it changes (for the
        // bridge's checks in Live).
        static juce::File heardKeyFile();

        // Detect (message thread). The chords heard in the current (or last) capture, and
        // applyDetect: with chords, they go on the end of the timeline as a new section,
        // "Detected" (core/model/Detect.h, in the key guessed, else the Key; one undo step; up to
        // ChordMonkey's Phase 3 they replaced the current workspace pattern), Key and Scale
        // follow a guess, and Detect = Off, as one set of gestures; returns true. Without
        // chords, Detect = Capture and false.
        params::DetectMode detectMode() const noexcept { return snapshot.detectMode(); }
        const HeardChords& heardChords();
        bool applyDetect();
        void cancelDetect();             // Detect = Off; the next capture starts empty

        // Listen (PLAN.md §3.4, Phase 5; message thread): the notes collected while the Listen
        // parameter is on (NoteCollector.h), with their lengths; and Clear (the next block
        // starts again).
        const CollectedNotes& collectedNotes();
        void clearCollectedNotes();

        // While Test workspace is on, the workspace is loaded from this file whenever it changes
        // (Phase 2, for the bridge's checks in Live): line n is pattern n (A-D) as a progression
        // (ChordSets.h), in the current Key; a blank or unreadable line is an empty pattern, and
        // lines starting with # are skipped.
        static juce::File testWorkspaceFile();

        // While Test workspace is on, the progression is also loaded from this file whenever it
        // changes (Phase 3, for the bridge's checks in Live): each line is a section, "Name |
        // numerals" (ChordSets.h; up to 8 chords, a bar each unless ":beats"), in the current
        // Key; lines starting with # are skipped.
        static juce::File testProgressionFile();

        // While Test workspace is on, keys are also played from this file (Phase 3, for the
        // bridge's checks in Live, whose bridge can't launch a clip): each line is "beat note
        // length" (song position and length in beats), played while the host plays, as host
        // MIDI on channel 1 at velocity 100. Up to 256 keys; lines starting with # are skipped.
        static juce::File testKeysFile();

        // Status for the editor and tests (written by the audio thread).
        std::atomic<int> lastChordSlot { -1 };
        std::atomic<int> soundingNotes { 0 };
        std::atomic<bool> transportPlaying { false };
        std::atomic<int> playingPattern { 0 }, playingSlot { -1 };   // the workspace pattern playing: none since Phase 3
        std::atomic<int> playingChord { -1 };                          // the timeline chord sounding (an index in order), or -1
        std::atomic<double> playhead { -1.0 };                         // the timeline position (beats), or -1 when not playing
        std::atomic<double> hostBpm { 120.0 };                        // the host's last tempo and meter
        std::atomic<int> hostNumerator { 4 }, hostDenominator { 4 };
        std::atomic<uint64_t> keyGuess { KeyGuess().pack() };

    private:
        using NoteBits = std::array<std::atomic<uint64_t>, 2>;
        static bool testBit (const NoteBits& b, int note) noexcept
        {
            return note >= 0 && note < 128 && ((b[(size_t) (note >> 6)].load (std::memory_order_relaxed) >> (note & 63)) & 1u) != 0;
        }
        void publishNoteStates() noexcept;

        // The UI's preferences and the preset's name aren't parameters: Live only saves them in
        // a set if the plug-in says its state changed (and not for a restore).
        void notifyStateChanged();
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { notifyStateChanged(); }
        std::atomic<bool> restoringState { false };

        static BusesProperties makeBuses();
        void timerCallback() override;
        void run (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
        Transport readTransport();

        params::Snapshot snapshot;
        ChordEngine engine;

        // Preallocated in the constructor / prepareToPlay; never resized in processBlock.
        EventBuffer<2048> input;
        static constexpr int inputReserve = 256;   // slots only key-ups may take (Phase 8)
        std::array<MidiEvent, 256> uiNotes {};
        juce::AbstractFifo uiFifo { 256 };
        NoteBits sounding {}, keysDown {}, performing {};
        BlockEvents output;
        juce::MidiBuffer generated;

        std::atomic<double> preparedSampleRate { 44100.0 };
        Transport lastTransport;
        SnapshotExchange<ChordDisplay> display;
        ChordDisplay lastDisplay;
        SnapshotExchange<HeardChords> heard;    // audio thread → message thread
        HeardChords heardScratch, heardCache;    // the audio thread's copy; the message thread's
        SnapshotExchange<CollectedNotes> collected { CollectedNotes {} };   // audio thread → message thread (~6 KB)
        std::unique_ptr<CollectedNotes> collectedScratch = std::make_unique<CollectedNotes>(),   // the audio thread's copy
                                        collectedCache = std::make_unique<CollectedNotes>();     // the message thread's

        ExportRecorder recorder;
        bool writeTake (const juce::File& file, juce::String& error) const;
        uint32_t testTakeWritten = 0;
        uint64_t heardKeyWritten = 0;
        int heardModeWritten = -1;
        void writeHeardKey();
        void readTestWorkspace();
        juce::Time testWorkspaceRead, testKeysRead, testProgressionRead;

        struct TestKeys
        {
            struct Key { double from = 0.0, to = 0.0; uint8_t note = 0; };
            std::array<Key, 256> keys {};
            int count = 0;
        };
        SnapshotExchange<TestKeys> testKeysForAudio;   // message thread → audio thread
        void addTestKeys (const Transport&, int numSamples) noexcept;
        juce::String presetName { "Init" }, presetCategory { "Init" };
        mutable juce::CriticalSection workspaceLock;
        Workspace workspace;
        SnapshotExchange<Workspace> workspaceForAudio;   // written under workspaceLock
        mutable juce::CriticalSection progressionLock;
        ChordProgression progression;
        UndoList history;                                // under progressionLock
        std::atomic<uint32_t> progressionChanges { 0 };
        Selection loopSelection;
        SnapshotExchange<Timeline> timelineForAudio;     // written under progressionLock
        void publishTimeline();                          // under progressionLock

        std::array<PreviewRequest, 32> previewRequests {};
        juce::AbstractFifo previewFifo { 32 };
        void pushPreview (const PreviewRequest&) noexcept;
        std::atomic<int> currentProgram { 0 }, pendingProgram { -1 };
        void loadProgram (int index);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordZimmerProcessor)
    };
}
