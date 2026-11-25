// ChordZimmer's look: "Night Studio" (PLAN.md §5.1). A dark, glowing, instrument-like surface:
// a navy-black gradient with a faint noise grain, frosted "glass" cards, chord colours by family
// (teal-sky for major, violet-magenta for minor, slate for the rest), and one amber accent.
//
// Every colour, radius, glow and font is a named token here, so the look is retuned in one
// place. `night` holds the tokens of PLAN.md §5.1; `colours` keeps ChordMonkey's names (which
// the forked panels use) mapped onto them. The helpers draw the shared pieces: the background,
// a glass panel, a family gradient, a glow, a chord orb.
//
// Fonts (SIL OFL, Resources/Fonts, embedded via BinaryData): Space Grotesk for headings and chord
// names (headingFont), Inter for everything else (font, and the LookAndFeel's default sans).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/theory/PaletteBuilder.h"

namespace chordzimmer::ui
{
    namespace night
    {
        // Editor background: a vertical gradient with a 4 % noise grain.
        const juce::Colour bgTop        { 0xff0a0d16 };
        const juce::Colour bgBottom     { 0xff121829 };
        constexpr float    bgNoise      = 0.04f;

        // Cards: #161C2E at 85 % with a 1 px white-8 % top highlight.
        const juce::Colour panel        { 0xff161c2e };
        constexpr float    panelAlpha   = 0.85f;
        const juce::Colour panelHighlight { 0x14ffffff };
        const juce::Colour panelEdge    { 0xff242c47 };

        const juce::Colour text         { 0xffe8ecf8 };
        const juce::Colour textDim      { 0xff7c86a6 };

        // Chord families (gradient from → to).
        const juce::Colour majorFrom    { 0xff2ee6c5 }, majorTo { 0xff1a9fd9 };
        const juce::Colour minorFrom    { 0xffb26bff }, minorTo { 0xffe04fb0 };
        const juce::Colour otherFrom    { 0xff6b7699 }, otherTo { 0xff454e6b };

        const juce::Colour accent       { 0xffffb547 };   // playhead, selection, Home away, Export
        const juce::Colour danger       { 0xffff5c7a };   // out-of-key tones

        // Radii.
        constexpr float panelRadius   = 14.0f;
        constexpr float controlRadius = 8.0f;
        constexpr float padRadius     = 12.0f;

        // Glows: a blurred shadow in the family colour.
        constexpr float glowIdle  = 0.35f;
        constexpr float glowHover = 0.60f;
        constexpr float glowPlay  = 0.80f;
        constexpr int   glowBlur  = 18;

        // Motion (PLAN.md §5.1): hovers and drags ease out over 120-180 ms, nothing over 300 ms.
        constexpr int hoverMs = 150, maxMotionMs = 300;
    }

    // ChordMonkey's colour names, in Night Studio values.
    namespace colours
    {
        const juce::Colour background   = night::bgTop;
        const juce::Colour panel        = night::panel;
        const juce::Colour raised       { 0xff1e2640 };
        const juce::Colour outline      = night::panelEdge;
        const juce::Colour text         = night::text;
        const juce::Colour dim          = night::textDim;
        const juce::Colour faint        { 0xff4a5475 };
        const juce::Colour chord        = night::accent;
        const juce::Colour sounding     = night::majorFrom;   // notes playing
        const juce::Colour whiteKey     { 0xffd8deee };
        const juce::Colour whiteKeyText { 0xff1a2033 };
        const juce::Colour blackKey     { 0xff121729 };
    }

    // Chord families (PLAN.md §3.1): from the chord's 3rd and 5th, not its name. Since Phase 4
    // the core's ChordFamily / chordFamily (PaletteBuilder.h) in place of the UI's own familyOf.
    using Family = ChordFamily;

    juce::Colour familyFrom (Family) noexcept;
    juce::Colour familyTo (Family) noexcept;
    juce::Colour familyMid (Family) noexcept;   // for glows and rings

    // The timeline's 8 section hues (PLAN.md §5.1: low saturation), 0 … numSectionColours-1.
    juce::Colour sectionColour (int index) noexcept;

    // Inter (body) and Space Grotesk (headings, chord names).
    juce::Font font (float height, bool bold = false);
    juce::Font headingFont (float height, bool bold = false);

    // The editor background: gradient and grain over `area`.
    void fillBackground (juce::Graphics&, juce::Rectangle<int> area);

    // A glass card: translucent fill, a 1 px top highlight, a hairline edge.
    void drawGlassPanel (juce::Graphics&, juce::Rectangle<float>, float radius = night::panelRadius);

    // A soft glow around `shape` in `colour` at `alpha` (night::glowIdle … glowPlay).
    void drawGlow (juce::Graphics&, const juce::Path& shape, juce::Colour colour, float alpha, int blur = night::glowBlur);

    // A chord orb / pad (PLAN.md §5.1): in key, a filled radial gradient of its family; out of
    // key, a 2 px gradient ring with a dark centre and a dashed inner hairline. `glow` is the
    // glow's alpha (0: none).
    void drawChordOrb (juce::Graphics&, juce::Rectangle<float>, Family, bool inKey, float glow, float radius = night::padRadius);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;   // a tick box
        void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
        static constexpr int comboArrowWidth = 20, comboTextInset = 3;   // a combo box's text has its width less these
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.5f, true); }
        juce::Font getLabelFont (juce::Label&) override { return font (12.0f); }
        juce::Font getPopupMenuFont() override { return font (14.0f); }
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };
}
