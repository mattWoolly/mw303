// Thread safety: state save and load from background threads, editor-style edits and
// parameter automation, all while processBlock runs. Built and run under ThreadSanitizer in
// build-plugin-tsan (with 'setarch -R'); in the normal build it still checks nothing breaks.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>

using namespace ptest;

TEST_CASE ("threads: state, edits and automation from other threads while processing", "[plugin][threads]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A"));
    c.commitPattern (false, 1, d.patternFromText ("G G G G"));
    c.setMidiMapping (74, kCutoff);
    d.prepare (kFs, 256);
    d.playHead.playing = true;
    d.playHead.bpm = 135.0;

    std::atomic<bool> stop { false };
    std::atomic<int> saves { 0 }, loads { 0 }, edits { 0 }, moves { 0 };

    // Host background save and load (pluginval "Background thread state", REAPER autosave).
    std::thread stateThread ([&] {
        std::string saved = d.stateText();
        Rng rng (1);
        while (! stop.load())
        {
            if (rng.chance (0.5))
            {
                saved = d.stateText();
                saves.fetch_add (1);
            }
            else
            {
                d.setStateText (saved);
                loads.fetch_add (1);
            }
            std::this_thread::yield();
        }
    });

    // The editor: pattern edits, settings, undo, MIDI learn bookkeeping.
    std::thread editThread ([&] {
        Rng rng (2);
        while (! stop.load())
        {
            seq::Pattern p = c.pattern (false, rng.nextInt (2));
            p.steps[static_cast<std::size_t> (rng.nextInt (16))].state = rng.chance (0.7) ? seq::StepState::Note : seq::StepState::Rest;
            p.steps[static_cast<std::size_t> (rng.nextInt (16))].note = static_cast<std::uint8_t> (rng.nextInt (12));
            c.commitPattern (false, rng.nextInt (2), p);
            if (rng.chance (0.1))
            {
                Settings s = c.settings();
                s.fxOrder = rng.chance (0.5) ? std::array<std::uint8_t, 6> { 5, 4, 3, 2, 1, 0 } : std::array<std::uint8_t, 6> { 0, 1, 2, 3, 4, 5 };
                s.internalBpm = static_cast<float> (60 + rng.nextInt (120));
                c.setSettings (s);
            }
            if (rng.chance (0.1)) c.undo();
            if (rng.chance (0.05)) c.setMidiMapping (70 + rng.nextInt (8), static_cast<ParamId> (rng.nextInt (kNumParams)));
            d.processor.handlePendingMessages();
            edits.fetch_add (1);
            std::this_thread::yield();
        }
    });

    // Host automation on its own thread.
    std::thread automationThread ([&] {
        Rng rng (3);
        const ParamId ids[] = { kCutoff, kResonance, kEnvMod, kDecay, kDistModel, kDistDrive, kDistOn, kDelayOn, kRevOn, kPlayMode, kPatternSlot, kSeqRun };
        while (! stop.load())
        {
            const ParamId id = ids[rng.nextInt (12)];
            auto& param = c.parameter (id);
            param.setValueNotifyingHost (static_cast<float> (rng.nextDouble()));
            moves.fetch_add (1);
            std::this_thread::yield();
        }
    });

    // The audio thread.
    juce::AudioBuffer<float> buffer (2, 2048);
    juce::MidiBuffer midi;
    Rng rng (4);
    for (int i = 0; i < 3000; ++i)
    {
        midi.clear();
        if (i % 20 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 40 + rng.nextInt (24), static_cast<juce::uint8> (100)), 0);
        if (i % 20 == 10) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        if (i % 7 == 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, rng.nextInt (128)), 3);
        const int n = 1 + rng.nextInt (2048);
        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, n);
        block.clear();
        d.processor.processBlock (block, midi);
        d.playHead.advance (n, kFs);
        for (int s = 0; s < n; s += 97)
            REQUIRE (std::isfinite (block.getSample (0, s)));
    }

    stop.store (true);
    stateThread.join();
    editThread.join();
    automationThread.join();
    CHECK (saves.load() > 0);
    CHECK (loads.load() > 0);
    CHECK (edits.load() > 0);
    CHECK (moves.load() > 0);

    // Everything is still consistent: a save reloads byte-identically.
    d.processor.handlePendingMessages();
    const std::string text = d.stateText();
    Driver e;
    e.setStateText (text);
    CHECK (e.stateText() == text);
}

TEST_CASE ("threads: construction and destruction while another instance processes", "[plugin][threads]")
{
    ScratchConfigDir scratch;
    Driver d;
    d.prepare();
    d.playHead.playing = true;
    std::atomic<bool> stop { false };
    std::thread maker ([&] {
        while (! stop.load())
        {
            Driver other;
            other.prepare (44100.0, 128);
            other.render (1024, 128);
            std::this_thread::yield();
        }
    });
    d.render (static_cast<std::int64_t> (kFs * 2), 256);
    stop.store (true);
    maker.join();
}
