// Chord from notes (PLAN.md §3.4, G7; Phase 5): a timeline chord from chosen notes and a root,
// for any pitch-class set, named by ChordNamer (a slash or "(add…)"/"(b9)" name when it has no
// standard one: C7#9, Fm(maj7), C(b9), Cadd9/D). Plain C++, no JUCE.
//
// The chord is voiced as the palette voices its chords: the root at chordBaseNote + its pitch
// class, the other pitch classes above it within the octave, lowest first (so ii from D F A is
// the palette's Dm, note for note). At most maxWorkspaceTones notes; a bass other than the root
// is the chord's bass note (played below it, a slash chord).
#pragma once

#include "Progression.h"
#include "../theory/Matcher.h"

namespace chordzimmer
{
    // `mask`: the pitch classes (bit pc); `root`: one of them (added if missing); `bass`: a pitch
    // class or -1. An empty chord for an empty mask or a root out of range.
    inline ProgressionChord chordFromNotes (unsigned mask, int root, int bass = -1, int ticks = defaultChordTicks) noexcept
    {
        mask &= 0xfffu;
        if (mask == 0 || root < 0)
            return {};
        root %= 12;
        mask |= 1u << root;
        std::array<int, 12> tones {};
        int n = 0;
        for (int i = 0; i < 12 && n < maxWorkspaceTones; ++i)
            if (((mask >> ((root + i) % 12)) & 1u) != 0)
                tones[(size_t) n++] = i;
        return makeProgressionChord (makeWorkspaceChord (root, tones.data(), n), ticks, 0, 0, bass < 0 || bass % 12 == root ? -1 : bass);
    }

    // The root a set of notes suggests: the namer's, given the set's bass (the lowest note heard,
    // or the lowest pitch class); -1 for no notes.
    inline int suggestedRoot (unsigned mask, int bass = -1) noexcept
    {
        mask &= 0xfffu;
        if (mask == 0)
            return -1;
        int lowest = bass;
        for (int pc = 0; lowest < 0 && pc < 12; ++pc)
            if (((mask >> pc) & 1u) != 0)
                lowest = pc;
        const ChordName name = nameChord (mask, lowest, makeSpelling (0, Scale::Major));
        return name.root >= 0 ? name.root : lowest;
    }
}
