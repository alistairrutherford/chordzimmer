#include "ChordZimmerEditor.h"
#include "../plugin/ChordZimmerProcessor.h"

namespace chordzimmer
{
    namespace
    {
        const juce::Identifier keyboardId ("keyboard"), widthId ("width"), heightId ("height"),
                               paletteFifthsId ("paletteFifths"), paletteScaleId ("paletteScale"), snapId ("snap"), zoomId ("zoom");
    }

    ChordZimmerEditor::ChordZimmerEditor (ChordZimmerProcessor& p)
        : AudioProcessorEditor (p), processor (p), bindings (p.state),
          topBar (p, bindings, [this] (bool shown) { showKeyboard (shown); }),
          transport (p, bindings), timeline (p, bindings), palette (p), navigator (bindings, nav), performance (p, bindings), match (p, bindings), outputKeys (p)
    {
        setLookAndFeel (&lookAndFeel);
        for (auto* c : std::initializer_list<juce::Component*> { &topBar, &transport, &timeline, &palette, &navigator, &outputKeys, &info })
            addAndMakeVisible (c);
        addChildComponent (performance);
        addChildComponent (match);
        addMouseListener (this, true);   // hovering over any child updates the Info View
        setWantsKeyboardFocus (true);

        // Read the saved preferences before anything resizes the editor: setResizeLimits and the
        // first layouts would otherwise store 0×0, then the minimum, over the saved size. Nothing
        // is stored until the editor is up, so opening it doesn't tell the host the state changed.
        auto& ui = processor.uiState;
        const bool keyboardShown = ui.getProperty (keyboardId, true);
        const int width = juce::jlimit (minWidth, maxWidth, (int) ui.getProperty (widthId, defaultWidth));
        const int height = juce::jlimit (minHeight, maxHeight, (int) ui.getProperty (heightId, defaultHeight));

        // The palette position (PLAN.md §3.2: UI state, saved with the plug-in), relative to home.
        const auto settings = processor.currentSettings();
        nav.homeKey = settings.key;
        nav.homeScale = settings.scale;
        nav.fifths = juce::jlimit (-maxFifths, maxFifths, (int) ui.getProperty (paletteFifthsId, 0));
        const int savedScale = ui.getProperty (paletteScaleId, -1);
        nav.paletteScale = savedScale >= 0 && savedScale < numScales ? (Scale) savedScale : settings.scale;
        navigator.refresh();
        palette.setContext ({ nav.paletteKey(), nav.paletteScale, nav.homeKey, nav.homeScale, nav.distance() });

        const int snap = ui.getProperty (snapId, (int) defaultSnap);
        transport.setSnap ((Snap) juce::jlimit (0, (int) Snap::Bar, snap));
        timeline.setSnap (transport.getSnap());
        timeline.setPixelsPerBeat ((double) ui.getProperty (zoomId, TimelineGeometry::defaultPixelsPerBeat));

        // Wiring.
        navigator.onChange = [this] { paletteMoved(); };
        navigator.onPerformance = [this] { showPerformance (! performance.isVisible()); };
        navigator.onMatch = [this] { showMatch (! match.isVisible()); };
        match.onSet = [this] (const ProgressionChord& c) { timeline.replaceSelectedChord (c); };
        match.onAdd = [this] (const ProgressionChord& c) { timeline.addChord (c); };
        palette.onAdd = [this] (const ProgressionChord& c) { timeline.addChord (c); };
        palette.onAddAsSection = [this] (const ProgressionChord& c) { timeline.addChordAsSection (c); };
        palette.onFocus = [this] (const PaletteCell& c)
        {
            focusChosen = true;
            setFocus (ChordId::of (c), c.numeral);
        };
        transport.onUndo = [this] { undo(); };
        transport.onRedo = [this] { redo(); };
        transport.onSnap = [this] (Snap s)
        {
            timeline.setSnap (s);
            processor.uiState.setProperty (snapId, (int) s, nullptr);
        };
        timeline.onZoom = [this] (double ppb) { processor.uiState.setProperty (zoomId, ppb, nullptr); };
        timeline.onImport = [this] (const juce::String& what) { info.showKey ("display.import", what); };

        outputKeys.setVisible (keyboardShown);
        topBar.setKeyboardShown (keyboardShown);
        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, maxWidth, maxHeight);
        setSize (width, height);
        sizeRestored = true;
        timerCallback();
        startTimerHz (30);
    }

    ChordZimmerEditor::~ChordZimmerEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void ChordZimmerEditor::showKeyboard (bool shown)
    {
        outputKeys.setVisible (shown);
        topBar.setKeyboardShown (shown);
        processor.uiState.setProperty (keyboardId, shown, nullptr);
        resized();
    }

    void ChordZimmerEditor::showPerformance (bool shown)
    {
        if (shown && match.isVisible())
            showMatch (false);
        performance.setVisible (shown);
        navigator.setPerformanceShown (shown);
        if (shown)
        {
            placePopover (performance, ui::PerformancePanel::preferredWidth(), ui::PerformancePanel::preferredHeight());
            performance.toFront (false);
        }
    }

    void ChordZimmerEditor::showMatch (bool shown)
    {
        if (shown && performance.isVisible())
            showPerformance (false);
        match.setVisible (shown);
        navigator.setMatchShown (shown);
        if (shown)
        {
            placeMatch();
            match.toFront (false);
        }
        updateMatch();
    }

    void ChordZimmerEditor::placePopover (juce::Component& c, int width, int height)
    {
        // Above the footer, at its right, over the palette.
        const int w = juce::jmin (width, getWidth() - 24);
        const auto anchor = navigator.getBounds();
        c.setBounds (anchor.getRight() - w, anchor.getY() - height - 6, w, height);
    }

    void ChordZimmerEditor::setFocus (const ChordId& f, const std::string& numeral)
    {
        const auto bare = relations_detail::bareNumeral (numeral);
        palette.setFocus (f, numeral, bare == "I" || bare == "i");
    }

    void ChordZimmerEditor::placeMatch()
    {
        // Over the transport and the timeline, the editor's width: the palette stays in view.
        match.setBounds (transport.getX(), transport.getY() - 4, transport.getWidth(), ui::MatchPanel::preferredHeight());
    }

    // The Match popover's notes, against the palette shown; its ranks on the palette.
    void ChordZimmerEditor::updateMatch()
    {
        std::vector<std::pair<int, int>> ranks;
        if (match.isVisible())
            for (const auto& m : match.getMatches())
                if (m.fifths == 0)
                    ranks.push_back ({ m.cell.variant * numPaletteColumns + m.cell.degree, m.rank });
        palette.setMatchRanks (ranks, match.isVisible() && ! match.noteSet().empty());
        const auto& prog = timeline.progression();
        const auto* target = prog.chord (timeline.selectedChord());
        match.setSetTarget (target == nullptr ? juce::String()
                                              : juce::String::fromUTF8 (timelineChordLabel (*target, nav.homeKey, nav.homeScale).name.c_str()));
    }

    void ChordZimmerEditor::paletteMoved()
    {
        palette.setContext ({ nav.paletteKey(), nav.paletteScale, nav.homeKey, nav.homeScale, nav.distance() });
        processor.uiState.setProperty (paletteFifthsId, nav.fifths, nullptr);
        processor.uiState.setProperty (paletteScaleId, (int) nav.paletteScale, nullptr);
    }

    void ChordZimmerEditor::undo()
    {
        processor.undoProgression();
        refreshNow();          // the timeline pulls the progression back
    }

    void ChordZimmerEditor::redo()
    {
        processor.redoProgression();
        refreshNow();
    }

    void ChordZimmerEditor::paint (juce::Graphics& g)
    {
        ui::fillBackground (g, getLocalBounds());
        if (outputKeys.isVisible())
            ui::drawGlassPanel (g, outputKeys.getBounds().toFloat().expanded (6.0f, 5.0f), 10.0f);
    }

    void ChordZimmerEditor::resized()
    {
        auto r = getLocalBounds();
        topBar.setBounds (r.removeFromTop (44));
        info.setBounds (r.removeFromBottom (ui::InfoView::stripHeight + 10).reduced (12, 0).withTrimmedBottom (10));
        if (outputKeys.isVisible())
            outputKeys.setBounds (r.removeFromBottom (62).reduced (20, 8));

        r.reduce (14, 8);
        transport.setBounds (r.removeFromTop (34));
        r.removeFromTop (6);
        timeline.setBounds (r.removeFromTop (ui::TimelinePanel::preferredHeight()));
        r.removeFromTop (10);
        navigator.setBounds (r.removeFromBottom (44));
        r.removeFromBottom (10);
        palette.setBounds (r);
        if (performance.isVisible())
            placePopover (performance, ui::PerformancePanel::preferredWidth(), ui::PerformancePanel::preferredHeight());
        if (match.isVisible())
            placeMatch();

        if (sizeRestored)
        {
            processor.uiState.setProperty (widthId, getWidth(), nullptr);
            processor.uiState.setProperty (heightId, getHeight(), nullptr);
        }
    }

    bool ChordZimmerEditor::keyPressed (const juce::KeyPress& k)
    {
        const auto mods = k.getModifiers();
        const int code = k.getKeyCode();
        const bool cmd = mods.isCommandDown();
        const juce::juce_wchar ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (cmd && (code == 'Z' || ch == 'z'))
        {
            mods.isShiftDown() ? redo() : undo();
            return true;
        }
        if (cmd && (code == 'C' || ch == 'c')) return timeline.copySelection() || true;
        if (cmd && (code == 'X' || ch == 'x')) return timeline.cutSelection() || true;
        if (cmd && (code == 'V' || ch == 'v')) return timeline.paste() || true;
        if (cmd && (code == 'D' || ch == 'd')) return timeline.duplicateSelection() || true;
        if (cmd && (ch == '=' || ch == '+')) { timeline.zoom (1.25); return true; }
        if (cmd && ch == '-') { timeline.zoom (0.8); return true; }
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) return timeline.deleteSelection() || true;
        if (k == juce::KeyPress::leftKey) { timeline.step (-1); return true; }
        if (k == juce::KeyPress::rightKey) { timeline.step (+1); return true; }
        if (k == juce::KeyPress::escapeKey)
        {
            if (performance.isVisible())
                showPerformance (false);
            else if (match.isVisible())
                showMatch (false);
            else
                timeline.setSelection ({});
            return true;
        }
        if (! cmd && k == juce::KeyPress::spaceKey)
        {
            bindings.set (params::play, bindings.value (params::play) > 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (! cmd && ch == 'l')
        {
            bindings.set (params::loop, bindings.value (params::loop) > 0.5f ? 0.0f : 1.0f);
            return true;
        }
        return false;
    }

    juce::Component* ChordZimmerEditor::infoComponentAt (juce::Point<int> p, juce::String& key)
    {
        // The innermost thing under the point with an entry: a control, a display, else its card.
        // (From the children down: the editor's own getComponentAt needs it to be on screen.)
        key = {};
        juce::Component* hit = nullptr;
        for (int i = getNumChildComponents(); --i >= 0 && hit == nullptr;)
        {
            auto* child = getChildComponent (i);
            if (child->isVisible() && child->getBounds().contains (p))
                hit = child->getComponentAt (child->getLocalPoint (this, p));
        }
        for (auto* c = hit; c != nullptr && c != this; c = c->getParentComponent())
        {
            if (auto* control = dynamic_cast<ui::ParamControl*> (c))
                key = control->getParamId();
            else if (c == &performance)
                key = performance.infoKeyAt (performance.getLocalPoint (this, p));
            else if (c == &match)
                key = match.infoKeyAt (match.getLocalPoint (this, p));
            else
                key = c->getComponentID();
            if (ui::InfoView::has (key))
                return c;
        }
        key = {};
        return nullptr;
    }

    juce::String ChordZimmerEditor::infoKeyAt (juce::Point<int> p)
    {
        juce::String key;
        infoComponentAt (p, key);
        return key;
    }

    void ChordZimmerEditor::showInfoAt (juce::Point<int> p)
    {
        juce::String key;
        auto* c = infoComponentAt (p, key);
        // Heard, Export, a palette cell and a timeline block say what they hold now (what was
        // heard, the bars; the chord's notes; its length), when they have something to say.
        juce::String live;
        if (key == params::analyse || key == "ui.export" || key == "ui.dragSelection" || key == "display.palette" || key == "display.timeline")
            if (auto* tip = dynamic_cast<juce::TooltipClient*> (c))
                live = tip->getTooltip();
        info.showKey (key, live);
    }

    void ChordZimmerEditor::mouseEnter (const juce::MouseEvent& e) { showInfoAt (e.getEventRelativeTo (this).getPosition()); }
    void ChordZimmerEditor::mouseMove (const juce::MouseEvent& e)  { showInfoAt (e.getEventRelativeTo (this).getPosition()); }
    void ChordZimmerEditor::mouseDrag (const juce::MouseEvent& e)  { showInfoAt (e.getEventRelativeTo (this).getPosition()); }

    void ChordZimmerEditor::mouseDown (const juce::MouseEvent& e)
    {
        // A click outside a popover (and its button, and any menu it opened) closes it. The
        // Match popover stays open over clicks in the palette and the timeline, which it works
        // with (hearing a ranked chord, selecting the chord Set replaces).
        const auto* c = e.eventComponent;
        if (c == nullptr || ! isParentOf (c))
            return;
        if (performance.isVisible() && c != &performance && ! performance.isParentOf (c) && c != &navigator.performance)
            showPerformance (false);
        const bool matchWorksWith = c == &palette || palette.isParentOf (c) || c == &timeline || timeline.isParentOf (c);
        if (match.isVisible() && c != &match && ! match.isParentOf (c) && c != &navigator.match && ! matchWorksWith)
            showMatch (false);
    }

    void ChordZimmerEditor::mouseExit (const juce::MouseEvent& e)
    {
        // Moving between children also sends exits: clear only on leaving the editor.
        if (! getLocalBounds().contains (e.getEventRelativeTo (this).getPosition()))
            info.showKey ({});
    }

    void ChordZimmerEditor::timerCallback()
    {
        topBar.refresh();

        ui::UiModel m;
        m.settings = processor.currentSettings();
        m.analyse = processor.analyseMode();
        m.guess = processor.readKeyGuess();
        m.detect = processor.detectMode();
        m.heardChords = m.detect != params::DetectMode::Off ? processor.heardChords().count : 0;
        m.playhead = processor.playhead.load();
        m.playingChord = processor.playingChord.load();
        m.beatsPerBar = juce::jlimit (1, 16, processor.hostNumerator.load());

        // The home key (Key, Scale) carries the palette along (CircleOfFifths::setHome).
        if (m.settings.key != nav.homeKey || m.settings.scale != nav.homeScale)
        {
            nav.setHome (m.settings.key, m.settings.scale);
            navigator.refresh();
            paletteMoved();
        }

        timeline.refresh (m);
        performance.refresh (m);
        if (match.isVisible())
        {
            match.refresh (m, { nav.paletteKey(), nav.paletteScale, nav.homeKey, nav.homeScale, nav.distance() });
            updateMatch();
        }
        outputKeys.refresh (m);
        transport.setUndoState (processor.undoState());

        // The chord now: the one sounding, else the first selected.
        const auto& prog = timeline.progression();
        ChordRef now = chordAtOrder (prog, m.playingChord);
        if (! prog.valid (now) && ! timeline.selection().empty())
        {
            const auto sel = prog.normalised (timeline.selection());
            now = ! sel.sections.empty() && (sel.chords.empty() || sel.sections.front() <= sel.chords.front().section)
                    ? ChordRef { sel.sections.front(), 0 } : (sel.chords.empty() ? ChordRef {} : sel.chords.front());
        }
        juce::String where;
        if (m.playhead >= 0.0)
            where = "Bar " + juce::String ((int) (m.playhead / m.beatsPerBar) + 1) + "." + juce::String ((int) std::fmod (m.playhead, (double) m.beatsPerBar) + 1);
        if (const auto* c = prog.chord (now))
        {
            const auto label = timelineChordLabel (*c, m.settings.key, m.settings.scale);
            transport.now.set (juce::String::fromUTF8 (label.name.c_str()), juce::String::fromUTF8 (label.numeral.c_str()), where);
        }
        else
            transport.now.set ({}, {}, where);

        // Chord Relations' focus (PLAN.md §3.5): a timeline chord newly selected; else the last
        // palette cell clicked; else the home tonic.
        const ChordRef selectedChord = timeline.selectedChord();
        if (const auto* c = prog.chord (selectedChord);
            c != nullptr && (selectedChord != focusedTimelineChord || processor.progressionVersion() != focusedVersion))
        {
            focusChosen = true;
            setFocus (ChordId::of (*c), timelineChordLabel (*c, m.settings.key, m.settings.scale).numeral);
        }
        focusedTimelineChord = selectedChord;
        focusedVersion = processor.progressionVersion();
        if (! focusChosen)
        {
            PaletteContext homeCtx;
            homeCtx.paletteKey = homeCtx.homeKey = m.settings.key;
            homeCtx.paletteScale = homeCtx.homeScale = m.settings.scale;
            const auto tonic = buildPaletteCell (homeCtx, 3, 0, makeSpelling (m.settings.key, m.settings.scale));
            setFocus (ChordId::of (tonic), tonic.numeral);
        }

        const auto* sounding = prog.chord (chordAtOrder (prog, m.playingChord));
        palette.setSounding (sounding != nullptr ? sounding->pitchClasses() : 0u, sounding != nullptr ? sounding->root() : -1, m.playhead);
    }
}
