// Undo and redo for the progression (PLAN.md §3.3: "all undoable via a command list"). Plain C++,
// no JUCE; message thread only.
//
// Each edit is a command: a labelled callable that changes a ChordProgression (one of its edit
// methods, or several in a row as one step). perform() runs it on a copy of the current
// progression and, if it changed anything, keeps the result as a new step; undo and redo move
// along the steps. The history holds whole progressions rather than inverse commands: a
// progression is small (at most maxProgressionChords chords, a few KB), and a snapshot makes
// every undo exact by construction, however the edit was made.
//
// Bounded: at most `limit` undo steps (the oldest are dropped). Memory: each step is one copy of
// the progression, so at the limits about limit × (256 chords × 24 bytes + 64 names) ≈ 1 MB.
// Allocation: perform() copies the progression (one vector per section, plus names over the
// small-string size) and may grow or shift the step list; undo/redo/current don't allocate.
// None of this touches the audio thread, which gets its copy through a SnapshotExchange
// (Phase 3).
#pragma once

#include "Progression.h"
#include <string>
#include <utility>
#include <vector>

namespace chordzimmer
{
    class UndoList
    {
    public:
        static constexpr int defaultLimit = 100;

        explicit UndoList (ChordProgression initial = {}, int limit = defaultLimit)
            : maxSteps (limit < 1 ? 1 : limit)
        {
            steps.reserve ((size_t) maxSteps + 1);
            steps.push_back ({ {}, std::move (initial) });
        }

        const ChordProgression& current() const noexcept { return steps[(size_t) at].progression; }

        // Runs `edit` (bool (ChordProgression&)) on a copy of the current progression. If it returns
        // true and the copy differs, the copy becomes current as a new step named `label`, and
        // the redo steps are dropped. Otherwise nothing changes and it returns false.
        template <class Edit>
        bool perform (std::string label, Edit&& edit)
        {
            ChordProgression next = current();
            if (! edit (next) || next == current())
                return false;
            steps.erase (steps.begin() + at + 1, steps.end());
            steps.push_back ({ std::move (label), std::move (next) });
            ++at;
            if ((int) steps.size() > maxSteps + 1)
            {
                steps.erase (steps.begin());
                --at;
            }
            return true;
        }

        bool canUndo() const noexcept { return at > 0; }
        bool canRedo() const noexcept { return at + 1 < (int) steps.size(); }

        // The step undo or redo would act on ("" if none), for the Edit menu.
        const std::string& undoLabel() const noexcept { return canUndo() ? steps[(size_t) at].label : none; }
        const std::string& redoLabel() const noexcept { return canRedo() ? steps[(size_t) at + 1].label : none; }

        bool undo() noexcept
        {
            if (! canUndo())
                return false;
            --at;
            return true;
        }

        bool redo() noexcept
        {
            if (! canRedo())
                return false;
            ++at;
            return true;
        }

        int undoDepth() const noexcept { return at; }
        int redoDepth() const noexcept { return (int) steps.size() - 1 - at; }
        int limit() const noexcept { return maxSteps; }

        // Starts again from `p` with no history (a restored state, a preset).
        void reset (ChordProgression p)
        {
            steps.clear();
            steps.push_back ({ {}, std::move (p) });
            at = 0;
        }

    private:
        struct Step
        {
            std::string label;
            ChordProgression progression;
        };

        int maxSteps;
        std::vector<Step> steps;          // steps[0] is the oldest state kept
        int at = 0;                       // the current step
        inline static const std::string none {};
    };
}
