// The progression timeline (PLAN.md §3.3, §5 item 2, §5.1; Phase 4): sections side by side as
// labelled lanes of chord blocks, width = duration, with the playhead, the loop region and the
// insertion marker. Every gesture maps the mouse to a Progression edit (core/model/
// TimelineLayout.h, tested by Tests/test_timeline.cpp) and runs it through the processor's
// undo history (editProgression).
//
//   tab row     a section's name in its colour. Click selects it, Shift/Cmd-click adds it,
//               drag moves it (Alt: a copy), drag its right edge to scale its chords,
//               double-click renames, right-click for rename, colour, duplicate, delete
//   chord row   pills in their family's gradient (filled in key, a ring out of it), the name and
//               numeral in the home key. Click selects and sounds it, drag moves (Alt: a copy,
//               anywhere, also between sections), drag its right edge for its duration,
//               right-click for inversion, octave, bass, Replace with ▸ (PLAN.md §3.5: its
//               substitutes and the palette chords nearest it, Relations.h), duplicate, delete
//   empty space and the gaps between sections: a click puts the insertion marker there (Paste
//               and the palette's double-click go to it); a palette chord dropped there makes a
//               new section
//
// Import MIDI… (Phase 9), beside it: a .mid's chords at the insertion marker, through a file
// chooser (or drop the file on the timeline, where the host lets drops through).
//
// Drag MIDI (PLAN.md §3.7, Phase 7), at the right of the title row: drag it into a track (or the
// Finder) for the selection as a MIDI clip, everything when nothing is selected, with section
// names as markers; Alt for the chords without strum.
//
// Keys (the editor passes them on, PLAN.md §5): Delete, Cmd-C / X / V (at the marker), Cmd-D
// (duplicate in place), ← →, Cmd +/- (zoom). The wheel scrolls; Alt+Shift+wheel zooms. The
// selection is the loop region (ChordZimmerProcessor::setLoopSelection).
#pragma once

#include "Keyboards.h"
#include "Theme.h"
#include "../core/midiio/MidiImport.h"
#include "../core/model/Relations.h"
#include "../core/model/TimelineLayout.h"

namespace chordzimmer::ui
{
    // The selection's drag-out (ChordZimmerProcessor::writeSelectionExport).
    class SelectionDragHandle : public juce::Component,
                                public juce::SettableTooltipClient
    {
    public:
        explicit SelectionDragHandle (ChordZimmerProcessor&);

        // What it holds now (the timeline's selection, or everything).
        void update (const ChordProgression&, const Selection&, int key, Scale, int beatsPerBar);
        const juce::String& getLabel() const noexcept { return label; }
        juce::File writeFile (bool chordsOnly, juce::String& error);   // what a drag carries

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        ChordZimmerProcessor& processor;
        Selection selection;
        juce::String label;
        bool enabled = false, dragging = false;
    };

    class TimelinePanel : public juce::Component,
                          public juce::DragAndDropTarget,
                          public juce::FileDragAndDropTarget,
                          public juce::SettableTooltipClient,
                          private juce::ScrollBar::Listener
    {
    public:
        TimelinePanel (ChordZimmerProcessor&, Bindings&);
        ~TimelinePanel() override;

        // From the editor's timer: the progression (only when it changed), the playhead, the
        // home key for the labels.
        void refresh (const UiModel&);
        static int preferredHeight() { return 132; }
        SelectionDragHandle dragHandle;

        // The prefix of a drag description that carries a chord (the palette's drags):
        // "czchord:" + the saved text of a one-chord progression (Progression.h).
        static juce::String chordDragDescription (const ProgressionChord&);
        static bool chordFromDragDescription (const juce::var&, ProgressionChord&);

        //======================================================================
        // Actions (the menus, the keys and the tests). Each edit is one undo step.
        const ChordProgression& progression() const noexcept { return model; }
        const Selection& selection() const noexcept { return selected; }
        void setSelection (const Selection&);
        InsertPoint marker() const noexcept { return insertAt; }
        void setMarker (InsertPoint);

        bool addChord (const ProgressionChord&);                         // at the marker
        bool addChordAsSection (const ProgressionChord&);                // a new section at the end
        bool dropChordAt (const ProgressionChord&, float x);             // a palette drop
        bool deleteSelection();
        bool replaceSelectedChord (const ProgressionChord&);             // Chord-from-notes Set (its notes and bass)
        ChordRef selectedChord() const noexcept;                         // the first selected chord on its own, else invalid
        bool copySelection();
        bool cutSelection();
        bool paste();                                                    // at the marker
        bool duplicateSelection();                                       // in place
        void step (int direction);                                       // ← →
        void zoom (double factor);                                       // around the view's centre
        bool renameSection (int section, const juce::String& name);
        bool setSectionColour (int section, int colour);
        bool setInversion (ChordRef, int);
        bool setOctave (ChordRef, int);
        bool setBass (ChordRef, int pitchClass);
        bool replaceChordWith (ChordRef, const WorkspaceChord&);         // Replace with ▸ (Phase 6)
        void startRenaming (int section);
        bool isRenaming() const noexcept { return renaming >= 0; }

        void setSnap (Snap s) noexcept { snap = s; }
        Snap getSnap() const noexcept { return snap; }
        double pixelsPerBeat() const noexcept { return ppb; }
        void setPixelsPerBeat (double);
        std::function<void (double)> onZoom;                               // to save it

        //======================================================================
        // Geometry (for the mouse and the tests), in this component's coordinates.
        TimelineLayout layout() const;
        juce::Rectangle<int> rulerArea() const;
        juce::Rectangle<int> tabArea() const;
        juce::Rectangle<int> chordArea() const;
        juce::Rectangle<float> chordBounds (ChordRef) const;
        juce::Rectangle<float> tabBounds (int section) const;
        TimelineHit hitAt (juce::Point<float>) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // A .mid dropped from the Finder or the host (PLAN.md §6 Phase 9, MidiImport.h): its
        // chords go in where it lands, as sections (its markers', else one named after the file),
        // one undo step. importFile does the same for a file at x (the tests); onImport says what
        // came in, or why nothing did, for the Info View.
        bool importFile (const juce::File&, float x);
        bool importFileAt (const juce::File&, InsertPoint);
        std::function<void (const juce::String&)> onImport;

        // Import MIDI… (the title row's button, the empty space's menu): a file chooser, then
        // the file's chords at the insertion marker. Drops don't reach a plug-in window in Live
        // (it keeps clip drags, and hides the window while the Finder is in front), so this is
        // the way in there. `chooseFile` asks for the file (the system's dialog; tests replace it).
        void chooseAndImport();
        std::function<void (std::function<void (const juce::File&)>)> chooseFile;
        juce::TextButton importButton { juce::String::fromUTF8 ("Import MIDI\xe2\x80\xa6") };
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray&, int x, int y) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray& files, int x, int y) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        enum class Drag { None, Pending, Move, ResizeChord, ResizeSection };

        bool edit (const juce::String& label, const std::function<bool (ChordProgression&, Selection&)>& change);
        void pullProgression();
        void pushLoopSelection();
        void endPreview();
        void showChordMenu (ChordRef);
        void showSectionMenu (int section);
        void showSpaceMenu();
        void finishRenaming (bool keep);
        void updateScrollBar();
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void scrollTo (double x);
        void followPlayhead();
        void updateTooltip (const TimelineHit&);
        TimelineGeometry geometry() const;
        TimelineChordLabel labelFor (const ProgressionChord&) const;

        ChordProgression& shown() { return drag == Drag::ResizeChord || drag == Drag::ResizeSection ? dragPreview : model; }
        const ChordProgression& shown() const { return drag == Drag::ResizeChord || drag == Drag::ResizeSection ? dragPreview : model; }

        ChordZimmerProcessor& processor;
        Bindings& bindings;
        ChordProgression model;
        uint32_t modelVersion = ~0u;
        Selection selected;
        InsertPoint insertAt;
        ProgressionClip clipboard;
        Snap snap = defaultSnap;
        double ppb = TimelineGeometry::defaultPixelsPerBeat;
        double scroll = 0.0;              // pixels scrolled from the start

        int homeKey = 0;
        Scale homeScale = Scale::Major;
        int beatsPerBar = 4;
        double playhead = -1.0;
        int playingChord = -1;
        bool loopOn = true;

        // The mouse.
        Drag drag = Drag::None;
        TimelineHit pressed;
        bool selectOnRelease = false, previewing = false;
        TimelineLayout dragLayout;        // the layout as the drag began
        ChordProgression dragPreview;     // what a resize shows while it moves
        float dragX = 0.0f;
        bool dragCopy = false;
        bool dropping = false;            // a move, a copy or a palette chord over us
        InsertPoint dropAt;
        TimelineHit hovered;

        juce::ScrollBar scrollBar { false };
        juce::TextEditor nameEditor;
        int renaming = -1;

        static constexpr int padding = 10, titleHeight = 22, rulerHeight = 14, tabHeight = 20, scrollHeight = 8;
        static constexpr int dragHandleWidth = 200, importButtonWidth = 96;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
