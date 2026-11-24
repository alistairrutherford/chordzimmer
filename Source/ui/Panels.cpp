#include "Panels.h"
#include "Theme.h"

namespace chordzimmer::ui
{
    //==========================================================================
    TopBar::TopBar (ChordZimmerProcessor& p, Bindings& b, std::function<void (bool)> showKeyboard)
        : presets (p), exportHandle (p), enable (b, params::enable, "On")
    {
        enable.setTooltip ("On: chords from every key. Off: notes pass through unchanged");
        addAndMakeVisible (enable);
        addAndMakeVisible (presets);
        addAndMakeVisible (exportHandle);
        keyboardButton.setClickingTogglesState (true);
        keyboardButton.setTooltip ("Show or hide the keyboard at the bottom");
        keyboardButton.setComponentID ("ui.keys");
        keyboardButton.onClick = [this, showKeyboard] { showKeyboard (keyboardButton.getToggleState()); };
        addAndMakeVisible (keyboardButton);
    }

    void TopBar::setKeyboardShown (bool shown) { keyboardButton.setToggleState (shown, juce::dontSendNotification); }

    void TopBar::refresh()
    {
        presets.refresh();
        exportHandle.refresh();
    }

    void TopBar::paint (juce::Graphics& g)
    {
        // A darker glass strip over the background, a hairline under it.
        g.setColour (night::bgTop.withAlpha (0.55f));
        g.fillRect (getLocalBounds());
        g.setGradientFill (juce::ColourGradient::horizontal (night::panelEdge.withAlpha (0.0f), 0.0f, night::panelEdge, (float) getWidth() * 0.5f));
        g.fillRect (getLocalBounds().removeFromBottom (1));

        // The wordmark: Space Grotesk, a teal-to-violet sweep, and an amber dot.
        const auto f = headingFont (21.0f, true);
        const auto r = nameArea.toFloat();
        const float w = juce::GlyphArrangement::getStringWidth (f, "ChordZimmer");
        g.setGradientFill (juce::ColourGradient::horizontal (night::majorFrom, r.getX(), night::minorFrom, r.getX() + w));
        g.setFont (f);
        g.drawText ("ChordZimmer", nameArea, juce::Justification::centredLeft);
        g.setColour (night::accent);
        g.fillEllipse (r.getX() + w + 3.0f, r.getCentreY() + 4.0f, 5.0f, 5.0f);
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (16, 6);
        nameArea = r.removeFromLeft (148);
        enable.setBounds (r.removeFromLeft (44).withSizeKeepingCentre (44, 28));
        r.removeFromLeft (12);
        keyboardButton.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 28));
        r.removeFromRight (8);
        exportHandle.setBounds (r.removeFromRight (104).withSizeKeepingCentre (104, 28));
        r.removeFromRight (12);
        presets.setBounds (r.removeFromLeft (juce::jmin (r.getWidth(), 360)).withTrimmedBottom (2));
    }

    //==========================================================================
    HeardKey::HeardKey (ChordZimmerProcessor& p, Bindings& b) : processor (p), bindings (b)
    {
        bindings.bound (params::analyse);
        setComponentID (params::analyse);         // its Info View entry (which shows its tooltip: what it heard)
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        update();
    }

    void HeardKey::refresh (const UiModel& m)
    {
        if (m.analyse == mode && m.guess == guess && m.settings.key == key && m.settings.scale == scale)
            return;
        mode = m.analyse;
        guess = m.guess;
        key = m.settings.key;
        scale = m.settings.scale;
        update();
    }

    bool HeardKey::matchesKey() const noexcept
    {
        return guess.has() && guess.tonic == key && scaleFamily (scale) == (guess.minor ? ScaleFamily::Minor : ScaleFamily::Major);
    }

    juce::Rectangle<int> HeardKey::boxBounds() const noexcept
    {
        auto r = getLocalBounds();
        r.removeFromTop (juce::jmin (captionHeight, r.getHeight() / 2));
        return r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 26));
    }

    juce::Rectangle<int> HeardKey::closeBounds() const noexcept
    {
        return analysing() ? boxBounds().removeFromRight (closeWidth) : juce::Rectangle<int>();
    }

    bool HeardKey::fits (const juce::String& s) const
    {
        const int room = boxBounds().getWidth() - 12 - (analysing() ? closeWidth : 0) - (! analysing() && matchesKey() ? 14 : 0);
        return juce::GlyphArrangement::getStringWidthInt (font (13.0f), s) <= room;
    }

    // The text's margins and the cross, around the shortest guess ("F#m?") and the longest
    // ("Ab major 100%").
    int HeardKey::minimumWidth() { return juce::GlyphArrangement::getStringWidthInt (font (13.0f), "F#m?") + 12 + closeWidth + 1; }
    int HeardKey::preferredWidth() { return juce::GlyphArrangement::getStringWidthInt (font (13.0f), "Ab major 100%") + 12 + closeWidth + 1; }

    juce::String HeardKey::chooseText (const juce::String& full, const juce::String& shorter, const juce::String& shortest) const
    {
        return fits (full) ? full : (fits (shorter) ? shorter : shortest);
    }

    void HeardKey::update()
    {
        const juce::String name = guess.has() ? juce::String (guessKeyName (guess.tonic, guess.minor)) : juce::String();
        const juce::String longName = name + (guess.minor ? " minor" : " major"), chordName = name + (guess.minor ? "m" : "");
        const juce::String percent = juce::String (guess.fit) + "%";
        highlighted = false;
        if (! analysing())
        {
            text = matchesKey() ? chooseText (longName, chordName, chordName) : juce::String ("Analyse");
            setTooltip (matchesKey() ? "Key is already " + longName + ", as heard. Click to analyse again"
                                     : "Analyse: stop playing chords and listen to the notes coming in, to guess their key");
        }
        else if (! guess.has())
        {
            text = juce::String::fromUTF8 ("Listening\xe2\x80\xa6");
            setTooltip ("Listening: play, or let a clip play into this track. The cross goes back to chords");
        }
        else
        {
            const bool confident = guess.state == GuessState::Confident;
            text = confident ? chooseText (longName + " " + percent, chordName + " " + percent, chordName)
                             : chooseText (longName + "?", chordName + "?", chordName + "?");
            highlighted = confident;
            const Scale target = scaleForGuess (scale, guess.minor);
            juce::String tip = "Use " + longName + ": set Key to " + name;
            if (target != scale)
                tip << " and Scale to " << scaleInfo (target).name;
            tip << ", and play chords again. " << percent << " is how well the notes fit " << longName << "'s profile";
            if (guess.runnerUpTonic >= 0)
                tip << "; next: " << guessKeyName (guess.runnerUpTonic, guess.runnerUpMinor) << (guess.runnerUpMinor ? " minor " : " major ")
                    << guess.runnerUpFit << "%";
            setTooltip (tip);
        }
        repaint();
    }

    void HeardKey::resized() { update(); }   // the text's form depends on the width

    void HeardKey::paint (juce::Graphics& g)
    {
        // The caption, as a ParamControl's (a Label's text, 5 px in), with a dot while listening.
        const auto captionFont = font (12.0f);
        g.setFont (captionFont);
        g.setColour (colours::dim);
        const auto caption = getLocalBounds().removeFromTop (juce::jmin (captionHeight, getHeight() / 2)).withTrimmedLeft (5);
        g.drawText ("Heard", caption, juce::Justification::centredLeft);
        if (analysing())
        {
            g.setColour (colours::sounding);
            const float x = (float) caption.getX() + juce::GlyphArrangement::getStringWidth (captionFont, "Heard") + 5.0f;
            g.fillEllipse (x, (float) caption.getCentreY() - 2.5f, 5.0f, 5.0f);
        }

        const auto box = boxBounds().toFloat().reduced (0.5f);
        const float radius = juce::jmin (night::controlRadius, box.getHeight() * 0.5f);
        g.setGradientFill (juce::ColourGradient::vertical (colours::raised.brighter (0.05f), box.getY(), colours::raised, box.getBottom()));
        g.fillRoundedRectangle (box, radius);
        g.setColour (highlighted ? colours::chord : colours::outline);
        g.drawRoundedRectangle (box, radius, 1.0f);

        auto area = boxBounds().reduced (6, 0);
        if (analysing())
        {
            const auto x = closeBounds().toFloat().withSizeKeepingCentre (8.0f, 8.0f);
            g.setColour (colours::dim);
            g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.5f);
            g.drawLine ({ x.getBottomLeft(), x.getTopRight() }, 1.5f);
            area.removeFromRight (closeWidth - 4);
        }
        else if (matchesKey())
        {
            // A tick before the key.
            const auto t = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (10.0f, 8.0f);
            juce::Path tick;
            tick.startNewSubPath (t.getX(), t.getCentreY());
            tick.lineTo (t.getX() + 3.5f, t.getBottom());
            tick.lineTo (t.getRight(), t.getY());
            g.setColour (colours::chord.withAlpha (0.8f));
            g.strokePath (tick, juce::PathStrokeType (1.6f));
        }

        const bool dim = analysing() ? ! highlighted : matchesKey();
        g.setColour (highlighted ? colours::chord : (dim ? colours::dim : colours::text));
        g.setFont (font (13.0f, highlighted));
        g.drawText (text, area, juce::Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
        {
            g.setColour (colours::chord.withAlpha (0.5f));
            g.drawRoundedRectangle (box.reduced (1.5f), radius - 1.0f, 1.0f);
        }
    }

    void HeardKey::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && boxBounds().contains (e.getPosition()))
            click (closeBounds().contains (e.getPosition()));
    }

    bool HeardKey::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
        {
            click (false);
            return true;
        }
        if (k == juce::KeyPress::escapeKey && analysing())
        {
            click (true);
            return true;
        }
        return false;
    }

    void HeardKey::click (bool close)
    {
        if (close)
        {
            if (analysing())
                bindings.set (params::analyse, (float) params::AnalyseMode::Off);
            return;
        }
        if (! analysing())
            bindings.set (params::analyse, (float) params::AnalyseMode::Listen);
        else if (guess.has())
            processor.applyGuess();
    }

    //==========================================================================
    IconButton::IconButton (const juce::String& infoKey, Icon i) : juce::Button (infoKey), icon (i)
    {
        setComponentID (infoKey);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void IconButton::setAccented (bool shouldBe)
    {
        if (accented != shouldBe)
        {
            accented = shouldBe;
            repaint();
        }
    }

    void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        getLookAndFeel().drawButtonBackground (g, *this, colours::raised, highlighted, down);
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        juce::Path p;
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
        bool stroke = false;
        switch (icon)
        {
            case Icon::Rewind:   // a bar and a triangle pointing left
                p.addRectangle (x, y, 2.0f, h);
                p.addTriangle (x + w, y, x + w, y + h, x + 2.5f, y + h * 0.5f);
                break;
            case Icon::Left:
                p.addTriangle (x + w * 0.8f, y, x + w * 0.8f, y + h, x + w * 0.15f, y + h * 0.5f);
                break;
            case Icon::Right:
                p.addTriangle (x + w * 0.2f, y, x + w * 0.2f, y + h, x + w * 0.85f, y + h * 0.5f);
                break;
            case Icon::Home:     // a roof and a body
                p.addTriangle (x + w * 0.5f, y, x - 1.0f, y + h * 0.5f, x + w + 1.0f, y + h * 0.5f);
                p.addRectangle (x + w * 0.18f, y + h * 0.5f, w * 0.64f, h * 0.5f);
                break;
            case Icon::Undo:
            case Icon::Redo:     // a curved arrow
            {
                const bool undo = icon == Icon::Undo;
                juce::Path arc;
                arc.addCentredArc (x + w * 0.5f, y + h * 0.62f, w * 0.38f, h * 0.36f, 0.0f,
                                   undo ? -juce::MathConstants<float>::halfPi : juce::MathConstants<float>::halfPi,
                                   undo ? juce::MathConstants<float>::halfPi * 1.6f : -juce::MathConstants<float>::halfPi * 1.6f, true);
                g.setColour ((isEnabled() ? colours::text : colours::faint));
                g.strokePath (arc, juce::PathStrokeType (1.6f));
                const float tipX = undo ? x + w * 0.12f : x + w * 0.88f;
                p.addTriangle (tipX, y + h * 0.62f - 4.0f, tipX, y + h * 0.62f + 4.0f, undo ? tipX - 4.0f : tipX + 4.0f, y + h * 0.62f);
                stroke = true;
                break;
            }
        }
        juce::ignoreUnused (stroke);
        g.setColour (! isEnabled() ? colours::faint : (accented ? night::accent : colours::text));
        g.fillPath (p);
    }

    //==========================================================================
    NowDisplay::NowDisplay() { setComponentID ("display.now"); }

    void NowDisplay::set (const juce::String& n, const juce::String& num, const juce::String& w)
    {
        if (n == name && num == numeral && w == where)
            return;
        name = n;
        numeral = num;
        where = w;
        repaint();
    }

    void NowDisplay::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().reduced (4, 0);
        g.setFont (font (11.5f));
        g.setColour (colours::dim);
        // Where the playhead is, when there's room for it next to the chord.
        if (r.getWidth() >= 150)
            g.drawText (where, r.removeFromRight (juce::GlyphArrangement::getStringWidthInt (font (11.5f), "Bar 999.4") + 6), juce::Justification::centredRight);
        if (name.isEmpty())
        {
            g.setColour (colours::faint);
            g.setFont (font (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94"), r, juce::Justification::centredRight);
            return;
        }
        const auto numeralFont = headingFont (15.0f, false);
        const int numeralWidth = juce::GlyphArrangement::getStringWidthInt (numeralFont, numeral) + 10;
        g.setFont (numeralFont);
        g.setColour (night::accent);
        g.drawText (numeral, r.removeFromRight (numeralWidth), juce::Justification::centredRight);
        g.setFont (headingFont (20.0f, true));
        g.setColour (colours::text);
        g.drawFittedText (name, r, juce::Justification::centredRight, 1, 0.7f);
    }

    //==========================================================================
    TransportBar::TransportBar (ChordZimmerProcessor& p, Bindings& b)
        : rewind ("ui.rewind", IconButton::Icon::Rewind), undo ("ui.undo", IconButton::Icon::Undo), redo ("ui.redo", IconButton::Icon::Redo),
          play (b, params::play, "Play"), loop (b, params::loop, "Loop"), syncMode (b, params::syncMode, "Mode"),
          startBar (b, params::startBar, "Start bar"), processor (p)
    {
        rewind.onClick = [this] { processor.rewind(); };
        undo.onClick = [this] { if (onUndo) onUndo(); };
        redo.onClick = [this] { if (onRedo) onRedo(); };
        syncMode.setCaptionLeft (40);
        startBar.setCaptionLeft (58);
        snap.addItemList ({ "Off", "1/16", "1/8", "1/4", "1/2", "Bar" }, 1);
        snap.setComponentID ("ui.snap");
        snap.setSelectedId ((int) defaultSnap + 1, juce::dontSendNotification);
        snap.onChange = [this] { if (onSnap) onSnap (getSnap()); };
        snapCaption.setText ("Snap", juce::dontSendNotification);
        snapCaption.setColour (juce::Label::textColourId, colours::dim);
        snapCaption.setInterceptsMouseClicks (false, false);
        for (auto* c : std::initializer_list<juce::Component*> { &rewind, &play, &loop, &syncMode, &startBar, &snapCaption, &snap, &undo, &redo, &now })
            addAndMakeVisible (c);
        setUndoState ({});
    }

    void TransportBar::setUndoState (const ChordZimmerProcessor::UndoState& u)
    {
        undo.setEnabled (u.canUndo);
        redo.setEnabled (u.canRedo);
    }

    void TransportBar::setSnap (Snap s) { snap.setSelectedId ((int) s + 1, juce::dontSendNotification); }
    Snap TransportBar::getSnap() const noexcept { return (Snap) juce::jlimit (0, (int) Snap::Bar, snap.getSelectedId() - 1); }

    void TransportBar::resized()
    {
        auto r = getLocalBounds();
        const auto place = [&r] (juce::Component& c, int w, int after = 6)
        {
            c.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (r.getHeight(), 28)));
            r.removeFromLeft (after);
        };
        place (rewind, 32, 4);
        place (play, 58, 4);
        place (loop, 58, 14);
        place (syncMode, 40 + 92, 12);
        place (startBar, 58 + 64, 12);
        place (snapCaption, 32, 0);
        place (snap, 70, 12);
        place (undo, 30, 4);
        place (redo, 30, 12);
        now.setBounds (r);
    }

    //==========================================================================
    PerformancePanel::PerformancePanel (ChordZimmerProcessor& p, Bindings& b) : heard (p, b), bindings (b)
    {
        auto& strum = cards[0];
        strum.title = "Strum";
        strum.infoKey = "card.strum";
        strum.controls = { &add (params::strum, "Direction"), &add (params::strumRate, "Rate") };

        auto& velocity = cards[1];
        velocity.title = "Velocity";
        velocity.infoKey = "card.velocity";
        velocity.controls = { &add (params::velocity, "Curve"), &add (params::velocityPeak, "Peak"), &add (params::velocityRange, "Range") };

        auto& voicing = cards[2];
        voicing.title = "Voicing";
        voicing.infoKey = "card.voicing";
        voicing.controls = { &add (params::voicing, "Voicing"), &add (params::bass), &add (params::voiceLeading, "Voice leading"),
                             &add (params::chordOctave, "Octave"), &add (params::stack) };

        auto& keys = cards[3];
        keys.title = "Keys in";
        keys.infoKey = "card.keys";
        addAndMakeVisible (heard);
        keys.controls = { &add (params::chordGroup, "Chord Group"), &add (params::progression), &add (params::hold),
                          &add (params::keyOctave, "Key octave"), &heard, &add (params::detect) };
    }

    ParamControl& PerformancePanel::add (const juce::String& paramId, const juce::String& label)
    {
        auto* c = controls.add (new ParamControl (bindings, paramId, label));
        addAndMakeVisible (c);
        return *c;
    }

    juce::String PerformancePanel::infoKeyAt (juce::Point<int> p) const
    {
        for (const auto& c : cards)
            if (c.bounds.contains (p))
                return c.infoKey;
        return {};
    }

    void PerformancePanel::refresh (const UiModel& m) { heard.refresh (m); }

    int PerformancePanel::preferredHeight()
    {
        const int card = titleHeight + controlHeight + 14;
        return 2 * margin + 3 * card + 2 * gap;
    }

    void PerformancePanel::paint (juce::Graphics& g)
    {
        // A popover: a darker, nearly opaque card with a shadow, over the palette.
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        juce::Path shape;
        shape.addRoundedRectangle (r, night::panelRadius);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 16, { 0, 4 }).drawForPath (g, shape);
        g.setColour (night::bgTop.withAlpha (0.97f));
        g.fillPath (shape);
        g.setColour (night::accent.withAlpha (0.35f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        for (const auto& c : cards)
        {
            drawGlassPanel (g, c.bounds.toFloat(), 10.0f);
            g.setColour (colours::dim);
            g.setFont (headingFont (11.5f, true));
            g.drawText (c.title.toUpperCase(), c.bounds.reduced (12, 0).removeFromTop (titleHeight).translated (0, 3), juce::Justification::centredLeft);
        }
    }

    void PerformancePanel::layoutCard (Card& c)
    {
        auto r = c.bounds.reduced (8, 0);
        r.removeFromTop (titleHeight + 2);
        auto row = r.removeFromTop (controlHeight);
        const int w = row.getWidth() / juce::jmax (1, (int) c.controls.size());
        for (auto* control : c.controls)
            control->setBounds (row.removeFromLeft (w).reduced (3, 0));
    }

    void PerformancePanel::resized()
    {
        auto r = getLocalBounds().reduced (margin);
        const int card = titleHeight + controlHeight + 14;
        auto line = r.removeFromTop (card);
        cards[0].bounds = line.removeFromLeft ((line.getWidth() - gap) * 2 / 5);
        line.removeFromLeft (gap);
        cards[1].bounds = line;
        r.removeFromTop (gap);
        cards[2].bounds = r.removeFromTop (card);
        r.removeFromTop (gap);
        cards[3].bounds = r.removeFromTop (card);
        for (auto& c : cards)
            layoutCard (c);
    }
}
