// The footer (PLAN.md §3.2, §5 item 4, §5.1; Phase 4): the home key, the key navigator, and the
// performance popover's button.
//
//   Home key, Scale   the Key and Scale parameters: the home key every in-key mark and numeral
//                     is measured against
//   ⌂                 back to the home key and mode (amber while the palette is away)
//   ◀ key ▶           the palette a fifth down or up, keeping the mode; the steps from home
//   mode ▾            the parallel modes of the palette's family on the same tonic
//   dial              the circle of fifths: 12 dots, home in amber, the palette's key lit, the
//                     arc between them; click a dot to go there
//   Performance       opens PLAN.md §3.6's popover (Panels.h, PerformancePanel)
//
// The palette position (CircleOfFifths.h) is UI state saved with the plug-in, not a parameter;
// the editor owns it and stores it in the processor's uiState.
#pragma once

#include "Panels.h"
#include "../core/theory/CircleOfFifths.h"

namespace chordzimmer::ui
{
    class FifthsDial : public juce::Component
    {
    public:
        FifthsDial();
        void set (int homeKey, int paletteKey, int fifths);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        juce::Point<float> dotCentre (int pitchClass) const;
        std::function<void (int)> onPick;          // a pitch class clicked

    private:
        int home = 0, palette = 0, steps = 0;
    };

    class NavigatorBar : public juce::Component
    {
    public:
        NavigatorBar (Bindings&, CircleOfFifths&);

        // After the navigator or the home key changed: the labels, the mode list, the dial.
        void refresh();

        // Navigator moves (each calls onChange).
        void stepFifth (int direction);
        void goHome();
        void setMode (Scale);
        void pickKey (int pitchClass);

        std::function<void()> onChange;
        std::function<void()> onPerformance;
        void setPerformanceShown (bool);

        void paint (juce::Graphics&) override;
        void resized() override;

        ParamControl key, scale;
        IconButton home, down, up;
        juce::ComboBox mode;
        FifthsDial dial;
        juce::TextButton performance { "Performance" };

    private:
        CircleOfFifths& nav;
        juce::Rectangle<int> keyLabel;
        ModeFamily listedFamily = ModeFamily::Count;   // the family the mode menu lists
    };
}
