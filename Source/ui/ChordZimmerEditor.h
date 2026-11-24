// The editor: ChordMonkey's (ChordMonkey's PLAN.md §5) cut down to what ChordZimmer keeps and
// dressed in the Night Studio theme (PLAN.md §5.1, Theme.h). One page, resizable. The palette,
// navigator and timeline of PLAN.md §5 replace the pads, workspace and effects in Phase 4.
//
//   top bar      the name · On · presets · Export · Keys
//   globals      Key · Heard (Analyse, the key heard) · Scale · Chord Group · Progression ·
//                Octave
//   chord pads   the seven white-key chords and the five black-key ones above them (they take
//                the space the rest leaves): the palette
//   workspace    the pattern's slots, A-D, Play, Detect, the pattern's MIDI drag
//   effects      Strum, Velocity; Voicing, Keys (Hold, Key octave)
//   bottom       the output keyboard (can be hidden), which also plays
//   Info View    a strip along the bottom, like Live's: point at any control or display to see
//                what it does (as NoteCap's and DrumChimp's; InfoView.h). It replaces tooltips.
//
// Everything here is a host parameter; the UI keeps only its own preferences (keyboard shown,
// size) in the plug-in state. Refreshed at 30 Hz from the processor's published display and
// note states.
#pragma once

#include "InfoView.h"
#include "Panels.h"
#include "Theme.h"
#include "WorkspacePanel.h"

namespace chordzimmer
{
    class ChordZimmerProcessor;

    class ChordZimmerEditor : public juce::AudioProcessorEditor,
                             public juce::DragAndDropContainer,
                             private juce::Timer
    {
    public:
        explicit ChordZimmerEditor (ChordZimmerProcessor&);
        ~ChordZimmerEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // For tests (Tests/Snapshot): read the processor now, without waiting for the timer;
        // the parameters some control is (or was) bound to; the keyboard; the pads; Export.
        void refreshNow() { timerCallback(); }
        const std::set<juce::String>& boundParameters() const noexcept { return bindings.boundIds(); }
        void showKeyboard (bool shown);
        ui::ChordPads& getChordPads() noexcept { return chordPads; }
        const ui::ExportHandle& getExportHandle() const noexcept { return topBar.exportHandle; }
        ui::GlobalRow& getGlobals() noexcept { return globals; }
        ui::WorkspacePanel& getWorkspacePanel() noexcept { return workspace; }

        // The Info View: the key ("strumRate", "ui.export", "card.voicing"…) of what is at `p` (editor
        // coordinates; empty over nothing described, the strip itself included), and showing one
        // as hovering does (tests call it).
        juce::String infoKeyAt (juce::Point<int> p);
        void showInfoAt (juce::Point<int> p);
        ui::InfoView& infoView() noexcept { return info; }

        void mouseEnter (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        // PLAN.md §5: about 1000 × 640.
        static constexpr int defaultWidth = 1000, defaultHeight = 640;
        static constexpr int minWidth = 760, minHeight = 600, maxWidth = 1500, maxHeight = 960;
        static constexpr int maxPadHeight = 300;   // both rows

    private:
        void timerCallback() override;
        juce::Component* infoComponentAt (juce::Point<int> p, juce::String& key);

        ChordZimmerProcessor& processor;
        ui::LookAndFeel lookAndFeel;
        ui::Bindings bindings;

        ui::TopBar topBar;
        ui::GlobalRow globals;
        ui::ChordPads chordPads;
        ui::WorkspacePanel workspace;
        ui::EffectsPanel effects;
        ui::OutputKeyboard outputKeys;
        ui::InfoView info;
        bool sizeRestored = false;      // until then, resized() leaves the saved size alone

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordZimmerEditor)
    };
}
