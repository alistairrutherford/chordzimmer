// How incoming notes map to chord slots (ChordMonkey's KeyRouter.h, All keys only). Plain C++.
#pragma once

#include "../theory/ChordModel.h"

namespace chordzimmer
{
    // A note's name as Live writes it (C3 = MIDI 60).
    inline void noteName (int note, char (&out)[8]) noexcept
    {
        static constexpr const char* letters[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        note = note < 0 ? 0 : (note > 127 ? 127 : note);
        const char* l = letters[note % 12];
        const int octave = note / 12 - 2;
        int i = 0;
        while (*l != 0)
            out[i++] = *l++;
        if (octave < 0)
            out[i++] = '-';
        out[i++] = (char) ('0' + (octave < 0 ? -octave : octave));
        out[i] = 0;
    }

    namespace keys
    {
        // Letter of a white key (C = 0 … B = 6), or -1.
        constexpr int whiteLetter (int pitchClass) noexcept
        {
            constexpr int t[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
            return t[((pitchClass % 12) + 12) % 12];
        }

        // Index of a black key (C# = 0 … A# = 4), or -1.
        constexpr int blackIndex (int pitchClass) noexcept
        {
            constexpr int t[12] = { -1, 0, -1, 1, -1, -1, 2, -1, 3, -1, 4, -1 };
            return t[((pitchClass % 12) + 12) % 12];
        }

        constexpr int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
        constexpr int blackOffsets[5] = { 1, 3, 6, 8, 10 };
    }

    // The keyboard (ChordChimp's PLAN.md §3.1): every octave's white keys C D E F G A B play chord
    // slots 0-6 and its black keys C# D# F# G# A# slots 7-11, the borrowed chords (ChordGroups.h).
    // ChordMonkey's Bind zones and Keys-Lock are gone (PLAN.md §2); Match and Chord-from-notes
    // take over the input in Phase 5.
    struct KeyMap
    {
        // The octave the test pattern and the editor's chord keys play in: C at MIDI 48 (Live's
        // C2), the chord zone's octave in Chordism at its default split.
        static constexpr int homeC = 48;

        // The chord slot a note plays: 0..6 for a white key, 7..11 for a black one.
        static constexpr int slotOf (int note) noexcept
        {
            const int w = keys::whiteLetter (note % 12);
            return w >= 0 ? w : numWhiteSlots + keys::blackIndex (note % 12);
        }

        // The key in the home octave that plays `slot`, or -1.
        static constexpr int keyForSlot (int slot) noexcept
        {
            if (slot >= 0 && slot < numWhiteSlots)
                return homeC + keys::whiteOffsets[slot];
            if (slot >= numWhiteSlots && slot < numChordSlots)
                return homeC + keys::blackOffsets[slot - numWhiteSlots];
            return -1;
        }
    };
}
