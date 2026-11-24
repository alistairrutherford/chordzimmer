// The editor (PLAN.md §5; Phase 4): the palette, the key navigator and the timeline, in the Night
// Studio theme (PLAN.md §5.1, Theme.h). One page, resizable. Top to bottom:
//
//   top bar      the name · On · presets · Export · Keys (Panels.h)
//   transport    ⏮ · Play · Loop · Sync mode · Start bar · Snap · undo, redo · the chord now
//   timeline     sections of chord blocks, playhead, loop region, insertion marker
//                (TimelinePanel.h)
//   palette      degree × complexity orbs (PalettePanel.h)
//   footer       home key · ⌂ ◀ key ▶ mode ▾ · the circle of fifths · Performance (NavigatorBar.h)
//   keyboard     the output keyboard (can be hidden), which also plays
//   Info View    a strip along the bottom, like Live's: point at any control or display to see
//                what it does (InfoView.h). It replaces tooltips.
//
// Performance opens PLAN.md §3.6's popover over the palette (Panels.h, PerformancePanel), which
// also holds what MIDI in does (Chord Group, Progression, Hold, Key octave, Heard, Detect).
// ChordMonkey's chord pads, workspace panel and effects cards are gone (PLAN.md §2).
//
// Keys (PLAN.md §5): Space play/stop, L loop, Cmd-Z / Shift-Cmd-Z, Delete, Cmd-C/X/V/D, ← →,
// Cmd +/-, Esc closes the popover.
//
// Parameters are host parameters; the editor keeps its own preferences (keyboard shown, size,
// palette position, snap, zoom) in the processor's uiState, saved with the plug-in. Refreshed at
// 30 Hz from the processor's published state.
#pragma once

#include "InfoView.h"
#include "NavigatorBar.h"
#include "PalettePanel.h"
#include "Panels.h"
#include "Theme.h"
#include "TimelinePanel.h"

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
        bool keyPressed (const juce::KeyPress&) override;

        // For tests (Tests/Snapshot): read the processor now, without waiting for the timer;
        // the parameters some control is (or was) bound to; the panels.
        void refreshNow() { timerCallback(); }
        const std::set<juce::String>& boundParameters() const noexcept { return bindings.boundIds(); }
        void showKeyboard (bool shown);
        void showPerformance (bool shown);
        bool performanceShown() const noexcept { return performance.isVisible(); }
        ui::TimelinePanel& getTimeline() noexcept { return timeline; }
        ui::PalettePanel& getPalette() noexcept { return palette; }
        ui::NavigatorBar& getNavigator() noexcept { return navigator; }
        ui::TransportBar& getTransport() noexcept { return transport; }
        ui::PerformancePanel& getPerformance() noexcept { return performance; }
        const ui::ExportHandle& getExportHandle() const noexcept { return topBar.exportHandle; }
        const CircleOfFifths& getNavigatorState() const noexcept { return nav; }

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
        void mouseDown (const juce::MouseEvent&) override;

        // PLAN.md §5: about 1000 × 640.
        static constexpr int defaultWidth = 1000, defaultHeight = 640;
        static constexpr int minWidth = 760, minHeight = 600, maxWidth = 1500, maxHeight = 960;

    private:
        void timerCallback() override;
        juce::Component* infoComponentAt (juce::Point<int> p, juce::String& key);
        void paletteMoved();               // the navigator changed: rebuild, save
        void undo();
        void redo();
        void placePerformance();

        ChordZimmerProcessor& processor;
        ui::LookAndFeel lookAndFeel;
        ui::Bindings bindings;
        CircleOfFifths nav;

        ui::TopBar topBar;
        ui::TransportBar transport;
        ui::TimelinePanel timeline;
        ui::PalettePanel palette;
        ui::NavigatorBar navigator;
        ui::PerformancePanel performance;
        ui::OutputKeyboard outputKeys;
        ui::InfoView info;
        bool sizeRestored = false;      // until then, resized() leaves the saved size alone

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordZimmerEditor)
    };
}
