// Editor snapshot test (PLAN.md §6 Phase 4, §7; ChordMonkey's Tests/Snapshot brought back for the
// new editor). Runs the real processor and editor without a host and checks:
//
//   1. snapshots   the editor in a fixed state (C major; Verse, Chorus, Bridge; the Chorus
//                  selected as the loop region; the playhead in the Verse) at the default size
//                  (1000 x 640), the smallest (760 x 600, keyboard hidden) and a large one
//                  (1200 x 800), the palette borrowing (a fifth down, Mixolydian), the
//                  performance popover open, and the Info View pointing at a palette chord. Each
//                  is compared with Tests/fixtures/snapshot/*.png (at most 0.5 % of pixels may
//                  differ by more than 24 levels: font rendering moves a little between macOS
//                  versions)
//   2. reachable   every host parameter has a control (the performance popover's included) but
//                  the test rig (Test pattern, Test workspace) and Pattern (the retired
//                  workspace's, PLAN.md §2)
//   3. Info View   every parameter has a brief entry; everything described can be pointed at
//                  (default and smallest size, the popover open and closed)
//   4. timeline    scripted mouse gestures on the real panel (PLAN.md §6 Phase 4): palette
//                  double-clicks and a palette drop, a chord's and a section's edge dragged, a
//                  chord moved into another section, a section Alt-dragged to the end, clicks
//                  and Shift-clicks (the loop region follows), the keys (Delete, Cmd-C/V at the
//                  marker, Cmd-D, ← →, Cmd-Z, Shift-Cmd-Z, Space, L), the menus' actions, every
//                  edit one undo step
//   5. palette     a click sounds the cell's chord through processBlock until let go; the
//                  navigator (◀ ▶, Home, mode, the dial) rebuilds it; Key carries it along; its
//                  position is saved in the UI state
//   6. undo        the processor's history: reset by a restore and by a preset with a
//                  progression, kept by one without; not saved
//   7. state       parameters, the UI's preferences (palette position, snap, zoom) and the
//                  progression restore into a new instance without reporting a change; the editor
//                  reopens at its saved size
//   8. Detect      captured chords go on the end of the timeline as "Detected", one undo step
//   9. real time   no allocation in processBlock while Preview plays and the editor edits the
//                  timeline between blocks (a malloc hook counts the audio thread's allocations)
//  10. Match       (Phase 5) Listen through processBlock (the input passes through, no
//                  allocation), the matches and the palette's rank badges, the note picker,
//                  ±2 fifths, Chord from notes' Add and Set, Clear, one popover at a time,
//                  Listen restored with the state; the editor-match snapshot
//  13. templates   (Phase 8) the factory templates load their key, scale and progression
//  14. hardening   (Phase 8; ChordMonkey's) no allocation in processBlock with everything on, the
//                  timeline playing and being edited; damaged states (random bytes, NaN and
//                  junk values, a broken progression and UI); zero-length, huge and mono blocks,
//                  bypass, all-notes-off, a reset and a rate change, SysEx, an input flood; 64
//                  instances
//  15. import      (Phase 9) a .mid dropped on the timeline (its chords, its markers' sections,
//                  one undo step) and on Match (its melody); files without chords or not MIDI
//  12. Relations   (Phase 6) the focus (the home tonic, a palette click, a timeline selection),
//                  the palette's tags and fading, Next ▸, Replace with; editor-relations
//  11. Drag MIDI   (Phase 7) the timeline's drag-out: everything or the selection, named after it,
//                  the render (Bass included) with section markers, Alt for chords only
//
//   ChordZimmerSnapshot <fixtures dir> <output dir> [--update | --no-pixels]
//   ChordZimmerSnapshot --bench [seconds]     (Tests/run_bench.sh: 8 instances, CPU per instance)
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Source/plugin/ChordZimmerProcessor.h"
#include "../../Source/ui/ChordZimmerEditor.h"
#include "../../Source/core/model/ChordSets.h"
#include <chrono>
#include <iostream>
#include <pthread.h>
#include <random>

using namespace chordzimmer;

// Every allocation the watched thread makes (malloc, realloc, new: libmalloc's logger hook sees
// them all). Only a pthread_t compare inside: the hook runs within malloc.
extern "C" void (*malloc_logger) (uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);

namespace
{
    std::atomic<pthread_t> allocWatched { nullptr };
    void* volatile allocSink = nullptr;
    std::atomic<int> allocCount { 0 };

    void allocLogger (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
    {
        if ((type & 2) != 0 && allocWatched.load (std::memory_order_relaxed) == pthread_self())   // 2: an allocation
            allocCount.fetch_add (1, std::memory_order_relaxed);
    }

    struct AllocWatch
    {
        AllocWatch() { malloc_logger = allocLogger; allocWatched = pthread_self(); }
        ~AllocWatch() { allocWatched = nullptr; }
    };

    int failures = 0;
    bool comparePixels = true;

    // Records whether the processor told the host its non-parameter state changed.
    struct StateWatch : juce::AudioProcessorListener
    {
        bool changed = false;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { changed |= d.nonParameterStateChanged; }
    };

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    void set (ChordZimmerProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.state.getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    float get (ChordZimmerProcessor& p, const juce::String& id) { return p.state.getRawParameterValue (id)->load(); }

    // A block of the processor with these MIDI events at sample 0; returns its output.
    juce::MidiBuffer block (ChordZimmerProcessor& p, std::initializer_list<juce::MidiMessage> events = {})
    {
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        for (const auto& e : events)
            midi.addEvent (e, 0);
        p.processBlock (audio, midi);
        return midi;
    }

    std::vector<int> noteOns (const juce::MidiBuffer& midi)
    {
        std::vector<int> n;
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                n.push_back (m.getMessage().getNoteNumber());
        std::sort (n.begin(), n.end());
        return n;
    }

    int noteOffs (const juce::MidiBuffer& midi)
    {
        int n = 0;
        for (const auto m : midi)
            n += m.getMessage().isNoteOff() ? 1 : 0;
        return n;
    }

    void runTimers (int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    // A mouse event on `c` at `at` (component coordinates).
    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier,
                            int clicks = 1, juce::Point<float> downAt = { -1.0f, -1.0f })
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downAt.x < 0.0f ? at : downAt, now, clicks,
                                 downAt.x >= 0.0f && downAt != at);
    }

    // Press at `from`, drag through to `to` in steps, release there.
    void dragMouse (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        c.mouseDown (mouse (c, from, mods));
        for (int i = 1; i <= 8; ++i)
            c.mouseDrag (mouse (c, from + (to - from) * ((float) i / 8.0f), mods, 1, from));
        c.mouseUp (mouse (c, to, mods.withoutMouseButtons(), 1, from));
    }

    void click (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        c.mouseDown (mouse (c, at, mods));
        c.mouseUp (mouse (c, at, mods.withoutMouseButtons()));
    }

    juce::KeyPress key (int code, juce::ModifierKeys mods = {}, juce::juce_wchar ch = 0) { return juce::KeyPress (code, mods, ch); }
    const juce::ModifierKeys cmd { juce::ModifierKeys::commandModifier };

    // "Verse[C:4 Am:4] Chorus[F:2]": names in the home key, lengths in beats.
    juce::String layout (const ChordProgression& p, int key = 0, Scale scale = Scale::Major)
    {
        juce::StringArray parts;
        for (const auto& s : p.sections)
        {
            juce::StringArray chords;
            for (const auto& c : s.chords)
                chords.add (juce::String (timelineChordLabel (c, key, scale).name) + ":" + juce::String (c.beats()).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."));
            parts.add (juce::String::fromUTF8 (s.name.c_str()) + "[" + chords.joinIntoString (" ") + "]");
        }
        return parts.joinIntoString (" ");
    }

    juce::String selText (const Selection& sel)
    {
        juce::StringArray s;
        for (int x : sel.sections)
            s.add ("S" + juce::String (x));
        for (auto c : sel.chords)
            s.add (juce::String (c.section) + "." + juce::String (c.index));
        return s.isEmpty() ? "-" : s.joinIntoString (" ");
    }

    // A section from numerals in C ("I vi IV:2 V:2"), through ChordSets' parser.
    ProgressionSection sectionOf (const char* name, const char* numerals, int colour)
    {
        Pattern pattern;
        std::string error;
        parseProgression (numerals, 0, pattern, error);
        auto p = progressionFromPattern (pattern, name);
        p.sections[0].colour = (uint8_t) colour;
        return p.sections[0];
    }

    ChordProgression song()
    {
        ChordProgression p;
        p.sections.push_back (sectionOf ("Verse", "I vi IV V", 0));
        p.sections.push_back (sectionOf ("Chorus", "IV V iii:2 vi:2 ii7:2 V7:2", 2));
        p.sections.push_back (sectionOf ("Bridge", "bVI bVII I:8", 3));
        return p;
    }

    // Fraction of pixels that differ by more than `tolerance` in any channel (1 if sizes differ).
    double difference (const juce::Image& a, const juce::Image& b, int tolerance)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return 1.0;
        const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        long differing = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto ca = da.getPixelColour (x, y), cb = db.getPixelColour (x, y);
                const int d = juce::jmax (std::abs (ca.getRed() - cb.getRed()), std::abs (ca.getGreen() - cb.getGreen()),
                                          std::abs (ca.getBlue() - cb.getBlue()));
                differing += d > tolerance ? 1 : 0;
            }
        return (double) differing / ((double) a.getWidth() * a.getHeight());
    }

    void snapshot (ChordZimmerEditor& editor, const juce::String& name, const juce::File& fixtures, const juce::File& out, bool update,
                   std::function<void()> beforeRender = {})
    {
        editor.refreshNow();
        if (beforeRender)
            beforeRender();
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
        const auto write = [&image] (const juce::File& f)
        {
            f.deleteFile();
            juce::FileOutputStream s (f);
            juce::PNGImageFormat().writeImageToStream (image, s);
        };
        write (out.getChildFile (name + ".png"));

        const auto reference = fixtures.getChildFile (name + ".png");
        if (! comparePixels)
        {
            check (image.isValid() && image.getWidth() == editor.getWidth(), name + ": rendered (pixels not compared)");
            return;
        }
        if (update || ! reference.existsAsFile())
        {
            write (reference);
            check (true, name + ": reference written (" + reference.getFullPathName() + ")");
            return;
        }
        const double d = difference (image, juce::ImageFileFormat::loadFrom (reference), 24);
        check (d <= 0.005, name + ": " + juce::String (d * 100.0, 3) + "% of pixels differ from the reference");
    }

    std::unique_ptr<ChordZimmerEditor> openEditor (ChordZimmerProcessor& p)
    {
        return std::unique_ptr<ChordZimmerEditor> (dynamic_cast<ChordZimmerEditor*> (p.createEditor()));
    }

    // Closes an editor as a host does: JUCE wants editorBeingDeleted() before the delete.
    template <class E>
    void closeEditor (juce::AudioProcessor& p, std::unique_ptr<E>& e)
    {
        p.editorBeingDeleted (e.get());
        e.reset();
    }

    // Not on screen: the test rig (Live's), Pattern (retired) and the import message (shown
    // after a .mid is dropped, not something to point at).
    bool exempt (const juce::String& id)
    {
        return id == params::testPattern || id == params::testWorkspace || id == params::pattern || id == "display.import";
    }
}

//==============================================================================
// 1-3: snapshots, reachability, the Info View.

static void testSnapshots (const juce::File& fixtures, const juce::File& out, bool update)
{
    std::cout << "== snapshots ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    block (*p);
    p->setProgression (song());
    auto editor = openEditor (*p);
    check (editor != nullptr, "the editor opens");
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->showKeyboard (true);
    editor->refreshNow();
    editor->getTimeline().setSelection (Selection::section (1));

    // The playhead in the Verse's IV, as the processor would publish it while playing.
    const auto playing = [&]
    {
        p->playhead = 9.25;
        p->playingChord = 2;
    };
    playing();
    snapshot (*editor, "editor", fixtures, out, update, playing);
    editor->setSize (ChordZimmerEditor::minWidth, ChordZimmerEditor::minHeight);
    editor->showKeyboard (false);
    snapshot (*editor, "editor-small", fixtures, out, update, playing);
    editor->showKeyboard (true);
    editor->setSize (1200, 800);
    snapshot (*editor, "editor-large", fixtures, out, update, playing);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);

    // Borrowing: a fifth down (F), Mixolydian.
    auto& nav = editor->getNavigator();
    nav.stepFifth (-1);
    nav.setMode (Scale::Mixolydian);
    p->playhead = -1.0;
    p->playingChord = -1;
    snapshot (*editor, "editor-borrowed", fixtures, out, update);
    nav.goHome();

    editor->showPerformance (true);
    snapshot (*editor, "editor-performance", fixtures, out, update);

    std::cout << "== every parameter reachable ==\n";
    juce::StringArray missing;
    int total = 0;
    for (auto* param : p->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
        {
            ++total;
            if (editor->boundParameters().count (withId->paramID) == 0 && ! exempt (withId->paramID))
                missing.add (withId->paramID);
        }
    check (total == params::numParameters && missing.isEmpty(),
           juce::String (total) + " parameters, " + juce::String (missing.size()) + " without a control (the test rig is Live's; Pattern is retired)"
               + (missing.isEmpty() ? juce::String() : ": " + missing.joinIntoString (", ")));

    std::cout << "== Info View ==\n";
    {
        juce::StringArray noEntry, tooLong;
        for (auto* param : p->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                if (ui::InfoView::titleFor (withId->paramID).isEmpty() || ui::InfoView::textFor (withId->paramID).isEmpty())
                    noEntry.add (withId->paramID);
        for (const auto& k : ui::InfoView::keys())
            if (ui::InfoView::textFor (k).length() > 200 || ui::InfoView::titleFor (k).length() > 24)
                tooLong.add (k);
        check (noEntry.isEmpty(), "every parameter has an Info View entry" + (noEntry.isEmpty() ? juce::String() : ": none for " + noEntry.joinIntoString (", ")));
        check (tooLong.isEmpty(), "entries stay brief (titles at most 24 characters, text at most 200)"
                                      + (tooLong.isEmpty() ? juce::String() : ": " + tooLong.joinIntoString (", ")));

        // Everything in the table can be pointed at, with the popover open and closed, at the
        // default size and the smallest (keyboard hidden there).
        for (const bool small : { false, true })
        {
            editor->setSize (small ? ChordZimmerEditor::minWidth : ChordZimmerEditor::defaultWidth,
                             small ? ChordZimmerEditor::minHeight : ChordZimmerEditor::defaultHeight);
            editor->showKeyboard (! small);
            juce::StringArray found, unreachable;
            editor->getPalette().showRelations (true);   // (Next ▸ shows only then)
            for (const int popover : { 0, 1, 2 })   // none, Performance, Match
            {
                editor->showPerformance (popover == 1);
                editor->showMatch (popover == 2);
                editor->refreshNow();
                for (int y = 0; y < editor->getHeight(); y += 2)
                    for (int x = 0; x < editor->getWidth(); x += 2)
                        found.addIfNotAlreadyThere (editor->infoKeyAt ({ x, y }));
            }
            for (const auto& k : ui::InfoView::keys())
                if (! found.contains (k) && ! exempt (k) && ! (small && k == "display.keyboard"))
                    unreachable.add (k);
            const auto strip = editor->infoView().getBounds();
            check (unreachable.isEmpty(), juce::String (small ? "smallest size" : "default size") + ": everything described can be pointed at ("
                                              + juce::String (found.size() - 1) + " keys)" + (unreachable.isEmpty() ? juce::String() : "; not: " + unreachable.joinIntoString (", ")));
            check (strip.getBottom() == editor->getHeight() - 10 && strip.getWidth() == editor->getWidth() - 24 && editor->infoKeyAt (strip.getCentre()).isEmpty(),
                   "the strip runs along the bottom and isn't itself something to point at");
        }
        editor->showPerformance (false);
        editor->showMatch (false);
        editor->getPalette().showRelations (false);
        editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
        editor->showKeyboard (true);
        editor->refreshNow();

        // Pointing at a palette chord shows what it is; leaving the editor shows the hint.
        auto& palette = editor->getPalette();
        const ui::PalettePanel::CellRef v7 { 5, 4 };     // the "7" row, degree V: G7
        const auto at = palette.cellBounds (v7).getCentre();
        palette.mouseMove (mouse (palette, at));
        editor->showInfoAt (editor->getLocalPoint (&palette, at.toInt()));
        check (editor->infoView().shownKey() == "display.palette" && editor->infoView().shownText().startsWith ("G7 (V7): G B D F. In the home key."),
               "pointing at G7 in the palette names it, its numeral and notes (" + editor->infoView().shownText().substring (0, 40) + ")");
        snapshot (*editor, "editor-info", fixtures, out, update);
        palette.mouseExit (mouse (palette, { -5.0f, -5.0f }));
        const juce::MouseEvent away = mouse (*editor, { -5.0f, -5.0f });
        editor->mouseExit (away);
        check (editor->infoView().shownKey().isEmpty() && editor->infoView().shownText() == ui::InfoView::hint(), "leaving the editor shows the hint");
    }
    closeEditor (*p, editor);
}

//==============================================================================
// 4: the timeline's gestures.

static void testTimeline()
{
    std::cout << "== timeline gestures ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->refreshNow();
    auto& t = editor->getTimeline();
    auto& palette = editor->getPalette();
    const auto cellAt = [&] (int variant, int degree) { return palette.cellBounds ({ variant, degree }).getCentre(); };
    const int triad = 3;
    const auto now = [&] { return layout (p->getProgression()); };

    // Double-clicks in the palette add at the marker, each after the last.
    for (int degree : { 0, 5, 3, 4 })
        palette.mouseDoubleClick (mouse (palette, cellAt (triad, degree), juce::ModifierKeys::leftButtonModifier, 2));
    editor->refreshNow();
    check (now() == "Section 1[C:4 Am:4 F:4 G:4]" && selText (t.selection()) == "0.3",
           "four double-clicks in the palette (I vi IV V): Section 1, each after the last, the last selected (" + now() + ")");
    check (p->undoState().canUndo && p->undoState().undoLabel == "Add chord", "each is an undo step (\"Add chord\")");

    // A palette chord dropped past the end: a new section.
    {
        ProgressionChord dm = progressionChordFromCell (*palette.cell ({ triad, 1 }));
        const juce::var desc = ui::TimelinePanel::chordDragDescription (dm);
        const auto end = t.chordBounds ({ 0, 3 }).getRight() + 40.0f;
        juce::DragAndDropTarget::SourceDetails details (desc, &palette, { (int) end, (int) t.chordArea().getCentreY() });
        check (t.isInterestedInDragSource (details), "the timeline takes a palette chord's drag");
        t.itemDragMove (details);
        t.itemDropped (details);
        check (now() == "Section 1[C:4 Am:4 F:4 G:4] Section 2[Dm:4]", "Dm dropped on empty space: Section 2 (" + now() + ")");
        juce::DragAndDropTarget::SourceDetails other (juce::var ("something else"), &palette, { 10, 10 });
        check (! t.isInterestedInDragSource (other), "other drags are refused");
    }

    // Drag G's right edge 2 beats right (snap 1/8): 6 beats.
    const float ppb = (float) t.pixelsPerBeat();
    auto g = t.chordBounds ({ 0, 3 });
    dragMouse (t, { g.getRight() - 1.0f, g.getCentreY() }, { g.getRight() - 1.0f + 2.0f * ppb + 3.0f, g.getCentreY() });
    check (now() == "Section 1[C:4 Am:4 F:4 G:6] Section 2[Dm:4]" && p->undoState().undoLabel == "Resize chord",
           "G's right edge dragged 2 beats: 6 beats, one undo step (" + now() + ")");

    // Section 1's tab edge dragged to half its length: every chord halves.
    auto tab = t.tabBounds (0);
    dragMouse (t, { tab.getRight() - 1.0f, tab.getCentreY() }, { tab.getX() + tab.getWidth() * 0.5f, tab.getCentreY() });
    check (now() == "Section 1[C:2 Am:2 F:2 G:3] Section 2[Dm:4]" && p->undoState().undoLabel == "Resize section",
           "Section 1's edge dragged to half: its chords scale (" + now() + ")");
    editor->keyPressed (key ('z', cmd, 'z'));
    check (now() == "Section 1[C:4 Am:4 F:4 G:6] Section 2[Dm:4]", "Cmd-Z undoes it");
    editor->keyPressed (key ('z', cmd.withFlags (juce::ModifierKeys::shiftModifier), 'Z'));
    check (now() == "Section 1[C:2 Am:2 F:2 G:3] Section 2[Dm:4]", "Shift-Cmd-Z redoes it");

    // Drag Am into Section 2, after Dm (with nothing selected first: a drag moves the whole
    // selection, and the edge drag selected Section 1).
    t.setSelection ({});
    auto am = t.chordBounds ({ 0, 1 });
    auto dm = t.chordBounds ({ 1, 0 });
    dragMouse (t, am.getCentre(), { dm.getRight() - 3.0f, dm.getCentreY() });
    check (now() == "Section 1[C:2 F:2 G:3] Section 2[Dm:4 Am:2]" && selText (t.selection()) == "1.1" && p->undoState().undoLabel == "Move",
           "Am dragged onto Dm's second half: moved after it, still selected (" + now() + ")");
    check (selText (p->getLoopSelection()) == "1.1", "the loop region follows the selection");

    // Alt-drag Section 1's tab past the end: a copy last.
    tab = t.tabBounds (0);
    const juce::ModifierKeys alt (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    dragMouse (t, tab.getCentre(), { t.chordBounds ({ 1, 1 }).getRight() + 30.0f, tab.getCentreY() }, alt);
    check (now() == "Section 1[C:2 F:2 G:3] Section 2[Dm:4 Am:2] Section 1[C:2 F:2 G:3]" && selText (t.selection()) == "S2"
               && p->undoState().undoLabel == "Duplicate",
           "Alt-drag Section 1's tab to the end: a copy there, selected (" + now() + ")");

    // Clicks: one chord, Shift adds, a click on empty space clears and sets the marker.
    click (t, t.chordBounds ({ 0, 1 }).getCentre());
    click (t, t.chordBounds ({ 1, 0 }).getCentre(), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    check (selText (t.selection()) == "0.1 1.0" && selText (p->getLoopSelection()) == "0.1 1.0", "click F, Shift-click Dm: both, the loop region too");
    editor->refreshNow();
    check (editor->getTransport().now.getName() == "F" && editor->getTransport().now.getNumeral() == "IV", "the transport shows the selected chord: F, IV");

    // Cmd-C, a click in the gap after Section 1 (the marker), Cmd-V: a new section there.
    editor->keyPressed (key ('c', cmd, 'c'));
    const auto gapX = (t.tabBounds (0).getRight() + t.tabBounds (1).getX()) * 0.5f;
    click (t, { gapX, t.chordBounds ({ 0, 0 }).getCentreY() });
    check (t.selection().empty() && t.marker() == InsertPoint::newSection (1), "a click in a gap: nothing selected, the marker between the sections");
    editor->keyPressed (key ('v', cmd, 'v'));
    check (now() == "Section 1[C:2 F:2 G:3] Section 1[F:2] Section 2[Dm:4] Section 2[Dm:4 Am:2] Section 1[C:2 F:2 G:3]"
               && p->undoState().undoLabel == "Paste",
           "Cmd-V pastes at the marker, not the end: a section per part (" + now() + ")");

    // Delete, Cmd-D, the arrows.
    t.setSelection (Selection::section (1));
    editor->keyPressed (key (juce::KeyPress::deleteKey));
    editor->keyPressed (key (juce::KeyPress::rightKey));
    check (now() == "Section 1[C:2 F:2 G:3] Section 2[Dm:4] Section 2[Dm:4 Am:2] Section 1[C:2 F:2 G:3]" && selText (t.selection()) == "0.0",
           "Delete removes the selected section; Right from nothing selects the first chord (" + now() + ")");
    t.setSelection (Selection::chord (2, 1));
    editor->keyPressed (key ('d', cmd, 'd'));
    check (now() == "Section 1[C:2 F:2 G:3] Section 2[Dm:4] Section 2[Dm:4 Am:2 Am:2] Section 1[C:2 F:2 G:3]" && selText (t.selection()) == "2.2",
           "Cmd-D duplicates Am in place, the copy selected (" + now() + ")");
    editor->keyPressed (key (juce::KeyPress::leftKey));
    editor->keyPressed (key (juce::KeyPress::leftKey));
    check (selText (t.selection()) == "2.0", "Left Left steps back two chords");

    // The menus' actions.
    check (t.setInversion ({ 0, 0 }, 1) && t.setOctave ({ 0, 0 }, -1) && t.setBass ({ 0, 1 }, 9) && t.renameSection (0, "Intro") && t.setSectionColour (0, 5),
           "inversion, octave, bass, rename and colour each change the progression");
    const auto edited = p->getProgression();
    check (edited.sections[0].name == "Intro" && edited.sections[0].colour == 5 && edited.sections[0].chords[0].inversion == 1
               && edited.sections[0].chords[0].octave == -1 && edited.sections[0].chords[1].bass == 9 && layout (edited).startsWith ("Intro[C/E:2 F/A:2"),
           "and they show: " + layout (edited).substring (0, 30));
    t.mouseDoubleClick (mouse (t, t.tabBounds (1).getCentre(), juce::ModifierKeys::leftButtonModifier, 2));
    check (t.isRenaming(), "double-clicking a tab starts renaming it");

    // Space and L toggle Play and Loop.
    const bool loopWas = get (*p, params::loop) > 0.5f;
    editor->keyPressed (key (juce::KeyPress::spaceKey, {}, ' '));
    editor->keyPressed (key ('L', {}, 'l'));
    check (get (*p, params::play) > 0.5f && (get (*p, params::loop) > 0.5f) != loopWas, "Space turns Play on; L toggles Loop");
    set (*p, params::play, 0.0f);

    // Snap: the transport's menu sets the timeline's, and it's saved.
    editor->getTransport().snap.setSelectedId ((int) Snap::Bar + 1, juce::sendNotificationSync);
    check (t.getSnap() == Snap::Bar && (int) p->uiState.getProperty ("snap", -1) == (int) Snap::Bar, "Snap Bar: the timeline snaps to bars; saved in the UI state");

    // Undo all the way back, then redo all.
    int undos = 0;
    while (p->undoProgression())
        ++undos;
    editor->refreshNow();
    check (p->getProgression().empty() && t.progression().empty() && undos == 17 && ! p->undoState().canUndo,
           "every edit was one undo step: " + juce::String (undos) + " undo back to empty");
    int redos = 0;
    while (p->redoProgression())
        ++redos;
    editor->refreshNow();
    check (redos == undos && layout (p->getProgression()) == layout (edited) && t.progression() == p->getProgression(), "and redo all the way forward again");
    closeEditor (*p, editor);
}

//==============================================================================
// 5: the palette and the navigator.

static void testPalette()
{
    std::cout << "== palette and navigator ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    block (*p);
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->refreshNow();
    auto& palette = editor->getPalette();
    auto& nav = editor->getNavigator();

    // A click sounds the cell (ii, Dm: D3 F3 A3) until let go.
    const ui::PalettePanel::CellRef dm { 3, 1 };
    const auto at = palette.cellBounds (dm).getCentre();
    check (palette.firstRow() == 2 && palette.cellAt (at) == dm, "the triads are in view at first; the cell under the mouse is ii");
    palette.mouseDown (mouse (palette, at));
    const auto on = noteOns (block (*p));
    palette.mouseUp (mouse (palette, at, {}));
    const auto off = block (*p);
    check (on == std::vector<int> { 50, 53, 57 } && noteOffs (off) == 3, "clicking ii sounds D F A (50 53 57) and letting go ends it");
    check (p->getProgression().empty(), "a click adds nothing");

    // Scrolling: up to the extended tiers and back.
    palette.scrollRows (+20);
    check (palette.firstRow() == numTierVariants - palette.visibleRows(), "scrolled to the top, the Altered tier shows");
    palette.scrollRows (-20);
    check (palette.firstRow() == 0, "and to the bottom, power chords and sus");
    palette.showRow (3);

    // The navigator.
    const auto ctx = [&] { return palette.getGrid().context; };
    nav.down.onClick();
    check (ctx().paletteKey == 5 && ctx().paletteScale == Scale::Major && ctx().homeKey == 0 && palette.getGrid().columns[3].numeral == "bVII",
           "Fifth down: the palette is F major; its fourth column reads bVII from C");
    check (! palette.getGrid().rows[3].cells[3].inKey && palette.getGrid().rows[3].cells[0].inKey, "Bb is hollow (out of C), F filled");
    check ((int) p->uiState.getProperty ("paletteFifths", 0) == -1, "the palette position is saved in the UI state");
    nav.mode.setSelectedId ((int) Scale::Dorian + 1, juce::sendNotificationSync);
    check (ctx().paletteKey == 5 && ctx().paletteScale == Scale::Dorian && (int) p->uiState.getProperty ("paletteScale", -1) == (int) Scale::Dorian,
           "mode menu Dorian: F Dorian, saved");
    nav.home.onClick();
    check (ctx().paletteKey == 0 && ctx().paletteScale == Scale::Major, "Home: back to C major");
    nav.up.onClick();
    nav.up.onClick();
    check (ctx().paletteKey == 2 && editor->getNavigatorState().distance() == 2, "Fifth up twice: D, two fifths up");
    nav.dial.onPick (10);
    check (ctx().paletteKey == 10 && editor->getNavigatorState().distance() == -2, "the dial's Bb: two fifths down");

    // Key carries an away palette along (it keeps its distance).
    set (*p, params::key, 2.0f);
    editor->refreshNow();
    check (ctx().homeKey == 2 && ctx().paletteKey == 0, "Key D: home is D, the palette two fifths down, C");
    nav.home.onClick();
    set (*p, params::key, 7.0f);
    set (*p, params::scale, (float) (int) Scale::NaturalMinor);
    editor->refreshNow();
    check (ctx().homeKey == 7 && ctx().paletteKey == 7 && ctx().paletteScale == Scale::NaturalMinor, "a palette at home follows Key and Scale (G minor)");

    // A new editor opens where the palette was left.
    nav.up.onClick();
    closeEditor (*p, editor);
    editor = openEditor (*p);
    check (editor->getPalette().getGrid().context.paletteKey == 2 && editor->getNavigatorState().distance() == 1, "a reopened editor keeps the palette's position");
    closeEditor (*p, editor);
}

//==============================================================================
// 6, 7: undo history and state.

static void testUndoAndState()
{
    std::cout << "== undo history ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->setProgression (song());
    check (! p->undoState().canUndo, "a loaded progression has no history");
    check (p->editProgression ("Rename", [] (ChordProgression& q) { return q.renameSection (0, "Intro"); }) && p->undoState().undoLabel == "Rename",
           "an edit is an undo step");
    check (! p->editProgression ("Nothing", [] (ChordProgression& q) { return q.renameSection (0, "Intro"); }) && p->undoState().undoLabel == "Rename",
           "an edit that changes nothing isn't");
    const uint32_t v = p->progressionVersion();
    check (p->undoProgression() && p->getProgression() == song() && p->progressionVersion() == v + 1 && p->undoState().canRedo, "undo goes back; the version moves on");
    p->redoProgression();

    juce::MemoryBlock saved;
    p->uiState.setProperty ("paletteFifths", 3, nullptr);
    p->uiState.setProperty ("zoom", 40.0, nullptr);
    p->getStateInformation (saved);
    auto restored = std::make_unique<ChordZimmerProcessor>();
    restored->editProgression ("Something", [] (ChordProgression& q) { return q.insertChord (InsertPoint::newSection (0), song().sections[0].chords[0]); });
    StateWatch watch;
    restored->addListener (&watch);
    restored->setStateInformation (saved.getData(), (int) saved.getSize());
    for (int i = 0; i < 5; ++i)
        runTimers (20);
    restored->removeListener (&watch);
    check (restored->getProgression() == p->getProgression() && ! restored->undoState().canUndo && ! restored->undoState().canRedo,
           "a restore brings the progression back and resets the history");
    check (! watch.changed, "a restore doesn't report a change (the set isn't marked as edited when it opens)");
    check ((int) restored->uiState.getProperty ("paletteFifths", 0) == 3 && (double) restored->uiState.getProperty ("zoom", 0.0) == 40.0,
           "the UI's preferences restore (palette position, zoom)");
    int differing = 0;
    for (auto* param : p->getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            differing += get (*p, withId->paramID) != get (*restored, withId->paramID) ? 1 : 0;
    check (differing == 0, "every parameter restores (" + juce::String (differing) + " differ)");

    // Presets: one with a progression resets the history; one without keeps it.
    restored->editProgression ("Rename", [] (ChordProgression& q) { return q.renameSection (0, "A"); });
    auto preset = restored->presetState();
    preset.removeChild (preset.getChildWithName ("PROGRESSION"), nullptr);
    restored->applyPreset (preset, "User", "No progression");
    check (restored->undoState().canUndo && restored->getProgression().sections[0].name == "A", "a preset without a progression keeps it and its history");
    restored->applyPreset (p->presetState(), "User", "With one");
    check (! restored->undoState().canUndo && restored->getProgression() == p->getProgression(), "a preset with one loads it and resets the history");

    std::cout << "== editor size ==\n";
    auto sized = std::make_unique<ChordZimmerProcessor>();
    StateWatch openWatch;
    sized->addListener (&openWatch);
    auto first = openEditor (*sized);
    sized->removeListener (&openWatch);
    check (first->getWidth() == ChordZimmerEditor::defaultWidth && first->getHeight() == ChordZimmerEditor::defaultHeight && ! openWatch.changed,
           "a new instance's editor opens at the default size (1000 x 640) and doesn't report a change");
    first->setSize (1111, 777);
    closeEditor (*sized, first);
    juce::MemoryBlock state;
    sized->getStateInformation (state);
    auto reopened = std::make_unique<ChordZimmerProcessor>();
    reopened->setStateInformation (state.getData(), (int) state.getSize());
    auto second = openEditor (*reopened);
    check (second->getWidth() == 1111 && second->getHeight() == 777, "after a save and restore the editor opens at the saved size (1111 x 777)");
    closeEditor (*reopened, second);
}

//==============================================================================
// 8: Detect writes into the timeline.

namespace
{
    struct PlayHead : juce::AudioPlayHead
    {
        bool playing = true, looping = false;
        double ppq = 0.0, bpm = 120.0, loopStart = 0.0, loopEnd = 8.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setIsLooping (looping);
            info.setLoopPoints (juce::AudioPlayHead::LoopPoints { loopStart, loopEnd });
            info.setPpqPosition (ppq);
            info.setBpm (bpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            return info;
        }
        void advance (int samples, double rate)
        {
            if (! playing)
                return;
            ppq += samples * bpm / (60.0 * rate);
            if (looping && ppq >= loopEnd)
                ppq = loopStart + (ppq - loopEnd);
        }
    };
}

static void testDetect()
{
    std::cout << "== Detect into the timeline ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    p->setProgression (song());
    PlayHead head;
    p->setPlayHead (&head);
    set (*p, params::detect, (float) (int) params::DetectMode::Capture);
    const std::vector<std::vector<int>> chords { { 60, 64, 67 }, { 55, 59, 62 }, { 57, 60, 64 }, { 53, 57, 60 } };
    const double spb = 48000.0 * 60.0 / head.bpm;
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < (int) (chords.size() * 4.0 * spb / 512.0) + 8; ++b)
    {
        midi.clear();
        const double start = head.ppq, end = head.ppq + 512 / spb;
        for (size_t c = 0; c < chords.size(); ++c)
            for (int note : chords[c])
            {
                const double on = (double) c * 4.0, off = on + 3.5;
                if (on >= start && on < end)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 90), (int) ((on - start) * spb));
                if (off >= start && off < end)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) ((off - start) * spb));
            }
        p->processBlock (audio, midi);
        head.advance (512, 48000.0);
    }
    runTimers (50);
    const bool applied = p->applyDetect();
    const auto prog = p->getProgression();
    check (applied && prog.numSections() == 4 && prog.sections[3].name == "Detected" && layout (prog).endsWith ("Detected[C:4 G:4 Am:4 F:4]"),
           "Use result: the chords heard go on the end as \"Detected\" (" + layout (prog).fromLastOccurrenceOf ("] ", false, false) + ")");
    check (p->undoState().undoLabel == "Detect" && get (*p, params::detect) < 0.5f && p->undoProgression() && p->getProgression() == song(),
           "one undo step (\"Detect\"), and Detect is Off again");
    p->setPlayHead (nullptr);
}

//==============================================================================
// 9: no allocation while the timeline plays and the editor edits it.

static void testRealTime()
{
    std::cout << "== real time ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    p->setProgression (song());
    editor->refreshNow();
    set (*p, params::syncMode, 1.0f);   // Preview: its own clock, no host transport
    set (*p, params::play, 1.0f);
    auto& t = editor->getTimeline();
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (1 << 16);
    int allocations = 0, ons = 0;
    for (int b = 0; b < 600; ++b)
    {
        if (b % 40 == 20)
        {
            // An edit from the editor between blocks: alternately a resize and its undo, a move.
            if ((b / 40) % 3 == 0)
                t.setInversion ({ 0, b % 4 }, (b / 40) % 3);
            else if ((b / 40) % 3 == 1)
                p->undoProgression();
            else
                t.setSelection (Selection::chord (1, (b / 40) % 4));
            editor->refreshNow();
        }
        midi.clear();
        const auto before = allocCount.load();
        {
            AllocWatch watch;
            p->processBlock (audio, midi);
        }
        allocations += allocCount.load() - before;
        for (const auto m : midi)
            ons += m.getMessage().isNoteOn() ? 1 : 0;
    }
    editor->refreshNow();
    check (ons > 20 && allocations == 0, juce::String (ons) + " notes played over 600 blocks with edits between them; "
                                             + juce::String (allocations) + " allocations in processBlock");
    check (p->playhead.load() > 0.0 && editor->getTransport().now.getName().isNotEmpty(), "the transport shows the chord playing ("
                                             + editor->getTransport().now.getName() + ")");
    set (*p, params::play, 0.0f);
    closeEditor (*p, editor);
}

//==============================================================================
// 10: Match and Chord-from-notes (Phase 5).

static void testMatch (const juce::File& fixtures, const juce::File& out, bool update)
{
    std::cout << "== Match ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    block (*p);
    p->setProgression (song());
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->showKeyboard (true);
    editor->refreshNow();
    auto& match = editor->getMatch();
    auto& palette = editor->getPalette();

    editor->showMatch (true);
    check (editor->matchShown() && ! editor->performanceShown(), "Match opens its popover");
    editor->showPerformance (true);
    check (! editor->matchShown() && editor->performanceShown(), "Performance closes it: one popover at a time");
    editor->showMatch (true);

    // Listen: G B D F, a beat each (120 BPM, no host: the engine's own clock), through processBlock.
    set (*p, params::listen, 1.0f);
    block (*p);
    const int notes[] = { 67, 71, 74, 77 };
    const int blocksPerBeat = 47;          // 24000 samples / 512
    std::vector<int> passed;
    int allocations = 0;
    juce::AudioBuffer<float> audio (2, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    for (int i = 0; i < 4; ++i)
        for (int b = 0; b < blocksPerBeat; ++b)
        {
            midi.clear();
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[i], (juce::uint8) 90), 0);
            if (b == blocksPerBeat - 4)
                midi.addEvent (juce::MidiMessage::noteOff (1, notes[i]), 0);
            const auto before = allocCount.load();
            {
                AllocWatch watch;
                p->processBlock (audio, midi);
            }
            allocations += allocCount.load() - before;
            for (const auto m : midi)
                if (m.getMessage().isNoteOn())
                    passed.push_back (m.getMessage().getNoteNumber());
        }
    editor->refreshNow();
    check (passed == std::vector<int> { 67, 71, 74, 77 } && allocations == 0, "Listen: the melody passes through, no chords, no allocation in processBlock");
    const auto& ms = match.getMatches();
    check (! ms.empty() && ms.front().cell.name == "G7" && ms.front().rank == 1, "the best match is G7 (" + juce::String (ms.empty() ? "-" : ms.front().cell.name) + ")");
    check (palette.matchShown() && palette.matchRank (5, 4) == 1, "the palette shows rank 1 on G7 (the 7 row, V)");
    check (match.noteSet().weight[7] > 0.8 && match.noteSet().weight[7] < 1.0 && match.noteSet().bass == 7, "the notes heard: about 0.9 beats each, G the bass");
    check (match.fromNotesName().startsWith ("G7"), "Chord from notes: G7 (" + match.fromNotesName() + ")");
    snapshot (*editor, "editor-match", fixtures, out, update);

    // Add puts it in at the marker; Set replaces the selected chord.
    auto& t = editor->getTimeline();
    t.setSelection (Selection::chord (0, 1));
    editor->refreshNow();
    check (match.set.isEnabled(), "with a chord selected, Set is enabled");
    match.set.onClick();
    check (layout (p->getProgression()).startsWith ("Verse[C:4 G7:4 F:4") && p->undoState().undoLabel == "Set chord",
           "Set replaces the Verse's Am with G7, keeping its length (" + layout (p->getProgression()).substring (0, 24) + ")");
    match.add.onClick();
    check (layout (p->getProgression()).startsWith ("Verse[C:4 G7:4 G7:4 F:4") && p->undoState().undoLabel == "Add chord", "Add puts another after it, at the marker");

    // The picker: picked notes win over heard ones.
    match.picker.setPicked ((1u << 2) | (1u << 5) | (1u << 9));    // D F A
    editor->refreshNow();
    check (! match.getMatches().empty() && match.getMatches().front().cell.name == "Dm" && match.fromNotesName().startsWith ("Dm"),
           "picking D F A: Dm, and Chord from notes is Dm");
    match.root.setSelectedId (5 + 2, juce::sendNotificationSync);   // F
    check (match.fromNotesChord().root() == 5, "a root of F builds it on F (" + match.fromNotesName() + ")");
    match.root.setSelectedId (1, juce::sendNotificationSync);
    match.picker.setPicked ((1u << 10) | (1u << 2) | (1u << 5));   // Bb D F
    editor->refreshNow();
    const auto narrow = match.getMatches().front().cell.name;
    match.wide.setToggleState (true, juce::sendNotificationSync);
    match.wide.onClick();
    check (match.getMatches().front().cell.name == "Bb" && match.getMatches().front().fifths == -1 && narrow != "Bb",
           "Bb D F: with ±2 fifths, Bb from the F palette (" + narrow + " without)");

    // Clear forgets both.
    match.clear.onClick();
    block (*p);
    editor->refreshNow();
    check (match.noteSet().empty() && match.getMatches().empty() && ! palette.matchShown(), "Clear: no notes, no matches, the palette unfaded");

    // Listen is saved with the state (a restore sets it back, as pluginval checks), not in presets.
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto restored = std::make_unique<ChordZimmerProcessor>();
    set (*restored, params::listen, 0.0f);
    restored->setStateInformation (state.getData(), (int) state.getSize());
    auto other = std::make_unique<ChordZimmerProcessor>();
    set (*p, params::listen, 0.0f);
    juce::MemoryBlock off;
    p->getStateInformation (off);
    set (*other, params::listen, 1.0f);
    other->setStateInformation (off.getData(), (int) off.getSize());
    check (get (*restored, params::listen) > 0.5f && get (*other, params::listen) < 0.5f && ! params::isPresetParameter (params::listen),
           "Listen restores with the state, on or off; it isn't in presets");
    set (*p, params::listen, 0.0f);
    editor->showMatch (false);
    check (! palette.matchShown(), "closing Match unfades the palette");
    closeEditor (*p, editor);
}

//==============================================================================
// 11: the selection's drag-out (Phase 7, PLAN.md §3.7).

static void testDragOut()
{
    std::cout << "== Drag MIDI ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    block (*p);
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->refreshNow();
    auto& t = editor->getTimeline();
    auto& handle = t.dragHandle;
    juce::String error;
    check (! handle.writeFile (false, error).existsAsFile() && error.isNotEmpty(), "an empty timeline: nothing to drag (" + error + ")");

    p->setProgression (song());
    set (*p, params::bass, (float) (int) BassMode::Root);
    editor->refreshNow();
    check (handle.getLabel() == "Drag MIDI: all, 12 bars", "nothing selected: all of it (" + handle.getLabel() + ")");
    const auto readBack = [] (const juce::File& f, MidiSong& song)
    {
        juce::MemoryBlock data;
        std::string why;
        return f.loadFileAsData (data) && readMidiFile ((const uint8_t*) data.getData(), data.getSize(), song, why);
    };
    MidiSong all;
    const auto allFile = handle.writeFile (false, error);
    const auto expected = renderSelection (p->getProgression(), {}, p->currentSettings(), 120.0);
    check (allFile.getFileName() == "Verse-Chorus-Bridge_C.mid" && readBack (allFile, all) && all.notes.size() == expected.notes.size()
               && all.markers == expected.markers && all.markers.size() == 3 && all.name == "Verse-Chorus-Bridge_C",
           "it writes Verse-Chorus-Bridge_C.mid: the render, with Bass, and three markers (" + juce::String ((int) all.notes.size()) + " notes)");

    t.setSelection (Selection::section (1));
    editor->refreshNow();
    MidiSong chorus;
    const auto chorusFile = handle.writeFile (false, error);
    const auto chorusExpected = renderSelection (p->getProgression(), Selection::section (1), p->currentSettings(), 120.0);
    bool same = readBack (chorusFile, chorus) && chorus.notes.size() == chorusExpected.notes.size();
    for (size_t i = 0; same && i < chorus.notes.size(); ++i)
        same = chorus.notes[i].start == chorusExpected.notes[i].start && chorus.notes[i].end == chorusExpected.notes[i].end
            && chorus.notes[i].pitch == chorusExpected.notes[i].pitch;
    check (handle.getLabel() == "Drag MIDI: Chorus, 4 bars" && chorusFile.getFileName() == "Chorus_C.mid" && same
               && chorus.markers == std::vector<MidiMarker> { { 0, "Chorus" } },
           "the Chorus selected: Chorus_C.mid, its notes exactly, one marker (" + handle.getLabel() + ")");
    t.setSelection (Selection::chord (2, 1));
    editor->refreshNow();
    check (handle.getLabel().startsWith ("Drag MIDI: Bb") && handle.writeFile (true, error).getFileName() == "Bb_C chords.mid",
           "one chord: named after it; with Alt, \"chords\" (" + handle.getLabel() + ")");
    const auto at = editor->getLocalPoint (&handle, handle.getLocalBounds().getCentre());
    editor->showInfoAt (at);
    check (editor->infoView().shownKey() == "ui.dragSelection" && editor->infoView().shownText().startsWith ("Drag into a track: the selection (Bb)"),
           "pointing at it says what it holds (" + editor->infoView().shownText().substring (0, 40) + ")");
    closeEditor (*p, editor);
}

//==============================================================================
// 12: Chord Relations (Phase 6, PLAN.md §3.5).

static void testRelations (const juce::File& fixtures, const juce::File& out, bool update)
{
    std::cout << "== Chord Relations ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    block (*p);
    p->setProgression (song());
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->showKeyboard (true);
    editor->refreshNow();
    auto& palette = editor->getPalette();
    auto& t = editor->getTimeline();
    const auto nextNames = [&]
    {
        juce::StringArray n;
        for (const auto& c : palette.next.getChords())
            n.add (juce::String::fromUTF8 (c.name.c_str()));
        return n.joinIntoString (" ");
    };

    palette.relationsButton.setToggleState (true, juce::sendNotificationSync);
    palette.relationsButton.onClick();
    check (palette.relationsShown() && palette.next.isVisible(), "the Relations toggle shows them, and Next");
    check (palette.getFocus().root == 0 && palette.relationOf (3, 3).label == "IV" && palette.relationOf (3, 4).label == "V"
               && palette.relationOf (3, 5).label == "rel" && nextNames() == "F G7 Am Dm Em",
           "with nothing chosen the focus is the home tonic, C: F IV, G V, Am rel; Next F G7 Am Dm Em");

    // A click on a palette cell (Am) makes it the focus.
    const ui::PalettePanel::CellRef am { 3, 5 };
    const auto at = palette.cellBounds (am).getCentre();
    palette.mouseDown (mouse (palette, at));
    palette.mouseUp (mouse (palette, at, {}));
    editor->refreshNow();
    check (palette.getFocus().root == 9 && palette.relationOf (3, 0).label == "rel" && palette.relationOf (3, 1).label == "iv/vi"
               && palette.relationOf (3, 0).common == 2 && palette.relationOf (3, 0).distance == 2 && nextNames() == "Dm F G7 Em C",
           "clicking Am: C rel (2 in common, 2 apart), Dm iv/vi; Next Dm F G7 Em C (" + nextNames() + ")");
    snapshot (*editor, "editor-relations", fixtures, out, update);

    // Selecting a timeline chord (the Verse's G) makes it the focus.
    t.setSelection (Selection::chord (0, 3));
    editor->refreshNow();
    check (palette.getFocus().root == 7 && palette.relationOf (3, 0).label == "IV/V" && nextNames() == "C Am F Em Dm",
           "selecting the Verse's G: C is IV/V; Next C Am F Em Dm (" + nextNames() + ")");

    // Next ▸: a double-click adds it at the marker (after G).
    palette.next.mouseDoubleClick (mouse (palette.next, palette.next.chipBounds (0).getCentre(), juce::ModifierKeys::leftButtonModifier, 2));
    check (layout (p->getProgression()).startsWith ("Verse[C:4 Am:4 F:4 G:4 C:4]"), "double-clicking Next's C adds it after G");

    // Replace with ▸: the first replacement for the Verse's first C (a 3rd below: Am).
    const auto list = replacementsFor (*p->getProgression().chord ({ 0, 0 }), 0, Scale::Major);
    check (! list.empty() && t.replaceChordWith ({ 0, 0 }, list.front().chord) && layout (p->getProgression()).startsWith ("Verse[Am:4 Am:4")
               && p->undoState().undoLabel == "Replace chord",
           "Replace with ▸ (" + juce::String (list.empty() ? "" : list.front().reason) + "): the Verse's C becomes Am, one undo step");

    palette.showRelations (false);
    check (! palette.next.isVisible(), "toggled off: Next hides");
    closeEditor (*p, editor);
}

//==============================================================================
// 13: factory templates (Phase 8, PLAN.md §2).

static void testTemplates()
{
    std::cout << "== factory templates ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    const auto& all = factoryPresets();
    check (p->getNumPrograms() == 19 && all[11].name == "Four Chords" && all[18].name == "Dorian Vamp",
           "19 programs: Init and 0.0.1's ten keep their numbers, the 8 templates follow");
    struct Want { const char* name; const char* layout; int key; Scale scale; };
    const Want want[] = {
        { "Four Chords", "Verse[C:4 G:4 Am:4 F:4] Chorus[F:4 C:4 G:4 Am:4]", 0, Scale::Major },
        { "Minor Axis", "Verse[Am:4 F:4 C:4 G:4] Chorus[F:4 G:4 Am:8]", 9, Scale::NaturalMinor },
        { "Two-Five-One", "A[Cm7:4 F7:4 Bbmaj7:8] Turnaround[Dm7:2 G7:2 Cm7:2 F7:2]", 10, Scale::Major },
        { "Twelve-Bar Blues", "Blues[A7:16 D7:8 A7:8 E7:4 D7:4 A7:4 E7:4]", 9, Scale::Mixolydian },
        { "Andalusian Cadence", "Cadence[Dm:4 C:4 Bb:4 A:4]", 2, Scale::HarmonicMinor },
        { "Canon Ground", "Canon[D:2 A:2 Bm:2 F#m:2 G:2 D:2 G:2 A:2]", 2, Scale::Major },
        { "Fifties", "Verse[G:4 Em:4 C:4 D:4] Bridge[C:4 Cm:4 G:8]", 7, Scale::Major },
        { "Dorian Vamp", "Vamp[Dm7:8 G7:8]", 2, Scale::Dorian },
    };
    bool allOk = true;
    juce::String why;
    for (int i = 11; i < 19; ++i)
    {
        const auto& w = want[i - 11];
        p->setCurrentProgram (i);
        const auto got = layout (p->getProgression());
        const bool ok = all[(size_t) i].name == w.name && all[(size_t) i].category == "Templates" && got == w.layout
                     && (int) get (*p, params::key) == w.key && (int) get (*p, params::scale) == (int) w.scale && ! p->undoState().canUndo;
        if (! ok && why.isEmpty())
            why = juce::String (w.name) + ": " + got;
        allOk = allOk && ok;
    }
    check (allOk, "each template loads its key, scale and progression, the history starting afresh" + (why.isEmpty() ? juce::String() : " (" + why + ")"));
    const auto before = p->getProgression();
    p->setCurrentProgram (1);   // Pop Anthem: no progression
    check (p->getProgression() == before, "a preset without a progression leaves the timeline as it was");
}

//==============================================================================
// 14: hardening (Phase 8; ChordMonkey's Phase 8 checks, for ChordZimmer's timeline).

namespace
{
    // Every effect at once, the timeline playing in Sync and keys playing chords.
    void everythingOn (ChordZimmerProcessor& p)
    {
        p.setProgression (song());
        set (p, params::chordGroup, (float) (int) ChordGroup::Thirteenths);
        set (p, params::stack, (float) (int) StackMode::PlusOneAndTwo);
        set (p, params::velocity, (float) (int) VelocityCurve::House);
        set (p, params::hold, (float) (int) HoldMode::Bar);
        set (p, params::voiceLeading, 1.0f);
        set (p, params::voicing, (float) (int) VoicingMode::Wide);
        set (p, params::bass, (float) (int) BassMode::RootFifth);
        set (p, params::strum, (float) (int) StrumMode::Up);
        set (p, params::play, 1.0f);
    }

    // Parameters that aren't finite, or outside their range.
    int insaneParameters (ChordZimmerProcessor& p)
    {
        int n = 0;
        for (auto* param : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            {
                const float v = r->getValue();
                n += std::isfinite (v) && v >= 0.0f && v <= 1.0f ? 0 : 1;
            }
        return n;
    }

    // A key (C3) still plays a chord, and letting go ends it.
    bool playsAChord (ChordZimmerProcessor& p)
    {
        set (p, params::listen, 0.0f);
        set (p, params::analyse, 0.0f);
        set (p, params::detect, 0.0f);
        set (p, params::enable, 1.0f);
        const auto on = noteOns (block (p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) }));
        block (p, { juce::MidiMessage::noteOff (1, 48) });
        block (p);
        return on.size() >= 3 && p.soundingNotes.load() == 0;
    }
}

static void testHardening()
{
    std::cout << "== hardening: no allocation on the audio thread ==\n";
    {
        const int before = allocCount.load();
        {
            AllocWatch watch;
            allocSink = new int (8);
            delete static_cast<int*> (allocSink);
            allocSink = std::malloc (1 << 20);
            std::free (allocSink);
        }
        check (allocCount.load() - before >= 2, "the malloc hook sees allocations (" + juce::String (allocCount.load() - before) + " counted)");
    }
    {
        // Two instances with everything on under a looping transport; between blocks (as the
        // host and the message thread would) parameters change, the transport stops, starts and
        // moves its loop, the timeline is edited, previews start, Listen turns on, bypass toggles.
        auto a = std::make_unique<ChordZimmerProcessor>();
        auto b = std::make_unique<ChordZimmerProcessor>();
        PlayHead head;
        head.looping = true;
        head.loopEnd = 16.0;
        for (auto* p : { a.get(), b.get() })
        {
            p->setPlayHead (&head);
            p->prepareToPlay (48000.0, 256);
            everythingOn (*p);
        }
        juce::AudioBuffer<float> audio (2, 256);
        juce::MidiBuffer midi;
        midi.ensureSize (1 << 16);
        std::mt19937 rng (8);
        int allocations = 0, blocks = 0, peak = 0, bypassed = 0, edits = 0;
        for (int i = 0; i < 4000; ++i)
        {
            if (rng() % 50 == 0) set (*a, params::key, (float) (rng() % 12));
            if (rng() % 90 == 0) set (*a, params::chordGroup, (float) (rng() % numChordGroups));
            if (rng() % 120 == 0) set (*b, params::syncMode, (float) (rng() % 2));
            if (rng() % 150 == 0) set (*a, params::loop, (float) (rng() % 2));
            if (rng() % 200 == 0) set (*b, params::listen, (float) (rng() % 2));
            if (rng() % 200 == 0) head.playing = ! head.playing;
            if (rng() % 300 == 0) { head.loopStart = (double) (rng() % 4); head.loopEnd = head.loopStart + 0.5 + (rng() % 8); }
            if (rng() % 40 == 0) a->pushUiNote (48 + (int) (rng() % 30), 100, rng() % 2 == 0);
            if (rng() % 60 == 0)
            {
                ++edits;
                const int s = (int) (rng() % 3);
                a->editProgression ("Fuzz", [&] (ChordProgression& q) { return q.validSection (s) && q.resizeSection (s, 384 + (int) (rng() % 8) * 96); });
                if (rng() % 2 == 0)
                    a->undoProgression();
                b->setLoopSelection (Selection::section (s));
            }
            if (rng() % 70 == 0)
            {
                const auto prog = a->getProgression();
                if (const auto* c = prog.chord ({ 0, (int) (rng() % 4) }))
                    b->previewChord (*c, rng() % 2 == 0 ? 0.0 : 1.0);
                else
                    b->endPreview();
            }
            if (rng() % 100 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // the timers
            for (auto* p : { a.get(), b.get() })
            {
                midi.clear();
                if (rng() % 8 == 0)
                    midi.addEvent (rng() % 2 == 0 ? juce::MidiMessage::noteOn (1, 36 + (int) (rng() % 48), (juce::uint8) 90)
                                                  : juce::MidiMessage::noteOff (1, 36 + (int) (rng() % 48)), (int) (rng() % 256));
                const bool bypass = p == b.get() && i % 500 > 480;
                const int before = allocCount.load();
                {
                    AllocWatch watch;
                    if (bypass)
                        p->processBlockBypassed (audio, midi);
                    else
                        p->processBlock (audio, midi);
                }
                allocations += allocCount.load() - before;
                bypassed += bypass ? 1 : 0;
                peak = std::max (peak, midi.getNumEvents());
                ++blocks;
            }
            head.advance (256, 48000.0);
        }
        check (allocations == 0, juce::String (blocks) + " blocks of two instances with everything on and the timeline playing (" + juce::String (edits)
                                     + " edits, " + juce::String (bypassed) + " bypassed, up to " + juce::String (peak) + " events out): "
                                     + juce::String (allocations) + " allocations in processBlock");
        for (auto* p : { a.get(), b.get() })
            p->setPlayHead (nullptr);
    }

    std::cout << "== hardening: damaged saved states ==\n";
    {
        auto p = std::make_unique<ChordZimmerProcessor>();
        p->prepareToPlay (44100.0, 512);
        std::mt19937 rng (88);
        int insane = 0;
        for (int i = 0; i < 300; ++i)
        {
            juce::MemoryBlock junk ((size_t) (rng() % 4096));
            for (size_t k = 0; k < junk.getSize(); ++k)
                junk[k] = (char) (rng() & 0xff);
            if (i % 3 == 0 && junk.getSize() > 8)
                std::memcpy (junk.getData(), "VC2!", 4);   // JUCE's binary-XML header, garbage after
            p->setStateInformation (junk.getData(), (int) junk.getSize());
            insane += insaneParameters (*p);
        }
        p->setStateInformation (nullptr, 0);
        p->setStateInformation ("xx", -5);
        check (insane == 0 && playsAChord (*p), "300 blobs of random bytes (a third with a valid header) are ignored; the instance still plays");

        const char* values[] = { "nan", "inf", "-inf", "1e300", "-1e300", "abc", "", "0x7fffffff", "-0" };
        int worst = 0;
        for (const char* v : values)
        {
            juce::String xml ("<ChordZimmer program=\"99999\" presetName=\"\">");
            for (auto* param : p->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
                    xml << "<PARAM id=\"" << r->paramID << "\" value=\"" << v << "\"/>";
            xml << "<PARAM id=\"noSuchParameter\" value=\"1\"/><PARAM id=\"key\" value=\"3\"/>"
                   "<PROGRESSION text=\"czp1 S:9:" << v << " C:99:0.4.7:384:0:0:-1\"/><WORKSPACE version=\"1\"><PATTERN index=\"7\" text=\"" << v << "\"/></WORKSPACE>"
                   "<UI keyboard=\"maybe\" width=\"-1\" height=\"1e9\" paletteFifths=\"" << v << "\" paletteScale=\"999\" snap=\"-7\" zoom=\"" << v << "\"/></ChordZimmer>";
            juce::MemoryBlock state;
            juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (xml), state);
            p->setStateInformation (state.getData(), (int) state.getSize());
            worst = std::max (worst, insaneParameters (*p));
            worst += p->getProgression().empty() ? 0 : 1;       // the damaged progression text loads empty
            std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());   // opens with the UI's garbage
            if (auto* e = dynamic_cast<ChordZimmerEditor*> (editor.get()))
                e->refreshNow();
            p->editorBeingDeleted (editor.get());
            editor.reset();
            worst += p->getCurrentProgram() < 0 || p->getCurrentProgram() >= p->getNumPrograms() ? 1 : 0;
        }
        check (worst == 0 && playsAChord (*p), "states whose every value is nan, inf, -inf, +/-1e300, text or empty, with unknown IDs, a damaged "
                                               "progression and workspace and a nonsense UI: parameters stay in range, the progression loads "
                                               "empty, the editor opens, the instance plays");

        auto tree = p->presetState();
        tree.getChildWithProperty ("id", params::key).setProperty ("value", "nan", nullptr);
        set (*p, params::key, 5.0f);
        p->applyPreset (tree, "User", "Damaged");
        check ((int) get (*p, params::key) == 0, "a preset whose Key is NaN loads Key's default (C)");
    }

    std::cout << "== hardening: host edge cases ==\n";
    {
        auto p = std::make_unique<ChordZimmerProcessor>();
        p->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> none (2, 0), huge (2, 8192), mono (1, 512);
        auto run = [&p] (juce::AudioBuffer<float>& audio, std::initializer_list<juce::MidiMessage> events, bool bypass = false)
        {
            juce::MidiBuffer midi;
            for (const auto& e : events)
                midi.addEvent (e, 0);
            if (bypass)
                p->processBlockBypassed (audio, midi);
            else
                p->processBlock (audio, midi);
            return midi;
        };

        const auto zero = run (none, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
        const auto big = run (huge, { juce::MidiMessage::noteOff (1, 48), juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100) });
        const auto monoOut = run (mono, { juce::MidiMessage::noteOff (1, 50) });
        check (noteOns (zero) == std::vector<int> { 48, 52, 55 } && noteOns (big) == std::vector<int> { 50, 53, 57 } && noteOffs (big) == 3
                   && noteOffs (monoOut) == 3 && p->soundingNotes.load() == 0,
               "a zero-length block plays its key; an 8192-sample block after prepareToPlay (..., 512) and a mono block work");

        run (mono, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
        const auto bypassed = run (mono, { juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90) }, true);
        check (noteOffs (bypassed) == 3 && noteOns (bypassed) == std::vector<int> { 64 }, "host bypass while a chord sounds ends it and passes input through");
        run (mono, { juce::MidiMessage::noteOff (1, 64), juce::MidiMessage::noteOff (1, 48) }, true);

        // The timeline playing in Preview, then the host's all-notes-off, a reset and a rate change.
        p->setProgression (song());
        set (*p, params::syncMode, 1.0f);
        set (*p, params::play, 1.0f);
        const auto started = run (mono, {});
        const auto allOff = run (mono, { juce::MidiMessage::allNotesOff (1) });
        int cc = 0;
        for (const auto m : allOff)
            cc += m.getMessage().isAllNotesOff() ? 1 : 0;
        set (*p, params::play, 0.0f);
        run (mono, {});
        run (mono, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) });
        p->releaseResources();
        const auto afterRelease = run (mono, {});
        p->prepareToPlay (96000.0, 128);
        run (mono, { juce::MidiMessage::noteOff (1, 48), juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100) });
        p->prepareToPlay (44100.0, 2048);
        const auto afterRate = run (mono, {});
        run (mono, { juce::MidiMessage::noteOff (1, 52) });
        check (noteOns (started).size() == 3 && noteOffs (allOff) >= 3 && cc == 1 && noteOffs (afterRelease) == 3 && noteOffs (afterRate) == 3
                   && p->soundingNotes.load() == 0,
               "the timeline in Preview stops on the host's all-notes-off (the CC passes on); releaseResources and a rate change end a chord");

        const juce::uint8 sysex[] = { 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00 };
        const auto passed = run (mono, { juce::MidiMessage::createSysExMessage (sysex, (int) sizeof (sysex)) });
        bool sysexIntact = false;
        for (const auto m : passed)
            sysexIntact |= m.getMessage().isSysEx() && m.getMessage().getSysExDataSize() == (int) sizeof (sysex)
                        && std::memcmp (m.getMessage().getSysExData(), sysex, sizeof (sysex)) == 0;
        check (sysexIntact, "SysEx passes through unchanged");

        juce::MidiBuffer flood;
        for (int i = 0; i < 2900; ++i)
            flood.addEvent (juce::MidiMessage::noteOn (1, 36 + i % 48, (juce::uint8) 100), i % 400);
        for (int n = 0; n < 128; ++n)
            flood.addEvent (juce::MidiMessage::noteOff (1, n), 450);
        juce::AudioBuffer<float> audio (2, 512);
        p->processBlock (audio, flood);
        run (mono, {});
        check (p->soundingNotes.load() == 0, "2900 note-ons and 128 key-ups in one block (input holds 2048): the key-ups get in, nothing sounds after");
    }

    std::cout << "== hardening: many instances ==\n";
    {
        std::vector<std::unique_ptr<ChordZimmerProcessor>> all;
        for (int i = 0; i < 64; ++i)
        {
            all.push_back (std::make_unique<ChordZimmerProcessor>());
            all.back()->prepareToPlay (44100.0, 256);
            all.back()->setProgression (song());
        }
        int played = 0;
        for (auto& p : all)
            played += noteOns (block (*p, { juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100) })).size() == 3 ? 1 : 0;
        std::shuffle (all.begin(), all.end(), std::mt19937 (64));
        while (all.size() > 2)
            all.pop_back();
        int ended = 0;
        for (auto& p : all)
        {
            block (*p, { juce::MidiMessage::noteOff (1, 48) });
            ended += p->soundingNotes.load() == 0 ? 1 : 0;
        }
        check (played == 64 && ended == 2, "64 instances with a progression play; destroyed in random order, the last two still work");
    }
}

//==============================================================================
// The benchmark (PLAN.md §6 Phase 8: 8 instances under 1 % CPU each; Tests/run_bench.sh).

static int runBench (double seconds)
{
    constexpr int instances = 8;
    constexpr double rate = 44100.0;
    std::cout << "== CPU per instance (" << instances << " instances, " << seconds << " s of audio at 44.1 kHz, 120 BPM) ==\n"
              << "   scenario           block   mean % of real time   99.9th percentile / worst block (% of a block's duration)\n";
    const char* names[] = { "idle", "timeline (Sync)", "everything on", "Preview", "Listen", "Detect" };
    double worstMean = 0.0;
    for (int scenario = 0; scenario < 6; ++scenario)
        for (const int blockSize : { 32, 128, 512 })
        {
            std::vector<std::unique_ptr<ChordZimmerProcessor>> procs;
            PlayHead head;
            head.looping = true;
            head.loopEnd = 48.0;
            head.playing = scenario != 3;
            for (int i = 0; i < instances; ++i)
            {
                procs.push_back (std::make_unique<ChordZimmerProcessor>());
                auto& p = *procs.back();
                p.setPlayHead (&head);
                p.prepareToPlay (rate, blockSize);
                if (scenario == 1 || scenario == 3)
                {
                    p.setProgression (song());
                    set (p, params::play, 1.0f);
                }
                if (scenario == 2)
                    everythingOn (p);
                if (scenario == 3)
                    set (p, params::syncMode, 1.0f);
                if (scenario == 4)
                    set (p, params::listen, 1.0f);
                if (scenario == 5)
                    set (p, params::detect, (float) (int) params::DetectMode::Capture);
            }
            // Listen, Detect and everything on: sixteenth notes up and down a scale come in.
            const int sixteenth = (int) (rate * 60.0 / 120.0 / 4.0);
            int tuneNote = -1;
            juce::AudioBuffer<float> audio (2, blockSize);
            juce::MidiBuffer midi;
            midi.ensureSize (1 << 16);
            const int numBlocks = (int) (seconds * rate / blockSize);
            double total = 0.0, worst = 0.0;
            std::vector<double> times;
            times.reserve ((size_t) numBlocks * instances);
            for (int n = 0; n < numBlocks; ++n)
            {
                juce::MidiBuffer tune;
                if (scenario == 2 || scenario >= 4)
                    for (int64_t smp = ((int64_t) n * blockSize + sixteenth - 1) / sixteenth * sixteenth; smp < (int64_t) (n + 1) * blockSize; smp += sixteenth)
                    {
                        const int at = (int) (smp - (int64_t) n * blockSize);
                        if (tuneNote >= 0)
                            tune.addEvent (juce::MidiMessage::noteOff (1, tuneNote), at);
                        const int step = (int) ((smp / sixteenth) % 14);
                        tuneNote = 60 + scaleOffset (Scale::NaturalMinor, step < 7 ? step : 14 - step);
                        tune.addEvent (juce::MidiMessage::noteOn (1, tuneNote, (juce::uint8) 100), at);
                    }
                for (auto& p : procs)
                {
                    midi.clear();
                    midi.addEvents (tune, 0, -1, 0);
                    const auto t0 = std::chrono::steady_clock::now();
                    p->processBlock (audio, midi);
                    const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                    total += dt;
                    worst = std::max (worst, dt);
                    times.push_back (dt);
                }
                head.advance (blockSize, rate);
            }
            const double audioSeconds = numBlocks * blockSize / rate;
            const double mean = 100.0 * total / instances / audioSeconds;
            const double peak = 100.0 * worst / (blockSize / rate);
            std::nth_element (times.begin(), times.begin() + (long) (times.size() * 999 / 1000), times.end());
            const double p999 = 100.0 * times[times.size() * 999 / 1000] / (blockSize / rate);
            worstMean = std::max (worstMean, mean);
            std::printf ("   %-17s %6d   %10.4f%%            %8.3f%% / %.3f%%\n", names[scenario], blockSize, mean, p999, peak);
            for (auto& p : procs)
                p->setPlayHead (nullptr);
        }
    check (worstMean < 1.0, "every scenario and block size: under 1% of one core per instance (worst mean " + juce::String (worstMean, 4) + "%)");
    return failures == 0 ? 0 : 1;
}

//==============================================================================
// 15: MIDI files dropped in (Phase 9).

static void testImport()
{
    std::cout << "== MIDI import ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    p->setProgression (song());
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->refreshNow();
    auto& t = editor->getTimeline();
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("fixtures/import");

    check (t.isInterestedInFileDrag ({ dir.getChildFile ("block_chords.mid").getFullPathName() }) && ! t.isInterestedInFileDrag ({ "/tmp/a.wav" }),
           "the timeline takes .mid files, not others");
    // Dropped past the end: a new section named after the file.
    const float end = t.tabBounds (2).getRight() + 40.0f;
    t.filesDropped ({ dir.getChildFile ("block_chords.mid").getFullPathName() }, (int) end, 40);
    check (layout (p->getProgression()).endsWith ("block_chords[C:4 G:4 Am:4 F:4]") && p->undoState().undoLabel == "Import MIDI"
               && selText (t.selection()) == "S3",
           "block_chords.mid dropped past the end: a section of its chords, one undo step, selected");
    check (editor->infoView().shownKey() == "display.import" && editor->infoView().shownText().startsWith ("block_chords.mid: 4 chords in 1 section"),
           "the Info View says what came in (" + editor->infoView().shownText() + ")");

    // Its markers make sections; dropped into the Verse (after its first chord) the chords join it.
    p->undoProgression();
    editor->refreshNow();
    const auto into = t.chordBounds ({ 0, 1 }).getX() + 2.0f;
    check (t.importFile (dir.getChildFile ("sections.mid"), into) && layout (p->getProgression()).startsWith ("Verse[C:4 C:4 G:4 F:4 G:4 Am:4"),
           "sections.mid dropped inside the Verse: its four chords go in there (" + layout (p->getProgression()).substring (0, 40) + ")");
    p->undoProgression();
    editor->refreshNow();
    check (t.importFile (dir.getChildFile ("sections.mid"), end) && layout (p->getProgression()).endsWith ("Verse[C:4 G:4] Chorus[F:4 G:4]"),
           "…and past the end: its two marked sections, Verse and Chorus");

    // A melody only: nothing for the timeline, and the Info View says to use Match.
    const auto before = p->getProgression();
    check (! t.importFile (dir.getChildFile ("melody_only.mid"), end) && p->getProgression() == before
               && editor->infoView().shownText().contains ("has no chords"),
           "melody_only.mid on the timeline: no chords, nothing changes, a hint to drop it on Match");
    check (! t.importFile (dir.getChildFile ("not_midi.mid"), end) && editor->infoView().shownText().contains ("not a MIDI file"), "a text file: says so");
    // 1.1: drums on channel 1 (as Live exports a clip) and a busy comp, read a beat at a time.
    check (! t.importFile (dir.getChildFile ("live_drums.mid"), end) && editor->infoView().shownText().contains ("is all drums"),
           "live_drums.mid: nothing changes, the Info View says it's all drums");
    editor->refreshNow();
    const float pastAll = t.tabBounds (p->getProgression().numSections() - 1).getRight() + 40.0f;
    check (t.importFile (dir.getChildFile ("busy_comp.mid"), pastAll) && layout (p->getProgression()).endsWith ("busy_comp[C:4 Am:4]")
               && editor->infoView().shownText().contains ("read a beat at a time"),
           "busy_comp.mid: C Am, the Info View saying it was read a beat at a time (" + layout (p->getProgression()).fromLastOccurrenceOf ("] ", false, false) + "; " + editor->infoView().shownText() + ")");
    p->undoProgression();
    editor->refreshNow();

    // On Match: the melody is the note set.
    editor->showMatch (true);
    auto& match = editor->getMatch();
    check (match.isInterestedInFileDrag ({ dir.getChildFile ("melody_only.mid").getFullPathName() }), "Match takes .mid files too");
    match.filesDropped ({ dir.getChildFile ("chords_melody.mid").getFullPathName() }, 10, 10);
    editor->refreshNow();
    check (match.importedName() == "chords_melody.mid" && match.noteSet().weight[0] == 5.0 && ! match.getMatches().empty()
               && match.getMatches().front().cell.name == "Am",
           "chords_melody.mid on Match: its melody (not its chords) is matched: Am first (" + juce::String (match.getMatches().empty() ? "-" : match.getMatches().front().cell.name) + ")");
    match.picker.setPicked ((1u << 0) | (1u << 4) | (1u << 7));
    check (match.getMatches().front().cell.name == "C", "picked notes still win over the file");
    match.clear.onClick();
    editor->refreshNow();
    check (match.importedName().isEmpty() && match.noteSet().empty(), "Clear forgets the file");
    // Import MIDI… and Open .mid… (the file chooser replaced): at the marker; on Match.
    p->setProgression (song());
    editor->refreshNow();
    t.setSelection (Selection::chord (0, 0));    // the marker after the Verse's C
    t.chooseFile = [dir] (std::function<void (const juce::File&)> then) { then (dir.getChildFile ("block_chords.mid")); };
    t.importButton.onClick();
    check (layout (p->getProgression()).startsWith ("Verse[C:4 C:4 G:4 Am:4 F:4 Am:4") && p->undoState().undoLabel == "Import MIDI",
           "Import MIDI… (block_chords.mid chosen): its chords go in at the marker, after the Verse's C (" + layout (p->getProgression()).substring (0, 40) + ")");
    editor->showMatch (true);
    match.chooseFile = [dir] (std::function<void (const juce::File&)> then) { then (dir.getChildFile ("melody_only.mid")); };
    match.openFile.onClick();
    editor->refreshNow();
    check (match.importedName() == "melody_only.mid" && ! match.getMatches().empty(), "Open .mid… on Match: the file's melody is matched");
    editor->showMatch (false);
    closeEditor (*p, editor);
}

// Follow (1.2): the palette brings the playing chord on screen, stepping round the circle of
// fifths for a borrowed one; once per change; saved with the plug-in; keyboard chords too.
static void testFollow()
{
    std::cout << "== Follow ==\n";
    auto p = std::make_unique<ChordZimmerProcessor>();
    p->prepareToPlay (48000.0, 512);
    block (*p);
    p->setProgression (song());
    auto editor = openEditor (*p);
    editor->setSize (ChordZimmerEditor::defaultWidth, ChordZimmerEditor::defaultHeight);
    editor->refreshNow();
    auto& palette = editor->getPalette();
    const auto& state = editor->getNavigatorState();
    const auto inView = [&] (int variant) { return variant >= palette.firstRow() && variant < palette.firstRow() + palette.visibleRows(); };

    // Off (the default): the Bridge's bVI (Ab) plays, the palette stays in C.
    check (! palette.following(), "Follow is off at first");
    p->playhead = 33.0;
    p->playingChord = 10;
    editor->refreshNow();
    check (state.fifths == 0, "off: the Bridge's Ab plays, the palette stays at home");

    // On: Ab is the IV of Eb major, three fifths down.
    palette.followBox.setToggleState (true, juce::sendNotification);
    editor->refreshNow();
    const auto ab = followSpot ((1u << 8) | (1u << 0) | (1u << 3), 8, state);
    check (state.fifths == -3 && ab.exact && inView (ab.variant) && (bool) p->uiState.getProperty ("follow", false),
           "on: the palette steps to Eb major (-3), Ab's row in view; saved (" + juce::String (state.fifths) + ")");
    const auto& cell = palette.getGrid().rows[(size_t) ab.variant].cells[(size_t) ab.degree];
    check (cell.exists && cell.name == "Ab", "the cell there is Ab");

    // Once per change: the palette moved by hand stays while the same chord plays.
    editor->getNavigator().goHome();
    editor->refreshNow();
    check (state.fifths == 0, "moved home by hand while the Ab still plays: it stays home");

    // The Chorus's ii7 (Dm7): in C major, the 7th row brought into view.
    palette.scrollRows (-20);
    p->playingChord = 8;
    editor->refreshNow();
    const auto dm7 = followSpot ((1u << 2) | (1u << 5) | (1u << 9) | (1u << 0), 2, state);
    check (state.fifths == 0 && dm7.exact && inView (dm7.variant), "the Chorus's Dm7: here in C, its row scrolled into view");

    // Stopped, then the same chord again: followed again.
    p->playingChord = 10;
    editor->refreshNow();
    check (state.fifths == -3, "the Ab again (after the Dm7): back to Eb major");
    editor->getNavigator().goHome();
    p->playhead = -1.0;
    p->playingChord = -1;
    editor->refreshNow();
    p->playhead = 33.0;
    p->playingChord = 10;
    editor->refreshNow();
    check (state.fifths == -3, "stopped, then the same Ab: followed again");
    p->playhead = -1.0;
    p->playingChord = -1;
    editor->getNavigator().goHome();
    editor->refreshNow();

    // A keyboard: Bb D F held (the on-screen keyboard) sounds Bb; F major (-1) holds it.
    for (int n : { 58, 62, 65 })
        p->pushUiNote (n, 100, true);
    block (*p);
    editor->refreshNow();
    check (state.fifths == -1, "Bb D F played on the keyboard: the palette steps to F major (-1) (" + juce::String (state.fifths) + ")");
    for (int n : { 58, 62, 65 })
        p->pushUiNote (n, 0, false);
    block (*p);
    editor->refreshNow();

    // Saved with the plug-in: a new editor opens ticked.
    editor.reset();
    auto again = openEditor (*p);
    again->refreshNow();
    check (again->getPalette().following(), "a new editor opens with Follow ticked");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc >= 2 && juce::String (argv[1]) == "--bench")
        return runBench (argc >= 3 ? juce::String (argv[2]).getDoubleValue() : 20.0);
    if (argc < 3)
    {
        std::cerr << "usage: ChordZimmerSnapshot <fixtures dir> <output dir> [--update | --no-pixels]\n"
                     "       ChordZimmerSnapshot --bench [seconds]\n";
        return 2;
    }
    const juce::File fixtures (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    const bool update = argc > 3 && juce::String (argv[3]) == "--update";
    comparePixels = ! (argc > 3 && juce::String (argv[3]) == "--no-pixels");
    fixtures.createDirectory();
    out.createDirectory();

    testSnapshots (fixtures, out, update);
    testTimeline();
    testPalette();
    testUndoAndState();
    testDetect();
    testRealTime();
    testMatch (fixtures, out, update);
    testDragOut();
    testRelations (fixtures, out, update);
    testTemplates();
    testHardening();
    testImport();
    testFollow();

    std::cout << (failures == 0 ? "\nsnapshot: all passed\n" : "\n*** snapshot: " + std::to_string (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
