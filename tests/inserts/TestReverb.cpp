#include "InsertTestUtil.h"
#include "acid/fx/Reverb.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdio>

using namespace acid;
using namespace acid::test;
using Catch::Matchers::WithinRel;

namespace
{
fx::ReverbParams lateOnly (double size, double fb)
{
    fx::ReverbParams p;
    p.on = true;
    p.mixPct = 100.0f;
    p.earlyLatePct = 100.0f;
    p.lateSizeM = static_cast<float> (size);
    p.feedbackPct = static_cast<float> (fb);
    p.lateBrightPct = 100.0f;
    p.modPct = 0.0f;
    return p;
}

Stereo reverbIr (const fx::ReverbParams& p, double sr, double seconds, float amp = 0.5f)
{
    fx::Reverb rv;
    rv.prepare (sr, 512);
    rv.setParams (p);
    rv.reset();
    auto buf = impulse (static_cast<std::size_t> (seconds * sr), amp);
    renderConst (rv, buf, p, 512);
    return buf;
}
}

TEST_CASE ("Reverb: decay time follows the design and grows with size and feedback", "[inserts][reverb]")
{
    const double sr = 48000.0;
    struct Case { double size, fb; };
    const Case cases[] = { { 8.0, 30.0 }, { 20.0, 30.0 }, { 45.0, 30.0 }, { 27.9, 5.0 }, { 27.9, 25.0 }, { 27.9, 45.0 } };
    double t[6] {};
    for (std::size_t i = 0; i < 6; ++i)
    {
        const double design = fx::Reverb::designT60 (cases[i].size, cases[i].fb);
        const auto ir = reverbIr (lateOnly (cases[i].size, cases[i].fb), sr, design * 1.2 + 0.5);
        t[i] = schroederT60 (ir.l, sr);
        INFO ("size " << cases[i].size << " fb " << cases[i].fb << ": T60 " << t[i] << " s, design " << design << " s");
        REQUIRE_THAT (t[i], WithinRel (design, 0.15));
    }
    REQUIRE (t[0] < t[1]);
    REQUIRE (t[1] < t[2]);
    REQUIRE (t[3] < t[4]);
    REQUIRE (t[4] < t[5]);
}

TEST_CASE ("Reverb: the output carries no DC", "[inserts][reverb]")
{
    const double sr = 48000.0;
    fx::Reverb rv;
    rv.prepare (sr, 512);
    auto p = lateOnly (27.9, 60.0);
    p.earlyLatePct = 50.0f;
    rv.setParams (p);
    rv.reset();

    // A DC step: the wet output must settle back to zero.
    Stereo buf (static_cast<std::size_t> (sr * 4.0));
    for (std::size_t i = 0; i < buf.size(); ++i) buf.l[i] = buf.r[i] = 0.5f;
    renderConst (rv, buf, p, 512);
    double mean = 0.0;
    for (std::size_t i = buf.size() - 24000; i < buf.size(); ++i) mean += static_cast<double> (buf.l[i] + buf.r[i]);
    mean /= 48000.0;
    INFO ("mean after a DC step " << mean);
    REQUIRE (std::abs (mean) < 1e-3);
}

TEST_CASE ("Reverb: stable and finite at every extreme setting", "[inserts][reverb]")
{
    const double sr = 48000.0;
    int combo = 0;
    for (const float esize : { 1.1f, 20.5f })
        for (const float lsize : { 5.1f, 51.1f })
            for (const float fb : { 0.0f, 100.0f })
                for (const float diff : { 0.0f, 100.0f })
                    for (const float bright : { 0.0f, 100.0f })
                        for (const float mod : { 0.0f, 100.0f })
                        {
                            fx::ReverbParams p;
                            p.on = true;
                            p.mixPct = 100.0f;
                            p.earlySizeM = esize;
                            p.lateSizeM = lsize;
                            p.feedbackPct = fb;
                            p.earlyDiffPct = p.lateDiffPct = diff;
                            p.earlyBrightPct = p.lateBrightPct = bright;
                            p.modPct = mod;
                            p.preDelayMs = static_cast<float> (combo % 3) * 500.0f;
                            p.earlyLatePct = static_cast<float> (combo % 2) * 100.0f;
                            ++combo;

                            fx::Reverb rv;
                            rv.prepare (sr, 512);
                            rv.setParams (p);
                            rv.reset();
                            auto buf = noise (static_cast<std::size_t> (sr * 3.0), 11, 1.0);
                            renderConst (rv, buf, p, 512);
                            INFO ("combo " << combo);
                            REQUIRE (allFinite (buf));
                            REQUIRE (peakAbs (buf) < 8.0);
                        }
}

TEST_CASE ("Reverb: impulse response energy is bounded even at maximum decay", "[inserts][reverb]")
{
    const double sr = 48000.0;
    fx::ReverbParams p;
    p.on = true;
    p.mixPct = 100.0f;
    p.earlyLatePct = 100.0f;
    p.lateSizeM = 51.1f;
    p.feedbackPct = 100.0f;
    p.lateBrightPct = 100.0f;
    p.lateDiffPct = 100.0f;
    p.modPct = 100.0f;
    const auto ir = reverbIr (p, sr, 20.0);
    const std::size_t sec = static_cast<std::size_t> (sr);
    const double first = energy (ir.l, 0, sec) + energy (ir.r, 0, sec);
    const double last = energy (ir.l, ir.size() - sec, ir.size()) + energy (ir.r, ir.size() - sec, ir.size());
    const double total = energy (ir.l) + energy (ir.r);
    INFO ("energy first second " << first << ", last second " << last << ", total " << total);
    REQUIRE (allFinite (ir));
    REQUIRE (last < first);
    // Designed T60 is 69 s: 19 s later the level should be about 19/69 * 60 = 16.5 dB down.
    REQUIRE (10.0 * std::log10 (first / last) > 12.0);
    REQUIRE (total < 100.0);
}

TEST_CASE ("Reverb: stereo tails are decorrelated and the mono sum keeps its energy", "[inserts][reverb]")
{
    for (const double sr : kRates)
        for (const double el : { 0.0, 50.0, 100.0 })
        {
            auto p = lateOnly (27.9, 40.0);
            p.earlyLatePct = static_cast<float> (el);
            p.modPct = 20.0f;
            p.lateBrightPct = 60.0f;
            const auto ir = reverbIr (p, sr, 1.5);
            const auto from = static_cast<std::size_t> (0.01 * sr), to = static_cast<std::size_t> (1.2 * sr);
            const double rho = correlation (ir.l, ir.r, from, to);
            std::vector<float> mono (ir.size());
            for (std::size_t i = 0; i < ir.size(); ++i) mono[i] = ir.l[i] + ir.r[i];
            const double ratio = energy (mono, from, to) / (energy (ir.l, from, to) + energy (ir.r, from, to));
            INFO ("sr " << sr << " early/late " << el << ": correlation " << rho << ", mono/stereo energy " << ratio);
            REQUIRE (std::abs (rho) < 0.3);
            REQUIRE (ratio > 0.7);
            REQUIRE (ratio < 1.3);
        }
}

namespace
{
/** Spectral peakiness of a reverb tail: strongest bin of a 0.68 s spectrum (from 150 ms)
    above the median of its own 1/3-octave band, worst band from 200 Hz to 6 kHz.
    An exponentially decaying noise tail measures about 11 dB, a four-comb reverb 43 dB. */
double peakiness (const std::vector<float>& ir, double sr)
{
    const std::size_t n = 1u << 15;
    const auto spec = powerSpectrum (ir, static_cast<std::size_t> (0.15 * sr), n);
    const double binHz = sr / static_cast<double> (n);
    double worst = 0.0;
    for (double lo = 200.0; lo < 6000.0; lo *= std::exp2 (1.0 / 3.0))
    {
        const auto a = static_cast<std::size_t> (lo / binHz), b = static_cast<std::size_t> (lo * std::exp2 (1.0 / 3.0) / binHz);
        std::vector<double> band (spec.begin() + static_cast<long> (a), spec.begin() + static_cast<long> (b));
        std::vector<double> sorted = band;
        std::nth_element (sorted.begin(), sorted.begin() + static_cast<long> (sorted.size() / 2), sorted.end());
        const double median = sorted[sorted.size() / 2];
        for (const double v : band) worst = std::max (worst, 10.0 * std::log10 (v / median));
    }
    return worst;
}
}

namespace
{
/** Persistent modes: the late reverb of continuous noise, cut into 4096-sample frames;
    each frame's spectrum is divided bin by bin by the median of the 1/3 octave around it,
    and the ratios are averaged over the frames. Noise averages out towards 0 dB; a mode
    that stays put does not. Returns the worst averaged bin, 500 Hz to 12 kHz, in dB (below
    500 Hz a third of an octave holds too few bins for a steady median). */
double persistentModeDb (const fx::ReverbParams& p, double sr)
{
    constexpr std::size_t kFrame = 4096;
    constexpr int kFrames = 36;
    const auto start = static_cast<std::size_t> (1.0 * sr);
    auto buf = noise (start + kFrames * kFrame, 17, 0.25);
    fx::Reverb rv;
    rv.prepare (sr, 512);
    rv.setParams (p);
    rv.reset();
    renderConst (rv, buf, p, 512);

    const double binHz = sr / static_cast<double> (kFrame);
    const auto lo = static_cast<std::size_t> (500.0 / binHz), hi = static_cast<std::size_t> (12000.0 / binHz);
    std::vector<double> mean (kFrame / 2, 0.0);
    for (int f = 0; f < kFrames; ++f)
    {
        const auto spec = powerSpectrum (buf.l, start + static_cast<std::size_t> (f) * kFrame, kFrame);
        for (std::size_t k = lo; k < hi; ++k)
        {
            const auto a = static_cast<std::size_t> (static_cast<double> (k) * std::exp2 (-1.0 / 6.0));
            const auto b = static_cast<std::size_t> (static_cast<double> (k) * std::exp2 (1.0 / 6.0)) + 1;
            std::vector<double> band (spec.begin() + static_cast<long> (a), spec.begin() + static_cast<long> (b));
            std::nth_element (band.begin(), band.begin() + static_cast<long> (band.size() / 2), band.end());
            mean[k] += spec[k] / band[band.size() / 2] / kFrames;
        }
    }
    double worst = 0.0;
    for (std::size_t k = lo; k < hi; ++k)
        worst = std::max (worst, 10.0 * std::log10 (mean[k]));
    return worst;
}
}

TEST_CASE ("Reverb: small rooms keep no persistent modes, even with the modulation off", "[inserts][reverb]")
{
    // The smallest late size with modulation 0 used to hold a mode 8.0 to 8.4 dB above its
    // surroundings (6 to 10.5 kHz) in every frame, and 8 m 6.3 to 7.3 dB. Larger rooms
    // measure 5.5 to 6.1 dB on this test: the statistics of noise. Small rooms now keep a
    // floor of tail modulation, and measure the same as the large ones.
    const double sr = 48000.0;
    for (const double size : { 5.1, 8.0, 27.9 })
        for (const double fb : { 60.0, 95.0 })
        {
            auto p = lateOnly (size, fb);
            p.lateBrightPct = 70.0f;
            const double worst = persistentModeDb (p, sr);
            INFO ("size " << size << " m, feedback " << fb << " %, modulation 0: worst averaged bin " << worst << " dB");
            CHECK (worst < 7.0);
        }
}

TEST_CASE ("Reverb: no metallic ringing at any size", "[inserts][reverb]")
{
    const double sr = 48000.0;
    constexpr double kLimitDb = 22.0;

    // The metric must catch a metallic tail: four parallel combs (T60 1.4 s), no diffusion.
    {
        std::vector<float> combs (static_cast<std::size_t> (2.0 * sr), 0.0f);
        for (const std::size_t d : { 1116u, 1188u, 1277u, 1356u })
        {
            const double g = std::pow (10.0, -3.0 * static_cast<double> (d) / (sr * 1.4));
            std::vector<double> y (combs.size(), 0.0);
            for (std::size_t i = 0; i < y.size(); ++i)
            {
                y[i] = (i == 0 ? 1.0 : 0.0) + (i >= d ? g * y[i - d] : 0.0);
                combs[i] += static_cast<float> (y[i]);
            }
        }
        REQUIRE (peakiness (combs, sr) > kLimitDb + 10.0);
    }

    for (const double size : { 5.1, 12.0, 27.9, 51.1 })
        for (const double fb : { 30.0, 60.0, 90.0 })
            for (const double mod : { 0.0, 10.0 })
            {
                auto p = lateOnly (size, fb);
                p.modPct = static_cast<float> (mod);
                p.lateBrightPct = 70.0f;
                const auto ir = reverbIr (p, sr, 2.0);
                const double worst = peakiness (ir.l, sr);
                INFO ("size " << size << " m, feedback " << fb << " %, mod " << mod << " %: " << worst << " dB");
                CHECK (worst < kLimitDb);

                if (fb == 60.0)
                {
                    char name[64];
                    std::snprintf (name, sizeof name, "reverb_ir_%02d_m_mod%02d.wav", static_cast<int> (size), static_cast<int> (mod));
                    dumpWav (name, ir, sr);
                }
            }
}

TEST_CASE ("Reverb: pre-delay holds the whole wet signal back", "[inserts][reverb]")
{
    const double sr = 48000.0;
    for (const float ms : { 0.0f, 37.0f, 250.0f, 1000.0f })
    {
        fx::ReverbParams p;
        p.on = true;
        p.mixPct = 100.0f;
        p.preDelayMs = ms;
        p.earlySizeM = 1.1f;
        const auto ir = reverbIr (p, sr, 1.3);
        const auto pre = static_cast<std::size_t> (std::llround (static_cast<double> (ms) * 0.001 * sr));
        std::size_t first = 1;   // sample 0 carries the (tiny, cos(pi/2)) dry part
        while (first < ir.size() && std::abs (ir.l[first]) < 1e-6f) ++first;
        INFO ("pre-delay " << ms << " ms: first wet sample " << first);
        REQUIRE (first >= pre);
        REQUIRE (first < pre + static_cast<std::size_t> (0.01 * sr));
    }
}

TEST_CASE ("Reverb: size changes glide without clicks", "[inserts][reverb]")
{
    const double sr = 48000.0;
    fx::Reverb rv;
    rv.prepare (sr, 256);
    auto p = lateOnly (10.0, 50.0);
    p.mixPct = 50.0f;
    p.earlyLatePct = 50.0f;
    rv.setParams (p);
    rv.reset();
    auto buf = sine (static_cast<std::size_t> (sr * 3.0), 110.0, sr, 0.25);
    renderBlocks (rv, buf, 256, [&p] (fx::Reverb& r, int pos) {
        const double t = pos / 48000.0;
        p.lateSizeM = t < 1.5 ? 10.0f : 45.0f;
        p.earlySizeM = t < 1.5 ? 3.0f : 18.0f;
        p.preDelayMs = t < 2.0 ? 0.0f : 120.0f;
        r.setParams (p);
    });
    // A click is a burst of high-frequency energy. For a 110 Hz tone (pitch-bent a little
    // while the taps slide) the second difference stays near w^2 times the amplitude; a
    // step of even 0.001 would push the ratio past 10.
    const double w = kTwoPi * 110.0 / sr;
    const auto clickiness = [&] (std::size_t from, std::size_t to) {
        double d2 = 0.0;
        for (std::size_t i = from; i < to; ++i)
            d2 = std::max (d2, static_cast<double> (std::abs (buf.l[i] - 2.0f * buf.l[i - 1] + buf.l[i - 2])));
        return d2 / (w * w * peakAbs (buf.l, from, to));
    };
    const double steady = clickiness (48000, 72000);
    const double during = clickiness (72000, buf.size());
    INFO ("second difference over w^2 * peak: steady " << steady << ", around the changes " << during);
    REQUIRE (steady < 2.0);
    REQUIRE (during < 2.0);
}
