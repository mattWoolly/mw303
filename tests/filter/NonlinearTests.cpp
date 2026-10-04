// Large-signal behaviour of the diode ladder: solver health under abuse, self-oscillation
// limits of the default calibration, sub-audio stability, tuning of the ideal mode and
// the real-time rules. Test names carry the research documents' target numbers
// ("04 Fn": docs/research/04 section 6; "02 Fn": the VCF table of docs/research/02).
#include "AllocGuard.h"
#include "FilterTestSupport.h"

#include "acid/common/Rng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace acid;
using namespace acid::test::filter;
using Catch::Approx;
using dsp::DiodeLadder;
using dsp::DiodeLadderTrims;

namespace
{
struct FreeRun { double level = 0.0, hz = 0.0; };

/** Kicks the filter, then runs it with no input: peak and zero-crossing frequency of the
    output over the last `tail` seconds. */
FreeRun freeRun (DiodeLadder& f, double fRes, double r, double seconds, double tail)
{
    f.reset();
    const double fs = f.getSampleRate();
    const auto n = static_cast<std::size_t> (seconds * fs), from = static_cast<std::size_t> ((seconds - tail) * fs);
    FreeRun result;
    double previous = 0.0, first = -1.0, last = -1.0;
    int count = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double y = f.process (i < 64 ? 0.5 : 0.0, fRes, r);
        if (i >= from)
        {
            result.level = std::max (result.level, std::abs (y));
            if (previous < 0.0 && y >= 0.0)
            {
                const double t = static_cast<double> (i) - y / (y - previous);
                if (first < 0.0) first = t; else { last = t; ++count; }
            }
        }
        previous = y;
    }
    if (count > 0) result.hz = fs * static_cast<double> (count) / (last - first);
    return result;
}

double freeRunningLevel (DiodeLadder& f, double fRes, double r, double seconds, double tail)
{
    return freeRun (f, fRes, r, seconds, tail).level;
}
}

TEST_CASE ("nonlinear: solver torture, random jumps every 10 ms (04 F6)", "[filter][nonlinear]")
{
    for (double fs : { 44100.0, 96000.0, 192000.0 })
    {
        DiodeLadder f;
        f.prepare (fs);
        Rng rng (static_cast<std::uint64_t> (fs));
        bool finite = true;
        double worst = 0.0;
        const int jumps = 1500;         // 15 s
        const int hop = static_cast<int> (0.010 * fs);
        double phase = 0.0;
        for (int j = 0; j < jumps; ++j)
        {
            const double fRes = expMap (rng.nextDouble(), 10.0, 0.45 * fs);
            const double r = 1.2 * rng.nextDouble();
            const double level = 6.0 * rng.nextDouble();
            const double hz = expMap (rng.nextDouble(), 20.0, 2000.0);
            for (int i = 0; i < hop; ++i)
            {
                phase += hz / fs;
                phase -= std::floor (phase);
                const double y = f.process (level * (1.0 - 2.0 * phase), fRes, r);
                finite = finite && std::isfinite (y);
                worst = std::max (worst, std::abs (y));
            }
        }
        const auto& st = f.stats();
        std::printf ("[nonlinear] torture at %.0f Hz: mean %.2f iterations, worst %u, not converged %llu of %llu, resets %llu\n",
                     fs, st.meanIterations(), st.maxIterations, static_cast<unsigned long long> (st.nonConverged),
                     static_cast<unsigned long long> (st.samples), static_cast<unsigned long long> (st.stateResets));
        INFO ("fs " << fs);
        CHECK (finite);
        CHECK (worst <= 1.0);                   // the output is a tanh
        CHECK (st.stateResets == 0);
        CHECK (st.maxIterations <= 8);
        CHECK (static_cast<double> (st.nonConverged) < 1.0e-3 * static_cast<double> (st.samples));
    }

    // Garbage in: NaN and infinities are swallowed, the state stays finite.
    DiodeLadder f;
    f.prepare (192000.0);
    const double bad[] = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), 1.0e300, -1.0e300 };
    for (int i = 0; i < 2000; ++i)
    {
        const double y = f.process (bad[i % 5], bad[(i / 5) % 5], bad[(i / 25) % 5]);
        REQUIRE (std::isfinite (y));
    }
    for (int i = 0; i < 2000; ++i)
        REQUIRE (std::isfinite (f.process (0.1, 1000.0, 0.5)));
}

TEST_CASE ("nonlinear: no self-oscillation below 1.2 kHz at the default calibration (02 F13)", "[filter][nonlinear]")
{
    DiodeLadder f;
    f.prepare (192000.0);
    for (double fRes : { 100.0, 300.0, 600.0, 1000.0, 1200.0 })
    {
        const double level = freeRunningLevel (f, fRes, 1.0, 1.5, 0.3);
        INFO ("fRes " << fRes << " level " << level);
        CHECK (level < 1.0e-4);
    }
    // Report where full resonance does start to sing.
    double onset = 0.0;
    for (double fRes = 1200.0; fRes <= 20000.0; fRes *= 1.05)
        if (freeRunningLevel (f, fRes, 1.0, 1.0, 0.2) > 1.0e-3) { onset = fRes; break; }
    std::printf ("[nonlinear] default calibration, full resonance: self-oscillation starts near %.0f Hz\n", onset);
    CHECK ((onset == 0.0 || onset > 1200.0));
}

TEST_CASE ("nonlinear: no free-running output below 20 Hz at any legal trim (02 F11b)", "[filter][nonlinear]")
{
    DiodeLadder f;
    f.prepare (48000.0);
    int singing = 0;
    for (double rb : { 10.0e3, 13.6e3, 20.0e3, 28.0e3, 35.0e3 })
        for (double kMax : { 17.0, 18.7, 20.0 })
            for (double r : { 1.0, 2.0 })
                for (double fRes : { 30.0, 100.0, 300.0, 1000.0, 5000.0 })
                {
                    f.setTrims (rb, kMax);
                    const FreeRun run = freeRun (f, fRes, r, 6.0, 1.0);
                    INFO ("rb " << rb << " kMax " << kMax << " r " << r << " fRes " << fRes << " level " << run.level << " at " << run.hz << " Hz");
                    // Either the filter has gone quiet, or it is singing in the audio
                    // band near its resonant frequency (high cutoff, or the doubled
                    // feedback of the high-resonance mod). Never a sub-audio motorboat.
                    const bool quiet = run.level < 1.0e-6;
                    if (! quiet) ++singing;
                    CHECK ((quiet || run.hz > 0.5 * fRes));
                    CHECK ((quiet || run.hz > 20.0));
                    if (r == 1.0 && fRes <= 300.0) CHECK (quiet);     // the hottest trims sing from about 1 kHz
                }
    std::printf ("[nonlinear] sub-audio stability: %d of 120 trim and cutoff combinations self-oscillate, all in the audio band\n", singing);
}

TEST_CASE ("nonlinear: the ideal ladder oscillates at the resonant frequency (04 F2)", "[filter][nonlinear]")
{
    for (double fs : { 44100.0, 96000.0, 192000.0 })
    {
        DiodeLadder f;
        f.prepare (fs);
        // The in-loop high-pass is put at 0.05 Hz: at 1 Hz its phase lead alone pulls a
        // 220 Hz oscillation 5 cents sharp.
        f.setTrims (DiodeLadderTrims::idealLadder (17.1, 0.05));
        for (double fRes : { 100.0, 440.0, 3000.0, 12000.0 })
        {
            if (fRes > 0.3 * fs) continue;
            const FreeRun run = freeRun (f, fRes, 1.0, 3.0, 1.0);
            REQUIRE (run.level > 1.0e-4);
            const double centsOff = 1200.0 * std::log2 (run.hz / fRes);
            std::printf ("[nonlinear] ideal ladder at %.0f Hz, fs %.0f: oscillates at %.2f Hz (%+.2f cents)\n", fRes, fs, run.hz, centsOff);
            INFO ("fs " << fs << " fRes " << fRes << " measured " << run.hz);
            CHECK (std::abs (centsOff) < 3.0);
        }
    }
}

TEST_CASE ("nonlinear: against RK4 of the continuous equations (04 F4)", "[filter][nonlinear]")
{
    // A band-limited saw at stock level and hot, fixed cutoff: the trapezoidal solve at
    // 192 kHz against RK4 at 64 times that.
    struct Case { double peak, fRes, r, limitDb; };
    for (const Case& c : { Case { 0.56, 1000.0, 0.8, -60.0 }, Case { 3.0, 1000.0, 0.5, -50.0 } })
    {
        const double fs = 192000.0;
        const int over = 64;                                            // RK4 steps per sample
        const std::size_t n = 19200;                                    // 0.1 s
        // The input on a grid twice as fine as the RK4 steps, for their midpoints.
        const std::vector<double> fine = additiveSaw (110.0, c.peak, 16000.0, fs * over * 2, n * over * 2 + 1);

        DiodeLadder f;
        f.prepare (fs);
        Rk4Ladder ref (f.getTrims(), f.subAudioBypass (c.r));
        const double h = 1.0 / (fs * over);
        double err = 0.0, sig = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            // The filter's sample i is the input at the end of RK4 step group i.
            for (int k = 0; k < over; ++k)
            {
                const std::size_t j = 2 * (i * over + static_cast<std::size_t> (k));
                ref.step (h, fine[j], fine[j + 1], fine[j + 2], c.fRes, c.fRes, c.fRes, c.r);
            }
            const double y = f.process (fine[2 * (i + 1) * over], c.fRes, c.r);
            const double yr = ref.output();
            if (i > n / 4)
            {
                err += (y - yr) * (y - yr);
                sig += yr * yr;
            }
        }
        const double errorDb = 10.0 * std::log10 (err / sig);
        std::printf ("[nonlinear] against RK4: peak %.2f, fRes %.0f, r %.1f: error %.1f dB\n", c.peak, c.fRes, c.r, errorDb);
        CHECK (errorDb < c.limitDb);
    }
}

TEST_CASE ("nonlinear: process, setTrims and reset never allocate", "[filter][realtime]")
{
    DiodeLadder f;
    f.prepare (192000.0);
    std::size_t count = 1;
    double sink = 0.0;
    {
        acid::test::ScopedAllocGuard guard;
        for (int i = 0; i < 20000; ++i)
        {
            if (i % 1000 == 0) f.setTrims (10.0e3 + i, 17.0 + 0.0001 * i);
            if (i % 7000 == 0) f.reset();
            sink += f.process (std::sin (0.01 * i), 200.0 + i, 0.9);
        }
        f.prepare (96000.0);
        count = guard.count();
    }
    CHECK (count == 0);
    CHECK (std::isfinite (sink));
}
