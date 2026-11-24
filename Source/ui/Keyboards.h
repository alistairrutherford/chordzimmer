// The editor's output keyboard (ChordChimp's PLAN.md §5): it plays through the engine like host MIDI
// input (ChordZimmerProcessor::pushUiNote), so clicking is the same as pressing a key.
//
//   OutputKeyboard  88 keys: the notes ChordZimmer sends (lit) and the keys held (outlined).
//                   Hideable.
//
// ChordMonkey's chord pads (one per white and black key) gave way to the palette in Phase 4
// (PalettePanel.h, PLAN.md §3.1).
#pragma once

#include "../plugin/ChordZimmerProcessor.h"
#include "ParamControl.h"

namespace chordzimmer::ui
{
    // What the editor shows, refreshed on its timer.
    struct UiModel
    {
        EngineSettings settings;          // the parameters now
        params::AnalyseMode analyse = params::AnalyseMode::Off;
        KeyGuess guess;                   // the key heard in Analyse mode
        params::DetectMode detect = params::DetectMode::Off;
        int heardChords = 0;              // chords heard in this capture
        double playhead = -1.0;           // the timeline position (beats), -1 when not playing
        int playingChord = -1;            // the timeline chord sounding (in order), or -1
        int beatsPerBar = 4;              // the host's meter
    };

    // Piano geometry for notes lo..hi (both white keys) across a width.
    struct PianoLayout
    {
        int lo = 60, hi = 84;
        juce::Rectangle<float> area;

        static bool isBlack (int note) noexcept;
        int whiteCount() const noexcept;
        juce::Rectangle<float> keyBounds (int note, float blackHeight = 0.62f) const;
        int noteAt (juce::Point<float> p, float blackHeight = 0.62f) const;   // -1 if none
    };

    // Plays a key while the mouse holds it (and a drag moves along the keys).
    class PlayingKeys
    {
    public:
        // `palette`: the chord pads' notes.
        explicit PlayingKeys (ChordZimmerProcessor& p, bool palette = false) : processor (p), toPalette (palette) {}
        ~PlayingKeys() { release(); }
        void press (int note, int velocity);
        void release();

    private:
        ChordZimmerProcessor& processor;
        bool toPalette = false;
        int held = -1;
    };

    class OutputKeyboard : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        explicit OutputKeyboard (ChordZimmerProcessor&);
        void refresh (const UiModel&);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordZimmerProcessor& processor;
        PlayingKeys playing;
        PianoLayout layout;
        std::array<bool, 128> sounding {}, down {};
        juce::Font labelFont { juce::FontOptions {} };   // made once: a new Font on every paint costs a typeface lookup
    };
}
