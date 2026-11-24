#include "TimelinePanel.h"

namespace chordzimmer::ui
{
    namespace
    {
        const char* const sectionColourNames[numSectionColours] = { "Blue", "Teal", "Violet", "Rose", "Amber", "Green", "Cyan", "Indigo" };

        juce::String lengthText (int ticks, int beatsPerBar)
        {
            const int bar = ticksPerBeat * juce::jmax (1, beatsPerBar);
            if (ticks % bar == 0)
                return juce::String (ticks / bar) + (ticks == bar ? " bar" : " bars");
            const juce::String beats = juce::String (ticks / (double) ticksPerBeat, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return beats + (ticks == ticksPerBeat ? " beat" : " beats");
        }
    }

    TimelinePanel::TimelinePanel (ChordZimmerProcessor& p, Bindings& b) : processor (p), bindings (b)
    {
        setComponentID ("display.timeline");
        setWantsKeyboardFocus (true);
        scrollBar.setAutoHide (true);
        scrollBar.addListener (this);
        addAndMakeVisible (scrollBar);
        nameEditor.setFont (headingFont (12.5f, true));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setColour (juce::TextEditor::backgroundColourId, night::bgTop);
        nameEditor.setColour (juce::TextEditor::outlineColourId, night::accent);
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, night::accent);
        nameEditor.setColour (juce::TextEditor::textColourId, colours::text);
        nameEditor.setInputRestrictions (maxSectionNameBytes);
        nameEditor.onReturnKey = [this] { finishRenaming (true); };
        nameEditor.onEscapeKey = [this] { finishRenaming (false); };
        nameEditor.onFocusLost = [this] { finishRenaming (true); };
        addChildComponent (nameEditor);
        pullProgression();
    }

    TimelinePanel::~TimelinePanel()
    {
        endPreview();
        scrollBar.removeListener (this);
    }

    //==========================================================================
    // Drags from the palette carry their chord as text.

    juce::String TimelinePanel::chordDragDescription (const ProgressionChord& c)
    {
        ChordProgression one;
        one.sections.push_back ({ "Chord", 0, { c } });
        return "czchord:" + juce::String (progressionToText (one));
    }

    bool TimelinePanel::chordFromDragDescription (const juce::var& description, ProgressionChord& out)
    {
        const auto text = description.toString();
        if (! text.startsWith ("czchord:"))
            return false;
        ChordProgression one;
        if (! progressionFromText (text.substring (8).toStdString(), one) || one.numChords() != 1)
            return false;
        out = one.sections[0].chords[0];
        return true;
    }

    //==========================================================================
    void TimelinePanel::refresh (const UiModel& m)
    {
        bool dirty = false;
        if (processor.progressionVersion() != modelVersion && drag == Drag::None)
            pullProgression();
        if (m.settings.key != homeKey || m.settings.scale != homeScale || m.beatsPerBar != beatsPerBar)
        {
            homeKey = m.settings.key;
            homeScale = m.settings.scale;
            beatsPerBar = juce::jlimit (1, 16, m.beatsPerBar);
            dirty = true;
        }
        const bool loop = bindings.value (params::loop) > 0.5f;
        if (loop != loopOn)
        {
            loopOn = loop;
            dirty = true;
        }
        if (m.playhead != playhead || m.playingChord != playingChord)
        {
            playhead = m.playhead;
            playingChord = m.playingChord;
            followPlayhead();
            dirty = true;
        }
        if (dirty)
            repaint();
    }

    void TimelinePanel::pullProgression()
    {
        modelVersion = processor.progressionVersion();
        model = processor.getProgression();
        selected = model.normalised (selected);
        insertAt = clampInsertPoint (model, insertAt);
        if (renaming >= model.numSections())
            finishRenaming (false);
        updateScrollBar();
        repaint();
    }

    void TimelinePanel::pushLoopSelection() { processor.setLoopSelection (selected); }

    void TimelinePanel::setSelection (const Selection& sel)
    {
        selected = model.normalised (sel);
        insertAt = markerAfter (model, selected);
        pushLoopSelection();
        repaint();
    }

    void TimelinePanel::setMarker (InsertPoint at)
    {
        insertAt = clampInsertPoint (model, at);
        repaint();
    }

    bool TimelinePanel::edit (const juce::String& label, const std::function<bool (ChordProgression&, Selection&)>& change)
    {
        Selection next = selected;
        const bool did = processor.editProgression (label.toStdString(), [&] (ChordProgression& q) { return change (q, next); });
        if (! did)
            return false;
        pullProgression();
        setSelection (next);
        return true;
    }

    //==========================================================================
    // Actions.

    // A chord put in selects itself, also when it made a new section, so the marker goes after
    // it and the next one joins it there.
    static bool insertSelectingChord (ChordProgression& q, InsertPoint at, const ProgressionChord& c, Selection& sel)
    {
        if (! q.insertChord (at, c, &sel))
            return false;
        if (! sel.sections.empty())
            sel = Selection::chord (sel.sections.front(), 0);
        return true;
    }

    bool TimelinePanel::addChord (const ProgressionChord& c)
    {
        const InsertPoint at = insertAt;
        return edit ("Add chord", [&] (ChordProgression& q, Selection& sel) { return insertSelectingChord (q, at, c, sel); });
    }

    bool TimelinePanel::addChordAsSection (const ProgressionChord& c)
    {
        return edit ("Add section", [&] (ChordProgression& q, Selection& sel) { return insertSelectingChord (q, InsertPoint::newSection (q.numSections()), c, sel); });
    }

    bool TimelinePanel::dropChordAt (const ProgressionChord& c, float x)
    {
        const InsertPoint at = dropPoint (layout(), x);
        return edit ("Add chord", [&] (ChordProgression& q, Selection& sel) { return insertSelectingChord (q, at, c, sel); });
    }

    bool TimelinePanel::deleteSelection()
    {
        const Selection sel = selected;
        const InsertPoint before = markerAfter (model, sel);
        const bool did = edit ("Delete", [&] (ChordProgression& q, Selection& out)
        {
            out = {};
            return q.remove (sel);
        });
        if (did)
            setMarker (before);
        return did;
    }

    ChordRef TimelinePanel::selectedChord() const noexcept
    {
        return selected.chords.empty() ? ChordRef {} : selected.chords.front();
    }

    bool TimelinePanel::replaceSelectedChord (const ProgressionChord& c)
    {
        const ChordRef r = selectedChord();
        if (! model.valid (r) || c.chord.empty())
            return false;
        return edit ("Set chord", [&] (ChordProgression& q, Selection&)
        {
            const bool notes = q.replaceChord (r, c.chord);
            const bool b = q.setBass (r, c.bass);
            return notes || b;
        });
    }

    bool TimelinePanel::copySelection()
    {
        if (selected.empty())
            return false;
        clipboard = model.copy (selected);
        return ! clipboard.empty();
    }

    bool TimelinePanel::cutSelection() { return copySelection() && deleteSelection(); }

    bool TimelinePanel::paste()
    {
        if (clipboard.empty())
            return false;
        const InsertPoint at = insertAt;
        const ProgressionClip clip = clipboard;
        return edit ("Paste", [&] (ChordProgression& q, Selection& sel) { return q.insert (at, clip, &sel); });
    }

    bool TimelinePanel::duplicateSelection()
    {
        const Selection sel = selected;
        return edit ("Duplicate", [&] (ChordProgression& q, Selection& out) { return q.duplicateInPlace (sel, &out); });
    }

    void TimelinePanel::step (int direction)
    {
        setSelection (stepSelection (model, selected, direction));
        // Keep it in view.
        const auto l = layout();
        const auto r = model.range (selected);
        if (! selected.empty())
        {
            const double x0 = l.xOfTick (r.start), x1 = l.xOfTick (r.end, true);
            const auto view = chordArea();
            if (x0 < view.getX())
                scrollTo (scroll - (view.getX() - x0) - 20.0);
            else if (x1 > view.getRight())
                scrollTo (scroll + (x1 - view.getRight()) + 20.0);
        }
    }

    void TimelinePanel::setPixelsPerBeat (double p)
    {
        ppb = juce::jlimit (TimelineGeometry::minPixelsPerBeat, TimelineGeometry::maxPixelsPerBeat, p);
        updateScrollBar();
        repaint();
    }

    void TimelinePanel::zoom (double factor)
    {
        const double anchor = chordArea().getCentreX();
        const auto z = zoomAround (geometry(), factor, anchor);
        ppb = z.pixelsPerBeat;
        scroll = 0.0;
        scrollTo (geometry().origin - z.origin);
        if (onZoom)
            onZoom (ppb);
    }

    bool TimelinePanel::renameSection (int s, const juce::String& name)
    {
        return edit ("Rename section", [&] (ChordProgression& q, Selection&) { return q.renameSection (s, name.toStdString()); });
    }

    bool TimelinePanel::setSectionColour (int s, int colour)
    {
        return edit ("Section colour", [&] (ChordProgression& q, Selection&) { return q.setSectionColour (s, colour); });
    }

    bool TimelinePanel::setInversion (ChordRef r, int v)
    {
        return edit ("Inversion", [&] (ChordProgression& q, Selection&) { return q.setInversion (r, v); });
    }

    bool TimelinePanel::setOctave (ChordRef r, int v)
    {
        return edit ("Octave", [&] (ChordProgression& q, Selection&) { return q.setOctave (r, v); });
    }

    bool TimelinePanel::setBass (ChordRef r, int pc)
    {
        return edit ("Bass note", [&] (ChordProgression& q, Selection&) { return q.setBass (r, pc); });
    }

    void TimelinePanel::startRenaming (int s)
    {
        if (! model.validSection (s))
            return;
        renaming = s;
        nameEditor.setBounds (tabBounds (s).toNearestInt().withWidth (juce::jmax (120, (int) tabBounds (s).getWidth())).expanded (0, 1));
        nameEditor.setText (juce::String::fromUTF8 (model.sections[(size_t) s].name.c_str()), juce::dontSendNotification);
        nameEditor.setVisible (true);
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
    }

    void TimelinePanel::finishRenaming (bool keep)
    {
        if (renaming < 0)
            return;
        const int s = renaming;
        renaming = -1;
        const auto text = nameEditor.getText();
        nameEditor.setVisible (false);
        if (keep)
            renameSection (s, text);
        grabKeyboardFocus();
    }

    void TimelinePanel::endPreview()
    {
        if (previewing)
            processor.endPreview();
        previewing = false;
    }

    //==========================================================================
    // Geometry.

    juce::Rectangle<int> TimelinePanel::rulerArea() const
    {
        return getLocalBounds().reduced (padding).withTrimmedTop (titleHeight).removeFromTop (rulerHeight);
    }

    juce::Rectangle<int> TimelinePanel::tabArea() const
    {
        return getLocalBounds().reduced (padding).withTrimmedTop (titleHeight + rulerHeight).removeFromTop (tabHeight);
    }

    juce::Rectangle<int> TimelinePanel::chordArea() const
    {
        return getLocalBounds().reduced (padding).withTrimmedTop (titleHeight + rulerHeight + tabHeight).withTrimmedBottom (scrollHeight + 2);
    }

    TimelineGeometry TimelinePanel::geometry() const
    {
        TimelineGeometry g;
        g.pixelsPerBeat = ppb;
        g.origin = chordArea().getX() + 8.0 - scroll;
        return g;
    }

    TimelineLayout TimelinePanel::layout() const { return TimelineLayout::make (shown(), geometry()); }

    juce::Rectangle<float> TimelinePanel::chordBounds (ChordRef r) const
    {
        const auto l = layout();
        const auto* span = l.chordSpan (r);
        if (span == nullptr)
            return {};
        const auto area = chordArea().toFloat();
        return { (float) span->x0, area.getY(), (float) span->width(), area.getHeight() };
    }

    juce::Rectangle<float> TimelinePanel::tabBounds (int s) const
    {
        const auto l = layout();
        if (s < 0 || s >= l.numSections())
            return {};
        const auto area = tabArea().toFloat();
        return { (float) l.sections[(size_t) s].x0, area.getY(), (float) l.sections[(size_t) s].width(), area.getHeight() };
    }

    TimelineHit TimelinePanel::hitAt (juce::Point<float> p) const
    {
        const auto view = getLocalBounds().reduced (padding).toFloat();
        if (! view.contains (p))
            return {};
        if (tabArea().toFloat().contains (p))
            return chordzimmer::hitTest (layout(), p.x, TimelineRow::Sections);
        if (chordArea().toFloat().contains (p))
            return chordzimmer::hitTest (layout(), p.x, TimelineRow::Chords);
        return {};
    }

    TimelineChordLabel TimelinePanel::labelFor (const ProgressionChord& c) const { return timelineChordLabel (c, homeKey, homeScale); }

    //==========================================================================
    // Scrolling.

    void TimelinePanel::updateScrollBar()
    {
        const double content = layout().width() + 16.0 + 60.0;    // room after the end to drop a new section
        const double visible = chordArea().getWidth();
        scroll = juce::jlimit (0.0, juce::jmax (0.0, content - visible), scroll);
        scrollBar.setRangeLimits (0.0, juce::jmax (content, visible), juce::dontSendNotification);
        scrollBar.setCurrentRange (scroll, visible, juce::dontSendNotification);
    }

    void TimelinePanel::scrollTo (double x)
    {
        scroll = x;
        updateScrollBar();
        repaint();
    }

    void TimelinePanel::scrollBarMoved (juce::ScrollBar*, double start)
    {
        scroll = start;
        repaint();
    }

    void TimelinePanel::followPlayhead()
    {
        if (playhead < 0.0 || model.empty() || drag != Drag::None)
            return;
        const double x = layout().xOfBeat (std::fmod (playhead, juce::jmax (1.0e-9, model.totalBeats())));
        const auto view = chordArea();
        if (x < view.getX() || x > view.getRight() - 4)
            scrollTo (scroll + (x - view.getX()) - view.getWidth() * 0.15);
    }

    void TimelinePanel::resized()
    {
        const auto r = getLocalBounds().reduced (padding);
        scrollBar.setBounds (r.withTop (r.getBottom() - scrollHeight));
        updateScrollBar();
        if (renaming >= 0)
            startRenaming (renaming);
    }

    //==========================================================================
    // Painting.

    void TimelinePanel::paint (juce::Graphics& g)
    {
        drawGlassPanel (g, getLocalBounds().toFloat());
        auto inner = getLocalBounds().reduced (padding);
        const auto& prog = shown();
        const auto l = layout();

        // Title, and what's there.
        g.setColour (colours::dim);
        g.setFont (headingFont (11.5f, true));
        g.drawText ("TIMELINE", inner.withHeight (titleHeight).withTrimmedLeft (4), juce::Justification::centredLeft);
        if (! prog.empty())
        {
            g.setFont (font (11.0f));
            g.drawText (juce::String (prog.numSections()) + (prog.numSections() == 1 ? " section, " : " sections, ") + juce::String (prog.numChords())
                            + (prog.numChords() == 1 ? " chord, " : " chords, ") + lengthText (prog.totalTicks(), beatsPerBar),
                        getLocalBounds().reduced (padding).removeFromTop (titleHeight).withTrimmedRight (4), juce::Justification::centredRight);
        }

        const auto ruler = rulerArea(), tabs = tabArea(), chords = chordArea();
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (inner.withTrimmedBottom (scrollHeight + 2));

        if (prog.empty())
        {
            g.setColour (colours::faint);
            g.setFont (font (12.5f));
            g.drawFittedText (juce::String::fromUTF8 ("Drag chords here from the palette, or double-click one. Click empty space to set the insertion marker."),
                              chords.withTrimmedTop (-tabHeight).reduced (8, 0), juce::Justification::centred, 2);
        }

        // The ruler: bar numbers and beat ticks, per section.
        const double ppt = l.geometry.pixelsPerTick();
        const int labelEvery = juce::jmax (1, (int) std::ceil (30.0 / (ppb * beatsPerBar)));
        g.setFont (font (9.5f));
        for (int s = 0; s < l.numSections(); ++s)
        {
            const int start = l.sectionTicks[(size_t) s], end = start + prog.sections[(size_t) s].ticks();
            for (int t = ((start + ticksPerBeat - 1) / ticksPerBeat) * ticksPerBeat; t < end; t += ticksPerBeat)
            {
                const float x = (float) (l.sections[(size_t) s].x0 + (t - start) * ppt);
                const int beat = t / ticksPerBeat;
                const bool barLine = beat % beatsPerBar == 0;
                if (! barLine && ppb < 8.0)
                    continue;
                g.setColour (colours::outline.withAlpha (barLine ? 1.0f : 0.6f));
                g.fillRect (x, (float) ruler.getBottom() - (barLine ? 6.0f : 3.0f), 1.0f, barLine ? 6.0f : 3.0f);
                if (barLine && (beat / beatsPerBar) % labelEvery == 0)
                {
                    g.setColour (colours::dim);
                    g.drawText (juce::String (beat / beatsPerBar + 1), juce::Rectangle<float> (x + 2.0f, (float) ruler.getY(), 30.0f, (float) ruler.getHeight() - 3.0f),
                                juce::Justification::centredLeft);
                }
            }
        }

        // The loop region (PLAN.md §5.1: an amber-tinted band): on the ruler, and behind the
        // selection.
        if (loopOn && ! prog.empty())
        {
            const auto range = prog.range (selected);
            const float x0 = (float) l.xOfTick (range.start), x1 = (float) l.xOfTick (range.end, true);
            g.setColour (night::accent.withAlpha (0.75f));
            g.fillRoundedRectangle (x0, (float) ruler.getBottom() - 2.0f, x1 - x0, 2.0f, 1.0f);
            if (! selected.empty())
            {
                g.setColour (night::accent.withAlpha (0.06f));
                g.fillRect (juce::Rectangle<float> (x0, (float) tabs.getY(), x1 - x0, (float) (chords.getBottom() - tabs.getY())));
            }
        }

        // Sections: a tab in the section's colour, then the chords as pills.
        const unsigned homeMask = scalePitchClasses (homeKey, homeScale);
        const double beatPhase = playhead >= 0.0 ? playhead - std::floor (playhead) : 0.0;
        const float pulse = (float) (1.0 - beatPhase) * (float) (1.0 - beatPhase);   // eased: bright on the beat
        const ChordRef sounding = chordAtOrder (prog, playingChord);
        const bool moving = drag == Drag::Move && ! dragCopy;
        for (int s = 0; s < l.numSections(); ++s)
        {
            const auto& sec = prog.sections[(size_t) s];
            const auto span = l.sections[(size_t) s];
            const auto tab = juce::Rectangle<float> ((float) span.x0, (float) tabs.getY(), (float) span.width(), (float) tabs.getHeight() - 2.0f);
            const auto hue = sectionColour (sec.colour);
            const bool secSelected = isSectionSelected (selected, s);
            const float fade = moving && secSelected ? 0.45f : 1.0f;

            juce::Path tabShape;
            tabShape.addRoundedRectangle (tab.getX(), tab.getY(), tab.getWidth(), tab.getHeight(), 6.0f, 6.0f, true, true, false, false);
            g.setColour (hue.withAlpha (0.34f * fade));
            g.fillPath (tabShape);
            // The lane: a faint wash of the section's colour behind its chords.
            g.setColour (hue.withAlpha (0.07f * fade));
            g.fillRect (juce::Rectangle<float> ((float) span.x0, (float) chords.getY(), (float) span.width(), (float) chords.getHeight()));
            g.setColour (hue.withAlpha (0.9f * fade));
            g.fillRect (tab.getX(), tab.getBottom() - 1.0f, tab.getWidth(), 1.0f);
            if (secSelected)
            {
                g.setColour (night::accent);
                g.strokePath (tabShape, juce::PathStrokeType (1.5f));
            }
            if (renaming != s && tab.getWidth() > 14.0f)
            {
                g.setColour (colours::text.withAlpha (fade));
                g.setFont (headingFont (12.0f, true));
                g.drawText (juce::String::fromUTF8 (sec.name.c_str()), tab.reduced (7.0f, 0.0f), juce::Justification::centredLeft, true);
            }

            for (int i = 0; i < (int) sec.chords.size(); ++i)
            {
                const auto& c = sec.chords[(size_t) i];
                const ChordRef ref { s, i };
                const auto span2 = l.chords[(size_t) s][(size_t) i];
                const auto pill = juce::Rectangle<float> ((float) span2.x0, (float) chords.getY(), (float) span2.width(), (float) chords.getHeight())
                                      .reduced (juce::jmin (1.5f, (float) span2.width() * 0.2f), 4.0f);
                const auto family = chordFamily (c.pitchClasses(), c.root());
                const bool inKey = (c.pitchClasses() & ~homeMask) == 0;
                const bool chosen = isChordSelected (selected, ref);
                const bool lit = ref == sounding;
                const float alpha = (moving && chosen ? 0.4f : 1.0f);

                juce::Path shape;
                const float radius = juce::jmin (night::controlRadius, pill.getWidth() * 0.5f);
                shape.addRoundedRectangle (pill, radius);
                if (lit)
                    drawGlow (g, shape, familyMid (family), (night::glowIdle + (night::glowPlay - night::glowIdle) * pulse) * alpha, 14);
                if (inKey)
                {
                    g.setGradientFill (juce::ColourGradient::horizontal (familyFrom (family).withAlpha (0.7f * alpha), pill.getX(),
                                                                        familyTo (family).withAlpha (0.7f * alpha), pill.getRight()));
                    g.fillPath (shape);
                }
                else
                {
                    g.setColour (night::bgTop.withAlpha (0.85f * alpha));
                    g.fillPath (shape);
                    g.setGradientFill (juce::ColourGradient::horizontal (familyFrom (family).withAlpha (alpha), pill.getX(),
                                                                        familyTo (family).withAlpha (alpha), pill.getRight()));
                    g.strokePath (shape, juce::PathStrokeType (1.6f));
                }
                if (lit)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.08f + 0.14f * pulse));
                    g.fillPath (shape);
                }
                if (chosen || (hovered.kind == TimelineHit::Kind::Chord && hovered.chord == ref))
                {
                    g.setColour (chosen ? night::accent : juce::Colours::white.withAlpha (0.35f));
                    g.drawRoundedRectangle (pill.reduced (0.5f), radius, chosen ? 2.0f : 1.0f);
                }

                if (pill.getWidth() >= 22.0f)
                {
                    const auto label = labelFor (c);
                    auto text = pill.reduced (5.0f, 4.0f);
                    const bool darkInk = inKey && family == ChordFamily::Major;
                    g.setColour ((darkInk ? night::bgTop : night::text).withAlpha (alpha));
                    g.setFont (headingFont (juce::jlimit (11.0f, 15.0f, text.getHeight() * 0.42f), true));
                    g.drawFittedText (juce::String::fromUTF8 (label.name.c_str()), text.removeFromTop (text.getHeight() * 0.56f).toNearestInt(),
                                      juce::Justification::centredBottom, 1, 0.6f);
                    g.setColour ((darkInk ? night::bgTop.withAlpha (0.7f) : night::text.withAlpha (0.7f)).withMultipliedAlpha (alpha));
                    g.setFont (font (10.5f));
                    g.drawFittedText (juce::String::fromUTF8 (label.numeral.c_str()), text.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
                }
            }
        }

        // The insertion marker (dashed), or where a drag would drop (solid, with a "+" for a copy
        // or a palette chord).
        const auto markerLine = [&] (InsertPoint at, bool solid)
        {
            const float x = (float) insertX (l, at);
            const float top = (float) tabs.getY(), bottom = (float) chords.getBottom();
            g.setColour (night::accent.withAlpha (solid ? 1.0f : 0.75f));
            if (solid)
            {
                g.fillRoundedRectangle (x - 1.0f, top, 2.0f, bottom - top, 1.0f);
                juce::Path tri;
                tri.addTriangle (x - 5.0f, top, x + 5.0f, top, x, top + 6.0f);
                g.fillPath (tri);
                if (at.kind == InsertPoint::Kind::NewSection)
                {
                    g.setColour (night::accent.withAlpha (0.18f));
                    g.fillRoundedRectangle (x - 6.0f, top, 12.0f, bottom - top, 4.0f);
                }
            }
            else
            {
                const float dash[] = { 3.0f, 3.0f };
                g.drawDashedLine ({ x, top, x, bottom }, dash, 2, 1.2f);
            }
        };
        if (dropping)
            markerLine (dropAt, true);
        else if (drag == Drag::None && (hasKeyboardFocus (true) || ! prog.empty()))
            markerLine (insertAt, false);

        // The playhead: an amber line with a short fading trail.
        if (playhead >= 0.0 && ! prog.empty())
        {
            const float x = (float) l.xOfBeat (std::fmod (playhead, juce::jmax (1.0e-9, prog.totalBeats())));
            const float top = (float) ruler.getY(), bottom = (float) chords.getBottom();
            g.setGradientFill (juce::ColourGradient::horizontal (night::accent.withAlpha (0.0f), x - 26.0f, night::accent.withAlpha (0.22f), x));
            g.fillRect (juce::Rectangle<float> (x - 26.0f, top, 26.0f, bottom - top));
            g.setColour (night::accent);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, top, 2.0f, bottom - top));
        }
    }

    //==========================================================================
    // The mouse.

    void TimelinePanel::mouseDown (const juce::MouseEvent& e)
    {
        if (renaming >= 0)
            finishRenaming (true);
        grabKeyboardFocus();
        const auto hit = hitAt (e.position);
        const bool isChord = hit.kind == TimelineHit::Kind::Chord || hit.kind == TimelineHit::Kind::ChordEdge;
        const bool isSection = hit.kind == TimelineHit::Kind::Section || hit.kind == TimelineHit::Kind::SectionEdge;

        if (e.mods.isPopupMenu())
        {
            if (isChord)
            {
                if (! isChordSelected (selected, hit.chord))
                    setSelection (Selection::chord (hit.chord.section, hit.chord.index));
                showChordMenu (hit.chord);
            }
            else if (isSection)
            {
                if (! isSectionSelected (selected, hit.section))
                    setSelection (Selection::section (hit.section));
                showSectionMenu (hit.section);
            }
            else
            {
                setMarker (dropPoint (layout(), e.position.x));
                showSpaceMenu();
            }
            return;
        }

        pressed = hit;
        dragX = e.position.x;
        selectOnRelease = false;
        dragLayout = layout();
        const bool toggle = e.mods.isShiftDown() || e.mods.isCommandDown();
        if (hit.isEdge())
        {
            if (! isSelected (selected, hit))
                setSelection (selectItem (model, {}, hit, false));
            drag = hit.kind == TimelineHit::Kind::ChordEdge ? Drag::ResizeChord : Drag::ResizeSection;
            dragPreview = model;
            return;
        }
        if (hit.kind != TimelineHit::Kind::None)
        {
            if (toggle || ! isSelected (selected, hit))
                setSelection (selectItem (model, selected, hit, toggle));
            else
                selectOnRelease = true;
            drag = toggle ? Drag::None : Drag::Pending;
            if (isChord && ! toggle)
                if (const auto* c = model.chord (hit.chord))
                {
                    processor.previewChord (*c);
                    previewing = true;
                }
            return;
        }
        if (! toggle)
            setSelection ({});
        setMarker (dropPoint (layout(), e.position.x));
        drag = Drag::None;
    }

    void TimelinePanel::mouseDrag (const juce::MouseEvent& e)
    {
        const float x = e.position.x;
        switch (drag)
        {
            case Drag::ResizeChord:
                dragPreview = model;
                resizeChordToX (dragPreview, dragLayout, pressed.chord, x, snap, beatsPerBar);
                repaint();
                break;
            case Drag::ResizeSection:
                dragPreview = model;
                resizeSectionToX (dragPreview, dragLayout, pressed.section, x, snap, beatsPerBar);
                repaint();
                break;
            case Drag::Pending:
                if (std::abs (x - dragX) < 4.0f)
                    break;
                endPreview();
                drag = Drag::Move;
                selectOnRelease = false;
                dropping = true;
                [[fallthrough]];
            case Drag::Move:
                dropAt = dropPoint (layout(), x);
                dragCopy = e.mods.isAltDown();
                setMouseCursor (dragCopy ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::DraggingHandCursor);
                repaint();
                break;
            case Drag::None:
                break;
        }
    }

    void TimelinePanel::mouseUp (const juce::MouseEvent& e)
    {
        endPreview();
        const float x = e.position.x;
        const Drag was = drag;
        drag = Drag::None;
        dropping = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        switch (was)
        {
            case Drag::ResizeChord:
            {
                const ChordRef r = pressed.chord;
                const auto l = dragLayout;
                const Snap sn = snap;
                const int bpb = beatsPerBar;
                edit ("Resize chord", [&] (ChordProgression& q, Selection&) { return resizeChordToX (q, l, r, x, sn, bpb); });
                break;
            }
            case Drag::ResizeSection:
            {
                const int s = pressed.section;
                const auto l = dragLayout;
                const Snap sn = snap;
                const int bpb = beatsPerBar;
                edit ("Resize section", [&] (ChordProgression& q, Selection&) { return resizeSectionToX (q, l, s, x, sn, bpb); });
                break;
            }
            case Drag::Move:
            {
                const Selection sel = selected;
                const bool copy = e.mods.isAltDown();
                const auto l = layout();
                edit (copy ? "Duplicate" : "Move", [&] (ChordProgression& q, Selection& out) { return dropSelection (q, l, sel, x, copy, &out); });
                break;
            }
            case Drag::Pending:
                if (selectOnRelease)
                    setSelection (selectItem (model, selected, pressed, false));
                break;
            case Drag::None:
                break;
        }
        repaint();
    }

    void TimelinePanel::mouseMove (const juce::MouseEvent& e)
    {
        const auto hit = hitAt (e.position);
        if (hit == hovered)
            return;
        hovered = hit;
        setMouseCursor (hit.isEdge() ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        updateTooltip (hit);
        repaint();
    }

    void TimelinePanel::mouseExit (const juce::MouseEvent&)
    {
        hovered = {};
        updateTooltip ({});
        repaint();
    }

    void TimelinePanel::updateTooltip (const TimelineHit& hit)
    {
        juce::String tip;
        if (const auto* c = model.chord (hit.chord); c != nullptr && (hit.kind == TimelineHit::Kind::Chord || hit.kind == TimelineHit::Kind::ChordEdge))
        {
            const auto label = labelFor (*c);
            tip << juce::String::fromUTF8 (label.name.c_str()) << " (" << juce::String::fromUTF8 (label.numeral.c_str()) << "), "
                << lengthText (c->ticks, beatsPerBar) << ". "
                << (hit.kind == TimelineHit::Kind::ChordEdge ? "Drag to change its length (snapped)." : "Drag to move it (Alt: a copy); right-click for more.");
        }
        else if (model.validSection (hit.section))
        {
            const auto& sec = model.sections[(size_t) hit.section];
            tip << juce::String::fromUTF8 (sec.name.c_str()) << ": " << (int) sec.chords.size() << (sec.chords.size() == 1 ? " chord, " : " chords, ")
                << lengthText (sec.ticks(), beatsPerBar) << ". "
                << (hit.kind == TimelineHit::Kind::SectionEdge ? "Drag to scale its chords." : "Drag to move it (Alt: a copy); double-click to rename.");
        }
        setTooltip (tip);
    }

    void TimelinePanel::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto hit = hitAt (e.position);
        if (hit.kind == TimelineHit::Kind::Section)
            startRenaming (hit.section);
    }

    void TimelinePanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (e.mods.isAltDown() && e.mods.isShiftDown())
        {
            const float delta = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
            const auto before = geometry();
            const auto z = zoomAround (before, 1.0 + juce::jlimit (-0.5, 0.5, (double) delta), e.position.x);
            ppb = z.pixelsPerBeat;
            scrollTo (scroll + (before.origin - z.origin));   // origin = chord area + 8 - scroll
            if (onZoom)
                onZoom (ppb);
            return;
        }
        const float delta = w.deltaX != 0.0f ? w.deltaX : w.deltaY;
        scrollTo (scroll - delta * 240.0);
    }

    //==========================================================================
    // Menus.

    void TimelinePanel::showChordMenu (ChordRef r)
    {
        const auto* c = model.chord (r);
        if (c == nullptr)
            return;
        juce::Component::SafePointer<TimelinePanel> self (this);
        juce::PopupMenu inversion, octave, bass, menu;
        static const char* const inversions[] = { "Root position", "1st inversion", "2nd inversion", "3rd inversion" };
        for (int i = 0; i <= maxChordInversion; ++i)
            inversion.addItem (inversions[i], true, c->inversion == i, [self, r, i] { if (self) self->setInversion (r, i); });
        for (int o = maxChordOctave; o >= minChordOctave; --o)
            octave.addItem (o == 0 ? juce::String ("As built") : (o > 0 ? "+" : "") + juce::String (o), true, c->octave == o,
                            [self, r, o] { if (self) self->setOctave (r, o); });
        bass.addItem ("None", true, c->bass < 0, [self, r] { if (self) self->setBass (r, -1); });
        const Spelling sp = makeSpelling (homeKey, homeScale);
        for (int k = 0; k < 12; ++k)
        {
            const int pc = (homeKey + k) % 12;
            bass.addItem (juce::String (sp.name (pc)), true, c->bass == pc, [self, r, pc] { if (self) self->setBass (r, pc); });
        }
        menu.addSectionHeader (juce::String::fromUTF8 (labelFor (*c).name.c_str()));
        menu.addSubMenu ("Inversion", inversion);
        menu.addSubMenu ("Octave", octave);
        menu.addSubMenu ("Bass note", bass);
        menu.addSeparator();
        menu.addItem ("Duplicate", [self] { if (self) self->duplicateSelection(); });
        menu.addItem ("Copy", [self] { if (self) self->copySelection(); });
        menu.addItem ("Cut", [self] { if (self) self->cutSelection(); });
        menu.addItem ("Paste after", ! clipboard.empty(), false, [self] { if (self) self->paste(); });
        menu.addItem ("Delete", [self] { if (self) self->deleteSelection(); });
        menu.addSeparator();
        menu.addItem ("Rename section" + juce::String::fromUTF8 ("\xe2\x80\xa6"), [self, s = r.section] { if (self) self->startRenaming (s); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    void TimelinePanel::showSectionMenu (int s)
    {
        if (! model.validSection (s))
            return;
        juce::Component::SafePointer<TimelinePanel> self (this);
        juce::PopupMenu colour, menu;
        for (int i = 0; i < numSectionColours; ++i)
            colour.addColouredItem (i + 100, sectionColourNames[i], sectionColour (i), true, model.sections[(size_t) s].colour == i);
        menu.addSectionHeader (juce::String::fromUTF8 (model.sections[(size_t) s].name.c_str()));
        menu.addItem ("Rename" + juce::String::fromUTF8 ("\xe2\x80\xa6"), [self, s] { if (self) self->startRenaming (s); });
        menu.addSubMenu ("Colour", colour);
        menu.addSeparator();
        menu.addItem ("Duplicate", [self] { if (self) self->duplicateSelection(); });
        menu.addItem ("Copy", [self] { if (self) self->copySelection(); });
        menu.addItem ("Cut", [self] { if (self) self->cutSelection(); });
        menu.addItem ("Paste after", ! clipboard.empty(), false, [self] { if (self) self->paste(); });
        menu.addItem ("Delete", [self] { if (self) self->deleteSelection(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [self, s] (int result)
        {
            if (self != nullptr && result >= 100 && result < 100 + numSectionColours)
                self->setSectionColour (s, result - 100);
        });
    }

    void TimelinePanel::showSpaceMenu()
    {
        juce::Component::SafePointer<TimelinePanel> self (this);
        juce::PopupMenu menu;
        menu.addItem ("Paste here", ! clipboard.empty(), false, [self] { if (self) self->paste(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    //==========================================================================
    // Drops from the palette.

    bool TimelinePanel::isInterestedInDragSource (const SourceDetails& d)
    {
        ProgressionChord c;
        return chordFromDragDescription (d.description, c);
    }

    void TimelinePanel::itemDragEnter (const SourceDetails& d) { itemDragMove (d); }

    void TimelinePanel::itemDragMove (const SourceDetails& d)
    {
        dropping = true;
        dropAt = dropPoint (layout(), (double) d.localPosition.x);
        repaint();
    }

    void TimelinePanel::itemDragExit (const SourceDetails&)
    {
        dropping = false;
        repaint();
    }

    void TimelinePanel::itemDropped (const SourceDetails& d)
    {
        dropping = false;
        ProgressionChord c;
        if (chordFromDragDescription (d.description, c))
            dropChordAt (c, (float) d.localPosition.x);
        repaint();
    }
}
