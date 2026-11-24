// The plug-in's defines for the snapshot app, which compiles Source/plugin and Source/ui outside
// a plug-in target (forced include, ChordZimmerSnapshot.jucer). Keep in step with ChordZimmer.jucer.
#pragma once
#define JucePlugin_Name "ChordZimmer"
#define JucePlugin_VersionString "0.0.1"

// The test runs the message loop between blocks (runDispatchLoopUntil).
#define JUCE_MODAL_LOOPS_PERMITTED 1
