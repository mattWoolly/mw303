// Review tests: defects found in review, encoded as tests that fail until they are fixed.
#include "InsertTestUtil.h"
#include "acid/fx/Chorus.h"
#include "acid/fx/Delay.h"
#include "acid/fx/Eq.h"
#include "acid/fx/Limiter.h"
#include "acid/fx/Reverb.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace acid;
using namespace acid::test;

// Below 100 % feedback the echoes must die away, whatever the loop filter's resonance.
// Measured: HP 400 Hz, 45 % feedback, 50 % resonance sits at -1 dBFS forever; LP at 100 %
// resonance runs away at 10 % feedback.
TEST_CASE ("review: delay echoes decay below 100 % feedback at any resonance", "[inserts][delay][review]")
{
    const double sr = 48000.0;
    struct Case { fx::DelayFilter type; float cutoff, feedback, reso; };
    for (const Case c : { Case { fx::DelayFilter::HighPass, 400.0f, 45.0f, 50.0f },
                          Case { fx::DelayFilter::HighPass, 400.0f, 30.0f, 75.0f },
                          Case { fx::DelayFilter::LowPass, 1000.0f, 20.0f, 75.0f },
                          Case { fx::DelayFilter::LowPass, 1000.0f, 10.0f, 100.0f } })
    {
        fx::Delay d;
        d.prepare (sr, 512);
        fx::DelayParams p;
        p.on = true;
        p.sync = true;
        p.bpm = 125.0;
        p.leftNum = p.rightNum = 3;
        p.leftDen = p.rightDen = 16;
        p.feedbackPct = c.feedback;
        p.mixPct = 30.0f;
        p.filterType = c.type;
        p.filterCutoffHz = c.cutoff;
        p.filterResoPct = c.reso;
        auto buf = noise (static_cast<std::size_t> (sr * 30.0), 5, 0.25);
        for (std::size_t i = static_cast<std::size_t> (sr * 1.0); i < buf.size(); ++i) buf.l[i] = buf.r[i] = 0.0f;
        d.setParams (p);
        d.reset();
        renderConst (d, buf, p, 512);
        const double tail = std::max (peakAbs (buf.l, static_cast<std::size_t> (sr * 29.0)),
                                      peakAbs (buf.r, static_cast<std::size_t> (sr * 29.0)));
        INFO ("type " << static_cast<int> (c.type) << " fb " << c.feedback << " reso " << c.reso << ": peak 28 s after the input stopped "
                      << 20.0 * std::log10 (std::max (tail, 1e-12)) << " dBFS");
        CHECK (tail < 1.0e-3);   // -60 dBFS
    }
}

// One non-finite sample from upstream must not silence or poison an insert for good.
// Measured: EQ, delay, reverb and chorus output NaN forever after one NaN or Inf input
// sample; the limiter after one Inf.
template <typename Fx, typename P>
static bool recovers (P p, float bad)
{
    const double sr = 48000.0;
    Fx fx;
    fx.prepare (sr, 256);
    fx.setParams (p);
    fx.reset();
    auto buf = sine (static_cast<std::size_t> (sr * 3.0), 220.0, sr, 0.25);
    buf.l[static_cast<std::size_t> (sr)] = bad;
    buf.r[static_cast<std::size_t> (sr)] = bad;
    renderConst (fx, buf, p, 256);
    for (std::size_t i = static_cast<std::size_t> (sr * 2.5); i < buf.size(); ++i)
        if (! std::isfinite (buf.l[i]) || ! std::isfinite (buf.r[i])) return false;
    return peakAbs (buf.l, static_cast<std::size_t> (sr * 2.5)) > 0.01;
}

TEST_CASE ("review: an insert recovers from one non-finite input sample", "[inserts][review]")
{
    for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        INFO ("bad sample " << bad);
        fx::EqParams eq;
        eq.on = true;
        eq.lowGainDb = 6.0f;
        CHECK (recovers<fx::Eq> (eq, bad));
        fx::DelayParams dl;
        dl.on = true;
        CHECK (recovers<fx::Delay> (dl, bad));
        fx::ReverbParams rv;
        rv.on = true;
        CHECK (recovers<fx::Reverb> (rv, bad));
        fx::ChorusParams ch;
        ch.on = true;
        CHECK (recovers<fx::Chorus> (ch, bad));
        fx::LimiterParams lm;
        lm.on = true;
        lm.thresholdDb = -12.0f;
        CHECK (recovers<fx::Limiter> (lm, bad));
    }
}
