// The workspace panel (ChordMonkey's PLAN.md §5, Phase 6): the current pattern's eight slots, as a card under
// the chord pads.
//
//   title row   WORKSPACE · Detect (Phase 8: capture chords played in, keyboard or clip; click
//               again to write them into the pattern, × to cancel) · Pattern ▾ (transpose to the
//               Key, copy to another pattern, clear) · Drag MIDI (drag the pattern into the host
//               as a clip: one loop, performance included; with Alt, chords only). ChordMonkey's
//               chord-set browser is gone (PLAN.md §2); the timeline replaces this panel in Phase 4.
//   body        A B C D (the Pattern parameter) · Play · the slots: chord name, numeral in the Key
//               and length; the one sounding lit
//
// Getting chords in: drag a chord pad onto a slot (replaces its chord) or past the last one
// (appends), or right-click a pad. Slots: click to hear one; drag to reorder; right-click for
// length, inversion, octave, substitutes (Phase 9), duplicate and delete. Every change goes through the processor's
// setWorkspace (message thread); the actions are public so the snapshot test can drive them.
#pragma once

#include "Keyboards.h"
#include "ParamControl.h"

namespace chordzimmer::ui
{
    // Drags the current pattern out as a .mid (ChordZimmerProcessor::writePatternExport).
    class PatternExportHandle : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        PatternExportHandle (ChordZimmerProcessor&, std::function<int()> pattern);
        void setEnabledFor (bool hasChords);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordZimmerProcessor& processor;
        std::function<int()> currentPattern;
        bool enabled = false, dragging = false;
    };

    class WorkspacePanel : public juce::Component,
                           public juce::DragAndDropTarget,
                           public juce::SettableTooltipClient
    {
    public:
        WorkspacePanel (ChordZimmerProcessor&, Bindings&);

        void refresh (const UiModel&);
        static int preferredHeight();

        // The pattern shown (the Pattern parameter).
        int currentPattern() const;

        // Actions (each one setWorkspace).
        void addPaletteChord (int paletteSlot, int targetSlot = -1);   // -1 or past the end: append; else replace that slot's chord
        void moveSlot (int from, int to);
        void setSlotLength (int slot, int halfBeats);
        void setSlotInversion (int slot, int inversion);
        void setSlotOctave (int slot, int octave);
        void setSlotChord (int slot, const WorkspaceChord&);   // a substitute: length, octave and inversion kept
        void duplicateSlot (int slot);
        void deleteSlot (int slot);
        void transposeToKey();
        void copyPatternTo (int other);
        void clearPattern();
        void detectClicked();                           // Off: start capturing; capturing: use the result
        void cancelDetect();
        juce::String detectText() const { return detectButton.getButtonText(); }

        // Slot geometry (for the mouse and the test).
        juce::Rectangle<float> slotBounds (int slot) const;
        int slotAt (juce::Point<float>) const;          // -1 outside the slots

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

        static constexpr int titleHeight = 22, bodyHeight = 52;

    private:
        void edit (const std::function<void (Pattern&)>& change);
        void showSlotMenu (int slot);
        void showPatternMenu();
        void stopPreview();

        ChordZimmerProcessor& processor;
        Bindings& bindings;
        UiModel model;
        std::array<juce::TextButton, numPatterns> tabs;
        ParamControl play;
        juce::TextButton patternButton { "Pattern" };
        juce::TextButton detectButton { "Detect" }, cancelDetectButton { juce::String::fromUTF8 ("\xc3\x97") };
        void updateDetect();
        PatternExportHandle exportHandle;
        juce::Rectangle<int> slotsArea;
        int previewing = -1, pressed = -1, dropHint = -1, shownPattern = -1;
    };
}
