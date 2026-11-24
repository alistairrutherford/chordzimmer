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
