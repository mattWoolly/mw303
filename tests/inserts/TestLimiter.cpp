#include "InsertTestUtil.h"
#include "acid/fx/Limiter.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
fx::LimiterParams lim (double thr, double ratio, double kneePct)
{
    fx::LimiterParams p;
    p.on = true;
    p.thresholdDb = static_cast<float> (thr);
    p.ratio = static_cast<float> (ratio);
    p.kneePct = static_cast<float> (kneePct);
    p.attackMs = 0.1f;
    p.releaseMs = 25.0f;
    return p;
}

Stereo run (fx::Limiter& l, const fx::LimiterParams& p, Stereo buf, int block = 256)
{
    l.setParams (p);
    l.reset();
    renderConst (l, buf, p, block);
    return buf;
}
}

TEST_CASE ("Limiter: static curve matches the soft-knee formula", "[inserts][limiter]")
{
    const double sr = 48000.0;
    struct Case { double thr, ratio, knee; };
    const Case cases[] = { { 0.0, 40.0, 100.0 }, { -12.0, 4.0, 50.0 }, { -24.0, 2.0, 0.0 }, { -48.0, 40.0, 0.0 }, { -6.0, 10.0, 75.0 } };
    for (const auto& c : cases)
        for (double inDb = -60.0; inDb <= 12.0; inDb += 3.0)
        {
            fx::Limiter l;
            l.prepare (sr, 256);
            const double a = dbToGain (inDb);
            Stereo buf (4800);
            for (std::size_t i = 0; i < buf.size(); ++i) { buf.l[i] = static_cast<float> (a); buf.r[i] = static_cast<float> (-0.5 * a); }
            const auto out = run (l, lim (c.thr, c.ratio, c.knee), buf);
            const double w = fx::Limiter::kMaxKneeDb * (1.0 - c.knee * 0.01);
            const double expected = fx::Limiter::staticCurveDb (inDb, c.thr, c.ratio, w);

            // Independent evaluation of the textbook curve.
            const double over = inDb - c.thr;
            double ref = inDb;
            if (2.0 * over >= w && ! (w > 0.0 && 2.0 * std::abs (over) < w)) ref = c.thr + over / c.ratio;
            else if (w > 0.0 && 2.0 * std::abs (over) < w) ref = inDb + (1.0 / c.ratio - 1.0) * (over + w / 2) * (over + w / 2) / (2.0 * w);
            REQUIRE_THAT (expected, WithinAbs (ref, 1e-9));

            INFO ("thr " << c.thr << " ratio " << c.ratio << " knee " << c.knee << " in " << inDb << " dB");
            REQUIRE_THAT (gainToDb (out.l.back()), WithinAbs (expected, 0.01));
            // Stereo-linked: the quieter channel gets the same gain.
            REQUIRE_THAT (out.r.back() / out.l.back(), WithinAbs (-0.5, 1e-5));
        }
}

TEST_CASE ("Limiter: attack and release are one-pole time constants", "[inserts][limiter]")
{
    for (const double sr : kRates)
        for (const double attackMs : { 0.5, 5.0, 50.0 })
            for (const double releaseMs : { 25.0, 150.0, 500.0 })
            {
                fx::Limiter l;
                l.prepare (sr, 256);
                auto p = lim (-20.0, 40.0, 100.0);
                p.attackMs = static_cast<float> (attackMs);
                p.releaseMs = static_cast<float> (releaseMs);

                const auto stepAt = static_cast<std::size_t> (0.01 * sr);
                const auto downAt = stepAt + static_cast<std::size_t> (attackMs * 0.001 * sr * 12.0);
                // The detector holds the last peak for 16 ms; the release starts after it.
                const auto hold = static_cast<std::size_t> (std::lround (0.016 * sr));
                const std::size_t n = downAt + hold + static_cast<std::size_t> (releaseMs * 0.001 * sr * 3.0);
                Stereo buf (n);
                for (std::size_t i = 0; i < n; ++i) buf.l[i] = buf.r[i] = (i >= stepAt && i < downAt) ? 1.0f : 0.01f;
                const auto out = run (l, p, buf);

                // Gain change in dB versus time: 0 dB in -> -19.5 dB target; release back to 0.
                const double full = fx::Limiter::staticCurveDb (0.0, -20.0, 40.0, 0.0);
                const auto gainAt = [&] (std::size_t i) { return gainToDb (out.l[i] / buf.l[i]); };
                std::size_t i = stepAt;
                while (gainAt (i) > full * (1.0 - std::exp (-1.0))) ++i;
                const double tauA = static_cast<double> (i - stepAt + 1) / sr * 1000.0;
                INFO ("sr " << sr << ": attack " << attackMs << " ms measured " << tauA);
                REQUIRE_THAT (tauA, WithinAbs (attackMs, std::max (0.02 * attackMs, 1.5e3 / sr)));

                const double startDb = gainAt (downAt - 1);
                for (i = downAt; i < downAt + hold; ++i)
                    REQUIRE_THAT (gainAt (i), WithinAbs (startDb, 0.01));   // held
                i = downAt + hold;
                while (gainAt (i) < startDb * std::exp (-1.0)) ++i;
                const double tauR = static_cast<double> (i - (downAt + hold)) / sr * 1000.0;
                INFO ("release " << releaseMs << " ms measured " << tauR);
                REQUIRE_THAT (tauR, WithinRel (releaseMs, 0.02));
            }
}

TEST_CASE ("Limiter: with soft clip on the output never exceeds 0 dBFS", "[inserts][limiter]")
{
    const double sr = 48000.0;
    for (const float makeup : { 0.0f, 12.0f, 24.0f })
        for (const float attack : { 0.1f, 100.0f })
        {
            fx::Limiter l;
            l.prepare (sr, 256);
            auto p = lim (0.0, 2.0, 0.0);
            p.gainDb = makeup;
            p.attackMs = attack;
            p.softClip = true;
            auto buf = noise (48000, 9, 16.0);    // +24 dBFS peaks
            for (std::size_t i = 0; i < 4800; ++i) buf.l[i] = buf.r[i] = 0.0f;   // let the detector rest first
            const auto out = run (l, p, buf);
            REQUIRE (peakAbs (out) <= 1.0);
            REQUIRE (allFinite (out));
        }

    // Below the clipper's knee the clipper is exact.
    REQUIRE (fx::Limiter::softClip (0.69) == 0.69);
    REQUIRE (fx::Limiter::softClip (-0.5) == -0.5);
    REQUIRE (fx::Limiter::softClip (1.0e6) <= 1.0);
}

TEST_CASE ("Limiter: reports gain reduction for the meter", "[inserts][limiter]")
{
    fx::Limiter l;
    l.prepare (48000.0, 256);
    const auto p = lim (-12.0, 40.0, 100.0);
    l.setParams (p);
    l.reset();
    Stereo buf (4800);
    for (std::size_t i = 0; i < buf.size(); ++i) buf.l[i] = buf.r[i] = 1.0f;
    l.process (buf.l.data(), buf.r.data(), 4800);
    REQUIRE_THAT (l.gainReductionDb(), WithinAbs (12.0 - 12.0 / 40.0, 0.01));

    Stereo quiet (256);
    for (std::size_t i = 0; i < quiet.size(); ++i) quiet.l[i] = quiet.r[i] = 0.01f;
    for (int k = 0; k < 100; ++k) l.process (quiet.l.data(), quiet.r.data(), 256);
    REQUIRE (l.gainReductionDb() < 0.01f);
}

TEST_CASE ("Limiter: on steady tones the peak hold meets the static curve", "[inserts][limiter]")
{
    // Before the hold, a 1 kHz tone into threshold -12 dB, ratio 40, hard knee came out
    // 0.35 to 0.39 dB above the static curve (the detector released between peaks).
    const double sr = 48000.0;
    for (const double inDb : { 0.0, 12.0 })
    {
        fx::Limiter l;
        l.prepare (sr, 256);
        auto p = lim (-12.0, 40.0, 100.0);
        p.attackMs = 1.0f;
        const auto out = run (l, p, sine (static_cast<std::size_t> (sr), 1000.0, sr, dbToGain (inDb)));
        const double outDb = gainToDb (peakAbs (out.l, static_cast<std::size_t> (0.5 * sr)));
        const double want = fx::Limiter::staticCurveDb (inDb, -12.0, 40.0, 0.0);
        INFO ("in " << inDb << " dB: out " << outDb << " dB, static curve " << want << " dB");
        CHECK (std::abs (outDb - want) < 0.05);
    }
}

TEST_CASE ("Limiter: the attack time holds on a tone", "[inserts][limiter]")
{
    // A 1 kHz tone stepping from 0 to +12 dB with a 10 ms attack: after five time constants
    // the output must be on target. Before the hold it was still 1.4 dB above.
    const double sr = 48000.0;
    fx::Limiter l;
    l.prepare (sr, 256);
    auto p = lim (-12.0, 40.0, 100.0);
    p.attackMs = 10.0f;
    p.releaseMs = 500.0f;
    const auto n = static_cast<std::size_t> (sr);
    auto buf = sine (n, 1000.0, sr, 1.0);
    const auto stepAt = n / 2;
    for (std::size_t i = stepAt; i < n; ++i) { buf.l[i] *= 4.0f; buf.r[i] *= 4.0f; }
    const auto out = run (l, p, buf);
    const auto from = stepAt + static_cast<std::size_t> (0.050 * sr);
    const double outDb = gainToDb (peakAbs (out.l, from, from + static_cast<std::size_t> (0.005 * sr)));
    const double want = fx::Limiter::staticCurveDb (gainToDb (4.0), -12.0, 40.0, 0.0);
    INFO ("50 ms after the step: " << outDb << " dB, target " << want << " dB");
    CHECK (outDb - want < 0.15);
}

TEST_CASE ("Limiter: a fast limiter does not distort a bass tone", "[inserts][limiter]")
{
    // 40 Hz with a 0.1 ms attack and 25 ms release: the detector used to follow the wave
    // itself, and the gain modulation came out as -26 dB of THD.
    const double sr = 48000.0;
    fx::Limiter l;
    l.prepare (sr, 256);
    auto p = lim (-12.0, 40.0, 100.0);
    const auto out = run (l, p, sine (static_cast<std::size_t> (sr), 40.0, sr, 1.0));
    std::vector<float> tail (out.l.begin() + static_cast<long> (sr / 2), out.l.end());
    const double fundamental = dftMagnitude (tail, 40.0, sr);
    double harmonics = 0.0;
    for (int k = 2; k <= 20; ++k)
        harmonics += std::pow (dftMagnitude (tail, 40.0 * k, sr), 2.0);
    const double thd = 10.0 * std::log10 (harmonics) - 20.0 * std::log10 (fundamental);
    INFO ("THD " << thd << " dB");
    CHECK (thd < -50.0);
}
