// Sub-oscillator, and the spectral purity of all three outputs after decimation.
#include "Fft.h"
#include "OscTestSupport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
SpurReport aliasing (double baseRate, int factor, double hz, Waveform wave, double gridHz,
                     dsp::Decimator::Quality quality = dsp::Decimator::Quality::Standard)
{
    // 2^17 base-rate samples through a Kaiser window (beta 30): 0.37 Hz bins, harmonics
    // masked, the largest remaining line below 20 kHz against the largest harmonic.
    const double seconds = 131072.0 / baseRate + 0.01;
    const auto x = renderDecimated (baseRate, factor, hz, seconds, wave, {}, quality);
    const auto mag = windowedSpectrum (x);
    return findSpurs (mag, (mag.size() - 1) * 2, baseRate, gridHz, 20000.0);
}
}

TEST_CASE ("sub-oscillator: pitch, level and alignment", "[osc][sub]")
{
    const double rate = 192000.0;

    for (int octaves : { 1, 2 })
    {
        dsp::OscillatorParams p;
        p.subOctaves = octaves;
        const double hz = kC3;
        const double subHz = hz / (octaves == 1 ? 2.0 : 4.0);
        const auto r = renderSettled (rate, hz, 1.4, p);

        // Pitch: exactly one or two octaves below the saw.
        CHECK (std::abs (cents (measureFrequency (r.sub, rate), subHz)) < 0.5);

        // Level: a symmetric square of +-1.5 V around zero.
        const auto [mn, mx] = std::minmax_element (r.sub.begin(), r.sub.end());
        const double amp = dsp::Oscillator::kSubAmplitudeVolts;
        CHECK (*mx == Approx (amp).margin (0.15));         // band-limited edges overshoot by 4 % of the step
        CHECK (*mn == Approx (-amp).margin (0.15));
        const auto up = crossings (r.sub, 0.0, true), down = crossings (r.sub, 0.0, false);
        REQUIRE (up.size() > 4);
        REQUIRE (down.size() > 4);
        const double period = up[2] - up[1];
        const double high = (down[1] > up[1] ? down[1] : down[2]) - up[1];
        CHECK (high / period == Approx (0.5).margin (1.0e-4));

        // Flat tops, and a mean of zero over whole cycles.
        std::size_t flat = 0;
        for (double v : r.sub)
            if (std::abs (std::abs (v) - amp) < 1.0e-9) ++flat;
        CHECK (static_cast<double> (flat) / static_cast<double> (r.sub.size()) > 0.95);
        double sum = 0.0;
        const auto a = static_cast<std::size_t> (up[1]), b = static_cast<std::size_t> (up[up.size() - 2]);
        for (std::size_t i = a; i < b; ++i) sum += r.sub[i];
        CHECK (std::abs (sum / static_cast<double> (b - a)) < 0.01);

        // Fundamental: 4/pi times the amplitude, which is the saw's fundamental within 1 dB.
        const double subH1 = toneAmplitude (r.sub, rate, subHz);
        const double sawH1 = toneAmplitude (r.saw, rate, hz);
        CHECK (subH1 == Approx (4.0 / kPi * amp).epsilon (0.002));
        CHECK (std::abs (20.0 * std::log10 (subH1 / sawH1)) < 1.0);
        // No even harmonics in a 50 % square, and the third is a third.
        CHECK (toneAmplitude (r.sub, rate, 2.0 * subHz) < 1.0e-4 * subH1);
        CHECK (toneAmplitude (r.sub, rate, 3.0 * subHz) == Approx (subH1 / 3.0).epsilon (0.002));

        // Every edge of the sub coincides with a saw reset.
        const auto sawRise = crossings (r.saw, 8.4, true);
        for (std::size_t k = 1; k + 1 < up.size(); ++k)
        {
            const auto it = std::lower_bound (sawRise.begin(), sawRise.end(), up[k] - 1.5);
            REQUIRE (it != sawRise.end());
            CHECK (std::abs (*it - up[k]) < 1.0);
        }
    }

    SECTION ("switching the octave does not disturb the dividers or the saw")
    {
        dsp::Oscillator a, b;
        a.prepare (rate);
        b.prepare (rate);
        dsp::OscillatorParams two;
        two.subOctaves = 2;
        for (int i = 0; i < 50000; ++i)
        {
            if (i == 20000) b.setParams (two);
            if (i == 35000) b.setParams ({});
            const auto x = a.tick (kC3), y = b.tick (kC3);
            REQUIRE (x.saw == y.saw);
            REQUIRE (x.square == y.square);
            if (i < 20000 || i > 35010) REQUIRE (x.sub == y.sub);
        }
    }
}

TEST_CASE ("band-limited edges keep their harmonics level up to 20 kHz", "[osc][sub][response]")
{
    // The sub is an ideal 50 % square, so its odd harmonics should be 4 A / (pi n). The
    // band-limiting kernel alone would droop by 0.6 dB at 20 kHz (at 192 kHz); with its
    // equaliser the harmonics stay level.
    Report rep ("osc-harmonic-flatness.txt");
    rep.line ("Largest deviation of the sub-oscillator's odd harmonics from 4A/(pi n), 0 to 20 kHz");
    struct Case { double rate; double limitDb; };
    for (const Case c : { Case { 384000.0, 0.01 }, Case { 192000.0, 0.05 }, Case { 176400.0, 0.07 }, Case { 96000.0, 0.7 }, Case { 88200.0, 0.9 } })
    {
        const double hz = 1000.0, subHz = 500.0;
        const auto r = renderSettled (c.rate, hz, 0.5);
        double worst = 0.0;
        for (int n = 1; n * subHz <= 20000.0; n += 2)
        {
            const double measured = toneAmplitude (r.sub, c.rate, n * subHz);
            const double ideal = 4.0 * dsp::Oscillator::kSubAmplitudeVolts / (kPi * n);
            worst = std::max (worst, std::abs (20.0 * std::log10 (measured / ideal)));
        }
        rep.line ("rate %6.0f Hz: %.3f dB", c.rate, worst);
        INFO ("rate " << c.rate);
        CHECK (worst < c.limitDb);
    }
}

TEST_CASE ("aliasing after decimation, 4x oversampling", "[osc][aliasing]")
{
    // Target: every component that is not a harmonic stays below -80 dBc in the audible
    // band. Pitches are chosen so that folded components do not land on harmonics.
    Report rep ("osc-aliasing.txt");
    rep.line ("Worst non-harmonic component below 20 kHz, relative to the strongest harmonic (dBc)");
    rep.line ("base rate   factor   pitch (Hz)      saw      (at Hz)    square    (at Hz)       sub    (at Hz)");

    for (double base : { 48000.0, 44100.0 })
        for (double hz : { 110.0, 440.0, 1046.5, 1760.0, 3520.0 })
        {
            const auto saw = aliasing (base, 4, hz, Waveform::Saw, hz);
            const auto square = aliasing (base, 4, hz, Waveform::Square, hz);
            const auto sub = aliasing (base, 4, hz, Waveform::Sub, hz * 0.5);
            rep.line ("%8.0f      4x     %8.1f     %7.1f   (%6.0f)   %7.1f   (%6.0f)   %7.1f   (%6.0f)", base, hz,
                      saw.worstSpurDbc, saw.worstSpurHz, square.worstSpurDbc, square.worstSpurHz, sub.worstSpurDbc, sub.worstSpurHz);
            INFO (base << " Hz base, " << hz << " Hz");
            CHECK (saw.worstSpurDbc < -80.0);
            CHECK (square.worstSpurDbc < -80.0);
            CHECK (sub.worstSpurDbc < -80.0);
            // The research's own acceptance figure (O2) for notes up to 1760 Hz.
            if (hz <= 1760.0)
            {
                CHECK (saw.worstSpurDbc < -90.0);
                CHECK (square.worstSpurDbc < -90.0);
            }
        }
}

TEST_CASE ("aliasing after decimation, other oversampling factors", "[osc][aliasing]")
{
    Report rep ("osc-aliasing-other-factors.txt");
    rep.line ("Worst non-harmonic component below 20 kHz (dBc)");
    rep.line ("base rate   factor   quality    pitch (Hz)      saw     square");

    struct Config { double base; int factor; dsp::Decimator::Quality quality; double limitLow, limitHigh; };
    const Config configs[] = {
        { 48000.0, 8, dsp::Decimator::Quality::Standard, -100.0, -90.0 },
        { 48000.0, 8, dsp::Decimator::Quality::Ultra, -100.0, -90.0 },
        { 44100.0, 4, dsp::Decimator::Quality::Ultra, -90.0, -80.0 },
        { 48000.0, 2, dsp::Decimator::Quality::Standard, -85.0, -70.0 },
        { 44100.0, 2, dsp::Decimator::Quality::Standard, -80.0, -70.0 },
        { 96000.0, 2, dsp::Decimator::Quality::Standard, -90.0, -80.0 },
        { 192000.0, 1, dsp::Decimator::Quality::Standard, -90.0, -80.0 },
    };
    for (const auto& c : configs)
        for (double hz : { 110.0, 440.0, 1760.0 })
        {
            const auto saw = aliasing (c.base, c.factor, hz, Waveform::Saw, hz, c.quality);
            const auto square = aliasing (c.base, c.factor, hz, Waveform::Square, hz, c.quality);
            rep.line ("%8.0f      %dx     %-8s   %8.1f     %7.1f   %7.1f", c.base, c.factor,
                      c.quality == dsp::Decimator::Quality::Ultra ? "ultra" : "standard", hz, saw.worstSpurDbc, square.worstSpurDbc);
            INFO (c.base << " Hz base, factor " << c.factor << ", " << hz << " Hz");
            const double limit = hz <= 440.0 ? c.limitLow : c.limitHigh;
            CHECK (saw.worstSpurDbc < limit);
            CHECK (square.worstSpurDbc < limit);
        }
}

TEST_CASE ("frequency modulation every sample leaves no artefacts", "[osc][fm]")
{
    // Linear FM by a sine: the frequency changes on every oversampled sample. The phase is
    // then 441 t + a cos (2 pi 7 t), the waveform is exactly periodic at 7 Hz, and every
    // true component sits on a multiple of 7 Hz. Anything off that grid is an artefact
    // (a click, a discontinuity, aliasing).
    const double base = 48000.0;
    const int factor = 4;
    const double rate = base * factor;
    const double carrier = 441.0, modHz = 7.0;
    Report rep ("osc-fm.txt");

    for (double depthHz : { 30.0, 200.0 })
    {
        dsp::Oscillator osc;
        osc.prepare (rate);
        dsp::Decimator decSaw, decSquare;
        decSaw.prepare (base, factor);
        decSquare.prepare (base, factor);

        const int settle = static_cast<int> (base);
        const int n = 262144;
        std::vector<double> saw (static_cast<std::size_t> (n)), square (static_cast<std::size_t> (n));
        double gs[4], gq[4];
        long t = 0;
        for (int i = -settle; i < n; ++i)
        {
            for (int m = 0; m < factor; ++m, ++t)
            {
                const double f = carrier + depthHz * std::sin (kTwoPi * modHz * static_cast<double> (t) / rate);
                const auto o = osc.tick (f);
                REQUIRE (std::isfinite (o.saw));
                gs[m] = o.saw;
                gq[m] = o.square;
            }
            const double ys = decSaw.processSample (gs), yq = decSquare.processSample (gq);
            if (i >= 0)
            {
                saw[static_cast<std::size_t> (i)] = ys;
                square[static_cast<std::size_t> (i)] = yq;
            }
        }

        const auto ms = windowedSpectrum (saw), mq = windowedSpectrum (square);
        const std::size_t fftSize = (ms.size() - 1) * 2;
        const auto rs = findSpurs (ms, fftSize, base, modHz, 20000.0, 11.0);
        const auto rq = findSpurs (mq, fftSize, base, modHz, 20000.0, 11.0);
        rep.line ("FM 441 Hz +- %.0f Hz at 7 Hz, 4 x 48 kHz: worst off-grid component saw %.1f dBc (%.0f Hz), square %.1f dBc (%.0f Hz)",
                  depthHz, rs.worstSpurDbc, rs.worstSpurHz, rq.worstSpurDbc, rq.worstSpurHz);
        CHECK (rs.worstSpurDbc < -80.0);
        CHECK (rq.worstSpurDbc < -80.0);
    }
}

TEST_CASE ("a fast glide follows the frequency input sample by sample", "[osc][fm]")
{
    // An octave slide in 5 ms: the number of cycles is the integral of the frequency, and
    // between resets the saw equals the follower map of the integrated phase.
    const double rate = 192000.0;
    dsp::Oscillator osc;
    osc.prepare (rate);
    const int n = static_cast<int> (0.3 * rate);

    double phase = 0.0;
    std::vector<double> expected (static_cast<std::size_t> (n)), out (static_cast<std::size_t> (n));
    std::vector<char> nearReset (static_cast<std::size_t> (n), 0);
    for (int i = 0; i < n; ++i)
    {
        const double seconds = static_cast<double> (i) / rate;
        const double f = 110.0 * std::exp2 (std::min (1.0, seconds / 0.005) + 0.3 * std::sin (kTwoPi * 40.0 * seconds));
        out[static_cast<std::size_t> (i)] = osc.tick (f).saw;
        phase += f / rate;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            for (int k = std::max (0, i - 4); k <= std::min (n - 1, i + 4); ++k) nearReset[static_cast<std::size_t> (k)] = 1;
        }
        expected[static_cast<std::size_t> (i)] = osc.sawVoltsAtPhase (phase);
    }

    double worst = 0.0;
    const int delay = dsp::Oscillator::kLatencySamples;
    for (int i = 0; i + delay < n; ++i)
        if (! nearReset[static_cast<std::size_t> (i)])
            worst = std::max (worst, std::abs (out[static_cast<std::size_t> (i + delay)] - expected[static_cast<std::size_t> (i)]));
    INFO ("worst deviation from the ideal ramp " << worst << " V");
    CHECK (worst < 1.0e-6);
}
