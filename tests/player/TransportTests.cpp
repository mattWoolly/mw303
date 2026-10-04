// Host loops, relocations and tempo ramps seen through the whole player: no stuck notes, no
// doubled notes, and timing that follows the host.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace ptest;

TEST_CASE ("Transport: the allocation guard used by these tests does count", "[transport]")
{
    acid::test::ScopedAllocGuard guard;
    std::vector<int>* v = new std::vector<int> (64, 1);
    volatile int sink = (*v)[3];
    (void) sink;
    delete v;
    REQUIRE (guard.count() >= 2);
}

TEST_CASE ("Transport: every pass of a host loop plays the same events", "[transport]")
{
    for (bool hostSplits : { true, false })
    {
        for (int blockSize : { 64, 500, 4096 })
        {
            Session s;
            setSteps (s.seq(), "C:S D E:A ~ . F:S G:S A . C+ . D:S ~ E . G-:S");   // the last step slides across the loop point
            s.seq().steps[2].ratchet = 2;
            s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest;
            s.host.looping = true;
            s.host.loopStart = 4.0;
            s.host.loopEnd = 8.0;                      // one bar, 96000 samples
            s.host.splitsAtLoop = hostSplits;
            s.host.locate (4.0);
            s.host.play();

            const std::int64_t pass = 16 * kStep;
            const int passes = 20;
            s.run (pass * passes, blockSize);
            s.host.stop();
            s.run (64, 64);

            INFO ("host splits " << hostSplits << " block size " << blockSize);
            REQUIRE (voiceBalance (s.notes()).empty());
            REQUIRE (midiBalance (s.midi).empty());
            REQUIRE (s.orderViolations == 0);
            REQUIRE (s.allocations == 0);

            // Cut the event list into passes and compare each with the first.
            std::vector<std::vector<Ev>> perPass (static_cast<std::size_t> (passes));
            for (Ev e : s.notes())
            {
                if (e.sample >= pass * passes) continue;      // the release at the final stop
                const auto index = static_cast<std::size_t> (e.sample / pass);
                e.sample %= pass;
                perPass[index].push_back (e);
            }

            REQUIRE (perPass[0].size() > 15);

            // The first pass starts cold; from the second on, each begins with the release of
            // the note that was sliding into the wrap. They are all alike.
            for (std::size_t p = 2; p < perPass.size(); ++p)
                REQUIRE (perPass[p] == perPass[1]);

            REQUIRE (perPass[1].front() == off (0));
            REQUIRE (perPass[1][1] == on (0, kC));            // not slid into: the wrap dropped the slide
            REQUIRE (std::vector<Ev> (perPass[1].begin() + 1, perPass[1].end()) == perPass[0]);
        }
    }
}

TEST_CASE ("Transport: a relocation at any point of a slide chain leaves nothing on", "[transport]")
{
    for (std::int64_t jumpAt = 500; jumpAt < 8 * kStep; jumpAt += 1777)
    {
        for (double target : { 0.0, 0.3, 1.0, 7.99, -2.0 })
        {
            Session s;
            setSteps (s.seq(), "C:S D:S E:S F:S G:S A:S B:S C+:S");
            s.host.play();
            s.run (jumpAt, 256);
            s.host.locate (target);
            s.run (4 * kStep, 256);
            s.host.stop();
            s.run (64, 64);

            INFO ("jump at " << jumpAt << " to " << target);
            REQUIRE (voiceBalance (s.notes()).empty());
            REQUIRE (midiBalance (s.midi).empty());

            // At the jump: one NoteOff, and the first note after it is struck afresh.
            std::size_t offsAtJump = 0;
            bool first = true;

            for (const Ev& e : s.notes())
            {
                if (e.sample == jumpAt && e.type == VoiceEvent::Type::NoteOff) ++offsAtJump;

                if (e.sample >= jumpAt && e.type == VoiceEvent::Type::NoteOn && first)
                {
                    REQUIRE_FALSE (e.legato);
                    first = false;
                }
            }

            REQUIRE (offsAtJump == 1);
        }
    }
}

TEST_CASE ("Transport: a tempo ramp is followed without relocations and without losing a step", "[transport]")
{
    // The host ramps from 100 to 160 BPM over four seconds and reports, as hosts do, the
    // tempo and position at the top of each block. The true position inside a block is
    // curved; the player extrapolates linearly and is corrected by the next report.
    const double sr = 48000.0;
    const double b0 = 100.0, b1 = 160.0, rampSeconds = 4.0;
    const double rate = (b1 - b0) / rampSeconds;                 // BPM per second

    auto bpmAt = [&] (double t) { return t < rampSeconds ? b0 + rate * t : b1; };
    auto ppqAt = [&] (double t)
    {
        if (t < rampSeconds) return (b0 * t + 0.5 * rate * t * t) / 60.0;
        return (b0 * rampSeconds + 0.5 * rate * rampSeconds * rampSeconds) / 60.0 + b1 * (t - rampSeconds) / 60.0;
    };
    auto timeOfPpq = [&] (double q)
    {
        const double qRampEnd = (b0 * rampSeconds + 0.5 * rate * rampSeconds * rampSeconds) / 60.0;
        if (q >= qRampEnd) return rampSeconds + (q - qRampEnd) * 60.0 / b1;
        return (-b0 + std::sqrt (b0 * b0 + 2.0 * rate * 60.0 * q)) / rate;
    };

    struct Case { int blockSize; double toleranceSamples; };

    for (const Case c : { Case { 64, 1.5 }, Case { 512, 2.5 }, Case { 4096, 48.0 } })
    {
        Session s (sr);
        setSteps (s.seq(), "C:S D:S E:S F:S");                  // a chain: any relocation would break it
        s.host.play();

        const std::int64_t total = static_cast<std::int64_t> (6.0 * sr);

        while (s.now < total)
        {
            const double t = static_cast<double> (s.now) / sr;
            s.host.bpm = bpmAt (t);
            s.host.ppqBase = ppqAt (t);
            s.host.samplesFromBase = 0;
            s.runBlock (c.blockSize);
        }

        s.host.stop();
        s.run (64, 64);

        const auto ons = s.noteOns();
        const int expectedSteps = static_cast<int> (std::floor (ppqAt (static_cast<double> (total) / sr) * 4.0)) + 1;

        INFO ("block size " << c.blockSize);
        REQUIRE (static_cast<int> (ons.size()) >= expectedSteps - 1);
        REQUIRE (static_cast<int> (ons.size()) <= expectedSteps);

        const int notes[] = { 0, 2, 4, 5 };
        double worst = 0.0;

        for (std::size_t k = 0; k < ons.size(); ++k)
        {
            REQUIRE (ons[k].note == kC + notes[k % 4]);         // in order, none skipped, none doubled
            REQUIRE (ons[k].legato == (k > 0));                 // the chain never broke
            worst = std::max (worst, std::abs (static_cast<double> (ons[k].sample) - timeOfPpq (0.25 * static_cast<double> (k)) * sr));
        }

        REQUIRE (worst <= c.toleranceSamples);
        REQUIRE (voiceBalance (s.notes()).empty());
        REQUIRE (midiBalance (s.midi).empty());
        REQUIRE (s.allocations == 0);
    }
}

TEST_CASE ("Transport: gates re-time themselves when the tempo changes mid-note", "[transport]")
{
    // A gate of 80 % of a step, started at 120 BPM; the tempo doubles 1000 samples in.
    Session s;
    setSteps (s.seq(), "C . . .");
    s.seq().gateLength = 80;                      // 0.2 quarter notes
    s.host.play();
    s.run (1000, 1000);
    s.host.setTempo (240.0);
    s.run (5000, 64);

    // 1000 samples at 24000 per quarter, then the remaining 0.2 - 1/24 quarters at 12000 per quarter.
    REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (1000 + 1900) });
}

TEST_CASE ("Transport: the sequencer is silent until the host plays, and a stopped host's cursor is ignored", "[transport]")
{
    Session s;
    setSteps (s.seq(), "C D E F");

    for (int i = 0; i < 50; ++i)
    {
        s.host.locate (static_cast<double> (i) * 0.37);     // scrubbing while stopped
        s.run (512, 512);
    }

    REQUIRE (s.notes().empty());
    REQUIRE_FALSE (s.status.running);
    REQUIRE (s.status.internalClock);

    s.host.locate (1.0);
    s.host.play();
    s.run (kStep, 64);
    REQUIRE (s.notes().front() == on (50 * 512, kC));
}
