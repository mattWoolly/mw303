// Properties every insert effect must have: transparent bypass, click-free on/off,
// block-size invariance, every sample rate, survival of parameter abuse, no allocation on
// the audio path, and a timing benchmark.
#include "AllocGuard.h"
#include "InsertTestUtil.h"
#include "acid/fx/Chorus.h"
#include "acid/fx/Delay.h"
#include "acid/fx/Eq.h"
#include "acid/fx/Limiter.h"
#include "acid/fx/Reverb.h"
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <limits>

using namespace acid;
using namespace acid::test;

namespace
{
/** A value from lo..hi, or now and then something hostile. */
float wild (Rng& rng, float lo, float hi, bool abuse)
{
    if (abuse)
    {
        const int k = rng.nextInt (12);
        if (k == 0) return std::numeric_limits<float>::quiet_NaN();
        if (k == 1) return std::numeric_limits<float>::infinity();
        if (k == 2) return -std::numeric_limits<float>::infinity();
        if (k == 3) return lo - 1.0e6f;
        if (k == 4) return hi + 1.0e6f;
    }
    return lo + (hi - lo) * rng.nextFloat();
}

int wildInt (Rng& rng, int lo, int hi, bool abuse)
{
    if (abuse && rng.chance (0.2)) return rng.nextInt (2) == 0 ? -1000 : 1000;
    return lo + rng.nextInt (hi - lo + 1);
}

template <typename Fx> struct Traits;

template <> struct Traits<fx::Eq>
{
    using P = fx::EqParams;
    static constexpr const char* name = "eq";
    static constexpr double minRealtime = 100.0;
    static P active()
    {
        P p;
        p.on = true;
        p.lowGainDb = 6.0f;
        p.loMidGainDb = -4.0f;
        p.hiMidGainDb = 3.0f;
        p.highGainDb = -6.0f;
        return p;
    }
    static P random (Rng& r, bool abuse)
    {
        P p;
        p.on = true;
        float* g[] = { &p.lowGainDb, &p.loMidGainDb, &p.hiMidGainDb, &p.highGainDb };
        float* f[] = { &p.lowFreqHz, &p.loMidFreqHz, &p.hiMidFreqHz, &p.highFreqHz };
        float* w[] = { &p.lowWidthOct, &p.loMidWidthOct, &p.hiMidWidthOct, &p.highWidthOct };
        for (int i = 0; i < 4; ++i)
        {
            *g[i] = wild (r, -24.0f, 24.0f, abuse);
            *f[i] = wild (r, 20.0f, 16000.0f, abuse);
            *w[i] = wild (r, 0.5f, 4.0f, abuse);
        }
        return p;
    }
};

template <> struct Traits<fx::Delay>
{
    using P = fx::DelayParams;
    static constexpr const char* name = "delay";
    static constexpr double minRealtime = 100.0;
    static P active()
    {
        P p;
        p.on = true;
        p.feedbackPct = 45.0f;
        p.filterType = fx::DelayFilter::HighPass;
        p.filterCutoffHz = 400.0f;
        p.spreadPct = 50.0f;
        return p;
    }
    static P random (Rng& r, bool abuse)
    {
        P p;
        p.on = true;
        p.sync = r.chance (0.5);
        p.timeSec = wild (r, 0.01f, 1.0f, abuse);
        p.leftNum = wildInt (r, 1, 32, abuse);
        p.leftDen = wildInt (r, 1, 32, abuse);
        p.leftScale = static_cast<fx::DelayScale> (wildInt (r, 0, 2, abuse) & 0xff);
        p.rightNum = wildInt (r, 1, 32, abuse);
        p.rightDen = wildInt (r, 1, 32, abuse);
        p.rightScale = static_cast<fx::DelayScale> (wildInt (r, 0, 2, abuse) & 0xff);
        p.feedbackPct = wild (r, 0.0f, 100.0f, abuse);
        p.filterType = static_cast<fx::DelayFilter> (wildInt (r, 0, 3, abuse) & 0xff);
        p.filterCutoffHz = wild (r, 10.0f, 16000.0f, abuse);
        p.filterResoPct = wild (r, 0.0f, 100.0f, abuse);
        p.spreadPct = wild (r, -100.0f, 100.0f, abuse);
        p.mixPct = wild (r, 0.0f, 100.0f, abuse);
        p.bpm = abuse && r.chance (0.2) ? (r.chance (0.5) ? 0.0 : std::numeric_limits<double>::quiet_NaN()) : 40.0 + 200.0 * r.nextDouble();
        return p;
    }
};

template <> struct Traits<fx::Reverb>
{
    using P = fx::ReverbParams;
    static constexpr const char* name = "reverb";
    static constexpr double minRealtime = 25.0;
    static P active()
    {
        P p;
        p.on = true;
        p.mixPct = 40.0f;
        p.preDelayMs = 20.0f;
        return p;
    }
    static P random (Rng& r, bool abuse)
    {
        P p;
        p.on = true;
        p.preDelayMs = wild (r, 0.0f, 1000.0f, abuse);
        p.earlySizeM = wild (r, 1.1f, 20.5f, abuse);
        p.lateSizeM = wild (r, 5.1f, 51.1f, abuse);
        p.earlyDiffPct = wild (r, 0.0f, 100.0f, abuse);
        p.lateDiffPct = wild (r, 0.0f, 100.0f, abuse);
        p.earlyBrightPct = wild (r, 0.0f, 100.0f, abuse);
        p.lateBrightPct = wild (r, 0.0f, 100.0f, abuse);
        p.feedbackPct = wild (r, 0.0f, 100.0f, abuse);
        p.earlyLatePct = wild (r, 0.0f, 100.0f, abuse);
        p.modPct = wild (r, 0.0f, 100.0f, abuse);
        p.mixPct = wild (r, 0.0f, 100.0f, abuse);
        if (abuse) p.lowCutHz = wild (r, 1.0f, 1000.0f, abuse);
        return p;
    }
};

template <> struct Traits<fx::Chorus>
{
    using P = fx::ChorusParams;
    static constexpr const char* name = "chorus";
    static constexpr double minRealtime = 60.0;
    static P active()
    {
        P p;
        p.on = true;
        p.fatnessPct = 60.0f;
        p.hpPct = 20.0f;
        return p;
    }
    static P random (Rng& r, bool abuse)
    {
        P p;
        p.on = true;
        p.offsetPct = wild (r, 0.0f, 100.0f, abuse);
        p.fatnessPct = wild (r, 0.0f, 100.0f, abuse);
        p.hpPct = wild (r, 0.0f, 100.0f, abuse);
        p.volumeDb = wild (r, -60.0f, 12.0f, abuse);
        p.depthPct = wild (r, 0.0f, 100.0f, abuse);
        p.ratePct = wild (r, 0.0f, 100.0f, abuse);
        p.spatialPct = wild (r, 0.0f, 100.0f, abuse);
        p.mixPct = wild (r, 0.0f, 100.0f, abuse);
        return p;
    }
};

template <> struct Traits<fx::Limiter>
{
    using P = fx::LimiterParams;
    static constexpr const char* name = "limiter";
    static constexpr double minRealtime = 100.0;
    static P active()
    {
        P p;
        p.on = true;
        p.thresholdDb = -12.0f;
        p.ratio = 8.0f;
        p.kneePct = 50.0f;
        p.gainDb = 6.0f;
        p.softClip = true;
        return p;
    }
    static P random (Rng& r, bool abuse)
    {
        P p;
        p.on = true;
        p.kneePct = wild (r, 0.0f, 100.0f, abuse);
        p.ratio = wild (r, 2.0f, 40.0f, abuse);
        p.attackMs = wild (r, 0.1f, 100.0f, abuse);
        p.releaseMs = wild (r, 25.0f, 500.0f, abuse);
        p.thresholdDb = wild (r, -48.0f, 0.0f, abuse);
        p.gainDb = wild (r, 0.0f, 24.0f, abuse);
        p.softClip = r.chance (0.5);
        return p;
    }
};

/** Music-like test input: a decaying 55 Hz saw-ish tone every 1/8 s plus a little noise. */
Stereo program (std::size_t n, double sr)
{
    Stereo s (n);
    Rng rng (99);
    const auto note = static_cast<std::size_t> (sr / 8.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i % note) / sr;
        const double ph = 55.0 * t - std::floor (55.0 * t);
        const double v = 0.4 * (2.0 * ph - 1.0) * std::exp (-t * 12.0) + 0.01 * (2.0 * rng.nextDouble() - 1.0);
        s.l[i] = static_cast<float> (v);
        s.r[i] = static_cast<float> (0.9 * v);
    }
    return s;
}
}

TEMPLATE_TEST_CASE ("Inserts: bypass is bit-transparent, before and after use", "[inserts][common]",
                    fx::Eq, fx::Delay, fx::Reverb, fx::Chorus, fx::Limiter)
{
    using T = Traits<TestType>;
    for (const double sr : kRates)
    {
        TestType fx;
        fx.prepare (sr, 512);
        auto off = T::active();
        off.on = false;
        auto in = program (static_cast<std::size_t> (sr * 0.5), sr);
        auto buf = in;
        renderConst (fx, buf, off, 512);
        REQUIRE (buf.l == in.l);
        REQUIRE (buf.r == in.r);

        // On for a while, then off: once the 5 ms fade is over the output is the input again.
        buf = in;
        const auto on = T::active();
        const auto switchOff = static_cast<int> (sr * 0.25);
        renderBlocks (fx, buf, 512, [&] (TestType& f, int pos) { f.setParams (pos < switchOff ? on : off); });
        const auto fadeEnd = static_cast<std::size_t> (switchOff + static_cast<int> (std::ceil (sr * 0.0051)) + 512);
        for (std::size_t i = fadeEnd; i < in.size(); ++i)
        {
            REQUIRE (buf.l[i] == in.l[i]);
            REQUIRE (buf.r[i] == in.r[i]);
        }
    }
}

TEMPLATE_TEST_CASE ("Inserts: switching on and off is click-free", "[inserts][common]",
                    fx::Eq, fx::Delay, fx::Reverb, fx::Chorus, fx::Limiter)
{
    using T = Traits<TestType>;
    const double sr = 48000.0;
    const auto n = static_cast<std::size_t> (sr * 2.0);
    const auto in = sine (n, 150.0, sr, 0.5);

    TestType ref;
    ref.prepare (sr, 256);
    auto onP = T::active();
    ref.setParams (onP);
    ref.reset();
    auto steadyOn = in;
    renderConst (ref, steadyOn, onP, 256);
    const double stepOn = maxStep (steadyOn.l, n / 2, n);
    const double stepOff = maxStep (in.l, 1, n);

    TestType fx;
    fx.prepare (sr, 256);
    auto buf = in;
    renderBlocks (fx, buf, 256, [&] (TestType& f, int pos) {
        auto p = onP;
        p.on = ((pos / 12000) % 2) == 1;   // toggles every 250 ms
        f.setParams (p);
    });
    const double worst = maxStep (buf.l, 1, n);
    INFO (T::name << ": worst step " << worst << ", steady on " << stepOn << ", off " << stepOff);
    REQUIRE (worst < 1.6 * std::max (stepOn, stepOff));
}

TEMPLATE_TEST_CASE ("Inserts: output does not depend on the block size", "[inserts][common]",
                    fx::Eq, fx::Delay, fx::Reverb, fx::Chorus, fx::Limiter)
{
    using T = Traits<TestType>;
    const double sr = 48000.0;
    const auto n = static_cast<std::size_t> (sr * 1.0);
    const auto in = program (n, sr);

    // Parameters change every 4096 samples (including on/off), the same in every render.
    std::vector<typename T::P> seg;
    Rng rng (2024);
    for (std::size_t i = 0; i <= n / 4096; ++i)
    {
        auto p = T::random (rng, false);
        p.on = (i % 5) != 3;
        seg.push_back (p);
    }

    std::vector<Stereo> outs;
    for (const int block : { 1, 64, 512, 4096 })
    {
        TestType fx;
        fx.prepare (sr, block);
        auto buf = in;
        renderBlocks (fx, buf, block, [&] (TestType& f, int pos) { f.setParams (seg[static_cast<std::size_t> (pos / 4096)]); });
        outs.push_back (std::move (buf));
    }
    for (std::size_t k = 1; k < outs.size(); ++k)
    {
        double worst = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            worst = std::max ({ worst, static_cast<double> (std::abs (outs[k].l[i] - outs[0].l[i])),
                                static_cast<double> (std::abs (outs[k].r[i] - outs[0].r[i])) });
        INFO (T::name << " block variant " << k << ": max difference " << worst);
        REQUIRE (worst == 0.0);
    }
}

TEMPLATE_TEST_CASE ("Inserts: every sample rate from 44.1 to 192 kHz gives the same sound", "[inserts][common]",
                    fx::Eq, fx::Delay, fx::Chorus, fx::Limiter)
{
    using T = Traits<TestType>;
    double ref = 0.0;
    for (const double sr : kRates)
    {
        TestType fx;
        fx.prepare (sr, 512);
        const auto p = T::active();
        fx.setParams (p);
        fx.reset();
        auto buf = sine (static_cast<std::size_t> (sr * 2.0), 300.0, sr, 0.3);
        renderConst (fx, buf, p, 512);
        REQUIRE (allFinite (buf));
        const double level = gainToDb (rms (buf.l, buf.size() / 2, buf.size()));
        if (sr == kRates[0]) ref = level;
        INFO (T::name << " at " << sr << " Hz: " << level << " dB vs " << ref << " dB");
        REQUIRE (std::abs (level - ref) < 0.5);
    }
}

TEST_CASE ("Inserts: reverb decay is the same at every sample rate", "[inserts][common][reverb]")
{
    fx::ReverbParams p;
    p.on = true;
    p.mixPct = 100.0f;
    p.earlyLatePct = 100.0f;
    p.lateBrightPct = 100.0f;
    p.modPct = 0.0f;
    p.feedbackPct = 35.0f;
    const double design = fx::Reverb::designT60 (p.lateSizeM, p.feedbackPct);
    for (const double sr : kRates)
    {
        fx::Reverb rv;
        rv.prepare (sr, 512);
        rv.setParams (p);
        rv.reset();
        auto buf = impulse (static_cast<std::size_t> (sr * (design * 1.2 + 0.5)), 0.5f);
        renderConst (rv, buf, p, 512);
        const double t60 = schroederT60 (buf.l, sr);
        INFO ("sr " << sr << ": T60 " << t60 << " s, design " << design << " s");
        REQUIRE (std::abs (t60 / design - 1.0) < 0.15);
    }
}

TEMPLATE_TEST_CASE ("Inserts: random parameter abuse stays finite and bounded", "[inserts][common]",
                    fx::Eq, fx::Delay, fx::Reverb, fx::Chorus, fx::Limiter)
{
    using T = Traits<TestType>;
    for (const double sr : { 44100.0, 192000.0 })
    {
        TestType fx;
        fx.prepare (sr, 256);
        Rng rng (static_cast<std::uint64_t> (sr));
        auto buf = noise (static_cast<std::size_t> (sr * 5.0), 77, 1.0);
        renderBlocks (fx, buf, 256, [&] (TestType& f, int) {
            auto p = T::random (rng, true);
            p.on = ! rng.chance (0.05);
            f.setParams (p);
            if (rng.chance (0.002)) f.reset();
        });
        INFO (T::name << " at " << sr);
        REQUIRE (allFinite (buf));
        REQUIRE (peakAbs (buf) < 1000.0);
    }
}

TEMPLATE_TEST_CASE ("Inserts: nothing allocates on the audio path", "[inserts][common]",
                    fx::Eq, fx::Delay, fx::Reverb, fx::Chorus, fx::Limiter)
{
    using T = Traits<TestType>;
    const double sr = 48000.0;
    TestType fx;
    fx.prepare (sr, 512);
    auto buf = program (static_cast<std::size_t> (sr * 2.0), sr);
    Rng rng (5);
    std::vector<typename T::P> ps;
    for (int i = 0; i < 200; ++i)
    {
        auto p = T::random (rng, i % 3 == 0);
        p.on = (i % 7) != 0;
        ps.push_back (p);
    }

    std::size_t count = 0;
    {
        ScopedAllocGuard guard;
        int k = 0;
        for (std::size_t pos = 0; pos + 512 <= buf.size(); pos += 512, ++k)
        {
            fx.setParams (ps[static_cast<std::size_t> (k) % ps.size()]);
            fx.process (buf.l.data() + pos, buf.r.data() + pos, 512);
            if (k == 50) fx.reset();
        }
        count = guard.count();
    }
    REQUIRE (count == 0);
}

TEMPLATE_TEST_CASE ("Inserts: timing benchmark", "[inserts][common][.benchmark]",
                    fx::Eq, fx::Delay, fx::Reverb, fx::Chorus, fx::Limiter)
{
    using T = Traits<TestType>;
    const double sr = 48000.0;
    const double seconds = 20.0;
    TestType fx;
    fx.prepare (sr, 256);
    const auto p = T::active();
    auto src = program (static_cast<std::size_t> (sr), sr);
    std::vector<float> l (256), r (256);

    const auto time = [&] (const typename T::P& params) {
        fx.setParams (params);
        fx.reset();
        const auto blocks = static_cast<std::size_t> (seconds * sr / 256.0);
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b = 0; b < blocks; ++b)
        {
            const std::size_t off = (b * 256) % (src.size() - 256);
            std::copy_n (src.l.begin() + static_cast<long> (off), 256, l.begin());
            std::copy_n (src.r.begin() + static_cast<long> (off), 256, r.begin());
            fx.setParams (params);
            fx.process (l.data(), r.data(), 256);
        }
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double> (t1 - t0).count();
    };

    const double onSec = time (p);
    auto offP = p;
    offP.on = false;
    const double offSec = time (offP);
    const double x = seconds / onSec;
    std::printf ("[benchmark] %-8s on: %7.1f x real time (%6.1f ns/stereo sample), off: %9.1f x\n",
                 T::name, x, onSec / (seconds * sr) * 1.0e9, seconds / offSec);
    // Regression floors, well under the figures measured on the build machine (a 2.2 GHz
    // Broadwell Xeon shared with parallel builds): reverb about 48x, chorus 160x, the rest 350x+.
    REQUIRE (x > T::minRealtime);
}
