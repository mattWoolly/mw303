#include "InsertTestUtil.h"
#include "acid/fx/Delay.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
fx::DelayParams wetOnly()
{
    fx::DelayParams p;
    p.on = true;
    p.mixPct = 100.0f;
    p.feedbackPct = 0.0f;
    return p;
}

std::size_t firstAbove (const std::vector<float>& v, double thr, std::size_t from = 0)
{
    for (std::size_t i = from; i < v.size(); ++i)
        if (static_cast<double> (std::abs (v[i])) > thr) return i;
    return v.size();
}

Stereo run (fx::Delay& d, const fx::DelayParams& p, Stereo buf, int block = 256)
{
    d.setParams (p);
    d.reset();
    renderConst (d, buf, p, block);
    return buf;
}
}

TEST_CASE ("Delay: synced times land exactly on the sample", "[inserts][delay]")
{
    struct Sync { int num, den; fx::DelayScale scale; };
    const Sync syncs[] = { { 3, 16, fx::DelayScale::Full }, { 1, 8, fx::DelayScale::Dotted }, { 1, 4, fx::DelayScale::Triplet },
                           { 5, 16, fx::DelayScale::Full }, { 1, 32, fx::DelayScale::Triplet }, { 7, 12, fx::DelayScale::Dotted } };

    for (const double sr : kRates)
        for (const double bpm : { 60.0, 97.3, 120.0, 174.0 })
            for (const auto& s : syncs)
            {
                fx::Delay d;
                d.prepare (sr, 256);
                auto p = wetOnly();
                p.sync = true;
                p.bpm = bpm;
                p.leftNum = s.num; p.leftDen = s.den; p.leftScale = s.scale;
                p.rightNum = 1;    p.rightDen = 4;    p.rightScale = fx::DelayScale::Full;

                const double m = s.scale == fx::DelayScale::Dotted ? 1.5 : (s.scale == fx::DelayScale::Triplet ? 2.0 / 3.0 : 1.0);
                const double secL = static_cast<double> (s.num) / s.den * 4.0 * 60.0 / bpm * m;
                const double secR = 0.25 * 4.0 * 60.0 / bpm;
                REQUIRE_THAT (fx::Delay::syncedSeconds (s.num, s.den, s.scale, bpm), WithinRel (secL, 1e-12));
                const auto expL = static_cast<std::size_t> (std::llround (secL * sr));
                const auto expR = static_cast<std::size_t> (std::llround (secR * sr));

                const auto out = run (d, p, impulse (std::max (expL, expR) + 64, 0.5f));
                INFO ("sr " << sr << " bpm " << bpm << " " << s.num << "/" << s.den);
                REQUIRE (d.delaySamples (0) == expL);
                REQUIRE (d.delaySamples (1) == expR);
                REQUIRE (firstAbove (out.l, 1e-3) == expL);
                REQUIRE (firstAbove (out.r, 1e-3) == expR);
                REQUIRE_THAT (out.l[expL], WithinAbs (0.5, 1e-6));
                REQUIRE (! d.timeClamped());
            }
}

TEST_CASE ("Delay: free time applies to both channels, exact to the sample", "[inserts][delay]")
{
    for (const double sr : kRates)
        for (const double t : { 0.01, 0.2, 0.3333, 1.0 })
        {
            fx::Delay d;
            d.prepare (sr, 256);
            auto p = wetOnly();
            p.sync = false;
            p.timeSec = static_cast<float> (t);
            const auto exp = static_cast<std::size_t> (std::llround (static_cast<double> (static_cast<float> (t)) * sr));
            const auto out = run (d, p, impulse (exp + 64, 0.5f));
            REQUIRE (firstAbove (out.l, 1e-3) == exp);
            REQUIRE (firstAbove (out.r, 1e-3) == exp);
        }
}

TEST_CASE ("Delay: feedback sets the echo decay rate", "[inserts][delay]")
{
    const double sr = 48000.0;
    for (const double fb : { 25.0, 50.0, 80.0, 100.0 })
    {
        fx::Delay d;
        d.prepare (sr, 256);
        auto p = wetOnly();
        p.sync = false;
        p.timeSec = 0.1f;
        p.feedbackPct = static_cast<float> (fb);
        const auto out = run (d, p, impulse (static_cast<std::size_t> (sr * 0.75), 0.25f));
        const std::size_t t = 4800;
        double prev = out.l[t];
        REQUIRE_THAT (prev, WithinAbs (0.25, 1e-6));
        for (int k = 2; k <= 6; ++k)
        {
            const double echo = out.l[t * static_cast<std::size_t> (k)];
            INFO ("feedback " << fb << " echo " << k);
            // The 5 Hz DC blocker in the loop takes about 0.07 % per pass.
            REQUIRE_THAT (echo / prev, WithinRel (fb * 0.01, 0.003));
            prev = echo;
        }
    }
}

TEST_CASE ("Delay: the filter acts inside the loop, once per repeat", "[inserts][delay]")
{
    const double sr = 48000.0;
    const double fc = 1000.0;
    struct Probe { fx::DelayFilter type; double hz; };
    const Probe probes[] = { { fx::DelayFilter::LowPass, 4000.0 }, { fx::DelayFilter::LowPass, 500.0 },
                             { fx::DelayFilter::HighPass, 250.0 }, { fx::DelayFilter::BandPass, 2000.0 },
                             { fx::DelayFilter::BandPass, 1000.0 } };
    for (const auto& pr : probes)
    {
        fx::Delay d;
        d.prepare (sr, 256);
        auto p = wetOnly();
        p.sync = false;
        p.timeSec = 0.3f;
        p.feedbackPct = 80.0f;
        p.filterType = pr.type;
        p.filterCutoffHz = static_cast<float> (fc);
        p.filterResoPct = 0.0f;

        // 200 ms tone burst at -12 dBFS.
        Stereo buf (static_cast<std::size_t> (sr * 1.2));
        for (std::size_t i = 0; i < static_cast<std::size_t> (0.2 * sr); ++i)
            buf.l[i] = buf.r[i] = static_cast<float> (0.25 * std::sin (kTwoPi * pr.hz * static_cast<double> (i) / sr));
        const auto out = run (d, p, buf);

        const auto seg = [&] (int k) { const auto s = static_cast<std::size_t> ((0.3 * k + 0.05) * sr); return rms (out.l, s, s + static_cast<std::size_t> (0.1 * sr)); };
        const double e1 = seg (1), e2 = seg (2), e3 = seg (3);

        const double wa = std::tan (kPi * pr.hz / sr) / std::tan (kPi * fc / sr);
        const double k = std::sqrt (2.0);
        const double den = std::sqrt ((1.0 - wa * wa) * (1.0 - wa * wa) + k * k * wa * wa);
        const double h = pr.type == fx::DelayFilter::LowPass ? 1.0 / den
                       : pr.type == fx::DelayFilter::HighPass ? wa * wa / den : k * wa / den;
        INFO ("type " << static_cast<int> (pr.type) << " at " << pr.hz << " Hz: |H| " << h);
        REQUIRE_THAT (e2 / e1, WithinRel (0.8 * h, 0.01));
        REQUIRE_THAT (e3 / e2, WithinRel (0.8 * h, 0.01));
    }
}

TEST_CASE ("Delay: stays bounded at 100 % feedback with any filter and spread", "[inserts][delay]")
{
    const double sr = 48000.0;
    for (const auto type : { fx::DelayFilter::Off, fx::DelayFilter::LowPass, fx::DelayFilter::BandPass, fx::DelayFilter::HighPass })
        for (const float spread : { -100.0f, 0.0f, 60.0f, 100.0f })
        {
            fx::Delay d;
            d.prepare (sr, 512);
            fx::DelayParams p;
            p.on = true;
            p.sync = false;
            p.timeSec = 0.05f;
            p.feedbackPct = 100.0f;
            p.mixPct = 100.0f;
            p.filterType = type;
            p.filterCutoffHz = 700.0f;
            p.filterResoPct = 100.0f;
            p.spreadPct = spread;
            auto buf = noise (static_cast<std::size_t> (sr * 20.0), 3, 1.0);
            for (std::size_t i = static_cast<std::size_t> (sr * 0.5); i < buf.size(); ++i) buf.l[i] = buf.r[i] = 0.0f;
            d.setParams (p);
            d.reset();
            renderConst (d, buf, p, 512);
            REQUIRE (allFinite (buf));
            REQUIRE (peakAbs (buf) <= 2.0);
        }
}

TEST_CASE ("Delay: below 100 % feedback every repeat is quieter, at any resonance", "[inserts][delay]")
{
    // Every loop filter output is normalised to unity peak gain, so the loop gain is at most
    // the feedback amount. Ten repeats later the echoes must be at least feedback^10 down
    // (2 dB of slack for the shape of the spectrum moving between the two windows). Before
    // the normalisation, LP or HP resonance sustained the echoes from 8 % feedback up.
    const double sr = 48000.0;
    const double period = 0.25;
    for (const auto type : { fx::DelayFilter::Off, fx::DelayFilter::LowPass, fx::DelayFilter::BandPass, fx::DelayFilter::HighPass })
        for (const float feedback : { 10.0f, 50.0f, 90.0f })
            for (const float reso : { 0.0f, 50.0f, 100.0f })
                for (const float cutoff : { 400.0f, 3000.0f })
                {
                    fx::Delay d;
                    d.prepare (sr, 512);
                    fx::DelayParams p;
                    p.on = true;
                    p.sync = false;
                    p.timeSec = static_cast<float> (period);
                    p.feedbackPct = feedback;
                    p.mixPct = 100.0f;
                    p.filterType = type;
                    p.filterCutoffHz = cutoff;
                    p.filterResoPct = reso;
                    auto buf = noise (static_cast<std::size_t> (sr * (2.5 + 10.0 * period)), 9, 0.5);
                    for (std::size_t i = static_cast<std::size_t> (sr * 0.2); i < buf.size(); ++i) buf.l[i] = buf.r[i] = 0.0f;
                    const auto out = run (d, p, buf, 512);
                    const auto at = [&] (double t) { return static_cast<std::size_t> (t * sr); };
                    const double early = rms (out.l, at (1.0), at (2.0));
                    const double late = rms (out.l, at (1.0 + 10.0 * period), at (2.0 + 10.0 * period));
                    const double bound = std::pow (feedback * 0.01, 10.0) * dbToGain (2.0);
                    INFO ("type " << static_cast<int> (type) << ", feedback " << feedback << " %, reso " << reso << " %, cutoff "
                                  << cutoff << " Hz: ten repeats on, " << gainToDb (late / early) << " dB (bound "
                                  << gainToDb (bound) << " dB)");
                    CHECK ((late <= early * bound || late < 1.0e-7));
                }
}

TEST_CASE ("Delay: time changes crossfade without clicks", "[inserts][delay]")
{
    const double sr = 48000.0;
    const std::size_t n = static_cast<std::size_t> (sr * 3.0);
    auto buf = sine (n, 220.0, sr, 0.25);
    fx::Delay d;
    d.prepare (sr, 256);
    auto p = wetOnly();
    p.mixPct = 60.0f;
    p.feedbackPct = 50.0f;
    p.sync = false;
    p.timeSec = 0.2f;
    d.setParams (p);
    d.reset();

    renderBlocks (d, buf, 256, [&p] (fx::Delay& dl, int pos) {
        const double t = pos / 48000.0;
        p.sync = t > 2.0;            // free -> synced switch
        p.bpm = 133.0;
        p.timeSec = t < 1.0 ? 0.2f : (t < 1.5 ? 0.37f : 0.011f);
        dl.setParams (p);
    });

    const double steady = maxStep (buf.l, 20000, 48000);
    const double during = maxStep (buf.l, 48000, n);
    INFO ("steady step " << steady << " worst step around changes " << during);
    REQUIRE (during < 1.5 * steady);
}

TEST_CASE ("Delay: times past the buffer clamp and raise the flag", "[inserts][delay]")
{
    fx::Delay d;
    d.prepare (48000.0, 256);
    auto p = wetOnly();
    p.sync = true;
    p.bpm = 40.0;
    p.leftNum = 32; p.leftDen = 1;     // 192 s
    p.rightNum = 1; p.rightDen = 4;
    d.setParams (p);
    REQUIRE (d.timeClamped());
    REQUIRE (d.delaySamples (0) == static_cast<std::size_t> (fx::Delay::kMaxDelaySeconds * 48000.0));
    p.leftNum = 1;
    d.setParams (p);
    REQUIRE (! d.timeClamped());
}

TEST_CASE ("Delay: spread turns a mono source into a ping-pong", "[inserts][delay]")
{
    const double sr = 48000.0;
    for (const float spread : { 100.0f, -100.0f })
    {
        fx::Delay d;
        d.prepare (sr, 256);
        auto p = wetOnly();
        p.sync = false;
        p.timeSec = 0.1f;
        p.feedbackPct = 50.0f;
        p.spreadPct = spread;
        const auto out = run (d, p, impulse (24000, 0.25f));
        const auto& first = spread > 0 ? out.l : out.r;
        const auto& second = spread > 0 ? out.r : out.l;
        REQUIRE_THAT (first[4800], WithinAbs (0.25, 1e-6));
        REQUIRE (std::abs (second[4800]) < 1e-6f);
        REQUIRE (std::abs (first[9600]) < 1e-3f);
        REQUIRE_THAT (second[9600], WithinRel (0.125, 0.01));
        REQUIRE_THAT (first[14400], WithinRel (0.0625, 0.01));
    }

    // Spread 0: independent lines, the echo lands on both sides.
    fx::Delay d;
    d.prepare (sr, 256);
    auto p = wetOnly();
    p.sync = false;
    p.timeSec = 0.1f;
    const auto out = run (d, p, impulse (6000, 0.25f));
    REQUIRE_THAT (out.l[4800], WithinAbs (0.25, 1e-6));
    REQUIRE_THAT (out.r[4800], WithinAbs (0.25, 1e-6));
}

TEST_CASE ("Delay: mix is an equal-power crossfade", "[inserts][delay]")
{
    for (const float mix : { 0.0f, 30.0f, 50.0f, 100.0f })
    {
        fx::Delay d;
        d.prepare (48000.0, 256);
        auto p = wetOnly();
        p.sync = false;
        p.timeSec = 0.01f;
        p.mixPct = mix;
        const auto out = run (d, p, impulse (1000, 0.5f));
        const double m = static_cast<double> (mix) * 0.01 * kPi / 2.0;
        REQUIRE_THAT (out.l[0], WithinAbs (0.5 * std::cos (m), 1e-6));
        REQUIRE_THAT (out.l[480], WithinAbs (0.5 * std::sin (m), 1e-6));
    }
}
