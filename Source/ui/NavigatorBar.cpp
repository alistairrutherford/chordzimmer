#include "NavigatorBar.h"

namespace chordzimmer::ui
{
    //==========================================================================
    FifthsDial::FifthsDial()
    {
        setComponentID ("display.dial");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void FifthsDial::set (int h, int p, int s)
    {
        if (h == home && p == palette && s == steps)
            return;
        home = h;
        palette = p;
        steps = s;
        repaint();
    }

    juce::Point<float> FifthsDial::dotCentre (int pc) const
    {
        // C at the top, then clockwise by fifths (G, D, A …).
        const auto r = getLocalBounds().toFloat().reduced (4.0f);
        const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        const int position = (((pc % 12) + 12) % 12) * 7 % 12;
        const float angle = juce::MathConstants<float>::twoPi * (float) position / 12.0f;
        return r.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius;
    }

    void FifthsDial::paint (juce::Graphics& g)
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        const float radius = dotCentre (0).getDistanceFrom (centre);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f);

        // The arc from home to the palette's key, the way the navigator went.
        if (steps != 0)
        {
            const float start = juce::MathConstants<float>::twoPi * (float) ((home * 7) % 12) / 12.0f;
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, start + juce::MathConstants<float>::twoPi * (float) steps / 12.0f, true);
            g.setColour (night::accent.withAlpha (0.8f));
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        for (int pc = 0; pc < 12; ++pc)
        {
            const auto c = dotCentre (pc);
            if (pc == palette)
            {
                g.setColour (night::majorFrom.withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c));
                g.setColour (night::majorFrom);
                g.fillEllipse (juce::Rectangle<float> (5.5f, 5.5f).withCentre (c));
            }
            if (pc == home)
            {
                g.setColour (night::accent);
                if (pc == palette)
                    g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c), 1.2f);
                else
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
            }
            else if (pc != palette)
            {
                g.setColour (colours::dim.withAlpha (0.7f));
                g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre (c));
            }
        }
    }

    void FifthsDial::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasClicked() || ! onPick)
            return;
        int best = 0;
        for (int pc = 1; pc < 12; ++pc)
            if (dotCentre (pc).getDistanceFrom (e.position) < dotCentre (best).getDistanceFrom (e.position))
                best = pc;
        onPick (best);
    }

    //==========================================================================
    NavigatorBar::NavigatorBar (Bindings& b, CircleOfFifths& n)
        : key (b, params::key, "Home"), scale (b, params::scale, {}, false),
          home ("ui.home", IconButton::Icon::Home), down ("ui.fifthDown", IconButton::Icon::Left), up ("ui.fifthUp", IconButton::Icon::Right), nav (n)
    {
        key.setCaptionLeft (42);
        home.onClick = [this] { goHome(); };
        down.onClick = [this] { stepFifth (-1); };
        up.onClick = [this] { stepFifth (+1); };
        mode.setComponentID ("ui.mode");
        mode.onChange = [this]
        {
            if (const int id = mode.getSelectedId(); id > 0)
                setMode ((Scale) (id - 1));
        };
        dial.onPick = [this] (int pc) { pickKey (pc); };
        performance.setComponentID ("ui.performance");
        performance.setClickingTogglesState (false);
        performance.onClick = [this] { if (onPerformance) onPerformance(); };
        for (auto* c : std::initializer_list<juce::Component*> { &key, &scale, &home, &down, &up, &mode, &dial, &performance })
            addAndMakeVisible (c);
        refresh();
    }

    void NavigatorBar::refresh()
    {
        home.setAccented (! nav.atHome());
        dial.set (nav.homeKey, nav.paletteKey(), nav.distance());
        if (modeFamily (nav.paletteScale) != listedFamily)
        {
            listedFamily = modeFamily (nav.paletteScale);
            mode.clear (juce::dontSendNotification);
            for (const Scale s : nav.modes())
                mode.addItem (scaleInfo (s).name, (int) s + 1);
        }
        mode.setSelectedId ((int) nav.paletteScale + 1, juce::dontSendNotification);
        repaint (keyLabel);
    }

    void NavigatorBar::stepFifth (int direction)
    {
        nav.step (direction);
        refresh();
        if (onChange)
            onChange();
    }

    void NavigatorBar::goHome()
    {
        nav.goHome();
        refresh();
        if (onChange)
            onChange();
    }

    void NavigatorBar::setMode (Scale s)
    {
        if (s == nav.paletteScale)
            return;
        nav.setMode (s);
        refresh();
        if (onChange)
            onChange();
    }

    void NavigatorBar::pickKey (int pc)
    {
        nav.fifths = fifthsBetween (nav.homeKey, pc);
        refresh();
        if (onChange)
            onChange();
    }

    void NavigatorBar::setPerformanceShown (bool shown) { performance.setToggleState (shown, juce::dontSendNotification); }

    void NavigatorBar::paint (juce::Graphics& g)
    {
        drawGlassPanel (g, getLocalBounds().toFloat(), 12.0f);
        // The palette's key and its steps from home, between ◀ and ▶.
        const Spelling sp = makeSpelling (nav.paletteKey(), nav.paletteScale);
        const int steps = nav.distance();
        auto r = keyLabel;
        g.setColour (nav.atHome() ? colours::text : night::accent);
        g.setFont (headingFont (17.0f, true));
        g.drawText (juce::String (sp.name (nav.paletteKey())), r.removeFromTop (r.getHeight() * 3 / 5), juce::Justification::centredBottom);
        g.setColour (colours::dim);
        g.setFont (font (10.0f));
        g.drawText (steps == 0 ? juce::String ("home") : (steps > 0 ? "+" : "") + juce::String (steps), r, juce::Justification::centredTop);

        // A hairline between the home key and the navigator.
        g.setColour (colours::outline);
        g.fillRect (home.getX() - 8, getHeight() / 2 - 10, 1, 20);
    }

    void NavigatorBar::resized()
    {
        auto r = getLocalBounds().reduced (10, 6);
        const auto place = [&r] (juce::Component& c, int w, int after)
        {
            c.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (r.getHeight(), 28)));
            r.removeFromLeft (after);
        };
        place (key, 42 + 60, 6);
        place (scale, juce::jlimit (120, 170, getWidth() / 6), 16);
        place (home, 30, 6);
        place (down, 28, 2);
        keyLabel = r.removeFromLeft (46);
        r.removeFromLeft (2);
        place (up, 28, 8);
        place (mode, juce::jlimit (118, 160, getWidth() / 6), 10);
        dial.setBounds (r.removeFromLeft (r.getHeight() + 4).withSizeKeepingCentre (r.getHeight() + 2, r.getHeight() + 2));
        performance.setBounds (r.removeFromRight (112).withSizeKeepingCentre (112, juce::jmin (r.getHeight(), 28)));
    }
}
