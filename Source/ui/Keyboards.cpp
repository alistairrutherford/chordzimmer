#include "Keyboards.h"
#include "Theme.h"
#include "../core/theory/ChordNamer.h"
#include "../core/theory/RomanNumeral.h"

namespace chordzimmer::ui
{
    //==========================================================================
    bool PianoLayout::isBlack (int note) noexcept { return keys::whiteLetter (note) < 0; }

    int PianoLayout::whiteCount() const noexcept
    {
        int n = 0;
        for (int k = lo; k <= hi; ++k)
            n += isBlack (k) ? 0 : 1;
        return juce::jmax (1, n);
    }

    juce::Rectangle<float> PianoLayout::keyBounds (int note, float blackHeight) const
    {
        const float ww = area.getWidth() / (float) whiteCount();
        int whitesBefore = 0;
        for (int k = lo; k < note; ++k)
            whitesBefore += isBlack (k) ? 0 : 1;
        if (! isBlack (note))
            return { area.getX() + ww * (float) whitesBefore, area.getY(), ww, area.getHeight() };
        const float bw = ww * 0.62f;
        return { area.getX() + ww * (float) whitesBefore - bw * 0.5f, area.getY(), bw, area.getHeight() * blackHeight };
    }

    int PianoLayout::noteAt (juce::Point<float> p, float blackHeight) const
    {
        for (int k = lo; k <= hi; ++k)
            if (isBlack (k) && keyBounds (k, blackHeight).contains (p))
                return k;
        for (int k = lo; k <= hi; ++k)
            if (! isBlack (k) && keyBounds (k, blackHeight).contains (p))
                return k;
        return -1;
    }

    //==========================================================================
    void PlayingKeys::press (int note, int velocity)
    {
        if (note == held)
            return;
        release();
        if (note < 0 || note > 127)
            return;
        processor.pushUiNote (note, velocity, true, toPalette);
        held = note;
    }

    void PlayingKeys::release()
    {
        if (held >= 0)
            processor.pushUiNote (held, 0, false, toPalette);
        held = -1;
    }

    //==========================================================================
    ChordPads::ChordPads (ChordZimmerProcessor& p) : processor (p), playing (p, true)
    {
        setComponentID ("display.pads");          // its Info View entry
        setTooltip ("The chords: one per white key and, above them, the black keys' borrowed chords (in any octave). Click or drag across to play; "
                    "drag one down onto the workspace, or right-click it, to add it to the pattern");
    }

    void ChordPads::refresh (const UiModel& m)
    {
        std::array<bool, numChordSlots> now {};
        for (int n = 0; n < 128; ++n)
            if (const int slot = KeyMap::slotOf (n); slot >= 0 && (processor.isKeyDown (n) || processor.isPerforming (n)))
                now[(size_t) slot] = true;

        const bool changed = now != active || ! (m.display == model.display);
        model = m;
        active = now;
        if (changed)
            repaint();
    }

    juce::Rectangle<float> ChordPads::padBounds (int slot) const
    {
        // The black keys' row on top, its pads centred over the gaps between the white ones,
        // as on a piano: C# over C|D, D# over D|E, F# over F|G, G# over G|A, A# over A|B. Inset
        // so the orbs' glow has room.
        constexpr float gap = 10.0f;
        constexpr int whiteBefore[numBlackSlots] = { 0, 1, 3, 4, 5 };
        const auto area = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
        const float w = (area.getWidth() - gap * (numWhiteSlots - 1)) / (float) numWhiteSlots;
        const float blackHeight = std::round (area.getHeight() * 0.36f);
        if (slot >= numWhiteSlots && slot < numChordSlots)
        {
            const float centre = area.getX() + (w + gap) * (float) (whiteBefore[slot - numWhiteSlots] + 1) - gap * 0.5f;
            const float bw = w * 0.7f;
            return { centre - bw * 0.5f, area.getY(), bw, blackHeight };
        }
        return { area.getX() + (w + gap) * (float) slot, area.getY() + blackHeight + gap, w, area.getHeight() - blackHeight - gap };
    }

    int ChordPads::slotAt (juce::Point<float> p) const
    {
        for (int slot = 0; slot < numChordSlots; ++slot)
            if (padBounds (slot).contains (p))
                return slot;
        return -1;
    }

    void ChordPads::paint (juce::Graphics& g)
    {
        const auto& d = model.display;
        // The home scale's pitch classes, for the in-key (filled) / out-of-key (hollow) look.
        unsigned scaleMask = 0;
        for (int i = 0; i < 7; ++i)
            scaleMask |= 1u << ((d.context.key + scaleOffset (d.context.scale, i)) % 12);

        for (int slot = 0; slot < numChordSlots; ++slot)
        {
            const auto r = padBounds (slot);
            const bool lit = active[(size_t) slot];
            const bool black = slot >= numWhiteSlots;
            const auto& chord = d.chords[(size_t) slot];

            // Family from the chord's 3rd and 5th (PLAN.md §3.1; PaletteBuilder takes this over in
            // Phase 1), filled when every note is in the scale.
            unsigned mask = 0;
            for (const auto n : chordNotes (chord, d.context))
                mask |= 1u << (n % 12);
            const int rootPc = ((d.context.key + scaleOffset (d.context.scale, chord.degree) + chord.steps[0].accidental) % 12 + 12) % 12;
            const Family family = familyOf (mask, rootPc);
            const bool inKey = mask != 0 && (mask & ~scaleMask) == 0;
            const float glow = lit ? night::glowPlay : (slot == hovered ? night::glowHover : night::glowIdle);
            drawChordOrb (g, r, family, inKey, glow, black ? night::padRadius - 2.0f : night::padRadius);
            if (lit)
            {
                g.setColour (juce::Colours::white.withAlpha (0.16f));
                g.fillRoundedRectangle (r, night::padRadius);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawRoundedRectangle (r.reduced (1.0f), night::padRadius, 1.5f);
            }

            // Ink: dark on the bright (major, filled) orbs, light elsewhere.
            const bool darkInk = inKey && family == Family::Major;
            const auto ink = darkInk ? night::bgTop : night::text;
            const auto inkDim = darkInk ? night::bgTop.withAlpha (0.65f) : (inKey ? night::text.withAlpha (0.75f) : night::textDim);

            static constexpr const char* letters[numChordSlots] = { "C", "D", "E", "F", "G", "A", "B", "C#", "D#", "F#", "G#", "A#" };
            g.setColour (inkDim);
            g.setFont (font (black ? 10.0f : 11.0f, true));
            g.drawText (letters[slot], r.reduced (black ? 8.0f : 11.0f, black ? 5.0f : 8.0f).removeFromTop (14.0f),
                        juce::Justification::topLeft);

            // Black pads are shorter: their numeral and name sit lower, under the key letter, and
            // above the last-played stripe.
            auto text = black ? r.reduced (4.0f, 6.0f).withTrimmedTop (r.getHeight() * 0.14f).withTrimmedBottom (5.0f) : r.reduced (6.0f, 10.0f);
            const float h = text.getHeight();
            g.setColour (ink);
            g.setFont (headingFont (black ? juce::jlimit (14.0f, 24.0f, h * 0.36f) : juce::jlimit (18.0f, 32.0f, h * 0.26f), true));
            g.drawFittedText (juce::String (romanNumeral (chord, d.context.scale)),
                              text.removeFromTop (h * 0.55f).toNearestInt(), juce::Justification::centredBottom, 1);
            g.setColour (inkDim);
            g.setFont (font (black ? juce::jlimit (10.5f, 14.0f, h * 0.2f) : juce::jlimit (12.0f, 16.0f, h * 0.14f), true));
            g.drawFittedText (juce::String (nameSlot (chord, d.context).text()), text.withTrimmedTop (black ? 2.0f : 4.0f).toNearestInt(),
                              juce::Justification::centredTop, 1, 0.7f);

            if (slot == d.lastSlot)
            {
                // The last chord played: an amber stripe (on the black pads, at the right edge).
                g.setColour (night::accent);
                if (black)
                    g.fillRoundedRectangle (r.reduced (0.0f, r.getHeight() * 0.28f).removeFromRight (3.0f).translated (-6.0f, 0.0f), 1.5f);
                else
                    g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.3f, 0.0f).removeFromBottom (3.0f).translated (0.0f, -8.0f), 1.5f);
            }
        }
    }

    void ChordPads::mouseMove (const juce::MouseEvent& e)
    {
        if (const int slot = slotAt (e.position); slot != hovered)
        {
            hovered = slot;
            repaint();
        }
    }

    void ChordPads::mouseExit (const juce::MouseEvent&)
    {
        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void ChordPads::mouseDown (const juce::MouseEvent& e)
    {
        lastPad = slotAt (e.position);
        if (e.mods.isPopupMenu())
        {
            // Phase 6: put the pad's chord in the workspace.
            if (lastPad >= 0 && onAddToPattern)
            {
                juce::PopupMenu m;
                m.addItem ("Add to the pattern", [this, pad = lastPad] { onAddToPattern (pad); });
                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (padBounds (lastPad).toNearestInt())));
            }
            return;
        }
        mouseDrag (e);
    }

    void ChordPads::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        // Out of the pads (Phase 6): the last pad played is dragged, to drop on the workspace.
        if (! getLocalBounds().toFloat().contains (e.position) && lastPad >= 0)
        {
            playing.release();
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
                container->startDragging ("palette:" + juce::String (lastPad), this);
            return;
        }
        if (const int slot = slotAt (e.position); slot >= 0)
        {
            lastPad = slot;
            playing.press (KeyMap::keyForSlot (slot), 100);
        }
    }

    void ChordPads::mouseUp (const juce::MouseEvent&) { playing.release(); }

    //==========================================================================
    OutputKeyboard::OutputKeyboard (ChordZimmerProcessor& p) : processor (p), playing (p)
    {
        setComponentID ("display.keyboard");      // its Info View entry
        layout.lo = 21;
        layout.hi = 108;
        labelFont = font (9.0f);
        setTooltip ("Output: the notes ChordZimmer plays (lit) and the keys held (outlined). Click to play.");
    }

    void OutputKeyboard::refresh (const UiModel&)
    {
        std::array<bool, 128> s {}, k {};
        for (int n = 0; n < 128; ++n)
        {
            s[(size_t) n] = processor.isNoteSounding (n);
            k[(size_t) n] = processor.isKeyDown (n);
        }
        // Phase 8: only the keys that changed (while chords play, the whole keyboard every frame
        // was most of the editor's CPU).
        layout.area = getLocalBounds().toFloat();
        for (int n = layout.lo; n <= layout.hi; ++n)
            if (s[(size_t) n] != sounding[(size_t) n] || k[(size_t) n] != down[(size_t) n])
                repaint (layout.keyBounds (n).getSmallestIntegerContainer().expanded (1));
        sounding = s;
        down = k;
    }

    void OutputKeyboard::paint (juce::Graphics& g)
    {
        layout.area = getLocalBounds().toFloat();
        const auto clip = g.getClipBounds().toFloat();
        for (int pass = 0; pass < 2; ++pass)
            for (int n = layout.lo; n <= layout.hi; ++n)
            {
                const bool black = PianoLayout::isBlack (n);
                if (black != (pass == 1))
                    continue;
                auto r = layout.keyBounds (n);
                if (! r.intersects (clip))
                    continue;
                if (sounding[(size_t) n])
                    g.setGradientFill (juce::ColourGradient::vertical (night::majorFrom, r.getY(), night::majorTo, r.getBottom()));
                if (black)
                {
                    if (! sounding[(size_t) n])
                        g.setGradientFill (juce::ColourGradient::vertical (colours::blackKey.brighter (0.25f), r.getY(), colours::blackKey, r.getBottom()));
                    g.fillRoundedRectangle (r.withTrimmedTop (-2.0f), 2.0f);
                }
                else
                {
                    if (! sounding[(size_t) n])
                        g.setGradientFill (juce::ColourGradient::vertical (colours::whiteKey, r.getY(), colours::whiteKey.darker (0.12f), r.getBottom()));
                    g.fillRoundedRectangle (r.reduced (0.5f, 0.0f).withTrimmedTop (-3.0f), 3.0f);
                    if (n % 12 == 0)
                    {
                        g.setColour (colours::whiteKeyText.withAlpha (0.5f));
                        g.setFont (labelFont);
                        g.drawText ("C" + juce::String (n / 12 - 2), r.removeFromBottom (12.0f), juce::Justification::centred);
                    }
                }
                if (down[(size_t) n])
                {
                    g.setColour (night::accent);
                    g.drawRoundedRectangle (layout.keyBounds (n).reduced (1.0f), 2.0f, 2.0f);
                }
            }
    }

    void OutputKeyboard::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }

    void OutputKeyboard::mouseDrag (const juce::MouseEvent& e)
    {
        layout.area = getLocalBounds().toFloat();
        const int n = layout.noteAt (e.position);
        const int velocity = juce::jlimit (1, 127, 40 + (int) (87.0f * e.position.y / (float) juce::jmax (1, getHeight())));
        if (n >= 0)
            playing.press (n, velocity);
    }

    void OutputKeyboard::mouseUp (const juce::MouseEvent&) { playing.release(); }
}
