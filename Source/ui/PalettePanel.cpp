#include "PalettePanel.h"
#include "TimelinePanel.h"

namespace chordzimmer::ui
{
    namespace
    {
        constexpr float minRowHeight = 36.0f;   // about four rows at the default size (PLAN.md §3.1)

        int triadRow() noexcept
        {
            for (int v = 0; v < numTierVariants; ++v)
                if (tierVariants[v].tier == Tier::Triads)
                    return v;
            return 0;
        }
    }

    //==========================================================================
    NextStrip::NextStrip() { setComponentID ("display.next"); }

    void NextStrip::setChords (const std::vector<PaletteCell>& c)
    {
        const bool same = c.size() == chords.size()
                       && std::equal (c.begin(), c.end(), chords.begin(), [] (const PaletteCell& a, const PaletteCell& b) { return a.name == b.name && a.root == b.root; });
        if (same)
            return;
        chords = c;
        repaint();
    }

    juce::Rectangle<float> NextStrip::chipBounds (int i) const
    {
        const auto r = getLocalBounds().toFloat().withTrimmedLeft (44.0f);
        const float w = juce::jmin (64.0f, r.getWidth() / 5.0f);
        return { r.getX() + w * (float) i, r.getY(), w - 4.0f, r.getHeight() };
    }

    int NextStrip::chipAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) chords.size(); ++i)
            if (chipBounds (i).contains (p))
                return i;
        return -1;
    }

    void NextStrip::paint (juce::Graphics& g)
    {
        g.setColour (colours::dim);
        g.setFont (font (11.0f, true));
        g.drawText (juce::String::fromUTF8 ("Next \xe2\x96\xb8"), getLocalBounds().removeFromLeft (44), juce::Justification::centredLeft);
        if (chords.empty())
        {
            g.setColour (colours::faint);
            g.setFont (font (11.0f));
            g.drawText ("click a chord", getLocalBounds().withTrimmedLeft (44), juce::Justification::centredLeft);
            return;
        }
        for (int i = 0; i < (int) chords.size(); ++i)
        {
            const auto& c = chords[(size_t) i];
            const auto r = chipBounds (i);
            drawChordOrb (g, r, c.family, c.inKey, 0.0f, r.getHeight() * 0.5f);
            g.setColour (c.inKey && c.family == ChordFamily::Major ? night::bgTop : night::text);
            g.setFont (headingFont (11.0f, true));
            g.drawFittedText (juce::String::fromUTF8 (c.name.c_str()), r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.6f);
        }
    }

    void NextStrip::mouseDown (const juce::MouseEvent& e)
    {
        if (const int i = chipAt (e.position); i >= 0 && onPreview)
            onPreview (progressionChordFromCell (chords[(size_t) i]));
    }

    void NextStrip::mouseUp (const juce::MouseEvent&)
    {
        if (onEndPreview)
            onEndPreview();
    }

    void NextStrip::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (const int i = chipAt (e.position); i >= 0 && onAdd)
            onAdd (progressionChordFromCell (chords[(size_t) i]));
    }

    //==========================================================================
    PalettePanel::PalettePanel (ChordZimmerProcessor& p) : processor (p)
    {
        setComponentID ("display.palette");
        relationsButton.setComponentID ("ui.relations");
        relationsButton.setClickingTogglesState (true);
        relationsButton.onClick = [this] { showRelations (relationsButton.getToggleState()); };
        addAndMakeVisible (relationsButton);
        next.onPreview = [this] (const ProgressionChord& c) { processor.previewChord (c); };
        next.onEndPreview = [this] { processor.endPreview(); };
        next.onAdd = [this] (const ProgressionChord& c) { if (onAdd) onAdd (c); };
        addChildComponent (next);
        context.paletteKey = -1;                  // so the first setContext builds
        setContext ({});
        bottomRow = juce::jmax (0, triadRow() - 1);   // PLAN.md §3.1: the triads in view at first
    }

    PalettePanel::~PalettePanel() { endPreview(); }

    void PalettePanel::setContext (const PaletteContext& c)
    {
        if (c.paletteKey == context.paletteKey && c.paletteScale == context.paletteScale && c.homeKey == context.homeKey && c.homeScale == context.homeScale)
            return;
        context = c;
        grid = buildPalette (context);
        spelling = makeSpelling (context.paletteKey, context.paletteScale);
        updateRelations();
        repaint();
    }

    void PalettePanel::setSounding (unsigned pcs, int root, double playhead)
    {
        const float p = playhead >= 0.0 ? (float) juce::square (1.0 - (playhead - std::floor (playhead))) : 0.0f;
        if (pcs == soundingPcs && root == soundingRoot && (pcs == 0 || std::abs (p - pulse) < 0.02f))
            return;
        soundingPcs = pcs;
        soundingRoot = root;
        pulse = p;
        repaint();
    }

    void PalettePanel::setMatchRanks (const std::vector<std::pair<int, int>>& list, bool active)
    {
        std::array<std::array<int8_t, numPaletteColumns>, numTierVariants> next {};
        for (const auto& [cellIndex, rank] : list)
        {
            const int v = cellIndex / numPaletteColumns, d = cellIndex % numPaletteColumns;
            if (v >= 0 && v < numTierVariants && rank > 0 && rank < 100)
                next[(size_t) v][(size_t) d] = (int8_t) rank;
        }
        if (next == ranks && active == matchActive)
            return;
        ranks = next;
        matchActive = active;
        repaint();
    }

    void PalettePanel::setFocus (const ChordId& f, const std::string& numeral, bool tonic)
    {
        if (f == focus && numeral == focusNumeral && tonic == focusTonic)
            return;
        focus = f;
        focusNumeral = numeral;
        focusTonic = tonic;
        updateRelations();
        repaint();
    }

    void PalettePanel::showRelations (bool shown)
    {
        relationsButton.setToggleState (shown, juce::dontSendNotification);
        next.setVisible (shown);
        repaint();
    }

    void PalettePanel::updateRelations()
    {
        relations = paletteRelations (grid, focus, focusNumeral, focusTonic);
        next.setChords (nextChords (focus, context.homeKey, context.homeScale));
    }

    int PalettePanel::matchRank (int v, int d) const noexcept
    {
        return v >= 0 && v < numTierVariants && d >= 0 && d < numPaletteColumns ? ranks[(size_t) v][(size_t) d] : 0;
    }

    //==========================================================================
    // Geometry.

    juce::Rectangle<int> PalettePanel::headerArea() const
    {
        return getLocalBounds().reduced (padding).withTrimmedTop (titleHeight).removeFromTop (headerHeight).withTrimmedLeft (labelWidth).withTrimmedRight (scrollWidth + 4);
    }

    juce::Rectangle<int> PalettePanel::rowsArea() const
    {
        return getLocalBounds().reduced (padding).withTrimmedTop (titleHeight + headerHeight).withTrimmedLeft (labelWidth).withTrimmedRight (scrollWidth + 4);
    }

    juce::Rectangle<int> PalettePanel::labelArea() const
    {
        const auto r = rowsArea();
        return { r.getX() - labelWidth, r.getY(), labelWidth, r.getHeight() };
    }

    int PalettePanel::visibleRows() const noexcept
    {
        return juce::jlimit (1, numTierVariants, (int) ((float) rowsArea().getHeight() / minRowHeight));
    }

    float PalettePanel::rowHeight() const { return (float) rowsArea().getHeight() / (float) visibleRows(); }

    void PalettePanel::scrollRows (int delta)
    {
        const int next = juce::jlimit (0, numTierVariants - visibleRows(), bottomRow + delta);
        if (next != bottomRow)
        {
            bottomRow = next;
            hovered = {};
            repaint();
        }
    }

    void PalettePanel::showRow (int variant)
    {
        if (variant < bottomRow)
            scrollRows (variant - bottomRow);
        else if (variant >= bottomRow + visibleRows())
            scrollRows (variant - (bottomRow + visibleRows() - 1));
    }

    const PaletteCell* PalettePanel::cell (CellRef r) const noexcept
    {
        if (r.variant < 0 || r.variant >= numTierVariants || r.degree < 0 || r.degree >= numPaletteColumns)
            return nullptr;
        return &grid.rows[(size_t) r.variant].cells[(size_t) r.degree];
    }

    juce::Rectangle<float> PalettePanel::cellBounds (CellRef r) const
    {
        if (cell (r) == nullptr || r.variant < bottomRow || r.variant >= bottomRow + visibleRows())
            return {};
        const auto area = rowsArea().toFloat();
        const float h = rowHeight(), w = area.getWidth() / (float) numPaletteColumns;
        const int fromTop = bottomRow + visibleRows() - 1 - r.variant;
        const auto slot = juce::Rectangle<float> (area.getX() + w * (float) r.degree, area.getY() + h * (float) fromTop, w, h);
        const float orbH = juce::jmin (h - 8.0f, 52.0f);
        const float orbW = juce::jmin (w - 10.0f, orbH * 2.1f);
        return slot.withSizeKeepingCentre (orbW, orbH);
    }

    PalettePanel::CellRef PalettePanel::cellAt (juce::Point<float> p) const
    {
        const auto area = rowsArea().toFloat();
        if (! area.contains (p))
            return {};
        const float h = rowHeight(), w = area.getWidth() / (float) numPaletteColumns;
        const int fromTop = (int) ((p.y - area.getY()) / h);
        const CellRef r { bottomRow + visibleRows() - 1 - fromTop, juce::jlimit (0, numPaletteColumns - 1, (int) ((p.x - area.getX()) / w)) };
        const auto* c = cell (r);
        return c != nullptr && c->exists && cellBounds (r).expanded (4.0f).contains (p) ? r : CellRef {};
    }

    juce::String PalettePanel::describe (CellRef r) const
    {
        const auto* c = cell (r);
        if (c == nullptr || ! c->exists)
            return {};
        juce::String notes, out;
        for (int i = 0; i < c->numNotes; ++i)
            notes << (i ? " " : "") << spelling.name (c->notes[(size_t) i] % 12);
        for (int pc = 0; pc < 12; ++pc)
            if ((c->outOfKey >> pc) & 1u)
                out << (out.isEmpty() ? "" : " ") << spelling.name (pc);
        return juce::String::fromUTF8 (c->name.c_str()) + " (" + juce::String::fromUTF8 (c->numeral.c_str()) + "): " + notes + ". "
             + (c->inKey ? juce::String ("In the home key.") : "Out of the home key: " + out + ".")
             + " Click to hear, drag to the timeline, double-click to add.";
    }

    void PalettePanel::resized()
    {
        const auto title = getLocalBounds().reduced (padding).removeFromTop (titleHeight);
        relationsButton.setBounds (title.withX (title.getX() + 70).withWidth (78).reduced (0, 1));
        next.setBounds (title.withX (title.getX() + 160).withWidth (juce::jmax (0, juce::jmin (380, title.getWidth() - 160 - 210))).reduced (0, 1));
        scrollRows (0);   // keep the rows within range at the new height
    }

    //==========================================================================
    void PalettePanel::paint (juce::Graphics& g)
    {
        drawGlassPanel (g, getLocalBounds().toFloat());
        auto inner = getLocalBounds().reduced (padding);
        const auto title = inner.removeFromTop (titleHeight);
        g.setColour (colours::dim);
        g.setFont (headingFont (11.5f, true));
        g.drawText ("PALETTE", title.withTrimmedLeft (4), juce::Justification::centredLeft);
        g.setFont (font (11.0f));
        const bool home = context.paletteKey == context.homeKey && context.paletteScale == context.homeScale;
        const int steps = fifthsBetween (context.homeKey, context.paletteKey);
        g.setColour (home ? colours::dim : night::accent);
        const juce::String away = steps == 0 ? juce::String()
                                             : ", " + juce::String (std::abs (steps)) + (std::abs (steps) == 1 ? " fifth " : " fifths ") + (steps > 0 ? "up" : "down");
        g.drawText (juce::String (spelling.name (context.paletteKey)) + " " + scaleInfo (context.paletteScale).name
                        + (home ? juce::String (", the home key") : ", borrowed" + away),
                    title.withTrimmedRight (4), juce::Justification::centredRight);

        // Column headers: the numeral relative to home, the root under it.
        const auto header = headerArea().toFloat();
        const float colW = header.getWidth() / (float) numPaletteColumns;
        for (int d = 0; d < numPaletteColumns; ++d)
        {
            const auto& col = grid.columns[(size_t) d];
            auto r = juce::Rectangle<float> (header.getX() + colW * (float) d, header.getY(), colW, header.getHeight());
            g.setColour (colours::text);
            g.setFont (headingFont (15.0f, true));
            g.drawText (juce::String::fromUTF8 (col.numeral.c_str()), r.removeFromTop (17.0f), juce::Justification::centred);
            g.setColour (colours::dim);
            g.setFont (font (10.5f));
            g.drawText (juce::String::fromUTF8 (col.rootName.c_str()), r, juce::Justification::centredTop);
        }

        // Rows, top to bottom, with their labels and a line between tiers.
        const auto rows = rowsArea().toFloat();
        const auto labels = labelArea().toFloat();
        const float h = rowHeight();
        const int shown = visibleRows();
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (getLocalBounds().reduced (padding).withTrimmedTop (titleHeight + headerHeight));
        for (int k = 0; k < shown; ++k)
        {
            const int v = bottomRow + shown - 1 - k;
            if (v < 0 || v >= numTierVariants)
                continue;
            const auto& row = grid.rows[(size_t) v];
            const float y = rows.getY() + h * (float) k;
            const auto labelBox = juce::Rectangle<float> (labels.getX(), y, labels.getWidth() - 6.0f, h);
            g.setColour (colours::dim);
            g.setFont (headingFont (12.0f, true));
            g.drawText (row.label, labelBox.withTrimmedBottom (h * 0.3f), juce::Justification::centredRight);
            const bool tierStart = v == 0 || tierVariants[v - 1].tier != row.tier;
            if (tierStart)
            {
                g.setColour (colours::faint);
                g.setFont (font (9.0f));
                static constexpr const char* shortNames[numTiers] = { "OTHER", "TRIADS", "6TH, 7TH", "COLOUR", "EXTENDED", "ALTERED" };
                g.drawText (shortNames[(int) row.tier], labelBox.withTrimmedTop (h * 0.55f), juce::Justification::centredRight, true);
                if (k < shown - 1)
                {
                    g.setColour (colours::outline.withAlpha (0.7f));
                    g.fillRect (labels.getX() + 4.0f, y + h - 0.5f, rows.getRight() - labels.getX() - 4.0f, 1.0f);
                }
            }

            for (int d = 0; d < numPaletteColumns; ++d)
            {
                const CellRef ref { v, d };
                const auto& c = row.cells[(size_t) d];
                auto orb = cellBounds (ref);
                if (! c.exists)
                {
                    g.setColour (colours::outline);
                    g.fillEllipse (orb.getCentreX() - 1.5f, orb.getCentreY() - 1.5f, 3.0f, 3.0f);
                    continue;
                }
                const bool hover = ref == hovered, down = ref == pressed && previewing;
                const bool lit = soundingPcs != 0 && c.pitchClasses == soundingPcs && c.root == soundingRoot;
                if (hover && ! down)
                    orb = orb.withSizeKeepingCentre (orb.getWidth() * 1.06f, orb.getHeight() * 1.06f);
                const float glow = lit ? night::glowIdle + (night::glowPlay - night::glowIdle) * pulse
                                       : (hover || down ? night::glowHover : night::glowIdle * 0.6f);
                const int rank = matchActive ? matchRank (v, d) : 0;
                const auto& relation = relations[(size_t) v][(size_t) d];
                const bool showRel = ! matchActive && relationsShown() && focus.valid();
                const bool isFocus = showRel && ChordId::of (c) == focus;
                const bool faded = (matchActive && rank == 0) || (showRel && ! isFocus && ! relation.related());
                if (faded)
                    g.beginTransparencyLayer (0.3f);
                drawChordOrb (g, orb, c.family, c.inKey, glow, orb.getHeight() * 0.5f);
                if (lit || down)
                {
                    g.setColour (juce::Colours::white.withAlpha (down ? 0.18f : 0.08f + 0.14f * pulse));
                    g.fillRoundedRectangle (orb, orb.getHeight() * 0.5f);
                }

                const bool darkInk = c.inKey && c.family == ChordFamily::Major;
                const auto ink = darkInk ? night::bgTop : night::text;
                auto text = orb.reduced (6.0f, 3.0f);
                g.setColour (ink);
                g.setFont (headingFont (juce::jlimit (12.0f, 17.0f, orb.getHeight() * 0.36f), true));
                g.drawFittedText (juce::String::fromUTF8 (c.name.c_str()), text.removeFromTop (text.getHeight() * 0.58f).toNearestInt(),
                                  juce::Justification::centredBottom, 1, 0.6f);
                g.setColour (darkInk ? night::bgTop.withAlpha (0.65f) : (c.inKey ? night::text.withAlpha (0.75f) : night::textDim));
                g.setFont (font (juce::jlimit (9.5f, 11.5f, orb.getHeight() * 0.24f)));
                g.drawFittedText (juce::String::fromUTF8 (c.numeral.c_str()), text.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
                if (faded)
                    g.endTransparencyLayer();
                if (isFocus)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.9f));
                    g.drawRoundedRectangle (orb.expanded (2.5f), orb.getHeight() * 0.5f + 2.5f, 1.5f);
                }
                if (showRel && ! isFocus && ! faded)
                {
                    // The function (an amber tag at the top right), the notes in common (dots at
                    // the bottom left) and the voice-leading distance (a number at the bottom right).
                    if (! relation.label.empty())
                    {
                        const auto tagText = juce::String::fromUTF8 (relation.label.c_str());
                        const float w = (float) juce::GlyphArrangement::getStringWidthInt (font (9.5f, true), tagText) + 8.0f;
                        const auto tag = juce::Rectangle<float> (w, 13.0f).withPosition (orb.getRight() - w + 4.0f, orb.getY() - 5.0f);
                        g.setColour (night::accent);
                        g.fillRoundedRectangle (tag, 6.5f);
                        g.setColour (night::bgTop);
                        g.setFont (font (9.5f, true));
                        g.drawText (tagText, tag, juce::Justification::centred);
                    }
                    g.setColour (night::accent.withAlpha (0.95f));
                    for (int k = 0; k < juce::jmin (relation.common, 4); ++k)
                        g.fillEllipse (orb.getX() + 7.0f + 5.0f * (float) k, orb.getBottom() - 4.5f, 3.0f, 3.0f);
                    if (relation.distance > 0)
                    {
                        g.setColour (colours::dim);
                        g.setFont (font (9.0f, true));
                        g.drawText (juce::String (relation.distance), juce::Rectangle<float> (16.0f, 10.0f).withPosition (orb.getRight() - 15.0f, orb.getBottom() - 9.0f),
                                    juce::Justification::centredRight);
                    }
                }
                if (rank > 0)
                {
                    // The match's rank: an amber badge on the orb's top-left (PLAN.md §5.1).
                    const auto badge = juce::Rectangle<float> (16.0f, 16.0f).withCentre (orb.getTopLeft().translated (6.0f, 3.0f));
                    g.setColour (night::accent);
                    g.fillEllipse (badge);
                    g.setColour (night::bgTop);
                    g.setFont (font (10.0f, true));
                    g.drawText (juce::String (rank), badge, juce::Justification::centred);
                }
            }
        }

        // The scroll position: a thin track on the right.
        const auto track = juce::Rectangle<float> ((float) getLocalBounds().reduced (padding).getRight() - (float) scrollWidth, rows.getY() + 2.0f,
                                                   (float) scrollWidth * 0.5f, rows.getHeight() - 4.0f);
        g.setColour (colours::outline.withAlpha (0.6f));
        g.fillRoundedRectangle (track, 1.5f);
        const float frac = (float) shown / (float) numTierVariants;
        const float top = 1.0f - (float) (bottomRow + shown) / (float) numTierVariants;
        g.setColour (colours::dim);
        g.fillRoundedRectangle (track.withY (track.getY() + track.getHeight() * top).withHeight (track.getHeight() * frac), 1.5f);
    }

    //==========================================================================
    // The mouse.

    void PalettePanel::endPreview()
    {
        if (previewing)
            processor.endPreview();
        previewing = false;
    }

    void PalettePanel::mouseDown (const juce::MouseEvent& e)
    {
        const auto ref = cellAt (e.position);
        pressed = ref;
        pressedAt = e.position;
        dragging = false;
        if (! ref.valid())
            return;
        if (e.mods.isPopupMenu())
        {
            showMenu (ref);
            return;
        }
        processor.previewChord (progressionChordFromCell (*cell (ref)));
        previewing = true;
        if (onFocus)
            onFocus (*cell (ref));
        repaint();
    }

    void PalettePanel::mouseDrag (const juce::MouseEvent& e)
    {
        if (! pressed.valid() || dragging || e.position.getDistanceFrom (pressedAt) < 5.0f)
            return;
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            endPreview();
            const auto orb = cellBounds (pressed);
            const auto image = createComponentSnapshot (orb.toNearestInt().expanded (2), true, 1.0f);
            container->startDragging (TimelinePanel::chordDragDescription (progressionChordFromCell (*cell (pressed))), this,
                                      juce::ScaledImage (image), true);
            repaint();
        }
    }

    void PalettePanel::mouseUp (const juce::MouseEvent&)
    {
        endPreview();
        pressed = {};
        dragging = false;
        repaint();
    }

    void PalettePanel::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto ref = cellAt (e.position);
        if (ref.valid() && onAdd)
            onAdd (progressionChordFromCell (*cell (ref)));
    }

    void PalettePanel::mouseMove (const juce::MouseEvent& e)
    {
        const auto ref = cellAt (e.position);
        if (ref == hovered)
            return;
        hovered = ref;
        setTooltip (describe (ref));
        repaint();
    }

    void PalettePanel::mouseExit (const juce::MouseEvent&)
    {
        hovered = {};
        setTooltip ({});
        repaint();
    }

    void PalettePanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        wheelAccumulator += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
        const int steps = (int) (wheelAccumulator / 0.12f);
        if (steps != 0)
        {
            wheelAccumulator -= (float) steps * 0.12f;
            scrollRows (steps);   // up the wheel: up the tiers
        }
    }

    void PalettePanel::showMenu (CellRef ref)
    {
        const auto chord = progressionChordFromCell (*cell (ref));
        juce::Component::SafePointer<PalettePanel> self (this);
        juce::PopupMenu menu;
        menu.addSectionHeader (juce::String::fromUTF8 (cell (ref)->name.c_str()));
        menu.addItem ("Add at the insertion marker", [self, chord] { if (self != nullptr && self->onAdd) self->onAdd (chord); });
        menu.addItem ("Add as a new section", [self, chord] { if (self != nullptr && self->onAddAsSection) self->onAddAsSection (chord); });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }
}
