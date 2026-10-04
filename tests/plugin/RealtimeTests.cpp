// Zero allocations in processBlock across 30 s of sequenced playback with host automation,
// preset loads, pattern edits and MIDI learn traffic from other threads, plus zero-sample,
// oversized and no-playhead blocks. The spy counts every malloc and free made on this thread
// while the audio context is set (RealtimeMain.cpp).
#include "AllocSpy.h"
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>

using namespace ptest;

TEST_CASE ("realtime: the spy sees allocations when it should", "[plugin][realtime]")
{
    allocspy::violations.store (0);
    {
        allocspy::ScopedAudioContext audio;
        std::vector<int> v (1000);
        v.push_back (1);
    }
    CHECK (allocspy::violations.load() >= 2);
    allocspy::violations.store (0);
    {
        std::vector<int> v (1000);
        v.push_back (1);
    }
    CHECK (allocspy::violations.load() == 0);
}

TEST_CASE ("realtime: processBlock allocates nothing over 30 s with everything else going on", "[plugin][realtime]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A"));
    c.commitPattern (false, 1, d.patternFromText ("G:A G . G+:S G . . G:V A# . . C+:AS . . ."));
    c.commitPattern (true, 0, d.patternFromText ("1 2 3 2+ 1 3- 2 1+"));
    c.setParam (kDistOn, 1.0f);
    c.setParam (kDelayOn, 1.0f);
    c.setParam (kRevOn, 1.0f);
    c.setParam (kChoOn, 1.0f);
    c.setParam (kEqOn, 1.0f);
    c.setParam (kLimOn, 1.0f);
    c.setMidiMapping (74, kCutoff);
    c.setMidiMapping (71, kResonance);
    Settings s = c.settings();
    s.launchQuant = seq::LaunchQuant::Immediate;
    c.setSettings (s);
    std::string error;
    const PresetEntry* sceneA = c.savePreset (PresetType::Scene, "RT A", "", "", "", true, &error);
    c.setParam (kCutoff, 80.0f);
    c.setParam (kDistModel, 9.0f);
    d.processor.handlePendingMessages();
    const PresetEntry* sceneB = c.savePreset (PresetType::Scene, "RT B", "", "", "", true, &error);
    const PresetEntry* synth = c.savePreset (PresetType::Synth, "RT S", "", "", "", true, &error);
    REQUIRE (sceneA != nullptr);
    REQUIRE (sceneB != nullptr);
    REQUIRE (synth != nullptr);

    const int block = 512;
    d.prepare (kFs, block);
    d.playHead.playing = true;
    d.playHead.bpm = 138.0;
    d.playHead.looping = true;
    d.playHead.loopStart = 0.0;
    d.playHead.loopEnd = 16.0;

    std::atomic<bool> stop { false };
    std::atomic<int> otherWork { 0 };

    // Preset loads, pattern edits, settings, undo, MIDI learn from the message thread.
    std::thread messageThread ([&] {
        Rng rng (11);
        const PresetEntry* scenes[] = { sceneA, sceneB, synth };
        while (! stop.load())
        {
            switch (rng.nextInt (6))
            {
                case 0: c.loadPreset (*scenes[rng.nextInt (3)]); break;
                case 1:
                {
                    seq::Pattern p = c.pattern (false, rng.nextInt (2));
                    p.steps[static_cast<std::size_t> (rng.nextInt (16))].note = static_cast<std::uint8_t> (rng.nextInt (12));
                    p.steps[static_cast<std::size_t> (rng.nextInt (16))].state = rng.chance (0.8) ? seq::StepState::Note : seq::StepState::Rest;
                    c.commitPattern (false, rng.nextInt (2), p);
                    break;
                }
                case 2:
                {
                    Settings t = c.settings();
                    t.fxOrder = rng.chance (0.5) ? std::array<std::uint8_t, 6> { 3, 1, 0, 2, 5, 4 } : std::array<std::uint8_t, 6> { 0, 1, 2, 3, 4, 5 };
                    t.slideIntoRest = static_cast<seq::SlideIntoRest> (rng.nextInt (3));
                    c.setSettings (t);
                    break;
                }
                case 3: c.undo(); break;
                case 4: c.startMidiLearn (static_cast<ParamId> (rng.nextInt (kNumParams))); break;
                case 5: c.generate (false, 1, rng.nextU64()); break;
            }
            d.processor.handlePendingMessages();
            otherWork.fetch_add (1);
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });

    // Host automation.
    std::thread automationThread ([&] {
        Rng rng (12);
        const ParamId ids[] = { kCutoff, kResonance, kEnvMod, kDecay, kAccent, kDistDrive, kDistModel, kDistColor, kDelayFeedback,
                                kRevMix, kChoDepth, kMasterVolume, kPlayMode, kPatternSlot, kPatternBank, kLiveTranspose, kOvrAccentSet, kWave };
        while (! stop.load())
        {
            auto& param = c.parameter (ids[rng.nextInt (18)]);
            param.setValueNotifyingHost (static_cast<float> (rng.nextDouble()));
            std::this_thread::sleep_for (std::chrono::microseconds (50));
        }
    });

    // The audio thread: 30 s at 48 kHz, mostly 512-sample blocks, with zero-sample blocks,
    // oversized blocks and stretches without a playhead mixed in.
    const std::int64_t total = static_cast<std::int64_t> (kFs * 30.0);
    juce::AudioBuffer<float> buffer (2, 8192);
    juce::MidiBuffer midi;
    midi.ensureSize (1 << 16);
    Rng rng (13);
    std::int64_t pos = 0;
    std::size_t blocks = 0;
    double peak = 0.0;
    allocspy::violations.store (0);

    while (pos < total)
    {
        int n = block;
        const int roll = rng.nextInt (100);
        if (roll == 0) n = 0;
        else if (roll < 3) n = 4096 + rng.nextInt (4097);
        else if (roll < 10) n = 1 + rng.nextInt (block);
        n = static_cast<int> (std::min<std::int64_t> (n, total - pos));

        midi.clear();
        if (blocks % 25 == 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, rng.nextInt (128)), 0);
        if (blocks % 25 == 12) midi.addEvent (juce::MidiMessage::controllerEvent (1, 71, rng.nextInt (128)), std::max (0, n - 1));
        if (blocks % 40 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36 + rng.nextInt (36), static_cast<juce::uint8> (90)), 0);
        if (blocks % 40 == 20) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        if (blocks % 97 == 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, 30 + rng.nextInt (20), 100), 0);   // feeds MIDI learn
        if (blocks % 300 == 0) midi.addEvent (juce::MidiMessage::pitchWheel (1, rng.nextInt (16384)), 0);

        d.playHead.present = (blocks / 500) % 3 != 2;     // every third stretch has no playhead at all

        juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, n);
        audio.clear();
        {
            allocspy::ScopedAudioContext context;
            d.processor.processBlock (audio, midi);
        }
        for (int i = 0; i < n; i += 13)
        {
            const float v = audio.getSample (0, i);
            REQUIRE (std::isfinite (v));
            peak = std::max (peak, static_cast<double> (std::abs (v)));
        }
        d.playHead.advance (n, kFs);
        pos += std::max (n, 1);
        ++blocks;
    }

    stop.store (true);
    messageThread.join();
    automationThread.join();

    const auto violations = allocspy::violations.load();
    INFO ("blocks " << blocks << ", other-thread operations " << otherWork.load() << ", peak " << peak);
    CHECK (violations == 0);
    CHECK (otherWork.load() > 100);
    CHECK (peak > 0.001);
}

TEST_CASE ("realtime: prepareToPlay, releaseResources and state calls are not in the audio budget", "[plugin][realtime]")
{
    // Documenting the contract: these may allocate (they run on other threads), processBlock
    // afterwards may not, whatever the block size relative to the hint.
    ScratchConfigDir scratch;
    Driver d;
    d.processor.controller().commitPattern (false, 0, d.patternFromText ("C C C C C C C C"));
    d.processor.controller().setParam (kSeqRun, 1.0f);
    d.playHead.present = false;
    for (const int hint : { 64, 256, 2048 })
    {
        d.prepare (kFs, hint);
        juce::AudioBuffer<float> buffer (2, 16384);
        juce::MidiBuffer midi;
        midi.ensureSize (4096);
        allocspy::violations.store (0);
        for (const int n : { 0, 1, 63, 64, 65, 512, 1023, 1024, 1025, 4096, 16384 })
        {
            juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, n);
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
            allocspy::ScopedAudioContext context;
            d.processor.processBlock (audio, midi);
        }
        INFO ("hint " << hint);
        CHECK (allocspy::violations.load() == 0);
        d.processor.releaseResources();
    }
}

TEST_CASE ("realtime: the host-integration paths allocate nothing either", "[plugin][realtime]")
{
    // CLAP format (latency padding and MIDI out through the delay line), a host that gives a
    // frame and no ppq (the LV2 wrapper), preview notes from the message thread, MIDI floods
    // over the input cap, and bypassed blocks.
    struct FrameHead final : juce::AudioPlayHead
    {
        std::int64_t frame = 0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (playing);
            p.setBpm (131.0);
            p.setTimeSignature (TimeSignature { 4, 4 });
            p.setTimeInSamples (frame);
            p.setBarCount (static_cast<std::int64_t> (static_cast<double> (frame) * 131.0 / (60.0 * kFs) / 4.0));
            return p;
        }
    } head;

    ScratchConfigDir scratch;
    Driver d;
    d.processor.setHostFormat (mw12FProcessor::HostFormat::Clap);
    d.processor.setPlayHead (&head);
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A"));
    Settings s = c.settings();
    s.midiThru = true;
    c.setSettings (s);
    d.processor.handlePendingMessages();
    d.prepare (kFs, 256);
    REQUIRE (d.processor.midiOutLatencyCompensation());

    juce::AudioBuffer<float> buffer (2, 8192);
    juce::MidiBuffer midi;
    midi.ensureSize (1 << 18);
    Rng rng (21);
    allocspy::violations.store (0);
    for (int b = 0; b < 3000; ++b)
    {
        const int n = rng.chance (0.05) ? 4096 : 1 + rng.nextInt (256);
        if (b % 50 == 0)
        {
            c.previewNote (36 + rng.nextInt (24), rng.chance (0.5), true);
            c.setParam (kSeqRun, rng.chance (0.5) ? 1.0f : 0.0f);
            c.setPlayMode (static_cast<seq::PlayMode> (rng.nextInt (3)));
        }
        if (b % 50 == 25) c.previewNote (36 + rng.nextInt (24), false, false);
        if (b % 333 == 0) c.stopPreview();
        head.playing = (b / 400) % 2 == 0;
        if (b % 700 == 0) head.frame += 12345;    // a jump

        midi.clear();
        if (b % 97 == 0)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 50, static_cast<juce::uint8> (100)), 0);
            for (int k = 0; k < 4200; ++k)
                midi.addEvent (juce::MidiMessage::channelPressureChange (1, k % 128), std::min (n - 1, 1));
            midi.addEvent (juce::MidiMessage::noteOff (1, 50), std::max (0, n - 1));
        }
        juce::AudioBuffer<float> audio (buffer.getArrayOfWritePointers(), 2, n);
        audio.clear();
        {
            allocspy::ScopedAudioContext context;
            if (b % 500 == 499)
                d.processor.processBlockBypassed (audio, midi);
            else
                d.processor.processBlock (audio, midi);
        }
        if (head.playing) head.frame += n;
        d.processor.handlePendingMessages();
    }
    CHECK (allocspy::violations.load() == 0);
}
