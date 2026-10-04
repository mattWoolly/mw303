// processBlock: silence until prepared, transport handling (no playhead, a playhead that
// appears and disappears, loops), oversized and zero-sample blocks, mono output, latency and
// tail reporting, prepare racing process.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>

using namespace ptest;

namespace
{
void putPattern (Driver& d)
{
    d.processor.controller().commitPattern (false, 0, d.patternFromText ("C . . . C . . . C . . . C . . ."));
    d.processor.controller().setParam (kDecay, 0.0f);
}
}

TEST_CASE ("process: silence and an empty MIDI buffer until prepared", "[plugin][process]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.processor.controller().setParam (kSeqRun, 1.0f);
    d.addNoteOn (0, 48);
    const Rendered r = d.render (4096, 512);
    CHECK (peakOf (r.left) == 0.0f);
    CHECK (r.midiOut.empty());

    d.prepare();
    d.processor.releaseResources();
    const Rendered after = d.render (4096, 512);
    CHECK (peakOf (after.left) == 0.0f);

    // releaseResources is idempotent (CLAP calls it on every deactivate).
    d.processor.releaseResources();
    d.processor.releaseResources();
    d.prepare();
    const Rendered again = d.render (static_cast<std::int64_t> (kFs), 512);
    CHECK (peakOf (again.left) > 0.01f);
}

TEST_CASE ("process: no playhead at all runs the internal clock with Run on", "[plugin][process][transport]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.playHead.present = false;
    Settings s = d.processor.controller().settings();
    s.internalBpm = 90.0f;                      // a quarter is 32000 samples
    d.processor.controller().setSettings (s);
    d.prepare();

    d.processor.controller().setParam (kSeqRun, 0.0f);
    const Rendered stopped = d.render (static_cast<std::int64_t> (kFs), 512);
    CHECK (peakOf (stopped.left) == 0.0f);

    d.processor.controller().setParam (kSeqRun, 1.0f);
    const Rendered r = d.render (static_cast<std::int64_t> (kFs * 4), 512);
    REQUIRE (allFinite (r.left));
    const auto on = detectOnsets (r.left, kFs);
    REQUIRE (on.size() >= 4);
    CHECK (std::abs (static_cast<double> (on[3] - on[1]) / 2.0 - 32000.0) < 150.0);
    CHECK (countNoteOns (r.midiOut) >= 4);
    CHECK (d.processor.controller().engineStatus().internalClock);
}

TEST_CASE ("process: a rolling host plays the pattern on its grid", "[plugin][process][transport]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.prepare();
    d.playHead.playing = true;
    d.playHead.bpm = 120.0;                     // a quarter is 24000 samples
    d.playHead.ppq = 0.0;
    const Rendered r = d.render (static_cast<std::int64_t> (kFs * 4), 512);
    const auto on = detectOnsets (r.left, kFs);
    REQUIRE (on.size() >= 6);
    CHECK (std::abs (static_cast<double> (on[5] - on[1]) / 4.0 - 24000.0) < 150.0);
    CHECK (on[0] < 700);                         // the first beat, plus the gate turn-on delay and the detector window
    CHECK (d.processor.controller().engineStatus().hostRolling);
    CHECK (! d.processor.controller().engineStatus().internalClock);
}

TEST_CASE ("process: a playhead that only reports changes sounds like one that reports every block", "[plugin][process][transport]")
{
    ScratchConfigDir scratch;
    const auto run = [] (bool disappear) {
        Driver d;
        putPattern (d);
        d.processor.controller().setParam (kCalDrift, 0.0f);
        d.prepare();
        d.playHead.playing = true;
        d.playHead.bpm = 128.0;
        int block = 0;
        return d.render (static_cast<std::int64_t> (kFs * 4), 512, [&] (std::int64_t) {
            // LV2 in Ardour: the position arrives once, then nothing until something changes.
            d.playHead.present = ! disappear || block == 0 || block % 97 == 0;
            ++block;
        });
    };
    const Rendered every = run (false);
    const Rendered sparse = run (true);
    REQUIRE (allFinite (every.left));
    REQUIRE (allFinite (sparse.left));
    const auto a = detectOnsets (every.left, kFs);
    const auto b = detectOnsets (sparse.left, kFs);
    REQUIRE (a.size() >= 6);
    REQUIRE (a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK (std::abs (a[i] - b[i]) <= 2);
    CHECK (countNoteOns (every.midiOut) == countNoteOns (sparse.midiOut));
}

TEST_CASE ("process: the host stopping ends the note and the clock keeps its place", "[plugin][process][transport]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.prepare();
    d.playHead.playing = true;
    d.playHead.bpm = 120.0;
    const auto half = static_cast<std::int64_t> (kFs * 2);
    const Rendered r = d.render (half * 2, 512, [&] (std::int64_t pos) {
        if (pos >= half) d.playHead.playing = false;
    });
    CHECK (countNoteOns (r.midiOut) == countNoteOffs (r.midiOut));
    const auto on = detectOnsets (r.left, kFs);
    for (const auto o : on)
        CHECK (o < half + 4096);
    // Nothing sounds in the last second.
    float tail = 0.0f;
    for (std::size_t i = static_cast<std::size_t> (half + static_cast<std::int64_t> (kFs)); i < r.left.size(); ++i)
        tail = std::max (tail, std::abs (r.left[i]));
    CHECK (tail < 1.0e-3f);
}

TEST_CASE ("process: a host loop keeps the notes on the grid", "[plugin][process][transport][loop]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.prepare();
    d.playHead.playing = true;
    d.playHead.bpm = 120.0;
    d.playHead.looping = true;
    d.playHead.loopStart = 0.0;
    d.playHead.loopEnd = 3.0;                    // three beats, so the loop cuts the 4-beat pattern
    const Rendered r = d.render (static_cast<std::int64_t> (kFs * 6), 512);
    REQUIRE (allFinite (r.left));
    const auto on = detectOnsets (r.left, kFs);
    REQUIRE (on.size() >= 8);
    for (std::size_t i = 1; i < on.size(); ++i)
    {
        const auto gap = on[i] - on[i - 1];
        // every gap is a whole number of quarters (24000 samples) within detector jitter
        const double beats = static_cast<double> (gap) / 24000.0;
        CHECK (std::abs (beats - std::round (beats)) < 0.02);
    }
    CHECK (countNoteOns (r.midiOut) == countNoteOffs (r.midiOut));
}

TEST_CASE ("process: oversized and zero-sample blocks", "[plugin][process][blocks]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.processor.controller().setParam (kCalDrift, 0.0f);
    d.prepare (kFs, 256);                        // the processor prepares at least 1024
    d.playHead.playing = true;
    d.playHead.bpm = 120.0;

    SECTION ("a 16384-sample block renders like 256-sample blocks")
    {
        const Rendered big = d.render (16384 * 8, 16384);
        Driver e;
        putPattern (e);
        e.processor.controller().setParam (kCalDrift, 0.0f);
        e.prepare (kFs, 256);
        e.playHead.playing = true;
        e.playHead.bpm = 120.0;
        const Rendered small = e.render (16384 * 8, 256);
        REQUIRE (allFinite (big.left));
        const auto a = detectOnsets (big.left, kFs);
        const auto b = detectOnsets (small.left, kFs);
        REQUIRE (a.size() == b.size());
        REQUIRE (a.size() >= 5);
        for (std::size_t i = 0; i < a.size(); ++i)
            CHECK (std::abs (a[i] - b[i]) <= 2);
        CHECK (countNoteOns (big.midiOut) == countNoteOns (small.midiOut));
        // MIDI out offsets of the oversized block stay inside it and ascend
        std::int64_t last = -1;
        for (const auto& m : big.midiOut) { CHECK (m.sample >= last); last = m.sample; }
    }

    SECTION ("random block sizes from 0 to 5000 keep every note")
    {
        Rng rng (7);
        const Rendered r = d.render (static_cast<std::int64_t> (kFs * 4), [&] { return rng.nextInt (5001); });
        REQUIRE (allFinite (r.left));
        CHECK (countNoteOns (r.midiOut) == countNoteOffs (r.midiOut));
        CHECK (countNoteOns (r.midiOut) >= 7);
    }

    SECTION ("a zero-sample block with MIDI keeps the MIDI for the next block")
    {
        d.processor.controller().setParam (kPlayMode, 0.0f);    // External: a note plays a note
        d.addNoteOn (0, 48);
        d.oneBlock (0);
        juce::AudioBuffer<float> captured;
        d.oneBlock (2048, &captured);
        float peak = 0.0f;
        for (int i = 0; i < captured.getNumSamples(); ++i) peak = std::max (peak, std::abs (captured.getSample (0, i)));
        CHECK (peak > 0.01f);
        d.addNoteOff (0, 48);
        d.oneBlock (0);
        d.oneBlock (0);
        d.oneBlock (512);
    }
}

TEST_CASE ("process: a mono output bus gets the mix of both channels", "[plugin][process][buses]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.numChannels = 1;
    auto layout = d.processor.getBusesLayout();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    REQUIRE (d.processor.checkBusesLayoutSupported (layout));
    REQUIRE (d.processor.setBusesLayout (layout));
    d.processor.setPlayConfigDetails (0, 1, kFs, 512);
    d.processor.prepareToPlay (kFs, 512);
    d.playHead.playing = true;
    const Rendered r = d.render (static_cast<std::int64_t> (kFs), 512);
    REQUIRE (allFinite (r.left));
    CHECK (peakOf (r.left) > 0.01f);

    juce::AudioProcessor::BusesLayout bad = layout;
    bad.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK (! d.processor.checkBusesLayoutSupported (bad));
}

TEST_CASE ("process: latency and tail follow the engine", "[plugin][process]")
{
    ScratchConfigDir scratch;
    Driver d;
    d.prepare (kFs, 512);
    CHECK (d.processor.getLatencySamples() == d.processor.engine().latencySamples());
    CHECK (d.processor.getTailLengthSeconds() == d.processor.engine().tailSeconds());
    CHECK (d.processor.getTailLengthSeconds() >= 0.0);

    // Offline rendering picks the offline quality tier at the next prepare.
    d.processor.setNonRealtime (true);
    d.prepare (kFs, 512);
    const int offlineLatency = d.processor.getLatencySamples();
    d.processor.setNonRealtime (false);
    d.prepare (kFs, 512);
    CHECK (d.processor.getLatencySamples() <= offlineLatency);
}

TEST_CASE ("process: prepareToPlay racing processBlock never breaks the engine", "[plugin][process][threads]")
{
    ScratchConfigDir scratch;
    Driver d;
    putPattern (d);
    d.processor.controller().setParam (kSeqRun, 1.0f);
    d.playHead.present = false;
    d.prepare (kFs, 256);

    std::atomic<bool> stop { false };
    std::atomic<int> prepares { 0 };
    std::thread preparer ([&] {
        const double rates[] = { 44100.0, 48000.0, 96000.0 };
        int k = 0;
        while (! stop.load())
        {
            d.processor.prepareToPlay (rates[k % 3], 128 + 64 * (k % 5));
            if (k % 7 == 6) d.processor.releaseResources();
            ++k;
            prepares.fetch_add (1);
            std::this_thread::yield();
        }
    });

    juce::AudioBuffer<float> buffer (2, 1024);
    juce::MidiBuffer midi;
    for (int i = 0; i < 4000; ++i)
    {
        midi.clear();
        if (i % 50 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 48, static_cast<juce::uint8> (100)), 0);
        if (i % 50 == 25) midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, 64 + (i % 900));
        block.clear();
        d.processor.processBlock (block, midi);
        for (int s = 0; s < block.getNumSamples(); ++s)
            REQUIRE (std::isfinite (block.getSample (0, s)));
    }
    stop.store (true);
    preparer.join();
    CHECK (prepares.load() > 10);
}
