#include "MatchPanel.h"
#include "../core/model/TimelineLayout.h"

namespace chordzimmer::ui
{
    namespace
    {
        const char* const sharpNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int barChoices[] = { 1, 2, 4, 8, 16 };
    }

    //==========================================================================
    NotePicker::NotePicker()
    {
        setComponentID ("display.picker");
        layout.lo = 60;
        layout.hi = 71;
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void NotePicker::setHeard (const NoteSet& s)
    {
        if (s.weight == heard.weight)
            return;
        heard = s;
        repaint();
    }

    void NotePicker::setPicked (unsigned mask)
    {
        pickedMask = mask & 0xfffu;
        repaint();
        if (onChange)
            onChange();
    }

    juce::Rectangle<float> NotePicker::keyBounds (int pc) const
    {
        auto l = layout;
        l.area = getLocalBounds().toFloat().reduced (1.0f);
        return l.keyBounds (60 + pc, 0.6f);
    }

    void NotePicker::paint (juce::Graphics& g)
    {
        double most = 0.0;
        for (double w : heard.weight)
            most = juce::jmax (most, w);
        for (const bool black : { false, true })
            for (int pc = 0; pc < 12; ++pc)
            {
                if (PianoLayout::isBlack (60 + pc) != black)
                    continue;
                const auto r = keyBounds (pc).reduced (black ? 0.0f : 0.5f, 0.0f);
                const bool picked = ((pickedMask >> pc) & 1u) != 0;
                const float heardLevel = most > 0.0 ? (float) (heard.weight[(size_t) pc] / most) : 0.0f;
                g.setColour (black ? colours::blackKey : colours::whiteKey.withMultipliedBrightness (0.92f));
                g.fillRoundedRectangle (r, 3.0f);
                if (heardLevel > 0.0f)
                {
                    // How long it was heard: a teal bar rising from the key's bottom.
                    const auto bar = r.reduced (2.0f).withTrimmedTop (r.getHeight() * (1.0f - heardLevel) * 0.9f);
                    g.setColour (night::majorFrom.withAlpha (0.35f + 0.5f * heardLevel));
                    g.fillRoundedRectangle (bar, 2.0f);
                }
                if (picked)
                {
                    g.setColour (night::accent);
                    g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 2.0f);
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getCentreX(), r.getBottom() - 7.0f }));
                }
                g.setColour (black ? colours::outline : colours::outline.withAlpha (0.5f));
                g.drawRoundedRectangle (r, 3.0f, 0.75f);
                if (! black)
                {
                    g.setColour (colours::whiteKeyText.withAlpha (0.7f));
                    g.setFont (font (9.0f));
                    g.drawText (sharpNames[pc], r.withTrimmedBottom (12.0f).removeFromBottom (12.0f), juce::Justification::centred);
                }
            }
    }

    void NotePicker::mouseDown (const juce::MouseEvent& e)
    {
        auto l = layout;
        l.area = getLocalBounds().toFloat().reduced (1.0f);
        const int note = l.noteAt (e.position, 0.6f);
        if (note >= 0)
            setPicked (pickedMask ^ (1u << (note - 60)));
    }

    //==========================================================================
    MatchList::MatchList() { setComponentID ("display.matches"); }

    void MatchList::setMatches (const std::vector<ChordMatch>& m)
    {
        const auto same = [&]
        {
            if (m.size() != matches.size())
                return false;
            for (size_t i = 0; i < m.size(); ++i)
                if (m[i].cell.name != matches[i].cell.name || m[i].fifths != matches[i].fifths || m[i].cell.numeral != matches[i].cell.numeral)
                    return false;
            return true;
        };
        if (same())
            return;
        matches = m;
        repaint();
    }

    juce::Rectangle<float> MatchList::chipBounds (int i) const
    {
        const auto r = getLocalBounds().toFloat();
        const float w = r.getWidth() / 8.0f;
        return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
    }

    int MatchList::chipAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) matches.size(); ++i)
            if (chipBounds (i).contains (p))
                return i;
        return -1;
    }

    void MatchList::paint (juce::Graphics& g)
    {
        if (matches.empty())
        {
            g.setColour (colours::faint);
            g.setFont (font (12.0f));
            g.drawText ("Listen, or pick notes, to see the chords that fit them.", getLocalBounds(), juce::Justification::centredLeft);
            return;
        }
        for (int i = 0; i < (int) matches.size(); ++i)
        {
            const auto& m = matches[(size_t) i];
            const auto chip = chipBounds (i).reduced (3.0f, 1.0f);
            drawChordOrb (g, chip, m.cell.family, m.cell.inKey, night::glowIdle * 0.5f, chip.getHeight() * 0.5f);
            const bool darkInk = m.cell.inKey && m.cell.family == ChordFamily::Major;
            auto text = chip.reduced (4.0f, 2.0f);
            g.setColour (darkInk ? night::bgTop : night::text);
            g.setFont (headingFont (13.0f, true));
            g.drawFittedText (juce::String::fromUTF8 (m.cell.name.c_str()), text.removeFromTop (text.getHeight() * 0.56f).toNearestInt(),
                              juce::Justification::centredBottom, 1, 0.6f);
            g.setColour ((darkInk ? night::bgTop : night::text).withAlpha (0.7f));
            g.setFont (font (9.5f));
            const juce::String where = m.fifths == 0 ? juce::String() : juce::String::fromUTF8 (m.fifths < 0 ? " \xe2\x97\x80" : " \xe2\x96\xb6") + juce::String (std::abs (m.fifths));
            g.drawFittedText (juce::String::fromUTF8 (m.cell.numeral.c_str()) + where, text.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
            // The rank: an amber badge.
            const auto badge = juce::Rectangle<float> (14.0f, 14.0f).withCentre (chip.getTopLeft().translated (5.0f, 4.0f));
            g.setColour (night::accent);
            g.fillEllipse (badge);
            g.setColour (night::bgTop);
            g.setFont (font (9.5f, true));
            g.drawText (juce::String (m.rank), badge, juce::Justification::centred);
        }
    }

    void MatchList::mouseDown (const juce::MouseEvent& e)
    {
        const int i = chipAt (e.position);
        if (i >= 0 && onPreview)
            onPreview (progressionChordFromCell (matches[(size_t) i].cell));
    }

    void MatchList::mouseUp (const juce::MouseEvent&)
    {
        if (onEndPreview)
            onEndPreview();
    }

    void MatchList::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const int i = chipAt (e.position);
        if (i >= 0 && onAdd)
            onAdd (progressionChordFromCell (matches[(size_t) i].cell));
    }

    //==========================================================================
    MatchPanel::MatchPanel (ChordZimmerProcessor& p, Bindings& b) : listen (b, params::listen, "Listen"), processor (p)
    {
        cards[0] = { "Notes", "card.notes", {} };
        cards[1] = { "Matches", "card.matches", {} };
        cards[2] = { "Chord from notes", "card.fromNotes", {} };

        for (int i = 0; i < (int) std::size (barChoices); ++i)
            bars.addItem (juce::String (barChoices[i]) + (barChoices[i] == 1 ? " bar" : " bars"), i + 1);
        bars.setSelectedId (3, juce::dontSendNotification);   // 4 bars
        bars.setComponentID ("ui.matchBars");
        bars.onChange = [this] { recompute(); };
        useHeard.setComponentID ("ui.matchUseHeard");
        useHeard.onClick = [this] { picker.setPicked (heard.mask()); };
        clear.setComponentID ("ui.matchClear");
        clear.onClick = [this]
        {
            processor.clearCollectedNotes();
            heard = {};
            picker.setHeard (heard);
            picker.setPicked (0);
        };
        wide.setComponentID ("ui.matchWide");
        wide.setClickingTogglesState (true);
        wide.onClick = [this] { recompute(); };
        picker.onChange = [this] { recompute(); };

        root.setComponentID ("ui.fromNotesRoot");
        root.addItem ("Auto", 1);
        bass.setComponentID ("ui.fromNotesBass");
        bass.addItem ("None", 1);
        for (int pc = 0; pc < 12; ++pc)
        {
            root.addItem (sharpNames[pc], pc + 2);
            bass.addItem (sharpNames[pc], pc + 2);
        }
        root.setSelectedId (1, juce::dontSendNotification);
        bass.setSelectedId (1, juce::dontSendNotification);
        root.onChange = [this] { recompute(); };
        bass.onChange = [this] { recompute(); };
        set.setComponentID ("ui.fromNotesSet");
        add.setComponentID ("ui.fromNotesAdd");
        set.onClick = [this] { if (onSet && ! fromNotesChord().chord.empty()) onSet (fromNotesChord()); };
        add.onClick = [this] { if (onAdd && ! fromNotesChord().chord.empty()) onAdd (fromNotesChord()); };

        list.onPreview = [this] (const ProgressionChord& c) { processor.previewChord (c); };
        list.onEndPreview = [this] { processor.endPreview(); };
        list.onAdd = [this] (const ProgressionChord& c) { if (onAdd) onAdd (c); };

        for (auto* c : std::initializer_list<juce::Component*> { &listen, &bars, &useHeard, &clear, &picker, &wide, &list, &root, &bass, &set, &add })
            addAndMakeVisible (c);
        recompute();
    }

    int MatchPanel::preferredHeight()
    {
        const int card = titleHeight + rowHeight + 14;
        return 2 * margin + 2 * card + gap;
    }

    double MatchPanel::windowBeats() const
    {
        const int i = juce::jlimit (0, (int) std::size (barChoices) - 1, bars.getSelectedId() - 1);
        return barChoices[i] * (double) beatsPerBar;
    }

    void MatchPanel::refresh (const UiModel& m, const PaletteContext& ctx)
    {
        const bool contextChanged = ctx.paletteKey != context.paletteKey || ctx.paletteScale != context.paletteScale
                                 || ctx.homeKey != context.homeKey || ctx.homeScale != context.homeScale || m.beatsPerBar != beatsPerBar;
        context = ctx;
        homeKey = m.settings.key;
        homeScale = m.settings.scale;
        beatsPerBar = juce::jlimit (1, 16, m.beatsPerBar);
        const NoteSet now = noteSetFromCollected (processor.collectedNotes(), windowBeats());
        const bool heardChanged = now.weight != heard.weight || now.bass != heard.bass;
        heard = now;
        picker.setHeard (heard);
        if (heardChanged || contextChanged)
            recompute();
    }

    ProgressionChord MatchPanel::fromNotesChord() const
    {
        const unsigned mask = notes.mask();
        if (mask == 0)
            return {};
        const int r = root.getSelectedId() > 1 ? root.getSelectedId() - 2 : suggestedRoot (mask, notes.bass);
        const int b = bass.getSelectedId() > 1 ? bass.getSelectedId() - 2 : -1;
        return chordFromNotes (mask, r, b);
    }

    juce::String MatchPanel::fromNotesName() const { return name; }

    void MatchPanel::setSetTarget (const juce::String& chordName)
    {
        set.setEnabled (chordName.isNotEmpty() && notes.mask() != 0);
        if (chordName != setTarget)
        {
            setTarget = chordName;
            repaint (nameArea);
        }
    }

    void MatchPanel::recompute()
    {
        const unsigned picked = picker.picked();
        notes = picked != 0 ? NoteSet::fromMask (picked) : heard;
        MatchOptions o;
        o.fifthsAround = wide.getToggleState() ? 2 : 0;
        matches = matchPalette (notes, context, o);
        list.setMatches (matches);

        const auto chord = fromNotesChord();
        if (chord.chord.empty())
            name = {};
        else
        {
            const auto label = timelineChordLabel (chord, homeKey, homeScale);
            name = juce::String::fromUTF8 (label.name.c_str()) + "  " + juce::String::fromUTF8 (label.numeral.c_str());
        }
        add.setEnabled (! chord.chord.empty());
        repaint (nameArea);
    }

    juce::String MatchPanel::infoKeyAt (juce::Point<int> p) const
    {
        if (nameArea.contains (p))
            return "display.fromNotes";
        for (const auto& c : cards)
            if (c.bounds.contains (p))
                return c.infoKey;
        return {};
    }

    void MatchPanel::paint (juce::Graphics& g)
    {
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
        g.setFont (font (12.0f));
        g.setColour (colours::dim);
        for (const auto& [area, text] : captions)
            g.drawText (text, area, juce::Justification::centredLeft);

        // Chord from notes: the chord's name and numeral.
        if (name.isEmpty())
        {
            g.setColour (colours::faint);
            g.setFont (font (12.0f));
            g.drawText ("Pick or play notes", nameArea, juce::Justification::centredLeft);
        }
        else
        {
            auto area = nameArea;
            g.setColour (colours::text);
            g.setFont (headingFont (17.0f, true));
            g.drawFittedText (name, area.removeFromTop (area.getHeight() * 3 / 5), juce::Justification::bottomLeft, 1, 0.6f);
            g.setColour (colours::dim);
            g.setFont (font (10.5f));
            g.drawFittedText (setTarget.isEmpty() ? juce::String ("Select a timeline chord to Set")
                                                  : juce::String::fromUTF8 ("Set replaces ") + setTarget,
                              area, juce::Justification::topLeft, 1, 0.7f);
        }
    }

    void MatchPanel::resized()
    {
        captions.clear();
        auto r = getLocalBounds().reduced (margin);
        const int card = titleHeight + rowHeight + 14;
        auto top = r.removeFromTop (card);
        r.removeFromTop (gap);
        cards[0].bounds = top.removeFromLeft ((top.getWidth() - gap) * 11 / 20);
        top.removeFromLeft (gap);
        cards[2].bounds = top;
        cards[1].bounds = r.removeFromTop (card);
        const auto rowOf = [&] (const Card& c) { return c.bounds.reduced (10, 0).withTrimmedTop (titleHeight + 2).withHeight (rowHeight); };
        const auto captioned = [&] (juce::Rectangle<int>& row, juce::Component& c, int w, const juce::String& caption)
        {
            auto col = row.removeFromLeft (w).reduced (3, 0);
            row.removeFromLeft (4);
            if (caption.isNotEmpty())
                captions.push_back ({ col.removeFromTop (captionHeight).withTrimmedLeft (2), caption });
            else
                col.removeFromTop (captionHeight);
            c.setBounds (col.withSizeKeepingCentre (col.getWidth(), juce::jmin (col.getHeight(), 26)));
        };

        auto notesRow = rowOf (cards[0]);
        listen.setBounds (notesRow.removeFromLeft (70).reduced (3, 0).withTrimmedTop (captionHeight));
        notesRow.removeFromLeft (4);
        captioned (notesRow, bars, 84, "Last");
        captioned (notesRow, useHeard, 80, {});
        captioned (notesRow, clear, 58, {});
        picker.setBounds (notesRow.reduced (2, 0));

        auto matchRow = rowOf (cards[1]);
        captioned (matchRow, wide, 82, {});
        list.setBounds (matchRow.withTrimmedTop (4).withTrimmedBottom (2));

        auto fromRow = rowOf (cards[2]);
        const int combo = juce::jlimit (64, 84, fromRow.getWidth() / 5);
        captioned (fromRow, root, combo, "Root");
        captioned (fromRow, bass, combo, "Bass");
        add.setBounds (fromRow.removeFromRight (58).reduced (2, 0).withTrimmedTop (captionHeight).withSizeKeepingCentre (54, 26));
        set.setBounds (fromRow.removeFromRight (58).reduced (2, 0).withTrimmedTop (captionHeight).withSizeKeepingCentre (54, 26));
        nameArea = fromRow.reduced (6, 0).withTrimmedTop (captionHeight - 6);
    }
}
