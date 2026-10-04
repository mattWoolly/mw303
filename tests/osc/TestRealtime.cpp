// Real-time rules, determinism, robustness against bad input, and the timing benchmark.
#include "AllocGuard.h"
#include "OscTestSupport.h"
#include "Wav.h"

#include "acid/common/Rng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

TEST_CASE ("oscillator and decimator never allocate", "[osc][realtime]")
{
    dsp::Oscillator osc;
    dsp::Decimator dec;
    std::vector<double> over (8 * 512);     // room for the largest factor
    std::vector<float> out (512);
    Rng rng (3);
    double sink = 0.0;
    std::size_t count = 1;

    {
        ScopedAllocGuard guard;

        // Everything, prepare included, is allocation-free.
        osc.prepare (192000.0);
        dec.prepare (48000.0, 4);

        for (int block = 0; block < 200; ++block)
        {
            if (block % 10 == 0)
            {
                dsp::OscillatorParams p;
                p.sawTopVolts = dsp::OscillatorParams::sawTopFromPwShift (200.0 * rng.nextDouble() - 100.0);
                p.shaperBeta = 100.0 + 300.0 * rng.nextDouble();
                p.flatTopSeconds = block % 20 == 0 ? 20.0e-6 : 0.0;
                p.subOctaves = 1 + (block / 10) % 2;
                p.squareInUse = (block / 10) % 3 != 0;
                osc.setParams (p);
            }
            const double f = expMap (rng.nextDouble(), 20.0, 4000.0);
            const auto needed = static_cast<std::size_t> (dec.factor() * 512);
            for (std::size_t i = 0; i < needed; ++i)
            {
                const auto o = osc.tick (f);
                over[i] = (block & 1) ? o.square : o.saw + 0.3 * o.sub;
            }
            dec.process (over.data(), out.data(), 512);
            sink += static_cast<double> (out[100]);

            if (block == 50) osc.reset();
            if (block == 100) dec.prepare (48000.0, 8, dsp::Decimator::Quality::Ultra);     // factor change on the fly
            if (block == 100) osc.prepare (384000.0);
            if (block == 150) { dec.prepare (44100.0, 2); osc.prepare (88200.0); dec.reset(); }
        }
        sink += dec.processSample (over.data());
        count = guard.count();
    }

    CHECK (count == 0);
    CHECK (std::isfinite (sink));
}

TEST_CASE ("oscillator is deterministic", "[osc][realtime]")
{
    // Two instances fed the same frequencies give bit-identical output, and reset()
    // followed by the same input repeats it.
    dsp::Oscillator a, b;
    a.prepare (176400.0);
    b.prepare (176400.0);
    Rng rng (99);
    std::vector<double> freqs (60000);
    double f = 220.0;
    for (std::size_t i = 0; i < freqs.size(); ++i)
    {
        if (i % 3000 == 0) f = expMap (rng.nextDouble(), 30.0, 2000.0);
        freqs[i] = f * (1.0 + 0.01 * std::sin (0.001 * static_cast<double> (i)));
    }

    std::vector<dsp::OscillatorOutput> first;
    for (double hz : freqs)
    {
        const auto x = a.tick (hz), y = b.tick (hz);
        REQUIRE (x.saw == y.saw);
        REQUIRE (x.square == y.square);
        REQUIRE (x.sub == y.sub);
        first.push_back (x);
    }

    a.reset();
    for (std::size_t i = 0; i < freqs.size(); ++i)
    {
        const auto x = a.tick (freqs[i]);
        REQUIRE (x.saw == first[i].saw);
        REQUIRE (x.square == first[i].square);
        REQUIRE (x.sub == first[i].sub);
    }
}

TEST_CASE ("oscillator survives bad frequencies", "[osc][realtime][robustness]")
{
    dsp::Oscillator osc;
    osc.prepare (192000.0);
    const double bad[] = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), -440.0, 0.0, 1.0e12, 1.0e-300, 96000.0, 86399.9 };
    Rng rng (5);
    for (int i = 0; i < 300000; ++i)
    {
        const double f = (i % 7 == 0) ? bad[static_cast<std::size_t> (rng.nextInt (9))] : 220.0;
        const auto o = osc.tick (f);
        REQUIRE (std::isfinite (o.saw));
        REQUIRE (std::isfinite (o.square));
        REQUIRE (std::isfinite (o.sub));
        REQUIRE (std::abs (o.saw) < 20.0);
        REQUIRE (std::abs (o.square) < 20.0);
    }
    CHECK (osc.getShaper().getStats().nonConverged == 0);

    // It recovers: back at a sane pitch the square is a square again.
    for (int i = 0; i < 96000; ++i) osc.tick (kC3);
    const auto r = render (osc, kC3, 20000);
    CHECK (measureSquare (r.square).duty == Approx (0.465).margin (0.03));

    // An unprepared oscillator is usable too (it assumes 192 kHz).
    dsp::Oscillator fresh;
    const auto o = fresh.tick (110.0);
    CHECK (o.saw == Approx (11.3).margin (0.3));
    CHECK (o.square == Approx (dsp::SquareShaper::kCollectorRailVolts).margin (0.01));
}

TEST_CASE ("no start-up transient: the first samples are already at their levels", "[osc][realtime]")
{
    dsp::Oscillator osc;
    osc.prepare (192000.0);
    const auto o = osc.tick (65.4);
    CHECK (o.saw == Approx (11.3).margin (0.01));
    CHECK (o.square == Approx (dsp::SquareShaper::kCollectorRailVolts).margin (1.0e-6));
    CHECK (std::abs (o.sub) == Approx (dsp::Oscillator::kSubAmplitudeVolts));
    CHECK (dsp::Oscillator::kLatencySamples == 3);
}

TEST_CASE ("rendered examples for listening and plotting", "[osc][wav]")
{
    // Saw, square and sub at C2, then the C2 to C4 jump, decimated 4x to 48 kHz. The DC is
    // taken off before the decimator (the voice's coupling capacitor does that) and 5 V is
    // full scale.
    const double base = 48000.0;
    const int factor = 4;
    dsp::Oscillator osc;
    osc.prepare (base * factor);
    dsp::Decimator ds, dq, db;
    ds.prepare (base, factor);
    dq.prepare (base, factor);
    db.prepare (base, factor);

    const int n = static_cast<int> (base * 2.0);
    std::vector<float> interleaved;
    interleaved.reserve (static_cast<std::size_t> (n) * 3);
    double gs[4], gq[4], gb[4];
    for (int i = 0; i < n; ++i)
    {
        const double hz = i < n / 2 ? kC2 : kC4;
        for (int m = 0; m < factor; ++m)
        {
            const auto o = osc.tick (hz);
            gs[m] = (o.saw - osc.sawMeanVolts()) * 0.2;
            gq[m] = (o.square - dsp::Oscillator::kSquareTypicalMeanVolts) * 0.2;
            gb[m] = o.sub * 0.2;
        }
        interleaved.push_back (static_cast<float> (ds.processSample (gs)));
        interleaved.push_back (static_cast<float> (dq.processSample (gq)));
        interleaved.push_back (static_cast<float> (db.processSample (gb)));
    }
    const std::string path = std::string (ACID_TEST_OUTPUT_DIR) + "/osc-saw-square-sub-C2-C4.wav";
    REQUIRE (writeWav (path, interleaved, 3, base));

    WavData back;
    REQUIRE (readWav (path, back));
    CHECK (back.numChannels == 3);
    CHECK (back.interleaved.size() == interleaved.size());
    float peak = 0.0f;
    for (float v : back.interleaved) peak = std::max (peak, std::abs (v));
    CHECK (peak < 1.0f);
    CHECK (peak > 0.4f);
}

TEST_CASE ("timing benchmark", "[osc][.benchmark]")
{
    using Clock = std::chrono::steady_clock;
    Report rep ("osc-benchmark.txt");
    rep.line ("Oscillator (saw, square and sub together), nanoseconds per oversampled sample, best of 5 runs");
    rep.line ("rate (Hz)   pitch (Hz)   ns/sample   %% of one core at that rate   solves/sample   iterations/solve");

    double worstTypical = 0.0;
    struct Case { double rate; double hz; bool typical; };
    for (const Case c : { Case { 192000.0, 65.41, true }, Case { 192000.0, 130.81, true }, Case { 192000.0, 261.63, true },
                          Case { 192000.0, 523.25, true }, Case { 192000.0, 1046.5, false }, Case { 192000.0, 3520.0, false },
                          Case { 96000.0, 130.81, true }, Case { 384000.0, 130.81, true } })
    {
        dsp::Oscillator osc;
        osc.prepare (c.rate);
        const int n = static_cast<int> (c.rate);    // one second of signal
        for (int i = 0; i < n / 4; ++i) osc.tick (c.hz);
        osc.resetStats();
        double best = 1.0e30, sink = 0.0;
        for (int run = 0; run < 5; ++run)
        {
            const auto t0 = Clock::now();
            for (int i = 0; i < n; ++i)
            {
                const auto o = osc.tick (c.hz);
                sink += o.saw + o.square + o.sub;
            }
            const auto t1 = Clock::now();
            best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count() / n);
        }
        REQUIRE (std::isfinite (sink));
        const auto& st = osc.getShaper().getStats();
        rep.line ("%8.0f     %8.2f     %6.1f            %5.2f                  %.2f            %.2f", c.rate, c.hz, best,
                  best * 1.0e-9 * c.rate * 100.0, static_cast<double> (st.solves) / (5.0 * n),
                  static_cast<double> (st.iterations) / static_cast<double> (std::max<std::uint64_t> (st.solves, 1)));
        if (c.typical) worstTypical = std::max (worstTypical, best * 1.0e-9 * c.rate);
    }

    // The same with the shaper idling (waveform switch on saw).
    double inUse = 0.0, idling = 0.0;
    for (bool use : { true, false })
    {
        dsp::Oscillator osc;
        osc.prepare (192000.0);
        dsp::OscillatorParams p;
        p.squareInUse = use;
        osc.setParams (p);
        const int n = 192000;
        double best = 1.0e30, sink = 0.0;
        for (int run = 0; run < 5; ++run)
        {
            const auto t0 = Clock::now();
            for (int i = 0; i < n; ++i) sink += osc.tick (130.81).saw;
            const auto t1 = Clock::now();
            best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count() / n);
        }
        REQUIRE (std::isfinite (sink));
        (use ? inUse : idling) = best;
    }
    rep.line ("192 kHz, 130.81 Hz, square not in use (shaper idling): %.1f ns/sample, %.2f %% of one core (in use: %.1f ns)",
              idling, idling * 1.0e-9 * 192000.0 * 100.0, inUse);
    CHECK (idling < 0.85 * inUse);

    rep.line ("Decimator, nanoseconds per base-rate output sample, best of 5 runs");
    rep.line ("base rate   factor   quality    ns/output   %% of one core");
    double worstDecimator = 0.0;
    for (double base : { 44100.0, 48000.0 })
        for (int factor : { 2, 4, 8 })
            for (auto quality : { dsp::Decimator::Quality::Standard, dsp::Decimator::Quality::Ultra })
            {
                dsp::Decimator dec;
                dec.prepare (base, factor, quality);
                const int numOut = 48000;
                std::vector<double> in (static_cast<std::size_t> (numOut * factor));
                std::vector<float> out (static_cast<std::size_t> (numOut));
                Rng rng (1);
                for (auto& v : in) v = rng.nextDouble() - 0.5;
                double best = 1.0e30;
                for (int run = 0; run < 5; ++run)
                {
                    const auto t0 = Clock::now();
                    dec.process (in.data(), out.data(), numOut);
                    const auto t1 = Clock::now();
                    best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count() / numOut);
                }
                rep.line ("%8.0f      %d     %-8s    %6.1f        %5.2f", base, factor,
                          quality == dsp::Decimator::Quality::Ultra ? "ultra" : "standard", best, best * 1.0e-9 * base * 100.0);
                worstDecimator = std::max (worstDecimator, best * 1.0e-9 * base);
            }

    // Loose ceilings (the machine is shared while the suite runs): the oscillator must
    // stay under 5 % of a core for notes in the instrument's range, the decimator under 2 %.
    CHECK (worstTypical < 0.05);
    CHECK (worstDecimator < 0.02);
}
