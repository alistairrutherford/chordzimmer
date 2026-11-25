// Match and Chord-from-notes (PLAN.md §3.4, §5 item 4; Phase 5): a popover from the footer's
// Match button. It lies over the transport and the timeline, not the palette, so the palette's
// rank badges stay in view. Three cards (Notes and Chord from notes side by side, Matches under
// them):
//
//   NOTES             Listen (play a phrase in, keys or a clip: it passes through and its notes
//                     are kept with their lengths), the last 1–16 bars of it, Use heard (copy
//                     what was heard into the picker), Clear; and the note picker, a one-octave
//                     strip of 12 keys showing how long each was heard (teal) and the notes
//                     picked (amber). Picked notes, if any, are the note set; else what was heard
//   MATCHES           the 8 best palette chords for the note set (Matcher.h), as ranked chips:
//                     click to hear, double-click to add at the timeline's marker. ±2 fifths
//                     also searches the palettes two fifths either side (borrowed chords). While
//                     the popover is open, the palette shows the ranks on its cells and fades
//                     the rest
//   CHORD FROM NOTES  the note set as one chord on a root (Auto: the namer's) and a bass, named
//                     (ChordFromNotes.h): Set replaces the selected timeline chord, Add puts it
//                     in at the marker
#pragma once

#include "Panels.h"
#include "../core/engine/NoteCollector.h"
#include "../core/midiio/MidiImport.h"
#include "../core/model/ChordFromNotes.h"

namespace chordzimmer::ui
{
    class NotePicker : public juce::Component
    {
    public:
        NotePicker();
        void setHeard (const NoteSet&);
        unsigned picked() const noexcept { return pickedMask; }
        void setPicked (unsigned mask);
        juce::Rectangle<float> keyBounds (int pitchClass) const;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        std::function<void()> onChange;

    private:
        PianoLayout layout;
        NoteSet heard;
        unsigned pickedMask = 0;
    };

    class MatchList : public juce::Component
    {
    public:
        MatchList();
        void setMatches (const std::vector<ChordMatch>&);
        int chipAt (juce::Point<float>) const;            // -1 if none
        juce::Rectangle<float> chipBounds (int index) const;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        std::function<void (const ProgressionChord&)> onPreview, onAdd;
        std::function<void()> onEndPreview;

    private:
        std::vector<ChordMatch> matches;
    };

    class MatchPanel : public juce::Component,
                       public juce::FileDragAndDropTarget
    {
    public:
        MatchPanel (ChordZimmerProcessor&, Bindings&);

        // From the editor's timer: what was heard, the palette (for the matches) and the home key.
        void refresh (const UiModel&, const PaletteContext&);

        // The note set (picked, else heard), the matches for it, and Chord-from-notes' chord.
        const NoteSet& noteSet() const noexcept { return notes; }
        const std::vector<ChordMatch>& getMatches() const noexcept { return matches; }
        ProgressionChord fromNotesChord() const;
        juce::String fromNotesName() const;
        // The timeline chord Set would replace ("" for none).
        void setSetTarget (const juce::String& chordName);

        // A .mid dropped on the popover (PLAN.md §6 Phase 9): its melody (the notes outside its
        // chords, MidiImport.h) is the note set, until Clear (picked notes still win).
        bool importFile (const juce::File&);
        // Open .mid… (Phase 9): the same through a file chooser, which works in Live, where
        // drops don't reach a plug-in window. `chooseFile` asks for it (tests replace it).
        void chooseAndImport();
        std::function<void (std::function<void (const juce::File&)>)> chooseFile;
        juce::TextButton openFile { juce::String::fromUTF8 ("Open .mid\xe2\x80\xa6") };
        const juce::String& importedName() const noexcept { return importedFrom; }
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray& files, int, int) override;

        // To the timeline (the editor wires them) and the processor's previews.
        std::function<void (const ProgressionChord&)> onSet, onAdd;

        juce::String infoKeyAt (juce::Point<int>) const;
        void paint (juce::Graphics&) override;
        void resized() override;
        static int preferredHeight();

        ParamControl listen;
        juce::ComboBox bars, root, bass;
        juce::TextButton useHeard { "Use heard" }, clear { "Clear" }, wide { juce::String::fromUTF8 ("\xc2\xb1" "2 fifths") };
        juce::TextButton set { "Set" }, add { "Add" };
        NotePicker picker;
        MatchList list;

    private:
        void recompute();
        double windowBeats() const;

        ChordZimmerProcessor& processor;
        PaletteContext context;
        int homeKey = 0;
        Scale homeScale = Scale::Major;
        int beatsPerBar = 4;
        NoteSet heard, notes, imported;
        juce::String importedFrom;
        bool fileOver = false;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Rectangle<int> importedArea;
        std::vector<ChordMatch> matches;
        juce::String name, setTarget;

        struct Card
        {
            juce::String title, infoKey;
            juce::Rectangle<int> bounds;
        };
        std::array<Card, 3> cards;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;
        juce::Rectangle<int> nameArea;

        static constexpr int titleHeight = 22, rowHeight = 40, gap = 8, margin = 12, captionHeight = 15;
    };
}
