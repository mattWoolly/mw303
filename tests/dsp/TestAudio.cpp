// The audio output: level, block-size invariance, determinism, sample rates, oversampling
// factors, filter tuning measured from the audio, the coupling corner trim and polarity.
#include "VoiceTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1e-12)); }

std::vector<SeqStep> testPattern()
{
    const auto n = [] (int note, bool accent = false, bool slide = false) { return SeqStep { note, accent, slide, false, false }; };
    SeqStep rest;
    rest.rest = true;
    SeqStep trill = n (41);
    trill.vibrato = true;
    return { n (36, true), n (36), n (48, false, true), n (39, true), n (36), rest, n (36, true), n (36, true),
             n (43, true), n (48, true), n (36, false, true), n (36, false, true), n (34), trill, n (60, true, true), n (36) };
}

/** Frequency of the resonant peak in a steady harmonic spectrum: the strongest harmonic
    of f0 near `around`, refined with a parabola through its neighbours (in dB). */
double resonantPeakHz (const std::vector<float>& x, double fs, double f0, double around)
{
    const int kLo = std::max (2, static_cast<int> (0.5 * around / f0)), kHi = static_cast<int> (2.0 * around / f0);
    std::vector<double> level;
    for (int k = kLo - 1; k <= kHi + 1; ++k)
        level.push_back (db (std::abs (hannBin (x, 0, x.size(), k * f0, fs))));
    std::size_t best = 1;
    for (std::size_t i = 1; i + 1 < level.size(); ++i)
        if (level[i] > level[best]) best = i;
    const double a = level[best - 1], b = level[best], c = level[best + 1];
    const double shift = 0.5 * (a - c) / (a - 2.0 * b + c);
    return (static_cast<double> (kLo - 1) + static_cast<double> (best) + shift) * f0;
}
}

TEST_CASE ("output level: a plain note at default settings peaks near -12 dBFS", "[voice][audio]")
{
    struct Row { int note; int wave; };
    for (const Row& row : { Row { 36, 0 }, Row { 24, 0 }, Row { 48, 0 }, Row { 60, 0 }, Row { 36, 1 } })
    {
        auto p = quietParams();
        p.wave = row.wave;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        skip (v, 0.5);
        const std::vector<SeqStep> pattern (16, SeqStep { row.note, false, false, false, false });
        const auto audio = render (v, makeEvents (pattern, 125.0, 48000.0, 1), 96000, 256);
        const double level = db (peakAbs (audio));
        std::printf ("[level] note %d %s: peak %.1f dBFS\n", row.note, row.wave == 0 ? "saw" : "square", level);
        INFO ("note " << row.note << " wave " << row.wave);
        if (row.note == 36 && row.wave == 0) CHECK (level == Approx (-12.0).margin (1.0));
        else                                 CHECK (level == Approx (-12.0).margin (4.0));
    }

    // Silence before the first note and after a panic.
    dsp::Voice v;
    start (v, quietParams(), 48000.0, 4);
    std::vector<float> quiet (48000);
    v.process (quiet.data(), 48000);
    CHECK (peakAbs (quiet) < 1.0e-6);
    v.noteOn (36, true, false, false);
    v.process (quiet.data(), 2400);
    v.reset();
    v.process (quiet.data(), 48000);
    CHECK (peakAbs (quiet) < 1.0e-6);
    CHECK_FALSE (v.meters().gate);
}

TEST_CASE ("rendering in blocks of 1, 64 and 4096 gives the same samples", "[voice][audio][blocks]")
{
    dsp::VoiceParams p;         // defaults, drift on
    p.resonance = 90.0;
    p.accent = 90.0;
    const auto events = makeEvents (testPattern(), 125.0, 48000.0, 2);
    const long long total = static_cast<long long> (4.0 * 48000.0);

    std::vector<std::vector<float>> renders;
    for (int block : { 1, 64, 4096 })
    {
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        renders.push_back (render (v, events, total, block));
    }
    CHECK (peakAbs (renders[0]) > 0.1);
    CHECK (std::memcmp (renders[0].data(), renders[1].data(), renders[0].size() * sizeof (float)) == 0);
    CHECK (std::memcmp (renders[0].data(), renders[2].data(), renders[0].size() * sizeof (float)) == 0);

    // Deterministic: a second instance, and the same instance after reset(), repeat it exactly.
    dsp::Voice v;
    start (v, p, 48000.0, 4);
    const auto again = render (v, events, total, 64);
    CHECK (std::memcmp (renders[0].data(), again.data(), again.size() * sizeof (float)) == 0);
    v.reset();
    const auto afterReset = render (v, events, total, 64);
    CHECK (std::memcmp (renders[0].data(), afterReset.data(), again.size() * sizeof (float)) == 0);
}

TEST_CASE ("sample rates 44.1, 48, 96 and 192 kHz sound the same", "[voice][audio][rates]")
{
    struct Rate { double fs; int os; };
    const Rate rates[] = { { 48000.0, 4 }, { 44100.0, 4 }, { 96000.0, 2 }, { 192000.0, 1 } };
    const double step = stepSeconds (125.0);

    std::vector<double> reference;
    for (const Rate& r : rates)
    {
        auto p = quietParams();
        p.resonance = 85.0;
        dsp::Voice v;
        start (v, p, r.fs, r.os);
        const auto audio = render (v, makeEvents (testPattern(), 125.0, r.fs, 2), static_cast<long long> (32 * step * r.fs), 128);
        REQUIRE (allFinite (audio));

        // Level of each step of the second bar, in dB.
        std::vector<double> levels;
        for (int k = 16; k < 32; ++k)
            levels.push_back (db (rms (audio, static_cast<std::size_t> (k * step * r.fs), static_cast<std::size_t> ((k + 1) * step * r.fs)) + 1e-6));
        if (reference.empty())
        {
            reference = levels;
            continue;
        }
        double worst = 0.0;
        for (std::size_t k = 0; k < levels.size(); ++k)
            if (reference[k] > -60.0) worst = std::max (worst, std::abs (levels[k] - reference[k]));
        std::printf ("[rates] %.0f Hz x%d: per-step level within %.2f dB of 48 kHz x4\n", r.fs, r.os, worst);
        INFO ("fs " << r.fs);
        CHECK (worst < 1.0);
    }
}

TEST_CASE ("filter tuning measured from the audio is the same at every rate and oversampling", "[voice][audio][rates]")
{
    struct Rate { double fs; int os; };
    double reference = 0.0;
    for (const Rate& r : { Rate { 48000.0, 4 }, Rate { 44100.0, 4 }, Rate { 96000.0, 2 }, Rate { 192000.0, 1 },
                           Rate { 48000.0, 2 }, Rate { 48000.0, 8 }, Rate { 48000.0, 1 } })
    {
        auto p = quietParams();
        p.cutoff = 90.0;
        p.resonance = 100.0;
        p.modOn = true;
        p.modAmpSustain = 100.0;        // hold the amplifier open
        dsp::Voice v;
        start (v, p, r.fs, r.os);
        v.noteOn (24, false, false, false);
        skip (v, 2.5);                  // the envelope is at rest
        Trace t;
        run (v, t, 1.0);
        const double expected = t.taps.back().cutoffHz;
        const double measured = resonantPeakHz (t.audio, r.fs, midiNoteToHz (24), expected);
        if (reference == 0.0) reference = measured;
        std::printf ("[tuning] %.0f Hz x%d: resonant peak %.1f Hz, control path says %.1f Hz\n", r.fs, r.os, measured, expected);
        INFO ("fs " << r.fs << " os " << r.os);
        CHECK (measured == Approx (expected).epsilon (0.04));
        CHECK (measured == Approx (reference).epsilon (0.015));
    }
}

TEST_CASE ("oversampling factors 1, 2, 4 and 8 all run and agree in level", "[voice][audio][oversampling]")
{
    const auto events = makeEvents (testPattern(), 125.0, 48000.0, 1);
    double reference = 0.0;
    for (int os : { 4, 1, 2, 8 })
    {
        auto p = quietParams();
        dsp::Voice v;
        start (v, p, 48000.0, os);
        CHECK (v.getOversampling() == os);
        const auto audio = render (v, events, 96000, 512);
        REQUIRE (allFinite (audio));
        const double level = db (rms (audio, 0, audio.size()));
        if (reference == 0.0) reference = level;
        std::printf ("[oversampling] x%d: rms %.2f dBFS, latency %d samples\n", os, level, v.latencySamples());
        CHECK (level == Approx (reference).margin (0.75));
        if (os == 1) CHECK (v.latencySamples() == 0);
        else         CHECK ((v.latencySamples() == 2 || v.latencySamples() == 3));
        CHECK (v.debugFilter().stats().stateResets == 0);
    }
    // Anything else is rounded down to a supported factor.
    dsp::Voice v;
    v.prepare (48000.0, 512, 3);
    CHECK (v.getOversampling() == 2);
    v.prepare (48000.0, 512, 0);
    CHECK (v.getOversampling() == 1);
    v.prepare (48000.0, 512, 16);
    CHECK (v.getOversampling() == 8);
}

TEST_CASE ("cal_c21 moves the bass corner at the amplifier input (02 F22)", "[voice][audio]")
{
    const auto fundamental = [] (double nanofarads)
    {
        auto p = quietParams();
        p.cutoff = 100.0;
        p.resonance = 0.0;              // the wiper path carries nothing
        p.calC21 = nanofarads;
        p.modOn = true;
        p.modAmpSustain = 100.0;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        v.noteOn (24, false, false, false);
        skip (v, 3.0);
        Trace t;
        run (v, t, 2.0);
        return db (std::abs (hannBin (t.audio, 0, t.size(), midiNoteToHz (24), 48000.0)));
    };
    const double flat = fundamental (31800.0), measuredDefault = fundamental (15.9), schematic = fundamental (10.0);
    std::printf ("[c21] 32.7 Hz fundamental: %.2f dB with 15.9 nF, %.2f dB with 10 nF, against a huge capacitor\n",
                 measuredDefault - flat, schematic - flat);
    // One pole at 45.05 Hz and at 71.6 Hz.
    CHECK (measuredDefault - flat == Approx (-4.62).margin (0.5));
    CHECK (schematic - flat == Approx (-7.63).margin (0.5));
}

TEST_CASE ("the output stage inverts and passes the amplifier's signal (03 #31)", "[voice][audio]")
{
    dsp::Voice v;
    start (v, quietParams(), 48000.0, 4);
    v.noteOn (48, true, false, false);
    Trace t;
    run (v, t, 0.2);
    double xy = 0.0, xx = 0.0, yy = 0.0;
    for (std::size_t i = 0; i < t.size(); ++i)
    {
        const double a = t.taps[i].amplifierOut, b = static_cast<double> (t.audio[i]);
        xy += a * b; xx += a * a; yy += b * b;
    }
    CHECK (xy / std::sqrt (xx * yy) < -0.98);
    CHECK (std::sqrt (yy / xx) == Approx (dsp::Voice::kOutputScale).epsilon (0.05));
}

namespace
{
/** 4th-order Butterworth high-pass (two sections, bilinear), run forward over x. */
std::vector<float> highPassed (const std::vector<float>& x, double fs, double cornerHz)
{
    std::vector<float> y (x.size());
    const double k = std::tan (kPi * cornerHz / fs);
    const double damping[2] = { 2.0 * std::cos (kPi / 8.0), 2.0 * std::cos (3.0 * kPi / 8.0) };
    std::vector<double> in (x.begin(), x.end());
    for (double d : damping)
    {
        const double norm = 1.0 / (1.0 + k * d + k * k);
        const double b0 = norm, b1 = -2.0 * norm, b2 = norm;
        const double a1 = 2.0 * (k * k - 1.0) * norm, a2 = (1.0 - k * d + k * k) * norm;
        double z1 = 0.0, z2 = 0.0;
        for (double& s : in)
        {
            const double out = b0 * s + z1;
            z1 = b1 * s - a1 * out + z2;
            z2 = b2 * s - a2 * out;
            s = out;
        }
    }
    for (std::size_t i = 0; i < y.size(); ++i) y[i] = static_cast<float> (in[i]);
    return y;
}
}

TEST_CASE ("filter ring at the service calibration point (02 F15, F19, F20)", "[voice][audio]")
{
    // Cutoff at centre, resonance full, Env Mod, Decay and Accent at minimum, saw, low C
    // (65.4 Hz). The test node carries the filtered saw with the ring on top of each reset
    // edge; the ring is measured on its own through a 4th-order high-pass at 250 Hz, which
    // removes the saw's first three harmonics (46, 22 and 9 dB down) and leaves the ring at
    // 500 Hz untouched (0.01 dB). Its peak-to-peak swing is the ring amplitude.
    //
    // F19 reads the service sketch (TP6 ringing between about 5.2 and 5.8 V) two ways:
    // document 02 as 0.6 V peak (+-40 %: 0.36 to 0.84 V), document 03 section 6.4 as 0.6 V
    // peak-to-peak. The model reads 0.37 V peak-to-peak at the default trims (0.17 V above
    // the saw, 0.20 V below) and 0.31 to 0.43 V across the ladder load's range, so at the
    // default it just meets the peak-to-peak reading (-39 %) and misses the peak reading by
    // about a factor of two at every legal trim. The model is not tuned to the sketch
    // (architect's decision); the measured value is asserted with a tolerance and the
    // discrepancy with the peak reading is a known gap. The ring period (F15) and the number
    // of cycles it takes to die away (F20: under 5, tolerance 3 to 8) are asserted as
    // documented, at every trim.
    for (double rb : { 20.0, 13.6, 28.0, 35.0 })
    {
        auto p = quietParams();
        p.cutoff = 50.0; p.resonance = 100.0; p.envMod = 0.0; p.decay = 0.0; p.accent = 0.0;
        p.calRb = rb;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        v.noteOn (36, false, false, false);
        skip (v, 1.0);
        // The filter's output node, in volts at the test point, one reading per base sample.
        std::vector<float> volts;
        for (int i = 0; i < 48000; ++i)
        {
            float s;
            v.process (&s, 1);
            volts.push_back (static_cast<float> (dsp::DiodeLadder::outputVolts (v.debugFilter().output())));
        }
        const double ringHz = resonantPeakHz (volts, 48000.0, midiNoteToHz (36), v.debugTaps().cutoffHz);
        const std::vector<float> ring = highPassed (volts, 48000.0, 250.0);
        double ringMax = -1.0e9, ringMin = 1.0e9;
        for (std::size_t i = 4800; i < ring.size(); ++i)
        {
            ringMax = std::max (ringMax, static_cast<double> (ring[i]));
            ringMin = std::min (ringMin, static_cast<double> (ring[i]));
        }
        const double ringPp = ringMax - ringMin, ringPeak = std::max (ringMax, -ringMin);
        double nodeMax = -1.0e9, nodeMin = 1.0e9;
        for (std::size_t i = 4800; i < volts.size(); ++i)
        {
            nodeMax = std::max (nodeMax, static_cast<double> (volts[i]));
            nodeMin = std::min (nodeMin, static_cast<double> (volts[i]));
        }

        // Decay, F20: cycles from the edge until the ring's swing, taken one ring cycle at a
        // time, has fallen below a tenth of its first cycle; the worst over the saw periods
        // in the second half of the second.
        const double sawPeriod = 48000.0 / midiNoteToHz (36);
        const auto cycle = static_cast<std::size_t> (48000.0 / ringHz + 0.5);
        std::size_t edge = 24000;
        while (edge + 1 < ring.size() && ! (ring[edge] > 0.5 * ringMax)) ++edge;    // first big swing after 0.5 s
        int cyclesToDie = 0, periods = 0;
        for (double from = static_cast<double> (edge) - 0.5 * static_cast<double> (cycle); from + sawPeriod < static_cast<double> (ring.size()); from += sawPeriod, ++periods)
        {
            const auto s = static_cast<std::size_t> (from);
            const double first = peakAbs (ring, s, s + cycle);
            int n = 1;
            while (n < 12 && peakAbs (ring, s + static_cast<std::size_t> (n) * cycle, s + static_cast<std::size_t> (n + 1) * cycle) > 0.1 * first) ++n;
            cyclesToDie = std::max (cyclesToDie, n);
        }
        std::printf ("[ring] calibration point, ladder load %.1f k: ring %.0f Hz (period %.2f ms, target 2 +- 0.5), %.3f V p-p (%+.3f / %+.3f), test node %+.3f / %+.3f V, under a tenth after %d cycles\n",
                     rb, ringHz, 1e3 / ringHz, ringPp, ringMax, ringMin, nodeMax, nodeMin, cyclesToDie);
        INFO ("rb " << rb);
        CHECK (1.0 / ringHz == Approx (0.002).margin (0.0005));
        CHECK (periods > 20);
        CHECK (cyclesToDie >= 3);                        // F20: under 5 cycles, 3 to 8 accepted
        CHECK (cyclesToDie <= 8);
        CHECK (ringPp > 0.25);                           // a ring is there at every trim
        CHECK (ringPeak < 0.36);                         // F19 as 0.6 V peak, -40 %: not met, see above
        if (rb == 20.0)
        {
            CHECK (ringPp == Approx (0.6).epsilon (0.4));    // F19 as 0.6 V peak-to-peak, +-40 %
            CHECK (ringPp == Approx (0.37).epsilon (0.1));   // the default, pinned at its measured size
        }
    }
}
