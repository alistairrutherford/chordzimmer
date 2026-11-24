#include "ChordZimmerEditor.h"
#include "../plugin/ChordZimmerProcessor.h"

namespace chordzimmer
{
    namespace
    {
        const juce::Identifier keyboardId ("keyboard"), widthId ("width"), heightId ("height");
    }

    ChordZimmerEditor::ChordZimmerEditor (ChordZimmerProcessor& p)
        : AudioProcessorEditor (p), processor (p), bindings (p.state),
          topBar (p, bindings, [this] (bool shown) { showKeyboard (shown); }),
          globals (p, bindings), chordPads (p), workspace (p, bindings), effects (bindings), outputKeys (p)
    {
        setLookAndFeel (&lookAndFeel);
        for (auto* c : std::initializer_list<juce::Component*> { &topBar, &globals, &chordPads, &workspace, &effects, &outputKeys, &info })
            addAndMakeVisible (c);
        addMouseListener (this, true);   // hovering over any child updates the Info View
        chordPads.onAddToPattern = [this] (int pad) { workspace.addPaletteChord (pad); };

        // Read the saved preferences before anything resizes the editor: setResizeLimits and the
        // first layouts would otherwise store 0×0, then the minimum, over the saved size. Nothing
        // is stored until the editor is up, so opening it doesn't tell the host the state changed.
        auto& ui = processor.uiState;
        const bool keyboardShown = ui.getProperty (keyboardId, true);
        const int width = juce::jlimit (minWidth, maxWidth, (int) ui.getProperty (widthId, defaultWidth));
        const int height = juce::jlimit (minHeight, maxHeight, (int) ui.getProperty (heightId, defaultHeight));

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

    void ChordZimmerEditor::paint (juce::Graphics& g)
    {
        ui::fillBackground (g, getLocalBounds());
        // Glass cards behind the globals, the pads and the output keyboard (the workspace, the
        // effects and the Info View draw their own).
        ui::drawGlassPanel (g, globals.getBounds().toFloat().expanded (8.0f, 5.0f));
        ui::drawGlassPanel (g, chordPads.getBounds().toFloat());
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

        r.reduce (14, 12);
        globals.setBounds (r.removeFromTop (42).reduced (8, 0));
        r.removeFromTop (14);
        effects.setBounds (r.removeFromBottom (ui::EffectsPanel::preferredHeight()));
        r.removeFromBottom (10);
        workspace.setBounds (r.removeFromBottom (ui::WorkspacePanel::preferredHeight()));
        r.removeFromBottom (10);
        // The pads take the space left, up to a height that still looks like pads; any more is
        // shared above and below them.
        chordPads.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), maxPadHeight)));

        if (sizeRestored)
        {
            processor.uiState.setProperty (widthId, getWidth(), nullptr);
            processor.uiState.setProperty (heightId, getHeight(), nullptr);
        }
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
            else if (c == &effects)
                key = effects.infoKeyAt (effects.getLocalPoint (this, p));
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
        // Heard, Export, Detect and the pattern's drag say what they hold now (what was heard, the
        // bars, what Detect will do, or why not).
        juce::String live;
        if (key == params::analyse || key == "ui.export" || key == params::detect || key == "ui.dragPattern")
            if (auto* tip = dynamic_cast<juce::TooltipClient*> (c))
                live = tip->getTooltip();
        info.showKey (key, live);
    }

    void ChordZimmerEditor::mouseEnter (const juce::MouseEvent& e) { showInfoAt (e.getEventRelativeTo (this).getPosition()); }
    void ChordZimmerEditor::mouseMove (const juce::MouseEvent& e)  { showInfoAt (e.getEventRelativeTo (this).getPosition()); }
    void ChordZimmerEditor::mouseDrag (const juce::MouseEvent& e)  { showInfoAt (e.getEventRelativeTo (this).getPosition()); }

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
        m.display = processor.readDisplay();
        m.settings = processor.currentSettings();
        m.analyse = processor.analyseMode();
        m.guess = processor.readKeyGuess();
        m.workspace = processor.getWorkspace();
        m.playingPattern = processor.playingPattern.load();
        m.playingSlot = processor.playingSlot.load();
        m.detect = processor.detectMode();
        m.heardChords = m.detect != params::DetectMode::Off ? processor.heardChords().count : 0;

        globals.refresh (m);
        chordPads.refresh (m);
        workspace.refresh (m);
        outputKeys.refresh (m);
        effects.refresh (m);
    }
}
