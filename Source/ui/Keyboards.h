// The editor's playable surfaces (ChordChimp's PLAN.md §5). Each plays through the engine like host MIDI
// input (ChordZimmerProcessor::pushUiNote), so clicking is the same as pressing a key.
//
//   ChordPads       seven pads, one per white-key letter (C … B), and above them five smaller
//                   ones over the gaps, one per black key (C# … A#: the borrowed chords): Night
//                   Studio orbs (Theme.h) in the chord's family colour, filled in key and hollow
//                   out of it, with the Roman numeral and name and the key letter in a corner;
//                   glowing brighter on hover and while the chord plays, with an amber stripe on
//                   the last one played. Click (or drag across) to play.
//   OutputKeyboard  88 keys: the notes ChordZimmer sends (lit) and the keys held (outlined).
//                   Hideable.
#pragma once

#include "../plugin/ChordZimmerProcessor.h"
#include "ParamControl.h"

namespace chordzimmer::ui
{
    // What the editor shows, refreshed on its timer.
    struct UiModel
    {
        ChordDisplay display;             // the chords, their context, last slot
        EngineSettings settings;          // the parameters now
        params::AnalyseMode analyse = params::AnalyseMode::Off;
        KeyGuess guess;                   // the key heard in Analyse mode
        Workspace workspace;              // Phase 6: the patterns
        int playingPattern = 0, playingSlot = -1;
        params::DetectMode detect = params::DetectMode::Off;   // Phase 8
        int heardChords = 0;              // chords heard in this capture
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

    class ChordPads : public juce::Component,
                      public juce::SettableTooltipClient
    {
    public:
        explicit ChordPads (ChordZimmerProcessor&);
        void refresh (const UiModel&);

        juce::Rectangle<float> padBounds (int slot) const;
        int slotAt (juce::Point<float>) const;   // -1 if none

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        // Right-click's "Add to the pattern" (Phase 6): the pad's slot (0..11).
        std::function<void (int)> onAddToPattern;

    private:
        ChordZimmerProcessor& processor;
        PlayingKeys playing;
        UiModel model;
        std::array<bool, numChordSlots> active {};
        int lastPad = -1;                 // the pad pressed or played last (what a drag out takes)
        int hovered = -1;                 // the pad under the mouse (a stronger glow)
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
