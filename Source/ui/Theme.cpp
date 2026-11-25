#include "Theme.h"
#include "BinaryData.h"

namespace chordzimmer::ui
{
    namespace
    {
        juce::Typeface::Ptr load (const char* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }

        // Loaded once, on first use (message thread).
        const juce::Typeface::Ptr& interRegular()    { static auto t = load (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize); return t; }
        const juce::Typeface::Ptr& interSemiBold()   { static auto t = load (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize); return t; }
        const juce::Typeface::Ptr& groteskMedium()   { static auto t = load (BinaryData::SpaceGroteskMedium_ttf, BinaryData::SpaceGroteskMedium_ttfSize); return t; }
        const juce::Typeface::Ptr& groteskBold()     { static auto t = load (BinaryData::SpaceGroteskBold_ttf, BinaryData::SpaceGroteskBold_ttfSize); return t; }

        juce::Font make (const juce::Typeface::Ptr& t, float height, bool bold)
        {
            if (t == nullptr)   // a font that didn't load: the system sans
                return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
            return juce::Font (juce::FontOptions (t).withHeight (height));
        }

        // A tile of grain: white and black specks averaging night::bgNoise, seeded so every
        // instance draws the same.
        const juce::Image& noiseTile()
        {
            static const juce::Image tile = []
            {
                constexpr int size = 128;
                juce::Image img (juce::Image::ARGB, size, size, true);
                juce::Random rng (0x2c1e);
                juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x)
                    {
                        const auto a = (juce::uint8) rng.nextInt ((int) (2.0f * night::bgNoise * 255.0f) + 1);
                        px.setPixelColour (x, y, (rng.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (a));
                    }
                return img;
            }();
            return tile;
        }
    }

    //==========================================================================
    juce::Colour sectionColour (int index) noexcept
    {
        // Eight hues round the wheel, low saturation and mid brightness: tints, not the chord
        // families' neon.
        static constexpr float hues[8] = { 0.60f, 0.47f, 0.76f, 0.95f, 0.10f, 0.30f, 0.53f, 0.68f };
        return juce::Colour::fromHSV (hues[((index % 8) + 8) % 8], 0.38f, 0.62f, 1.0f);
    }

    juce::Colour familyFrom (Family f) noexcept
    {
        return f == Family::Major ? night::majorFrom : (f == Family::Minor ? night::minorFrom : night::otherFrom);
    }

    juce::Colour familyTo (Family f) noexcept
    {
        return f == Family::Major ? night::majorTo : (f == Family::Minor ? night::minorTo : night::otherTo);
    }

    juce::Colour familyMid (Family f) noexcept { return familyFrom (f).interpolatedWith (familyTo (f), 0.5f); }

    juce::Font font (float height, bool bold) { return make (bold ? interSemiBold() : interRegular(), height, bold); }
    juce::Font headingFont (float height, bool bold) { return make (bold ? groteskBold() : groteskMedium(), height, bold); }

    //==========================================================================
    void fillBackground (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto r = area.toFloat();
        g.setGradientFill (juce::ColourGradient::vertical (night::bgTop, r.getY(), night::bgBottom, r.getBottom()));
        g.fillRect (area);
        // A faint cool bloom from the top left, so the surface isn't flat.
        g.setGradientFill (juce::ColourGradient (night::majorTo.withAlpha (0.07f), r.getX() + r.getWidth() * 0.15f, r.getY(),
                                                 night::majorTo.withAlpha (0.0f), r.getX() + r.getWidth() * 0.55f, r.getY() + r.getHeight() * 0.6f, true));
        g.fillRect (area);
        g.setTiledImageFill (noiseTile(), 0, 0, 1.0f);
        g.fillRect (area);
    }

    void drawGlassPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        g.setGradientFill (juce::ColourGradient::vertical (night::panel.brighter (0.06f).withAlpha (night::panelAlpha), r.getY(),
                                                          night::panel.withAlpha (night::panelAlpha), r.getBottom()));
        g.fillRoundedRectangle (r, radius);
        g.setColour (night::panelEdge.withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        // The top highlight: a 1 px line along the top edge, inside the corners.
        g.setColour (night::panelHighlight);
        g.fillRect (r.getX() + radius * 0.7f, r.getY() + 1.0f, r.getWidth() - radius * 1.4f, 1.0f);
    }

    void drawGlow (juce::Graphics& g, const juce::Path& shape, juce::Colour colour, float alpha, int blur)
    {
        if (alpha <= 0.0f)
            return;
        juce::DropShadow (colour.withAlpha (alpha), blur, {}).drawForPath (g, shape);
    }

    void drawChordOrb (juce::Graphics& g, juce::Rectangle<float> r, Family f, bool inKey, float glow, float radius)
    {
        juce::Path shape;
        shape.addRoundedRectangle (r, radius);
        drawGlow (g, shape, familyMid (f), glow);

        if (inKey)
        {
            // Filled: a radial gradient lit from the upper left, and a soft sheen along the top.
            juce::ColourGradient fill (familyFrom (f), r.getX() + r.getWidth() * 0.3f, r.getY() + r.getHeight() * 0.2f,
                                       familyTo (f), r.getRight(), r.getBottom(), true);
            g.setGradientFill (fill);
            g.fillPath (shape);
            g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::white.withAlpha (0.18f), r.getY(),
                                                              juce::Colours::white.withAlpha (0.0f), r.getY() + r.getHeight() * 0.45f));
            g.fillPath (shape);
            return;
        }

        // Hollow: a dark centre inside a 2 px gradient ring, and a dashed inner hairline.
        g.setColour (night::bgTop.withAlpha (0.92f));
        g.fillPath (shape);
        g.setGradientFill (juce::ColourGradient (familyFrom (f), r.getX(), r.getY(), familyTo (f), r.getRight(), r.getBottom(), false));
        g.strokePath (shape, juce::PathStrokeType (2.0f));
        juce::Path inner, dashed;
        inner.addRoundedRectangle (r.reduced (5.0f), juce::jmax (2.0f, radius - 4.0f));
        const float dashes[] = { 3.0f, 4.0f };
        juce::PathStrokeType (0.75f).createDashedStroke (dashed, inner, dashes, 2);
        g.setColour (familyMid (f).withAlpha (0.55f));
        g.fillPath (dashed);
    }

    //==========================================================================
    LookAndFeel::LookAndFeel()
    {
        setColourScheme ({ colours::background, colours::panel, colours::raised, colours::outline, colours::text,
                           colours::chord, colours::background, colours::raised, colours::text });
        setColour (juce::ComboBox::backgroundColourId, colours::raised);
        setColour (juce::ComboBox::outlineColourId, colours::outline);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::arrowColourId, colours::dim);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::raised.brighter (0.15f));
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedTextColourId, night::accent);
        setColour (juce::PopupMenu::headerTextColourId, colours::dim);
        setColour (juce::TextButton::buttonColourId, colours::raised);
        setColour (juce::TextButton::buttonOnColourId, colours::chord);
        setColour (juce::TextButton::textColourOffId, colours::dim);
        setColour (juce::TextButton::textColourOnId, night::bgTop);
        setColour (juce::Label::textColourId, colours::dim);
        setColour (juce::Slider::backgroundColourId, colours::raised);
        setColour (juce::Slider::trackColourId, colours::chord.withAlpha (0.55f));
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, colours::raised);
        setColour (juce::TooltipWindow::textColourId, colours::text);
        setColour (juce::TextEditor::backgroundColourId, colours::raised);
        setColour (juce::TextEditor::outlineColourId, colours::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, night::accent);
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::AlertWindow::backgroundColourId, colours::panel);
        setColour (juce::AlertWindow::textColourId, colours::text);
        setColour (juce::ListBox::backgroundColourId, colours::panel);
        setColour (juce::ScrollBar::thumbColourId, colours::faint);
    }

    juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
    {
        // The default sans (popup menus, text editors, anything not given a font): Inter.
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            if (auto t = f.isBold() ? interSemiBold() : interRegular())
                return t;
        return LookAndFeel_V4::getTypefaceForFont (f);
    }

    // A tick box (Follow, 1.2): a rounded square, amber with a dark tick when on; the label after it.
    void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
    {
        const float alpha = b.isEnabled() ? 1.0f : 0.4f;
        const auto r = b.getLocalBounds().toFloat();
        const float side = juce::jmin (14.0f, r.getHeight() - 4.0f);
        const juce::Rectangle<float> box (r.getX() + 1.0f, r.getCentreY() - side * 0.5f, side, side);
        if (b.getToggleState())
        {
            g.setColour ((down ? night::accent.brighter (0.12f) : night::accent).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (box, 3.5f);
            juce::Path tick;
            tick.startNewSubPath (box.getX() + side * 0.25f, box.getCentreY());
            tick.lineTo (box.getX() + side * 0.43f, box.getBottom() - side * 0.27f);
            tick.lineTo (box.getRight() - side * 0.22f, box.getY() + side * 0.28f);
            g.setColour (night::bgTop.withMultipliedAlpha (alpha));
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (colours::raised.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (box, 3.5f);
            g.setColour ((highlighted ? night::accent.withAlpha (0.55f) : colours::faint).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (box.reduced (0.5f), 3.5f, 1.0f);
        }
        g.setColour ((b.getToggleState() || highlighted ? colours::text : colours::dim).withMultipliedAlpha (alpha));
        g.setFont (font (12.5f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (side + 7.0f), juce::Justification::centredLeft, false);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float radius = juce::jmin (night::controlRadius, r.getHeight() * 0.5f);
        const bool on = b.getToggleState();
        const float alpha = b.isEnabled() ? 1.0f : 0.4f;
        if (on)
        {
            // Lit: the amber accent, a touch brighter at the top.
            auto c = b.findColour (juce::TextButton::buttonOnColourId);
            if (down)
                c = c.brighter (0.12f);
            g.setGradientFill (juce::ColourGradient::vertical (c.brighter (0.15f).withMultipliedAlpha (alpha), r.getY(),
                                                              c.darker (0.12f).withMultipliedAlpha (alpha), r.getBottom()));
            g.fillRoundedRectangle (r, radius);
            return;
        }
        auto c = base;
        if (down)
            c = c.brighter (0.18f);
        else if (highlighted)
            c = c.brighter (0.09f);
        g.setGradientFill (juce::ColourGradient::vertical (c.brighter (0.05f).withMultipliedAlpha (alpha), r.getY(),
                                                          c.withMultipliedAlpha (alpha), r.getBottom()));
        g.fillRoundedRectangle (r, radius);
        g.setColour ((highlighted ? night::accent.withAlpha (0.55f) : colours::outline).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, radius, 1.0f);
        g.setColour (night::panelHighlight.withMultipliedAlpha (alpha));
        g.fillRect (r.getX() + radius, r.getY() + 1.0f, r.getWidth() - 2.0f * radius, 1.0f);
    }

    void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        auto c = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (highlighted && ! b.getToggleState())
            c = colours::text;
        g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
    }

    void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
    {
        const auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        const float radius = juce::jmin (night::controlRadius, r.getHeight() * 0.5f);
        const auto bg = box.findColour (juce::ComboBox::backgroundColourId);
        g.setGradientFill (juce::ColourGradient::vertical (bg.brighter (0.05f), r.getY(), bg, r.getBottom()));
        g.fillRoundedRectangle (r, radius);
        g.setColour (box.isMouseOver (true) ? night::accent.withAlpha (0.55f) : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, radius, 1.0f);
        g.setColour (night::panelHighlight);
        g.fillRect (r.getX() + radius, r.getY() + 1.0f, r.getWidth() - 2.0f * radius, 1.0f);

        juce::Path arrow;
        const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
        arrow.startNewSubPath (ax - 3.5f, ay - 1.5f);
        arrow.lineTo (ax, ay + 2.0f);
        arrow.lineTo (ax + 3.5f, ay - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
    {
        const auto r = juce::Rectangle<int> (width, height).toFloat();
        g.fillAll (night::panel);
        g.setColour (colours::outline);
        g.drawRect (r, 1.0f);
    }

    void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        // 3 px each side, not a Label's 5: the globals row fits its longest choices at its
        // narrowest (ChordChimp's docs/KEY_DETECTION_PLAN.md K5.1).
        label.setBounds (4, 1, box.getWidth() - comboArrowWidth, box.getHeight() - 2);
        label.setBorderSize ({ 1, comboTextInset, 1, comboTextInset });
        label.setFont (getComboBoxFont (box));
    }

    void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& s)
    {
        if (style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0.0f, 0.0f, style, s);
            return;
        }
        const auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (0.5f);
        const float radius = juce::jmin (night::controlRadius, r.getHeight() * 0.5f);
        g.setColour (s.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (r, radius);
        const auto fill = r.withRight (juce::jmax (r.getX(), pos));
        const auto track = s.findColour (juce::Slider::trackColourId);
        g.setGradientFill (juce::ColourGradient::horizontal (track.withMultipliedAlpha (0.45f), r.getX(), track, juce::jmax (r.getX() + 1.0f, pos)));
        g.fillRoundedRectangle (fill, radius);
        g.setColour (s.isMouseOver (true) ? night::accent.withAlpha (0.55f) : colours::outline);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (12.5f));
        l->setColour (juce::Label::textColourId, colours::text);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return l;
    }
}
