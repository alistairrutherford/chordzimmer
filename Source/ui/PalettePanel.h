// The chord palette (PLAN.md §3.1, §5 item 3, §5.1; Phase 4): the 7 degree columns of the
// palette's key and mode × the tier rows of PaletteTiers.h (PaletteBuilder.h builds the grid).
//
//   header      each column's numeral relative to the home key (a borrowed column reads bVII,
//               iv …) and its root
//   rows        bottom to top: Other (5, sus2, sus4), Triads, Sixths & sevenths, Colour,
//               Extended, Altered; the row's label on the left. About four rows show; the wheel
//               scrolls; the triads are in view at first
//   cells       Night Studio orbs (Theme.h): the chord's family colour, filled when every note is
//               in the home key, a hollow ring when not; the name, and the numeral under it. An
//               empty cell is a faint dot. Hover lifts the orb and the Info View says the notes,
//               and which are out of key; the chord the timeline plays glows with the beat
//
// Click to hear a cell (until the mouse is let go); drag it onto the timeline; double-click to
// add it at the timeline's insertion marker; right-click to add it there or as a new section.
#pragma once

#include "Keyboards.h"
#include "Theme.h"
#include "../core/theory/CircleOfFifths.h"
#include "../core/theory/PaletteBuilder.h"

namespace chordzimmer::ui
{
    class PalettePanel : public juce::Component,
                         public juce::SettableTooltipClient
    {
    public:
        explicit PalettePanel (ChordZimmerProcessor&);
        ~PalettePanel() override;

        // The palette's key and mode and the home key (the navigator). Rebuilds the grid when
        // they change.
        void setContext (const PaletteContext&);
        const PaletteGrid& getGrid() const noexcept { return grid; }

        // The chord the timeline sounds (its pitch classes and root; 0 / -1 for none) and the
        // playhead's beat (for the pulse).
        void setSounding (unsigned pitchClasses, int root, double playhead);

        // Rows: `firstRow` is the bottom one shown (a variant index, 0 = "5").
        int firstRow() const noexcept { return bottomRow; }
        int visibleRows() const noexcept;
        void scrollRows (int delta);
        void showRow (int variant);               // scrolls it into view

        struct CellRef
        {
            int variant = -1, degree = -1;
            bool valid() const noexcept { return variant >= 0 && degree >= 0; }
            bool operator== (const CellRef& o) const noexcept { return variant == o.variant && degree == o.degree; }
        };
        CellRef cellAt (juce::Point<float>) const;          // an existing cell shown there, or invalid
        juce::Rectangle<float> cellBounds (CellRef) const;  // its orb; empty when not shown
        const PaletteCell* cell (CellRef) const noexcept;

        // Double-click and the menu (the editor sends them to the timeline).
        std::function<void (const ProgressionChord&)> onAdd, onAddAsSection;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // What the Info View shows for a cell: name, numeral, notes, in or out of key.
        juce::String describe (CellRef) const;

    private:
        juce::Rectangle<int> headerArea() const;
        juce::Rectangle<int> rowsArea() const;
        juce::Rectangle<int> labelArea() const;
        float rowHeight() const;
        void endPreview();
        void showMenu (CellRef);

        ChordZimmerProcessor& processor;
        PaletteContext context;
        PaletteGrid grid;
        Spelling spelling;
        int bottomRow = 0;
        float wheelAccumulator = 0.0f;
        CellRef hovered, pressed;
        bool previewing = false, dragging = false;
        juce::Point<float> pressedAt;
        unsigned soundingPcs = 0;
        int soundingRoot = -1;
        float pulse = 0.0f;

        static constexpr int padding = 10, titleHeight = 18, headerHeight = 30, labelWidth = 52, scrollWidth = 6;
    };
}
