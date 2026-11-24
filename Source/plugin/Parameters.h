// Host parameters (APVTS). Every setting a test needs is one of these, because LiveBridge
// reaches the plug-in only through Live's device parameters (ChordChimp's PLAN.md §8).
//
// IDs, names and choice lists are saved in Live sets and automation: never rename or reorder
// them, only add new ones. The IDs are Chordism's (ChordChimp's PLAN.md §3.4), so its presets load.
// ChordZimmer starts from ChordMonkey's list without Arp, Repeat, Step seq, Bind, Keys-Lock and
// Style (PLAN.md §2); the list is fixed from ChordZimmer 0.0.1 on.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/ChordEngine.h"

namespace chordzimmer::params
{
    inline constexpr const char* enable       = "enable";
    inline constexpr const char* key          = "key";
    inline constexpr const char* scale        = "scale";
    inline constexpr const char* chordGroup   = "chordGroup";
    inline constexpr const char* chordOctave  = "chordOctave";
    inline constexpr const char* testPattern  = "testPattern";

    // Phase 2 (appended). Its choices are the progressions without Custom (Progressions.h):
    // Chordism's values are converted on the way in (fromChordism).
    inline constexpr const char* progression  = "progression";

    // Phase 3 (appended): the effects.
    inline constexpr const char* stack        = "stack";
    inline constexpr const char* strum        = "strum";
    inline constexpr const char* strumRate    = "strumRate";
    inline constexpr const char* velocity     = "velocity";
    inline constexpr const char* velocityPeak = "velocityPeak";
    inline constexpr const char* velocityRange = "velocityRange";
    inline constexpr const char* hold         = "hold";

    // Phase 7 (appended): voice leading.
    inline constexpr const char* voiceLeading  = "voiceLeading";

    // ChordChimp Phase 8 (appended): whether the octave played moves the chord (ChordChimp's PLAN.md §3.1).
    // Not in presets: it's how the player plays, like Enable.
    inline constexpr const char* keyOctave     = "keyOctave";

    // Key detection (appended; ChordChimp's docs/KEY_DETECTION_PLAN.md K5.4): Off, Listen (no chords; the
    // input passes through and its key is guessed), Use guess (a command: the processor's timer
    // sets Key and Scale to the guess and Analyse back to Off). Not in presets, not saved.
    inline constexpr const char* analyse       = "analyse";
    enum class AnalyseMode { Off, Listen, UseGuess };

    // Pattern playback (appended; ChordMonkey's PLAN.md §3.2, Phase 2): Play (the current pattern plays in
    // time while the host plays) and Pattern (A-D; a switch while playing waits for the bar).
    // Pattern is in presets since Phase 5 (they hold the workspace); Play isn't.
    inline constexpr const char* play          = "play";
    inline constexpr const char* pattern       = "pattern";

    // Test rig (appended, Phase 2): while on, the processor loads the workspace from the test
    // rig's Workspace.txt whenever it changes (ChordZimmerProcessor::testWorkspaceFile), so the
    // bridge can give it patterns in Live. Not in presets; no control.
    inline constexpr const char* testWorkspace = "testWorkspace";

    // Voicing (appended; ChordMonkey's PLAN.md §3.4, Phase 4): preset parameters.
    inline constexpr const char* voicing       = "voicing";
    inline constexpr const char* bass          = "bass";

    // Detect (appended; ChordMonkey's PLAN.md §3.6, Phase 8): Off, Capture (no chords; the input passes
    // through, is grouped into chords and its key guessed), Use result (a command: the
    // processor's timer writes the chords into the current pattern, sets Key and Scale to the
    // guess and Detect back to Off). Not in presets, not saved.
    inline constexpr const char* detect        = "detect";
    enum class DetectMode { Off, Capture, UseResult };

    // ChordChimp Phase 1 (2026-10-25) removed Chordism's Split, Mute CG / SS, the shifters and
    // Test shifter, every Smart Scale and pattern parameter, Test smart, Sync group and
    // Look-ahead; Phase 2 its Custom 1-7, Strum time, Seq sustain and the four step patterns
    // (ChordChimp's PLAN.md §3.5). A saved state's or preset's values for them are ignored.
    constexpr int numParameters = 23;

    // A Chordism preset's tree, made ChordZimmer's: the Progression value becomes a choice
    // (Chordism's Custom: Diatonic). The other kept values mean the same.
    void fromChordism (juce::ValueTree& tree);

    // Parameters a preset doesn't touch: Enable, Key octave, Analyse, Detect, Play and the test
    // rig (Test pattern, Test workspace) (PresetManager).
    bool isPresetParameter (const juce::String& paramId);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Lock-free reads of the current values, for the audio thread.
    class Snapshot
    {
    public:
        explicit Snapshot (juce::AudioProcessorValueTreeState& state);

        EngineSettings engineSettings() const noexcept;
        ChordGroup chordGroup() const noexcept;
        int key() const noexcept;
        Scale currentScale() const noexcept;
        ChordFxSettings chordFx() const noexcept;
        AnalyseMode analyseMode() const noexcept;
        DetectMode detectMode() const noexcept;
        bool testWorkspace() const noexcept;

    private:
        std::atomic<float>* enable;
        std::atomic<float>* keyParam;
        std::atomic<float>* scale;
        std::atomic<float>* group;
        std::atomic<float>* chordOctave;
        std::atomic<float>* testPattern;
        std::atomic<float>* progression;
        std::atomic<float>* keyOctaveParam;
        std::atomic<float>* analyseParam;
        std::atomic<float>* playParam;
        std::atomic<float>* patternParam;
        std::atomic<float>* testWorkspaceParam;
        std::atomic<float>* detectParam;

        std::atomic<float>* fxStack;
        std::atomic<float>* fxStrum;
        std::atomic<float>* fxStrumRate;
        std::atomic<float>* fxVelocity;
        std::atomic<float>* fxVelocityPeak;
        std::atomic<float>* fxVelocityRange;
        std::atomic<float>* fxHold;
        std::atomic<float>* fxVoiceLeading;
        std::atomic<float>* fxVoicing;
        std::atomic<float>* fxBass;
    };
}
