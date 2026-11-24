#include "InfoView.h"

namespace chordzimmer::ui
{
    namespace
    {
        struct Info { const char* key; const char* title; const char* text; };

        // One entry per control, display and card. Keep each to a line or two: what it does, and
        // what each of its settings means. UTF-8.
        const Info table[] = {
            // ---- The top bar ----------------------------------------------------------------
            { "enable", "On", "On: keys play chords and the timeline can play. Off: notes pass through unchanged." },
            { "ui.prev", "Previous preset", "Loads the preset before this one." },
            { "ui.next", "Next preset", "Loads the preset after this one." },
            { "ui.presetName", "Presets",
              "The preset loaded. Click to browse and search every preset, factory and your own, and to show their folder." },
            { "ui.save", "Save", "Saves these settings, and the progression, as a preset of your own, with a name and a category." },
            { "ui.export", "Export",
              "Drag into a track: the last bars ChordZimmer played (up to 32), as a MIDI file. Ready once a bar has finished." },
            { "ui.keys", "Keys", "Shows or hides the keyboard along the bottom." },

            // ---- The transport ---------------------------------------------------------------
            { "ui.rewind", "Rewind", "Back to the start of the loop region. In Sync mode the song's position decides, so use Live's own." },
            { "play", "Play", "On: the timeline plays: with the song (Sync) or on its own clock (Preview). Space also toggles it." },
            { "loop", "Loop", "On: the loop region repeats: the selected chords or sections, else the whole progression. L also toggles it." },
            { "syncMode", "Mode",
              "Sync: locked to the song's position from Start bar, while Live plays. Preview: its own clock at Live's tempo, also with Live stopped." },
            { "startBar", "Start bar", "In Sync mode, the song bar the progression starts at (looping, it also plays before it)." },
            { "ui.snap", "Snap", "The grid a chord's or section's edge snaps to when dragged: 1/16 to a bar, or Off." },
            { "ui.undo", "Undo", "Undoes the last change to the timeline (Cmd-Z)." },
            { "ui.redo", "Redo", "Redoes what Undo undid (Shift-Cmd-Z)." },
            { "display.now", "Now", "The chord playing (or the one selected), its numeral in the home key, and the bar and beat." },

            // ---- The timeline ----------------------------------------------------------------
            { "ui.dragSelection", "Drag MIDI",
              "Drag into a track: the selected chords or sections (nothing selected: all) as a MIDI clip, section names as markers. Alt: no strum." },
            { "display.import", "MIDI file", "Drop a .mid on the timeline for its chords (its markers become sections), or on Match for its melody." },
            { "display.timeline", "Timeline",
              "Sections of chords. Drag a chord or tab to move it (Alt: copy), its edge to resize; Shift-click selects more; right-click for more." },

            // ---- The palette -----------------------------------------------------------------
            { "ui.relations", "Relations", "Shows how each chord relates to the one last clicked or selected: its function (V/vi, IV, rel, sub), notes in common (dots), voices' moves." },
            { "display.next", "Next", "Five chords that usually follow the one last clicked or selected. Click to hear, double-click to add at the marker." },
            { "display.palette", "Palette",
              "The chords of the palette's key by degree and complexity. Filled: in the home key; ring: not. Click to hear, drag to the timeline." },

            // ---- The footer ------------------------------------------------------------------
            { "key", "Home key", "The key the progression is in: the numerals and the filled (in key) chords are measured against it." },
            { "scale", "Scale", "The home key's scale (or mode)." },
            { "ui.home", "Home", "Brings the palette back to the home key and scale. Amber while the palette is borrowing." },
            { "ui.fifthDown", "A fifth down", "Moves the palette a fifth down the circle (C to F), keeping the mode, to borrow chords." },
            { "ui.fifthUp", "A fifth up", "Moves the palette a fifth up the circle (C to G), keeping the mode, to borrow chords." },
            { "ui.mode", "Mode", "The palette's mode: the parallel modes of its family on the same tonic (C Ionian, C Dorian …)." },
            { "display.dial", "Circle of fifths", "Home in amber, the palette's key lit, the way between them drawn. Click a key to move the palette there." },
            { "ui.performance", "Performance", "Opens how the chords play: strum, velocity, voicing, octave, and what notes played in do." },
            { "ui.match", "Match", "Opens Match: play or pick notes to see the palette chords that fit them, or make a chord of them." },

            // ---- The Match popover -----------------------------------------------------------
            { "card.notes", "Notes", "The notes to match: what Listen heard, or the notes picked on the strip (picked ones win)." },
            { "listen", "Listen", "On: notes played in (keys or a clip) pass through and are kept, with their lengths, for Match. No chords meanwhile." },
            { "ui.matchBars", "Last", "How much of what was heard counts: the last 1 to 16 bars of playing (silence after doesn't count)." },
            { "ui.matchUseHeard", "Use heard", "Copies the notes heard onto the strip as picked notes, to add or take some away." },
            { "ui.matchClear", "Clear", "Forgets the notes heard and the notes picked." },
            { "display.picker", "Note picker", "Click keys to pick notes (amber). Teal shows how long each note was heard." },
            { "card.matches", "Matches", "The palette chords that fit the notes best, ranked. Their ranks also show on the palette." },
            { "ui.matchWide", "Wider", "On: also searches the palettes two fifths either side of this one (borrowed chords, marked with an arrow)." },
            { "display.matches", "Matches", "The best chords for the notes: long notes on the root and 3rd count most, clashes count against. Click to hear, double-click to add." },
            { "card.fromNotes", "Chord from notes", "The notes as one chord, any notes at all, on the root you choose." },
            { "ui.fromNotesRoot", "Root", "The chord's root. Auto: the one its name suggests." },
            { "ui.fromNotesBass", "Bass", "A bass note under the chord (a slash chord), or none." },
            { "display.fromNotes", "Chord", "The chord the notes make, named, with its numeral in the home key." },
            { "ui.fromNotesSet", "Set", "Replaces the selected timeline chord with this one (its length stays)." },
            { "ui.fromNotesAdd", "Add", "Puts this chord into the timeline at the insertion marker." },

            // ---- The performance popover -----------------------------------------------------
            { "card.strum", "Strum", "The chord's notes start one after another instead of together." },
            { "strum", "Strum direction",
              "Off: together. Up: lowest first. Down: highest first. Alternating: up, then down. Random: a new order each time." },
            { "strumRate", "Strum rate", "The time between strummed notes, a note value (at the last tempo while stopped)." },
            { "card.velocity", "Velocity", "Each note's velocity: the peak, or a curve across the bar below it." },
            { "velocity", "Velocity curve",
              "Off: the timeline at the peak, keys as played. Up, Down and their halves and quarters: across the bar. Afrobeat, House…: accents." },
            { "velocityPeak", "Velocity peak", "The loudest velocity (1 to 127): the timeline's velocity when the curve is Off." },
            { "velocityRange", "Velocity range", "How far the curve dips below the peak. 0%: every note at the peak. 100%: the curve spans 1 to the peak." },
            { "card.voicing", "Voicing", "How the chord is spread, given a bass, led from one chord to the next, placed and stacked." },
            { "voicing", "Voicing",
              "Close: as built. Open: the second-highest note an octave down. Spread: the lowest stays, the rest up. Wide: the 2nd and 4th highest down." },
            { "bass", "Bass", "Adds the chord's root (Root), or its root and fifth (Root + 5th), an octave below the chord." },
            { "voiceLeading", "Voice leading", "Smooth: each chord takes the inversion nearest the one before, so the voices move little. Off: as written." },
            { "chordOctave", "Octave", "The register the chords play in, up or down 3 octaves." },
            { "stack", "Stack", "Adds every note again an octave up (+1), two up (+2), or both (+1&2)." },
            { "card.keys", "Keys in", "What notes played into ChordZimmer do: play chords of the home key, or be heard (Heard, Detect)." },
            { "chordGroup", "Chord Group", "The chords a key plays: triads, sevenths and other stacks, built on each degree of the scale." },
            { "progression", "Progression", "Which chord each white key plays: the degrees in order, or a progression's." },
            { "hold", "Hold",
              "Latch: a chord keeps playing after you let go, until the next chord. Bar: the same, ending at the latest at the next bar line." },
            { "keyOctave", "Key octave",
              "Ignore: every octave plays the same chord. Follow: the octave you play moves the chord (C2 plays it as written)." },
            { "analyse", "Heard",
              "Click to listen: play, or let a clip play in, and ChordZimmer guesses the key (no chords meanwhile). Click the guess to use it." },
            { "detect", "Detect",
              "Capture: play chords in (keys or a clip). Use result: they go on the end of the timeline as a new section, with the key heard." },

            // ---- The output keyboard --------------------------------------------------------
            { "display.keyboard", "Output", "The notes ChordZimmer plays (lit) and the keys held (outlined). Click to play." },

            // ---- Not on screen --------------------------------------------------------------
            { "pattern", "Pattern", "Kept for older sets: which workspace pattern (A to D) was shown. The timeline replaced the patterns." },
            { "testPattern", "Test pattern", "For testing: plays a fixed pattern of chord keys with the host's transport. Set from the host only." },
            { "testWorkspace", "Test workspace", "For testing: loads the test rig's patterns, progression and keys. Set from the host only." },
        };

        const Info* find (const juce::String& key)
        {
            if (key.isEmpty())
                return nullptr;
            for (const auto& i : table)
                if (key == i.key)
                    return &i;
            return nullptr;
        }
    }

    juce::String InfoView::titleFor (const juce::String& key)
    {
        const auto* i = find (key);
        return i != nullptr ? juce::String::fromUTF8 (i->title) : juce::String();
    }

    juce::String InfoView::textFor (const juce::String& key)
    {
        const auto* i = find (key);
        return i != nullptr ? juce::String::fromUTF8 (i->text) : juce::String();
    }

    bool InfoView::has (const juce::String& key) { return find (key) != nullptr; }

    juce::StringArray InfoView::keys()
    {
        juce::StringArray out;
        for (const auto& i : table)
            out.add (i.key);
        return out;
    }

    juce::String InfoView::hint() { return "Point at a control or display to see what it does."; }

    void InfoView::showKey (const juce::String& newKey, const juce::String& live)
    {
        const auto* i = find (newKey);
        const auto newTitle = i != nullptr ? juce::String::fromUTF8 (i->title) : juce::String();
        const auto newText = i == nullptr ? hint() : live.isNotEmpty() ? live : juce::String::fromUTF8 (i->text);
        key = i != nullptr ? newKey : juce::String();
        if (newTitle == title && newText == text)
            return;
        title = newTitle;
        text = newText;
        repaint();
    }

    void InfoView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        drawGlassPanel (g, r, 10.0f);
        r = r.reduced (14.0f, 8.0f);
        if (title.isNotEmpty())
        {
            g.setColour (night::accent);
            g.setFont (headingFont (13.0f, true));
            g.drawText (title, r.removeFromTop (18.0f), juce::Justification::centredLeft);
        }
        g.setColour (title.isEmpty() ? colours::dim : colours::text.withAlpha (0.85f));
        g.setFont (font (12.5f));
        g.drawFittedText (text, r.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }
}
