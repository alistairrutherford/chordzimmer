// ChordZimmer's factory presets: the ChordMonkey (Chordism) presets that don't rely on what
// ChordZimmer dropped (Arp, Repeat, Step seq, Styles, chord sets: PLAN.md §2), in three
// categories: Genre, Artist-style (described by style only, never by an artist's name) and Vibe.
// Each is a list of host parameter values on top of the defaults (plain values: a choice's
// index, a bool's 0/1, a percentage 0-100), so the list is checked by the compiler against the
// engine's enums. Program 0 of the plug-in is "Init" (every default), so a new instance is its
// program 0. The programs keep their numbers from 0.0.1 on: new presets only ever go at the end.
// A factory preset leaves the workspace alone.
//
// Templates (PLAN.md §2: "a few factory template progressions on the presets menu", Phase 8):
// presets that also carry a progression, its sections written as numerals in the preset's Key
// ("I:4 V:4 vi:4 IV:4", ChordSets.h's numerals, ":beats" for a length; flats for a minor key's
// borrowed degrees: "i bVI bIII bVII"). Loading one replaces the timeline (one load: the undo
// history starts again); the other presets leave the timeline alone.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <utility>
#include <vector>

namespace chordzimmer
{
    struct FactoryPreset
    {
        juce::String category, name;
        std::vector<std::pair<juce::String, float>> values;   // parameter ID → plain value
        std::vector<std::pair<const char*, const char*>> sections {};   // a template's (name, numerals), in order
    };

    // A template's progression (its sections' numerals in `key`); empty if it has none or a
    // numeral doesn't read.
    juce::String templateProgressionText (const FactoryPreset&, int key);

    // "Init" first, then the factory presets in browser order.
    const std::vector<FactoryPreset>& factoryPresets();
}
