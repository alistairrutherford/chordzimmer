#include "FactoryPresets.h"
#include "Parameters.h"

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

            return p;
        }
    }

    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> presets = make();
        return presets;
    }
}
