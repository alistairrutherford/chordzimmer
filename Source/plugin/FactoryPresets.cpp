#include "FactoryPresets.h"
#include "Parameters.h"
#include "../core/model/ChordSets.h"
#include "../core/model/Progression.h"

namespace chordzimmer
{
    namespace
    {
        using V = std::vector<std::pair<juce::String, float>>;

        float f (Scale s) { return (float) (int) s; }
        float f (ChordGroup g) { return (float) (int) g; }
        float f (Progression p) { return (float) progressionChoice (p); }   // the parameter's choice
        float f (StackMode m) { return (float) (int) m; }
        float f (StrumMode m) { return (float) (int) m; }
        float f (VelocityCurve c) { return (float) (int) c; }
        float f (HoldMode m) { return (float) (int) m; }
        // Strum rate indices (NoteValues.h).
        constexpr float strum1_64 = 2.0f, strum1_32 = 4.0f, strum1_16T = 5.0f;

        std::vector<FactoryPreset> make()
        {
            using namespace params;
            std::vector<FactoryPreset> p;
            p.push_back ({ "Init", "Init", {} });

            // Genre.
            p.push_back ({ "Genre", "Pop Anthem", {
                { key, 0 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Pop) },
                { stack, f (StackMode::PlusOne) }, { strum, f (StrumMode::Up) }, { strumRate, strum1_64 } } });
            p.push_back ({ "Genre", "Neo-Soul Keys", V {
                { key, 3 }, { scale, f (Scale::Dorian) }, { chordGroup, f (ChordGroup::NeoSoul) }, { progression, f (Progression::TwoFiveOne) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_32 }, { velocity, f (VelocityCurve::Breakbeat) }, { velocityPeak, 110 },
                { velocityRange, 30 } } });
            p.push_back ({ "Genre", "Lo-Fi Sevenths", V {
                { key, 5 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Fifties) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_32 }, { hold, f (HoldMode::Bar) } } });
            p.push_back ({ "Genre", "Jazz Two-Five", {
                { key, 10 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Ninths) }, { progression, f (Progression::TwoFiveOne) },
                { strum, f (StrumMode::Down) }, { strumRate, strum1_64 } } });
            p.push_back ({ "Genre", "Gospel Thirteenths", {
                { key, 8 }, { scale, f (Scale::Major) }, { chordGroup, f (ChordGroup::Thirteenths) }, { progression, f (Progression::CircleOfFifths) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_64 }, { velocity, f (VelocityCurve::Up) }, { velocityRange, 25 } } });

            // Artist-style: by style, never by name.
            p.push_back ({ "Artist-style", "Cinematic Swells", {
                { key, 2 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::OpenTriads) }, { progression, f (Progression::Descending) },
                { stack, f (StackMode::PlusOne) }, { hold, f (HoldMode::Latch) }, { velocity, f (VelocityCurve::UpDown) }, { velocityRange, 50 } } });
            p.push_back ({ "Artist-style", "Ambient Pads", V {
                { key, 4 }, { scale, f (Scale::Lydian) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Thirds) },
                { stack, f (StackMode::PlusOneAndTwo) }, { hold, f (HoldMode::Latch) } } });

            // Vibe.
            p.push_back ({ "Vibe", "Dreamy", V {
                { key, 5 }, { scale, f (Scale::Lydian) }, { chordGroup, f (ChordGroup::Add9) }, { progression, f (Progression::Thirds) },
                { strum, f (StrumMode::Up) }, { strumRate, strum1_16T } } });
            p.push_back ({ "Vibe", "Dark", {
                { key, 0 }, { scale, f (Scale::PhrygianDominant) }, { chordGroup, f (ChordGroup::Triads) }, { progression, f (Progression::Descending) },
                { chordOctave, -1 }, { stack, f (StackMode::PlusOne) }, { velocity, f (VelocityCurve::Down) }, { velocityRange, 30 } } });
            p.push_back ({ "Vibe", "Melancholy", V {
                { key, 11 }, { scale, f (Scale::NaturalMinor) }, { chordGroup, f (ChordGroup::Sevenths) }, { progression, f (Progression::Canon) },
                { strum, f (StrumMode::Down) }, { strumRate, strum1_32 } } });

            // Templates (Phase 8): progressions to start from, with their key and scale. Common
            // progressions everyone uses, written out by hand; appended after 0.0.1's programs.
            p.push_back ({ "Templates", "Four Chords", V { { key, 0 }, { scale, f (Scale::Major) } },
                           { { "Verse", "I:4 V:4 vi:4 IV:4" }, { "Chorus", "IV:4 I:4 V:4 vi:4" } } });
            p.push_back ({ "Templates", "Minor Axis", V { { key, 9 }, { scale, f (Scale::NaturalMinor) } },
                           { { "Verse", "i:4 bVI:4 bIII:4 bVII:4" }, { "Chorus", "bVI:4 bVII:4 i:8" } } });
            p.push_back ({ "Templates", "Two-Five-One", V { { key, 10 }, { scale, f (Scale::Major) }, { voiceLeading, 1 } },
                           { { "A", "ii7:4 V7:4 Imaj7:8" }, { "Turnaround", "iii7:2 VI7:2 ii7:2 V7:2" } } });
            p.push_back ({ "Templates", "Twelve-Bar Blues", V { { key, 9 }, { scale, f (Scale::Mixolydian) } },
                           { { "Blues", "I7:16 IV7:8 I7:8 V7:4 IV7:4 I7:4 V7:4" } } });
            p.push_back ({ "Templates", "Andalusian Cadence", V { { key, 2 }, { scale, f (Scale::HarmonicMinor) } },
                           { { "Cadence", "i:4 bVII:4 bVI:4 V:4" } } });
            p.push_back ({ "Templates", "Canon Ground", V { { key, 2 }, { scale, f (Scale::Major) } },
                           { { "Canon", "I:2 V:2 vi:2 iii:2 IV:2 I:2 IV:2 V:2" } } });
            p.push_back ({ "Templates", "Fifties", V { { key, 7 }, { scale, f (Scale::Major) } },
                           { { "Verse", "I:4 vi:4 IV:4 V:4" }, { "Bridge", "IV:4 iv:4 I:8" } } });
            p.push_back ({ "Templates", "Dorian Vamp", V { { key, 2 }, { scale, f (Scale::Dorian) } },
                           { { "Vamp", "i7:8 IV7:8" } } });

            return p;
        }
    }

    juce::String templateProgressionText (const FactoryPreset& preset, int key)
    {
        ChordProgression prog;
        for (const auto& [name, numerals] : preset.sections)
        {
            ProgressionSection section;
            section.name = name;
            section.colour = (uint8_t) (prog.numSections() % numSectionColours);
            for (const auto& token : juce::StringArray::fromTokens (numerals, " ", ""))
            {
                // "numeral:beats": the numeral through ChordSets.h, the length here (a pattern
                // slot holds at most 8 beats; a timeline chord up to 16 bars).
                WorkspaceChord chord;
                int half = 0;
                std::string error;
                const auto numeral = token.upToFirstOccurrenceOf (":", false, false);
                const double beats = token.containsChar (':') ? token.fromFirstOccurrenceOf (":", false, false).getDoubleValue() : 4.0;
                if (! parseNumeral (numeral.toStdString(), key, chord, half, error) || beats <= 0.0)
                    return {};
                section.chords.push_back (makeProgressionChord (chord, (int) std::lround (beats * ticksPerBeat)));
            }
            if (section.chords.empty())
                return {};
            prog.sections.push_back (std::move (section));
        }
        return prog.empty() ? juce::String() : juce::String (progressionToText (prog));
    }

    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> presets = make();
        return presets;
    }
}
