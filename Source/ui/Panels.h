// The editor's bars and the performance popover (PLAN.md §5; Phase 4):
//
//   TopBar            the name · On · the preset browser · Export (drag out the last bars
//                     played) · Keys (show or hide the output keyboard)
//   TransportBar      PLAN.md §3.3's transport: ⏮ rewind · Play · Loop · Sync mode · Start bar ·
//                     Snap · undo, redo · the chord now (name, numeral, where)
//   PerformancePanel  PLAN.md §3.6's performance popover, opened from the footer: Strum,
//                     Velocity, Voicing (with Octave) and Keys (what MIDI in does: Chord Group,
//                     Progression, Hold, Key octave, Heard, Detect). ChordMonkey's global row
//                     and effects cards moved here; Key and Scale (the home key) are in the
//                     footer (NavigatorBar.h)
//   HeardKey          the Heard box (ChordChimp's docs/KEY_DETECTION_PLAN.md K5): Analyse, then
//                     the key heard; a click on the guess sets Key (and Scale) to it, ✕ stops
//   IconButton        a button with a drawn icon (rewind, undo, redo, home, ◀ ▶)
//
// Test pattern and Test workspace (the LiveBridge rig) have no control: Live's device
// parameters reach them. Pattern (ChordMonkey's workspace patterns) has none since Phase 4: the
// timeline replaced the workspace (PLAN.md §2).
#pragma once

#include "Keyboards.h"
#include "PresetBar.h"

namespace chordzimmer::ui
{
    class IconButton : public juce::Button
    {
    public:
        enum class Icon { Rewind, Undo, Redo, Home, Left, Right };

        IconButton (const juce::String& infoKey, Icon);
        void setAccented (bool shouldBe);          // amber icon (Home away from home)
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Icon icon;
        bool accented = false;
    };

    class TopBar : public juce::Component
    {
    public:
        TopBar (ChordZimmerProcessor&, Bindings&, std::function<void (bool)> showKeyboard);
        void setKeyboardShown (bool shown);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

        PresetBar presets;
        ExportHandle exportHandle;

    private:
        ParamControl enable;
        juce::TextButton keyboardButton { "Keys" };
        juce::Rectangle<int> nameArea;
    };

    // The transport bar's readout: the chord sounding (or the one selected), its numeral in the
    // home key, and where the playhead is (bar.beat).
    class NowDisplay : public juce::Component
    {
    public:
        NowDisplay();
        void set (const juce::String& name, const juce::String& numeral, const juce::String& where);
        const juce::String& getName() const noexcept { return name; }
        const juce::String& getNumeral() const noexcept { return numeral; }
        void paint (juce::Graphics&) override;

    private:
        juce::String name, numeral, where;
    };

    class TransportBar : public juce::Component
    {
    public:
        TransportBar (ChordZimmerProcessor&, Bindings&);
        void resized() override;

        void setUndoState (const ChordZimmerProcessor::UndoState&);
        void setSnap (Snap);
        Snap getSnap() const noexcept;

        std::function<void()> onUndo, onRedo;
        std::function<void (Snap)> onSnap;

        IconButton rewind, undo, redo;
        ParamControl play, loop, syncMode, startBar;
        juce::ComboBox snap;
        NowDisplay now;

    private:
        ChordZimmerProcessor& processor;
        juce::Label snapCaption;
    };

    // The Analyse control and the key it heard. It follows the Analyse parameter, so a change
    // from the host shows at once.
    class HeardKey : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        HeardKey (ChordZimmerProcessor&, Bindings&);
        void refresh (const UiModel&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        // What a click does: on the box, or on its ✕ (`close`). For tests too.
        void click (bool close);
        const juce::String& getText() const noexcept { return text; }
        juce::Rectangle<int> boxBounds() const noexcept;
        juce::Rectangle<int> closeBounds() const noexcept;   // empty unless analysing
        // Whether `s` fits the box as it is now; the widths the column needs for its shortest
        // and its longest text.
        bool fits (const juce::String& s) const;
        static int minimumWidth();
        static int preferredWidth();

    private:
        void update();
        juce::String chooseText (const juce::String& full, const juce::String& shorter, const juce::String& shortest) const;
        bool analysing() const noexcept { return mode != params::AnalyseMode::Off; }
        bool matchesKey() const noexcept;

        ChordZimmerProcessor& processor;
        Bindings& bindings;
        params::AnalyseMode mode = params::AnalyseMode::Off;
        KeyGuess guess;
        int key = 0;
        Scale scale = Scale::Major;
        juce::String text;
        bool highlighted = false;     // the guess, confident: accent
        static constexpr int captionHeight = 15, closeWidth = 18;
    };

    class PerformancePanel : public juce::Component
    {
    public:
        PerformancePanel (ChordZimmerProcessor&, Bindings&);
        void refresh (const UiModel&);
        void paint (juce::Graphics&) override;
        void resized() override;

        // The Info View's key for a point on the panel itself: the card it is on.
        juce::String infoKeyAt (juce::Point<int>) const;
        static int preferredWidth() { return 660; }
        static int preferredHeight();

        HeardKey heard;

    private:
        struct Card
        {
            juce::String title, infoKey;
            std::vector<juce::Component*> controls;
            juce::Rectangle<int> bounds;
        };

        ParamControl& add (const juce::String& paramId, const juce::String& label = {});
        void layoutCard (Card&);

        Bindings& bindings;
        juce::OwnedArray<ParamControl> controls;
        std::array<Card, 4> cards;

        static constexpr int titleHeight = 22, controlHeight = 40, gap = 8, margin = 12;
    };
}
