// Bad audio from upstream: one non-finite or absurdly large sample must not latch NaN or
// infinity into any filter, line or detector, in any insert, at extreme settings.
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

namespace
{
constexpr float kBad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), -3.0e38f, 1.0e30f };

/** Runs a 220 Hz tone with a burst of bad samples at 1 s; true when the output is finite
    everywhere and the tone is back by 2.5 s. */
template <typename Fx, typename P>
bool survives (const P& p, float bad)
{
    const double sr = 48000.0;
    Fx fx;
    fx.prepare (sr, 256);
    fx.setParams (p);
    fx.reset();
    auto buf = sine (static_cast<std::size_t> (sr * 3.0), 220.0, sr, 0.25);
    for (std::size_t i = 0; i < 3; ++i)
    {
        buf.l[static_cast<std::size_t> (sr) + i] = bad;
        buf.r[static_cast<std::size_t> (sr) + 2 * i] = bad;
    }
    renderConst (fx, buf, p, 256);
    return allFinite (buf) && peakAbs (buf.l, static_cast<std::size_t> (sr * 2.5)) > 0.01;
}
}

TEST_CASE ("Inserts: bad samples never latch, at extreme settings", "[inserts][robust]")
{
    for (const float bad : kBad)
    {
        INFO ("bad sample " << bad);

        fx::EqParams eq;
        eq.on = true;
        eq.lowGainDb = eq.loMidGainDb = eq.hiMidGainDb = eq.highGainDb = 24.0f;
        eq.lowWidthOct = eq.highWidthOct = 0.5f;
        CHECK (survives<fx::Eq> (eq, bad));

        fx::DelayParams dl;
        dl.on = true;
        dl.feedbackPct = 100.0f;
        dl.filterType = fx::DelayFilter::BandPass;
        dl.filterResoPct = 100.0f;
        dl.spreadPct = 100.0f;
        CHECK (survives<fx::Delay> (dl, bad));

        fx::ReverbParams rv;
        rv.on = true;
        rv.feedbackPct = 100.0f;
        rv.lateSizeM = 51.1f;
        rv.modPct = 100.0f;
        CHECK (survives<fx::Reverb> (rv, bad));

        fx::ChorusParams ch;
        ch.on = true;
        ch.volumeDb = 12.0f;
        ch.fatnessPct = 100.0f;
        CHECK (survives<fx::Chorus> (ch, bad));

        fx::LimiterParams lm;
        lm.on = true;
        lm.thresholdDb = -48.0f;
        lm.gainDb = 24.0f;
        CHECK (survives<fx::Limiter> (lm, bad));
        lm.softClip = true;
        CHECK (survives<fx::Limiter> (lm, bad));
    }
}
