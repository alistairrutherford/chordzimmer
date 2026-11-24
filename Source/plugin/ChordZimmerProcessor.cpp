#include "ChordZimmerProcessor.h"
#include "../ui/ChordZimmerEditor.h"
#include "../core/model/ChordSets.h"
#include "../core/model/Detect.h"
#include <cmath>

namespace chordzimmer
{
    namespace
    {
        // Phase 8: a saved value that isn't a finite number (a damaged set or preset file) is
        // dropped rather than making the parameter NaN: a restore keeps the parameter's value,
        // a preset gives it its default.
        void dropNonFiniteValues (juce::ValueTree& tree)
        {
            for (auto child : tree)
                if (child.hasType ("PARAM") && child.hasProperty ("value") && ! std::isfinite ((double) child.getProperty ("value")))
                    child.removeProperty ("value", nullptr);
        }

        // Analyse and Detect aren't saved (ChordChimp's docs/KEY_DETECTION_PLAN.md K5.4): a set
        // saved mid-analysis or mid-capture opens playing chords.
        void dropAnalyse (juce::ValueTree& tree)
        {
            tree.removeChild (tree.getChildWithProperty ("id", params::analyse), nullptr);
            tree.removeChild (tree.getChildWithProperty ("id", params::detect), nullptr);
            tree.removeChild (tree.getChildWithProperty ("id", params::listen), nullptr);
        }

        const char* analyseName (params::AnalyseMode m)
        {
            return m == params::AnalyseMode::Listen ? "Listen" : (m == params::AnalyseMode::UseGuess ? "Use guess" : "Off");
        }
    }

    juce::AudioProcessor::BusesProperties ChordZimmerProcessor::makeBuses()
    {
        // An instrument with a silent stereo output: hosts route MIDI-only instruments more
        // reliably when there's an audio bus.
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    }

    ChordZimmerProcessor::ChordZimmerProcessor()
        : AudioProcessor (makeBuses()),
          state (*this, nullptr, "ChordZimmer", params::createLayout()),
          snapshot (state)
    {
        generated.ensureSize (output.capacity * 4);
        uiState.addListener (this);
        startTimerHz (20);
    }

    ChordZimmerProcessor::~ChordZimmerProcessor()
    {
        stopTimer();
        uiState.removeListener (this);
    }

    void ChordZimmerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        preparedSampleRate.store (sampleRate);
        generated.ensureSize (output.capacity * 4);
        engine.requestReset();
    }

    void ChordZimmerProcessor::releaseResources()
    {
        engine.requestReset();
    }

    // Phase 8: JUCE's VST3 wrapper calls this when the host stops processing (setProcessing
    // (false)). No MIDI can go out then, so every note ends on the first block after it resumes:
    // in Live, a device switched off leaves its chord sounding until it is on again.
    void ChordZimmerProcessor::reset()
    {
        engine.requestReset();
    }

    bool ChordZimmerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return layouts.getMainInputChannelSet().isDisabled()
            && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    Transport ChordZimmerProcessor::readTransport()
    {
        Transport t = lastTransport;
        t.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : preparedSampleRate.load();
        t.playing = false;
        t.looping = false;
        t.hasBarStart = false;

        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                t.playing = pos->getIsPlaying();
                if (auto b = pos->getBpm(); b.hasValue() && std::isfinite (*b) && *b > 0.0)
                    t.bpm = *b;
                if (auto p = pos->getPpqPosition(); p.hasValue() && std::isfinite (*p))
                    t.ppq = *p;
                if (auto ts = pos->getTimeSignature(); ts.hasValue() && ts->numerator > 0 && ts->denominator > 0)
                {
                    t.numerator = ts->numerator;
                    t.denominator = ts->denominator;
                }
                // Loop points and the bar position, for the effects' clock (Source/core/engine/Clock.h).
                if (auto loop = pos->getLoopPoints(); loop.hasValue() && std::isfinite (loop->ppqStart) && std::isfinite (loop->ppqEnd))
                {
                    t.looping = pos->getIsLooping() && loop->ppqEnd > loop->ppqStart;
                    t.loopStart = loop->ppqStart;
                    t.loopEnd = loop->ppqEnd;
                }
                if (auto bar = pos->getPpqPositionOfLastBarStart(); bar.hasValue() && std::isfinite (*bar))
                {
                    t.hasBarStart = true;
                    t.barStartPpq = *bar;
                }
            }
        }
        return t;
    }

    void ChordZimmerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, false);
    }

    void ChordZimmerProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, true);
    }

    void ChordZimmerProcessor::run (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        const int numSamples = buffer.getNumSamples();

        const Transport transport = readTransport();

        EngineSettings settings = snapshot.engineSettings();
        if (bypassed)
        {
            // Host bypass behaves like Enable off: generated notes end, input passes through.
            settings.enabled = false;
            settings.testPattern = false;
        }

        // Short messages go through the engine; anything longer (SysEx) passes straight on. Keys
        // played on the editor come first, at the block's first sample.
        input.clear();
        generated.clear();
        {
            const auto scope = uiFifo.read (uiFifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) input.push (uiNotes[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) input.push (uiNotes[(size_t) (scope.startIndex2 + i)]);
        }
        for (const auto meta : midi)
        {
            if (meta.numBytes >= 1 && meta.numBytes <= 3)
            {
                MidiEvent e;
                e.sample = meta.samplePosition;
                e.size = (uint8_t) meta.numBytes;
                for (int i = 0; i < meta.numBytes; ++i)
                    e.data[i] = meta.data[i];
                // A host flooding more than the buffer holds loses note-ons first: the last
                // slots are kept for key-ups, so no key is left down.
                if (input.space() > inputReserve || e.isNoteOff() || e.isAllNotesOff())
                    input.push (e);
            }
            else
            {
                generated.addEvent (meta.data, meta.numBytes, meta.samplePosition);
            }
        }

        if (snapshot.testWorkspace() && ! bypassed)
            addTestKeys (transport, numSamples);

        if (workspaceForAudio.hasNew())
            engine.setWorkspace (workspaceForAudio.read());
        if (timelineForAudio.hasNew())
            engine.setTimeline (timelineForAudio.read());
        {
            const auto scope = previewFifo.read (previewFifo.getNumReady());
            for (int i = 0; i < scope.blockSize1; ++i) engine.queuePreview (previewRequests[(size_t) (scope.startIndex1 + i)]);
            for (int i = 0; i < scope.blockSize2; ++i) engine.queuePreview (previewRequests[(size_t) (scope.startIndex2 + i)]);
        }

        output.clear();
        engine.process (settings, transport, input.begin(), input.size(), numSamples, output);
        if (transport.bpm > 0.0 && std::isfinite (transport.bpm))
            hostBpm.store (transport.bpm, std::memory_order_relaxed);
        hostNumerator.store (transport.numerator, std::memory_order_relaxed);
        hostDenominator.store (transport.denominator, std::memory_order_relaxed);
        playingChord.store (engine.getPlayingChord(), std::memory_order_relaxed);
        playhead.store (engine.getPlayhead(), std::memory_order_relaxed);
        recorder.record (transport, numSamples, output.begin(), output.size());

        for (const auto& e : output)
            generated.addEvent (e.data, e.size, e.sample);

        // Copy rather than swap: swapping would hand `generated` the host's (possibly small)
        // buffer, and the next block would allocate.
        midi.clear();
        midi.addEvents (generated, 0, -1, 0);

        lastTransport = transport;
        if (transport.playing)
            lastTransport.ppq = transport.ppq + numSamples / transport.samplesPerBeat();

        publishNoteStates();
        lastChordSlot.store (engine.getLastChordSlot(), std::memory_order_relaxed);
        keyGuess.store (engine.getKeyGuess().pack(), std::memory_order_relaxed);
        if (engine.takeCollectedChanged())
        {
            engine.getCollected (*collectedScratch);
            collected.publish (*collectedScratch);
        }
        if (engine.takeHeardChanged())
        {
            engine.getHeard (heardScratch);
            heard.publish (heardScratch);
        }
        soundingNotes.store (engine.getVoices().soundingNotes(), std::memory_order_relaxed);
        transportPlaying.store (transport.playing, std::memory_order_relaxed);

        ChordDisplay now { engine.getChords(), engine.getChordContext(), engine.getLastChordSlot() };
        if (! (now == lastDisplay))
        {
            lastDisplay = now;
            display.publish (now);
        }
    }

    void ChordZimmerProcessor::publishNoteStates() noexcept
    {
        std::array<uint64_t, 2> s {}, k {}, p {};
        for (int n = 0; n < 128; ++n)
        {
            const uint64_t bit = (uint64_t) 1 << (n & 63);
            if (engine.getVoices().isSounding (n)) s[(size_t) (n >> 6)] |= bit;
            if (engine.isKeyDown (n))              k[(size_t) (n >> 6)] |= bit;
            if (engine.isPerforming (n))           p[(size_t) (n >> 6)] |= bit;
        }
        for (size_t i = 0; i < 2; ++i)
        {
            sounding[i].store (s[i], std::memory_order_relaxed);
            keysDown[i].store (k[i], std::memory_order_relaxed);
            performing[i].store (p[i], std::memory_order_relaxed);
        }
    }

    void ChordZimmerProcessor::pushUiNote (int note, int velocity, bool on, bool palette) noexcept
    {
        if (note < 0 || note > 127)
            return;
        const auto scope = uiFifo.write (1);
        const MidiEvent e = (on ? MidiEvent::noteOn (0, note, juce::jlimit (1, 127, velocity), 0) : MidiEvent::noteOff (0, note, 0))
                                .withOrigin (palette ? MidiEvent::fromPalette : MidiEvent::fromKeys);
        if (scope.blockSize1 > 0) uiNotes[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) uiNotes[(size_t) scope.startIndex2] = e;
    }

    //==========================================================================
    void ChordZimmerProcessor::timerCallback()
    {
        // A program the host chose off the message thread.
        if (const int program = pendingProgram.exchange (-1); program >= 0)
            loadProgram (program);

        // The host (or the bridge) asked for the guess, or for Detect's result.
        if (snapshot.analyseMode() == params::AnalyseMode::UseGuess)
            applyGuess();
        if (snapshot.detectMode() == params::DetectMode::UseResult)
            applyDetect();

        // The test rig's workspace, from the bridge.
        if (snapshot.testWorkspace())
            readTestWorkspace();
        else
            testWorkspaceRead = testProgressionRead = {};

        // The test rig's takes and guesses, for checks in Live.
        if (snapshot.engineSettings().testPattern)
        {
            writeHeardKey();
            const auto info = recorder.info();
            if (info.take != 0 && ! info.recording && info.take != testTakeWritten && info.completedBars() > 0)
            {
                testTakeWritten = info.take;
                juce::String error;
                writeTake (testTakeFile(), error);
            }
        }
    }

    //==========================================================================
    // Key detection.

    void ChordZimmerProcessor::applyGuess()
    {
        const KeyGuess g = readKeyGuess();
        const auto s = snapshot.engineSettings();

        // The parameters that change, set together: every gesture begins, then every value,
        // then every gesture ends, so a host can make it one undo step.
        std::array<std::pair<juce::RangedAudioParameter*, float>, 3> changes {};
        size_t n = 0;
        const auto change = [&] (const char* id, float plain)
        {
            if (auto* p = state.getParameter (id))
                if (const float v = p->convertTo0to1 (plain); std::abs (p->getValue() - v) > 1.0e-6f)
                    changes[n++] = { p, v };
        };
        if (g.has())
        {
            change (params::key, (float) g.tonic);
            change (params::scale, (float) (int) scaleForGuess (s.scale, g.minor));
            change (params::analyse, (float) params::AnalyseMode::Off);
        }
        else
        {
            change (params::analyse, (float) params::AnalyseMode::Listen);   // nothing to use yet: keep listening
        }
        for (size_t i = 0; i < n; ++i) changes[i].first->beginChangeGesture();
        for (size_t i = 0; i < n; ++i) changes[i].first->setValueNotifyingHost (changes[i].second);
        for (size_t i = 0; i < n; ++i) changes[i].first->endChangeGesture();
    }

    //==========================================================================
    // Detect.

    const CollectedNotes& ChordZimmerProcessor::collectedNotes()
    {
        if (collected.hasNew())
            *collectedCache = collected.read();
        return *collectedCache;
    }

    void ChordZimmerProcessor::clearCollectedNotes()
    {
        engine.requestCollectedReset();
        *collectedCache = {};
    }

    const HeardChords& ChordZimmerProcessor::heardChords()
    {
        if (heard.hasNew())
            heardCache = heard.read();
        return heardCache;
    }

    bool ChordZimmerProcessor::applyDetect()
    {
        const HeardChords& h = heardChords();
        const KeyGuess g = readKeyGuess();
        const auto s = snapshot.engineSettings();

        std::array<std::pair<juce::RangedAudioParameter*, float>, 3> changes {};
        size_t n = 0;
        const auto change = [&] (const char* id, float plain)
        {
            if (auto* p = state.getParameter (id))
                if (const float v = p->convertTo0to1 (plain); std::abs (p->getValue() - v) > 1.0e-6f)
                    changes[n++] = { p, v };
        };
        const auto apply = [&]
        {
            for (size_t i = 0; i < n; ++i) changes[i].first->beginChangeGesture();
            for (size_t i = 0; i < n; ++i) changes[i].first->setValueNotifyingHost (changes[i].second);
            for (size_t i = 0; i < n; ++i) changes[i].first->endChangeGesture();
        };

        const int key = g.has() ? g.tonic : s.key;
        const Scale scale = g.has() ? scaleForGuess (s.scale, g.minor) : s.scale;
        const Pattern p = patternFromHeard (h, key, scale);
        if (p.empty())
        {
            change (params::detect, (float) params::DetectMode::Capture);   // nothing yet: keep capturing
            apply();
            return false;
        }

        // PLAN.md §2: the timeline replaces the workspace (Phase 4), so the chords go on its end.
        const ChordProgression heardSection = progressionFromPattern (p, "Detected");
        editProgression ("Detect", [&] (ChordProgression& q)
        {
            ProgressionClip clip;
            clip.parts = heardSection.sections;
            return q.insert (InsertPoint::newSection (q.numSections()), clip);
        });
        engine.requestHeardReset();      // the next capture starts empty
        heardCache = {};
        if (g.has())
        {
            change (params::key, (float) key);
            change (params::scale, (float) (int) scale);
        }
        change (params::detect, (float) params::DetectMode::Off);
        apply();
        return true;
    }

    void ChordZimmerProcessor::cancelDetect()
    {
        if (auto* p = state.getParameter (params::detect))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) params::DetectMode::Off));
            p->endChangeGesture();
        }
        engine.requestHeardReset();
        heardCache = {};
    }

    juce::File ChordZimmerProcessor::heardKeyFile()
    {
        return testTakeFile().getSiblingFile ("Heard key.json");
    }

    juce::File ChordZimmerProcessor::testWorkspaceFile()
    {
        return testTakeFile().getSiblingFile ("Workspace.txt");
    }

    juce::File ChordZimmerProcessor::testProgressionFile()
    {
        return testTakeFile().getSiblingFile ("Progression.txt");
    }

    juce::File ChordZimmerProcessor::testKeysFile()
    {
        return testTakeFile().getSiblingFile ("Keys.txt");
    }

    // Audio thread: the test rig's keys that start or end in this block, into `input`.
    void ChordZimmerProcessor::addTestKeys (const Transport& t, int numSamples) noexcept
    {
        const TestKeys& k = testKeysForAudio.read();
        if (! t.playing || k.count == 0 || numSamples <= 0)
            return;
        const double spb = t.samplesPerBeat();
        const double from = t.ppq, to = t.ppq + numSamples / spb;
        bool added = false;
        for (int i = 0; i < k.count; ++i)
        {
            const auto& key = k.keys[(size_t) i];
            for (const bool on : { true, false })
            {
                const double at = on ? key.from : key.to;
                if (at >= from && at < to && input.space() > inputReserve)
                {
                    const int sample = juce::jlimit (0, numSamples - 1, (int) std::llround ((at - from) * spb));
                    input.push (on ? MidiEvent::noteOn (0, key.note, 100, sample) : MidiEvent::noteOff (0, key.note, sample));
                    added = true;
                }
            }
        }
        if (added)
            input.sortBySample();
    }

    void ChordZimmerProcessor::readTestWorkspace()
    {
        const auto file = testWorkspaceFile();
        const auto modified = file.getLastModificationTime();
        if (const auto keysFile = testKeysFile(); keysFile.existsAsFile() && keysFile.getLastModificationTime() != testKeysRead)
        {
            testKeysRead = keysFile.getLastModificationTime();
            TestKeys k;
            juce::StringArray lines;
            keysFile.readLines (lines);
            for (const auto& line : lines)
            {
                const auto t = juce::StringArray::fromTokens (line, " \t", "");
                if (line.trimStart().startsWithChar ('#') || t.size() < 3 || k.count == (int) k.keys.size())
                    continue;
                const double from = t[0].getDoubleValue(), length = t[2].getDoubleValue();
                const int note = t[1].getIntValue();
                if (note >= 0 && note <= 127 && length > 0.0 && std::isfinite (from) && std::isfinite (length))
                    k.keys[(size_t) k.count++] = { from, from + length, (uint8_t) note };
            }
            testKeysForAudio.publish (k);
        }

        const auto progressionFile = testProgressionFile();
        if (const auto changed = progressionFile.getLastModificationTime();
            progressionFile.existsAsFile() && changed != testProgressionRead)
        {
            testProgressionRead = changed;
            ChordProgression p;
            juce::StringArray lines;
            progressionFile.readLines (lines);
            for (const auto& line : lines)
            {
                if (line.trimStart().startsWithChar ('#') || line.trim().isEmpty())
                    continue;
                const bool named = line.containsChar ('|');
                const auto name = named ? line.upToFirstOccurrenceOf ("|", false, false).trim() : juce::String();
                const auto numerals = named ? line.fromFirstOccurrenceOf ("|", false, false) : line;
                std::string error;
                Pattern pattern;
                if (parseProgression (numerals.toStdString(), snapshot.key(), pattern, error))
                {
                    auto section = progressionFromPattern (pattern, name.isEmpty() ? p.defaultSectionName() : name.toStdString());
                    if (! section.empty() && p.numSections() < maxProgressionSections)
                        p.sections.push_back (std::move (section.sections[0]));
                }
            }
            setProgression (p);
        }

        if (! file.existsAsFile() || modified == testWorkspaceRead)
            return;
        testWorkspaceRead = modified;

        Workspace w;
        int n = 0;
        juce::StringArray lines;
        file.readLines (lines);
        for (const auto& line : lines)
        {
            if (line.trimStart().startsWithChar ('#'))
                continue;
            if (n == numPatterns)
                break;
            std::string error;
            Pattern p;
            if (parseProgression (line.toStdString(), snapshot.key(), p, error))
                w.patterns[(size_t) n] = p;
            ++n;
        }
        setWorkspace (w);
    }

    void ChordZimmerProcessor::writeHeardKey()
    {
        const KeyGuess g = readKeyGuess();
        const auto mode = snapshot.analyseMode();
        if (g.pack() == heardKeyWritten && (int) mode == heardModeWritten)
            return;
        heardKeyWritten = g.pack();
        heardModeWritten = (int) mode;

        const auto keyObject = [] (int tonic, bool minor, int fit)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("key", guessKeyName (tonic, minor));
            o->setProperty ("mode", minor ? "minor" : "major");
            o->setProperty ("fit", fit / 100.0);
            return o;
        };
        auto* root = g.has() ? keyObject (g.tonic, g.minor, g.fit) : new juce::DynamicObject();
        juce::var json (root);
        root->setProperty ("analyse", analyseName (mode));
        root->setProperty ("state", g.state == GuessState::Confident ? "confident" : (g.state == GuessState::Tentative ? "tentative" : "none"));
        if (g.has() && g.runnerUpTonic >= 0)
            root->setProperty ("runnerUp", juce::var (keyObject (g.runnerUpTonic, g.runnerUpMinor, g.runnerUpFit)));
        root->setProperty ("notes", g.notes);

        const auto file = heardKeyFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::JSON::toString (json));
    }

    void ChordZimmerProcessor::notifyStateChanged()
    {
        if (! restoringState)
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }

    void ChordZimmerProcessor::pushPreview (const PreviewRequest& r) noexcept
    {
        const auto scope = previewFifo.write (1);
        if (scope.blockSize1 > 0) previewRequests[(size_t) scope.startIndex1] = r;
        else if (scope.blockSize2 > 0) previewRequests[(size_t) scope.startIndex2] = r;
    }

    void ChordZimmerProcessor::previewChord (const ProgressionChord& chord, double beats) noexcept
    {
        PreviewRequest r;
        r.notes = progressionChordNotes (chord);
        r.root = (int8_t) chord.root();
        r.on = r.notes.size > 0;
        r.beats = beats > 0.0 && std::isfinite (beats) ? (float) beats : 0.0f;
        pushPreview (r);
    }

    void ChordZimmerProcessor::endPreview() noexcept
    {
        pushPreview (PreviewRequest {});
    }

    void ChordZimmerProcessor::pushSlotPreview (int slot, bool on) noexcept
    {
        if (slot < 0 || slot >= numPatternSlots)
            return;
        const int note = MidiEvent::workspaceKey + slot;
        const auto scope = uiFifo.write (1);
        const MidiEvent e = (on ? MidiEvent::noteOn (0, note, 100, 0) : MidiEvent::noteOff (0, note, 0)).withOrigin (MidiEvent::fromWorkspace);
        if (scope.blockSize1 > 0) uiNotes[(size_t) scope.startIndex1] = e;
        else if (scope.blockSize2 > 0) uiNotes[(size_t) scope.startIndex2] = e;
    }

    //==========================================================================
    // Programs and presets.

    int ChordZimmerProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

    const juce::String ChordZimmerProcessor::getProgramName (int index)
    {
        const auto& all = factoryPresets();
        return index >= 0 && index < (int) all.size() ? all[(size_t) index].name : juce::String();
    }

    void ChordZimmerProcessor::setCurrentProgram (int index)
    {
        if (index < 0 || index >= getNumPrograms())
            return;
        currentProgram = index;
        if (juce::MessageManager::existsAndIsCurrentThread())
            loadProgram (index);
        else
            pendingProgram = index;       // the timer loads it
    }

    void ChordZimmerProcessor::loadProgram (int index)
    {
        const auto& p = factoryPresets()[(size_t) juce::jlimit (0, getNumPrograms() - 1, index)];
        applyPreset (PresetManager::factoryTree (index), p.category, p.name);
        currentProgram = index;
    }

    void ChordZimmerProcessor::applyPreset (const juce::ValueTree& preset, const juce::String& category, const juce::String& name)
    {
        auto tree = preset.createCopy();
        dropNonFiniteValues (tree);
        for (auto* p : getParameters())
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (param == nullptr || ! params::isPresetParameter (param->paramID))
                continue;
            const auto child = tree.getChildWithProperty ("id", param->paramID);
            const float value = child.hasProperty ("value") ? param->convertTo0to1 ((float) (double) child.getProperty ("value"))
                                                            : param->getDefaultValue();
            if (std::abs (param->getValue() - value) > 1.0e-6f)
                param->setValueNotifyingHost (value);
        }
        if (const auto ws = tree.getChildWithName ("WORKSPACE"); ws.isValid())
            setWorkspace (workspaceFromTree (ws));
        if (const auto pg = tree.getChildWithName ("PROGRESSION"); pg.isValid())
            setProgression (progressionFromTree (pg));
        engine.requestReset();            // all notes off on a preset change (Chordism §5)
        setPresetIdentity (category, name);
    }

    juce::ValueTree ChordZimmerProcessor::presetState()
    {
        juce::ValueTree tree ("ChordZimmer");
        for (const auto& child : state.copyState())
            if (child.hasType ("PARAM") && params::isPresetParameter (child.getProperty ("id").toString()))
                tree.appendChild (child.createCopy(), nullptr);
        tree.appendChild (workspaceToTree (getWorkspace()), nullptr);
        tree.appendChild (progressionToTree (getProgression()), nullptr);
        tree.setProperty ("version", JucePlugin_VersionString, nullptr);
        return tree;
    }

    void ChordZimmerProcessor::setPresetIdentity (const juce::String& category, const juce::String& name)
    {
        presetCategory = category;
        presetName = name;
        notifyStateChanged();
    }

    //==========================================================================
    // Export.

    juce::File ChordZimmerProcessor::testTakeFile()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("ChordZimmer").getChildFile ("Exports").getChildFile ("Test take.mid");
    }

    bool ChordZimmerProcessor::writeTake (const juce::File& file, juce::String& error) const
    {
        TakeInfo info;
        std::vector<RecordedEvent> events;
        bool copied = false;
        for (int attempt = 0; attempt < 4 && ! copied; ++attempt)
            copied = recorder.snapshot (info, events);
        if (! copied || info.completedBars() <= 0)
        {
            error = "Nothing to export yet: play at least one whole bar.";
            return false;
        }
        const auto song = exportBars (info, events);
        const auto bytes = writeMidiFile (song);
        file.getParentDirectory().createDirectory();
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            error = "Couldn't write " + file.getFullPathName();
            return false;
        }
        return true;
    }

    juce::File ChordZimmerProcessor::writeExport (juce::String& error)
    {
        const auto info = recorder.info();
        const int bars = juce::jmin (info.completedBars(), ExportRecorder::maxBars);
        const auto s = snapshot.engineSettings();
        const juce::String name = "ChordZimmer " + juce::String (keyName (s.key)).upToFirstOccurrenceOf ("/", false, false) + " "
                                + scaleInfo (s.scale).name + " " + juce::String (bars) + (bars == 1 ? " bar" : " bars");
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChordZimmer Export")
                              .getChildFile (juce::File::createLegalFileName (name) + ".mid");
        return writeTake (file, error) ? file : juce::File();
    }

    juce::File ChordZimmerProcessor::writePatternExport (int pattern, bool chordsOnly, juce::String& error)
    {
        const auto w = getWorkspace();
        if (pattern < 0 || pattern >= numPatterns || w.patterns[(size_t) pattern].empty())
        {
            error = "Nothing to drag: the pattern is empty.";
            return {};
        }
        const auto s = snapshot.engineSettings();
        auto song = renderPattern (w, pattern, s, hostBpm.load(), hostNumerator.load(), hostDenominator.load(), chordsOnly);
        const int bars = (int) std::ceil (w.patterns[(size_t) pattern].totalHalfBeats() * 0.5 / 4.0 - 1.0e-9);
        const juce::String name = "ChordZimmer " + juce::String (patternName (pattern)) + " " + juce::String (keyName (s.key)).upToFirstOccurrenceOf ("/", false, false)
                                + (chordsOnly ? " chords" : "") + " " + juce::String (bars) + (bars == 1 ? " bar" : " bars");
        song.name = name.toStdString();      // the clip's name in the host
        const auto bytes = writeMidiFile (song);
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ChordZimmer Export")
                              .getChildFile (juce::File::createLegalFileName (name) + ".mid");
        file.getParentDirectory().createDirectory();
        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            error = "Couldn't write " + file.getFullPathName();
            return {};
        }
        return file;
    }

    juce::AudioProcessorEditor* ChordZimmerProcessor::createEditor()
    {
        return new ChordZimmerEditor (*this);
    }

    //==========================================================================
    // The workspace (ChordMonkey's PLAN.md §3.2), saved as one PATTERN child per pattern with its text
    // (patternToText):
    //
    //   <WORKSPACE version="1"><PATTERN index="0" text="0|0:0.4.7:8:0:0 …"/> … </WORKSPACE>

    Workspace ChordZimmerProcessor::getWorkspace() const
    {
        const juce::ScopedLock sl (workspaceLock);
        return workspace;
    }

    void ChordZimmerProcessor::setWorkspace (const Workspace& w)
    {
        {
            const juce::ScopedLock sl (workspaceLock);
            if (workspace == w)
                return;
            workspace = w;
            workspaceForAudio.publish (workspace);
        }
        notifyStateChanged();
    }

    juce::ValueTree ChordZimmerProcessor::workspaceToTree (const Workspace& w)
    {
        juce::ValueTree tree ("WORKSPACE");
        tree.setProperty ("version", 1, nullptr);
        for (int i = 0; i < numPatterns; ++i)
        {
            juce::ValueTree p ("PATTERN");
            p.setProperty ("index", i, nullptr);
            p.setProperty ("text", juce::String (patternToText (w.patterns[(size_t) i])), nullptr);
            tree.appendChild (p, nullptr);
        }
        return tree;
    }

    // Patterns missing or unreadable come back empty.
    Workspace ChordZimmerProcessor::workspaceFromTree (const juce::ValueTree& tree)
    {
        Workspace w;
        for (const auto& p : tree)
        {
            const int i = (int) p.getProperty ("index", -1);
            if (p.hasType ("PATTERN") && i >= 0 && i < numPatterns)
                patternFromText (p.getProperty ("text").toString().toStdString(), w.patterns[(size_t) i]);
        }
        return w;
    }

    //==========================================================================
    // The progression (PLAN.md §3.3), saved as its text (Progression.h):
    //
    //   <PROGRESSION text="czp1 S:0:Verse C:0:0.4.7:384:0:0:-1 …"/>
    //
    // The text carries its own version, so the element has none.

    ChordProgression ChordZimmerProcessor::getProgression() const
    {
        const juce::ScopedLock sl (progressionLock);
        return progression;
    }

    void ChordZimmerProcessor::setProgression (const ChordProgression& p)
    {
        {
            const juce::ScopedLock sl (progressionLock);
            history.reset (p);
            if (progression == p)
                return;
            progression = p;
            loopSelection = progression.normalised (loopSelection);
            publishTimeline();
        }
        ++progressionChanges;
        notifyStateChanged();
    }

    // The editor's edits (Phase 4): through the UndoList, then as setProgression without its
    // reset.
    bool ChordZimmerProcessor::editProgression (const std::string& label, const std::function<bool (ChordProgression&)>& edit)
    {
        {
            const juce::ScopedLock sl (progressionLock);
            if (! history.perform (label, [&edit] (ChordProgression& q) { return edit (q); }))
                return false;
            progression = history.current();
            loopSelection = progression.normalised (loopSelection);
            publishTimeline();
        }
        ++progressionChanges;
        notifyStateChanged();
        return true;
    }

    bool ChordZimmerProcessor::undoProgression()
    {
        {
            const juce::ScopedLock sl (progressionLock);
            if (! history.undo())
                return false;
            progression = history.current();
            loopSelection = progression.normalised (loopSelection);
            publishTimeline();
        }
        ++progressionChanges;
        notifyStateChanged();
        return true;
    }

    bool ChordZimmerProcessor::redoProgression()
    {
        {
            const juce::ScopedLock sl (progressionLock);
            if (! history.redo())
                return false;
            progression = history.current();
            loopSelection = progression.normalised (loopSelection);
            publishTimeline();
        }
        ++progressionChanges;
        notifyStateChanged();
        return true;
    }

    ChordZimmerProcessor::UndoState ChordZimmerProcessor::undoState() const
    {
        const juce::ScopedLock sl (progressionLock);
        return { history.canUndo(), history.canRedo(), history.undoLabel(), history.redoLabel() };
    }

    Selection ChordZimmerProcessor::getLoopSelection() const
    {
        const juce::ScopedLock sl (progressionLock);
        return loopSelection;
    }

    void ChordZimmerProcessor::setLoopSelection (const Selection& sel)
    {
        const juce::ScopedLock sl (progressionLock);
        loopSelection = progression.normalised (sel);
        publishTimeline();
    }

    void ChordZimmerProcessor::publishTimeline()
    {
        // The Timeline is ~12 KB: built on the heap, not the message thread's stack.
        const auto t = std::make_unique<Timeline> (makeTimeline (progression, loopSelection));
        timelineForAudio.publish (*t);
    }

    juce::ValueTree ChordZimmerProcessor::progressionToTree (const ChordProgression& p)
    {
        juce::ValueTree tree ("PROGRESSION");
        tree.setProperty ("text", juce::String (progressionToText (p)), nullptr);
        return tree;
    }

    ChordProgression ChordZimmerProcessor::progressionFromTree (const juce::ValueTree& tree)
    {
        ChordProgression p;
        if (tree.hasType ("PROGRESSION"))
            progressionFromText (tree.getProperty ("text").toString().toStdString(), p);
        return p;
    }

    void ChordZimmerProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto tree = state.copyState();
        dropAnalyse (tree);
        tree.appendChild (uiState.createCopy(), nullptr);
        tree.appendChild (workspaceToTree (getWorkspace()), nullptr);
        tree.appendChild (progressionToTree (getProgression()), nullptr);
        tree.setProperty ("program", currentProgram.load(), nullptr);
        tree.setProperty ("presetName", presetName, nullptr);
        tree.setProperty ("presetCategory", presetCategory, nullptr);
        tree.setProperty ("version", JucePlugin_VersionString, nullptr);
        if (auto xml = tree.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void ChordZimmerProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;

        auto tree = juce::ValueTree::fromXml (*xml);
        currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
        presetName = tree.getProperty ("presetName", presetName).toString();
        presetCategory = tree.getProperty ("presetCategory", presetCategory).toString();
        const auto ui = tree.getChildWithName (uiState.getType());
        restoringState = true;
        if (ui.isValid())
        {
            uiState.copyPropertiesAndChildrenFrom (ui, nullptr);
            tree.removeChild (ui, nullptr);
        }
        restoringState = false;

        // A state saved before Phase 1 has no workspace: it restores empty.
        const auto ws = tree.getChildWithName ("WORKSPACE");
        {
            const juce::ScopedLock sl (workspaceLock);
            workspace = workspaceFromTree (ws);
            workspaceForAudio.publish (workspace);
        }
        tree.removeChild (ws, nullptr);

        // A state saved before Phase 2 has no progression: it restores empty.
        const auto pg = tree.getChildWithName ("PROGRESSION");
        {
            const juce::ScopedLock sl (progressionLock);
            progression = progressionFromTree (pg);
            history.reset (progression);
            loopSelection = {};
            publishTimeline();
        }
        ++progressionChanges;
        tree.removeChild (pg, nullptr);
        dropNonFiniteValues (tree);
        dropAnalyse (tree);

        pendingProgram = -1;              // a restore replaces a program change still waiting
        state.replaceState (tree);

        // APVTS leaves a parameter alone when its value rounds to the saved one (a bool a host
        // set to 0.3 is already "off"), so the host's raw value would survive the restore:
        // set each saved one exactly. (APVTS adds a child without a value for a parameter the
        // state doesn't have; that one keeps its value.)
        for (auto* p : getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto child = tree.getChildWithProperty ("id", param->paramID); child.hasProperty ("value"))
                    if (const float v = param->convertTo0to1 ((float) (double) child.getProperty ("value")); std::abs (param->getValue() - v) > 1.0e-6f)
                        param->setValueNotifyingHost (v);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chordzimmer::ChordZimmerProcessor();
}
