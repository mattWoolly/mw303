// The playhead tracker: the nine required feeds (steady, sparse, relocations, tempo change,
// pre-roll, stop and start, block sizes, block-size invariance) and the internal clock.
#include "Harness.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ptest;
using Catch::Approx;

namespace
{
TransportInfo playingAt (double ppq, double bpm = 120.0)
{
    TransportInfo t;
    t.valid = true;
    t.playing = true;
    t.ppqValid = true;
    t.ppq = ppq;
    t.bpmValid = true;
    t.bpm = bpm;
    return t;
}

TransportInfo stoppedAt (double ppq, double bpm = 120.0)
{
    TransportInfo t = playingAt (ppq, bpm);
    t.playing = false;
    return t;
}

/** A sixteen-step pattern of plain notes with rising pitch, for event-stream comparisons. */
void fillScale (Pattern& p)
{
    setSteps (p, "C C# D D# E F F# G G# A A# B C+ C#+ D+ D#+");
}
}

TEST_CASE ("Tracker 1: a host that reports every block is followed exactly", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);

    const double qps = 120.0 / (60.0 * 48000.0);
    std::int64_t sample = 0;

    for (int block = 0; block < 500; ++block)
    {
        const ClockBlock& b = tracker.process (playingAt (static_cast<double> (sample) * qps), 256, 120.0);

        REQUIRE (b.numSegments == 1);
        REQUIRE (b.segments[0].offset == 0);
        REQUIRE (b.segments[0].numSamples == 256);
        REQUIRE (b.segments[0].startPpq == static_cast<double> (sample) * qps);
        REQUIRE (b.segments[0].discontinuity == (block == 0));
        REQUIRE (b.hostStarted == (block == 0));
        REQUIRE_FALSE (b.relocated);
        REQUIRE (b.hostRolling);
        REQUIRE (b.source == ClockSource::Host);
        REQUIRE (b.quartersPerSample == Approx (qps).epsilon (1e-12));

        sample += 256;
    }

    REQUIRE (tracker.position() == Approx (static_cast<double> (sample) * qps).epsilon (1e-12));
}

TEST_CASE ("Tracker 2: a host that reports once and then says nothing is extrapolated", "[tracker]")
{
    // The steady feed and the sparse feed must give the same positions...
    PlayheadTracker steady, sparse;
    steady.prepare (48000.0);
    sparse.prepare (48000.0);

    const double qps = 120.0 / (60.0 * 48000.0);
    std::int64_t sample = 0;

    for (int block = 0; block < 4000; ++block)
    {
        const double ppq = 8.0 + static_cast<double> (sample) * qps;
        const ClockBlock a = steady.process (playingAt (ppq), 128, 120.0);
        const ClockBlock b = sparse.process (block == 0 ? playingAt (ppq) : TransportInfo {}, 128, 120.0);

        REQUIRE (b.hostRolling);
        REQUIRE (b.source == ClockSource::Host);
        REQUIRE (b.numSegments == 1);
        REQUIRE (b.segments[0].discontinuity == (block == 0));
        REQUIRE (b.segments[0].startPpq == Approx (a.segments[0].startPpq).margin (1e-9));

        sample += 128;
    }

    // ...and the same events, sample for sample.
    Session every, once;
    fillScale (every.seq());
    fillScale (once.seq());
    once.host.newsOnlyOnChange = true;
    every.host.play();
    once.host.play();
    every.run (48000 * 20, 128);
    once.run (48000 * 20, 128);

    REQUIRE (every.voice.size() > 300);
    REQUIRE (every.voice == once.voice);
    REQUIRE (every.midi == once.midi);
    REQUIRE (once.status.running);
}

TEST_CASE ("Tracker 3: a jump backwards is a relocation", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);
    const double qps = 120.0 / (60.0 * 48000.0);

    tracker.process (playingAt (15.0), 480, 120.0);
    const ClockBlock steady = tracker.process (playingAt (15.0 + 480 * qps), 480, 120.0);
    REQUIRE_FALSE (steady.relocated);
    REQUIRE_FALSE (steady.segments[0].discontinuity);

    const ClockBlock wrapped = tracker.process (playingAt (12.0), 480, 120.0);
    REQUIRE (wrapped.relocated);
    REQUIRE (wrapped.segments[0].discontinuity);
    REQUIRE (wrapped.segments[0].startPpq == 12.0);
    REQUIRE_FALSE (wrapped.hostStarted);
    REQUIRE (tracker.position() == Approx (12.0 + 480 * qps));

    // Through the player: the sounding note is released at the jump and the step at the
    // landing point plays at once. Nothing is left on, nothing doubles.
    Session s;
    fillScale (s.seq());
    s.host.play();
    s.run (kStep * 6 + 100, 64);       // inside step 6, gate open
    REQUIRE (s.status.gateOpen);
    const std::int64_t jump = s.now;
    s.host.locate (0.5);               // back to step 2
    s.run (kStep * 4, 64);
    s.host.stop();
    s.run (64, 64);

    const auto notes = s.notes();
    REQUIRE (voiceBalance (notes).empty());
    REQUIRE (midiBalance (s.midi).empty());

    bool sawOffAtJump = false, sawStep2AtJump = false;
    for (const Ev& e : notes)
    {
        if (e.sample == jump && e.type == VoiceEvent::Type::NoteOff) sawOffAtJump = true;
        if (e.sample == jump && e.type == VoiceEvent::Type::NoteOn && e.note == kC + 2) sawStep2AtJump = true;
    }
    REQUIRE (sawOffAtJump);
    REQUIRE (sawStep2AtJump);
}

TEST_CASE ("Tracker 4: a jump forwards is a relocation", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);
    const double qps = 120.0 / (60.0 * 48000.0);

    tracker.process (playingAt (0.0), 480, 120.0);
    const ClockBlock jumped = tracker.process (playingAt (64.0), 480, 120.0);
    REQUIRE (jumped.relocated);
    REQUIRE (jumped.segments[0].discontinuity);
    REQUIRE (jumped.segments[0].startPpq == 64.0);

    // A difference well inside the tolerance is adopted without a relocation.
    const ClockBlock nudged = tracker.process (playingAt (64.0 + 480 * qps + 0.004), 480, 120.0);
    REQUIRE_FALSE (nudged.relocated);
    REQUIRE_FALSE (nudged.segments[0].discontinuity);
    REQUIRE (nudged.segments[0].startPpq == 64.0 + 480 * qps + 0.004);

    Session s;
    fillScale (s.seq());
    s.host.play();
    s.run (kStep * 2 + 500, 64);
    const std::int64_t jump = s.now;
    s.host.locate (2.25 + 0.1);        // into the middle of step 9: step 10 is the next to play
    s.run (kStep * 3, 64);
    s.host.stop();
    s.run (64, 64);

    const auto notes = s.notes();
    REQUIRE (voiceBalance (notes).empty());
    REQUIRE (midiBalance (s.midi).empty());

    // Step 9 started before the landing point: it is not retriggered.
    for (const Ev& e : notes)
        if (e.type == VoiceEvent::Type::NoteOn && e.sample >= jump)
            REQUIRE (e.note != kC + 9);

    const std::int64_t step10 = jump + sampleOf (2.5 - 2.35, 120.0, 48000.0);
    bool found = false;
    for (const Ev& e : notes)
        if (e.type == VoiceEvent::Type::NoteOn && e.note == kC + 10 && e.sample == step10)
            found = true;
    REQUIRE (found);
}

TEST_CASE ("Tracker 5: a tempo change re-times the clock without a jump", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);

    double ppq = 0.0;
    tracker.process (playingAt (ppq, 120.0), 480, 120.0);
    ppq += 480 * 120.0 / (60.0 * 48000.0);

    const ClockBlock fast = tracker.process (playingAt (ppq, 150.0), 480, 120.0);
    REQUIRE_FALSE (fast.relocated);
    REQUIRE_FALSE (fast.segments[0].discontinuity);
    REQUIRE (fast.bpm == 150.0);
    REQUIRE (fast.quartersPerSample == Approx (150.0 / (60.0 * 48000.0)).epsilon (1e-12));
    ppq += 480 * 150.0 / (60.0 * 48000.0);
    REQUIRE (tracker.position() == Approx (ppq).epsilon (1e-12));

    // With no news the new tempo keeps being used.
    tracker.process ({}, 480, 120.0);
    ppq += 480 * 150.0 / (60.0 * 48000.0);
    REQUIRE (tracker.position() == Approx (ppq).epsilon (1e-12));

    // Through the player: steps follow the new tempo from the block that reports it.
    Session s;
    fillScale (s.seq());
    s.host.play();
    s.run (kStep * 4, 64);              // four steps at 120
    s.host.setTempo (150.0);
    s.run (48000, 64);
    s.host.stop();
    s.run (64, 64);

    REQUIRE (voiceBalance (s.notes()).empty());
    const auto ons = s.noteOns();
    REQUIRE (ons.size() >= 8);
    for (std::size_t k = 0; k < 4; ++k) REQUIRE (ons[k].sample == static_cast<std::int64_t> (k) * kStep);
    for (std::size_t k = 4; k < 8; ++k) REQUIRE (ons[k].sample == 4 * kStep + static_cast<std::int64_t> (k - 4) * 4800);
}

TEST_CASE ("Tracker 6: negative positions during pre-roll", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);
    const double qps = 120.0 / (60.0 * 48000.0);

    for (int block = 0; block < 300; ++block)
    {
        const double ppq = -2.0 + block * 256 * qps;
        const ClockBlock b = tracker.process (playingAt (ppq), 256, 120.0);
        REQUIRE (b.segments[0].startPpq == ppq);
        REQUIRE (b.segments[0].discontinuity == (block == 0));
    }

    // Steps fire normally before bar 1, with the pattern phase the song position dictates.
    Session s;
    fillScale (s.seq());
    s.host.locate (-1.0);               // four steps before the origin: steps 12..15 come first
    s.host.play();
    s.run (kStep * 8, 64);
    s.host.stop();
    s.run (64, 64);

    const auto ons = s.noteOns();
    REQUIRE (ons.size() == 8);
    const int expected[] = { 12, 13, 14, 15, 0, 1, 2, 3 };
    for (std::size_t k = 0; k < 8; ++k)
    {
        REQUIRE (ons[k].sample == static_cast<std::int64_t> (k) * kStep);
        REQUIRE (ons[k].note == kC + expected[k]);
    }
    REQUIRE (voiceBalance (s.notes()).empty());
}

TEST_CASE ("Tracker 7: stop, then start at the same position", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);
    const double qps = 120.0 / (60.0 * 48000.0);

    tracker.process (playingAt (4.0), 480, 120.0);
    const ClockBlock stopped = tracker.process (stoppedAt (4.0 + 480 * qps), 480, 120.0);
    REQUIRE (stopped.hostStopped);
    REQUIRE_FALSE (stopped.hostRolling);
    REQUIRE (stopped.source == ClockSource::Internal);
    REQUIRE_FALSE (stopped.segments[0].discontinuity);     // the internal clock carries on from there

    const ClockBlock idle = tracker.process (stoppedAt (4.0 + 480 * qps), 480, 120.0);
    REQUIRE_FALSE (idle.hostStopped);

    const ClockBlock restarted = tracker.process (playingAt (4.0 + 480 * qps), 480, 120.0);
    REQUIRE (restarted.hostStarted);
    REQUIRE (restarted.hostRolling);
    REQUIRE (restarted.segments[0].discontinuity);
    REQUIRE (restarted.segments[0].startPpq == 4.0 + 480 * qps);

    // Through the player: the note is released at the stop and the pattern picks up where
    // the song position says, with no step played twice.
    Session s;
    fillScale (s.seq());
    s.host.play();
    s.run (kStep * 3 + 1000, 64);
    const std::int64_t stopAt = s.now;
    s.host.stop();
    s.run (4800, 64);
    REQUIRE_FALSE (s.status.running);
    REQUIRE_FALSE (s.status.gateOpen);
    const std::int64_t startAt = s.now;
    s.host.play();                       // same position: 1000 samples into step 3
    s.run (kStep * 2, 64);
    s.host.stop();
    s.run (64, 64);

    const auto notes = s.notes();
    REQUIRE (voiceBalance (notes).empty());
    REQUIRE (midiBalance (s.midi).empty());

    std::vector<Ev> expected = {
        on (0, kC), off (kGate), on (kStep, kC + 1), off (kStep + kGate), on (2 * kStep, kC + 2), off (2 * kStep + kGate),
        on (3 * kStep, kC + 3), off (stopAt),
        on (startAt + 5000, kC + 4), off (startAt + 5000 + kGate), on (startAt + 11000, kC + 5), off (startAt + 12000)
    };
    REQUIRE (notes == expected);
}

TEST_CASE ("Tracker 8: block sizes 1, 7, 64, 4096 and an oversized block", "[tracker]")
{
    const double qps = 120.0 / (60.0 * 48000.0);

    for (int blockSize : { 1, 7, 64, 4096, 20000 })
    {
        PlayheadTracker tracker;
        tracker.prepare (48000.0);
        std::int64_t sample = 0;

        // Steady, then silent: the extrapolated position must agree to within rounding.
        while (sample < 48000 * 4)
        {
            const bool news = sample < 48000;
            const ClockBlock& b = tracker.process (news ? playingAt (static_cast<double> (sample) * qps) : TransportInfo {}, blockSize, 120.0);
            REQUIRE (b.numSegments == 1);
            REQUIRE (b.segments[0].numSamples == blockSize);
            REQUIRE (b.segments[0].startPpq == Approx (static_cast<double> (sample) * qps).margin (1e-9));
            sample += blockSize;
        }

        // The player takes a block far larger than it was prepared for.
        Session s (48000.0, 512);
        fillScale (s.seq());
        s.host.play();
        s.run (48000 * 4, blockSize);
        s.host.stop();
        s.run (blockSize, blockSize);
        REQUIRE (s.noteOns().size() == 32);
        REQUIRE (voiceBalance (s.notes()).empty());
        REQUIRE (s.orderViolations == 0);
        REQUIRE (s.allocations == 0);
    }
}

TEST_CASE ("Tracker 9: the same host timeline gives the same events in any blocking", "[tracker]")
{
    // A timeline with a seek, a tempo change and a stop, cut into blocks six different ways.
    // The host events fall on multiples of 4096 samples so every blocking sees them at the
    // same sample.
    auto render = [] (int blockSize, std::uint64_t seed)
    {
        Session s;
        fillScale (s.seq());
        s.seq().steps[3].slide = true;
        s.seq().steps[7].state = StepState::Tie;
        acid::Rng rng (seed);

        auto runTo = [&] (std::int64_t end)
        {
            while (s.now < end)
            {
                std::int64_t n = blockSize > 0 ? blockSize : 1 + rng.nextInt (700);
                s.runBlock (static_cast<int> (std::min<std::int64_t> (n, end - s.now)));
            }
        };

        s.host.play();
        runTo (4096 * 20);
        s.host.locate (10.0);
        runTo (4096 * 37);
        s.host.setTempo (133.0);
        runTo (4096 * 60);
        s.host.stop();
        runTo (4096 * 62);
        s.host.locate (1.0);
        s.host.play();
        runTo (4096 * 80);
        s.host.stop();
        runTo (4096 * 81);

        REQUIRE (s.orderViolations == 0);
        REQUIRE (s.allocations == 0);
        REQUIRE (voiceBalance (s.notes()).empty());
        return std::make_pair (s.voice, s.midi);
    };

    const auto reference = render (64, 0);
    REQUIRE (reference.first.size() > 100);

    for (int blockSize : { 1, 7, 512, 4096 })
        REQUIRE (render (blockSize, 0) == reference);

    REQUIRE (render (0, 11) == reference);
    REQUIRE (render (0, 12) == reference);
}

TEST_CASE ("Tracker: the internal clock runs when there is no host transport", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (44100.0);

    std::int64_t sample = 0;
    for (int block = 0; block < 1000; ++block)
    {
        const ClockBlock& b = tracker.process ({}, 441, 90.0);
        REQUIRE (b.source == ClockSource::Internal);
        REQUIRE_FALSE (b.hostRolling);
        REQUIRE (b.bpm == 90.0);
        REQUIRE (b.segments[0].startPpq == Approx (static_cast<double> (sample) * 90.0 / (60.0 * 44100.0)).margin (1e-9));
        REQUIRE_FALSE (b.segments[0].discontinuity);
        sample += 441;
    }

    // Ten seconds at 90 BPM is fifteen quarter notes, with no drift.
    REQUIRE (tracker.position() == Approx (15.0).margin (1e-9));
}

TEST_CASE ("Tracker: the internal clock takes the host tempo once it is known, and re-anchors on a change", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);

    tracker.process ({}, 48000, 100.0);                       // one second at the fallback tempo
    REQUIRE (tracker.position() == Approx (100.0 / 60.0).margin (1e-9));

    tracker.process (stoppedAt (0.0, 180.0), 48000, 100.0);   // stopped host, but it reports a tempo
    REQUIRE (tracker.bpm() == 180.0);
    REQUIRE (tracker.position() == Approx (100.0 / 60.0 + 3.0).margin (1e-9));

    // A wild tempo is clamped, a broken one ignored.
    TransportInfo broken = stoppedAt (0.0, 0.0);
    tracker.process (broken, 480, 100.0);
    REQUIRE (tracker.bpm() == 180.0);
}

TEST_CASE ("Tracker: a host that plays without giving a position is counted in samples", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);

    TransportInfo t;
    t.valid = true;
    t.playing = true;
    t.bpmValid = true;
    t.bpm = 120.0;

    const ClockBlock first = tracker.process (t, 24000, 120.0);
    REQUIRE (first.hostStarted);
    REQUIRE (first.hostRolling);
    REQUIRE (first.segments[0].startPpq == 0.0);

    const ClockBlock second = tracker.process (t, 24000, 120.0);
    REQUIRE_FALSE (second.segments[0].discontinuity);
    REQUIRE (second.segments[0].startPpq == Approx (1.0).margin (1e-12));
}

TEST_CASE ("Tracker: a loop that wraps inside a block splits the block", "[tracker]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);
    const double qps = 120.0 / (60.0 * 48000.0);

    TransportInfo t = playingAt (8.0 - 1000 * qps);     // 1000 samples before the loop end
    t.looping = true;
    t.loopStartPpq = 4.0;
    t.loopEndPpq = 8.0;

    const ClockBlock& b = tracker.process (t, 4096, 120.0);
    REQUIRE (b.numSegments == 2);
    REQUIRE (b.segments[0].offset == 0);
    REQUIRE (b.segments[0].numSamples == 1000);
    REQUIRE (b.segments[1].offset == 1000);
    REQUIRE (b.segments[1].numSamples == 3096);
    REQUIRE (b.segments[1].discontinuity);
    REQUIRE (b.segments[1].startPpq == Approx (4.0).margin (1e-9));
    REQUIRE (tracker.position() == Approx (4.0 + 3096 * qps).margin (1e-9));

    // The host's next report agrees with the prediction: no relocation.
    TransportInfo next = playingAt (4.0 + 3096 * qps);
    next.looping = true;
    next.loopStartPpq = 4.0;
    next.loopEndPpq = 8.0;
    const ClockBlock& c = tracker.process (next, 4096, 120.0);
    REQUIRE (c.numSegments == 1);
    REQUIRE_FALSE (c.relocated);

    // Loop points are ignored when the host says it is not looping.
    PlayheadTracker other;
    other.prepare (48000.0);
    t.looping = false;
    REQUIRE (other.process (t, 4096, 120.0).numSegments == 1);
}
