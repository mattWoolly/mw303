// The effects chain: parameter mapping, ordering, bypass, transitions, real-time safety.
#include "../dist/DistTestUtil.h"
#include "AllocGuard.h"
#include "Wav.h"
#include "acid/fx/FxChain.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace acid;
using namespace acid::fx;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

struct Stereo
{
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
};

Stereo fromMono (const std::vector<float>& mono)
{
    return { mono, mono };
}

/** Renders buf in place in blocks; before each block perBlock (chain, position) may change
    parameters. */
template <typename PerBlock>
void renderBlocks (FxChain& chain, Stereo& buf, int blockSize, PerBlock&& perBlock)
{
    const int n = static_cast<int> (buf.size());
    for (int pos = 0; pos < n; pos += blockSize)
    {
        const int len = std::min (blockSize, n - pos);
        perBlock (chain, pos);
        chain.process (buf.l.data() + pos, buf.r.data() + pos, len);
    }
}

void renderConst (FxChain& chain, Stereo& buf, const FxParams& p, int blockSize = 256)
{
    renderBlocks (chain, buf, blockSize, [&p] (FxChain& c, int) { c.setParams (p); });
}

/** A chain prepared at fs with p applied instantly (no fades). */
std::unique_ptr<FxChain> makeChain (const FxParams& p, double fs = kFs, int oversampling = 1)
{
    auto chain = std::make_unique<FxChain>();
    chain->setDistortionOversampling (oversampling);
    chain->prepare (fs, 512);
    chain->setParams (p);
    chain->reset();
    return chain;
}

double maxAbsDiff (const Stereo& a, const Stereo& b, std::size_t from = 0)
{
    double m = 0.0;
    for (std::size_t i = from; i < a.size(); ++i)
        m = std::max ({ m, static_cast<double> (std::abs (a.l[i] - b.l[i])), static_cast<double> (std::abs (a.r[i] - b.r[i])) });
    return m;
}

bool bitIdentical (const Stereo& a, const Stereo& b)
{
    return a.l == b.l && a.r == b.r;
}

bool allFinite (const Stereo& s)
{
    return disttest::allFinite (s.l) && disttest::allFinite (s.r);
}

double maxStep (const Stereo& s, std::size_t from, std::size_t to)
{
    double m = 0.0;
    for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, s.size()); ++i)
        m = std::max ({ m, static_cast<double> (std::abs (s.l[i] - s.l[i - 1])), static_cast<double> (std::abs (s.r[i] - s.r[i - 1])) });
    return m;
}

void dumpWav (const std::string& name, const Stereo& s, double fs)
{
    std::vector<float> inter (s.size() * 2);
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        inter[2 * i] = s.l[i];
        inter[2 * i + 1] = s.r[i];
    }
    acid::test::writeWav (std::string (ACID_TEST_OUTPUT_DIR) + "/" + name, inter, 2, fs);
}

/** Every block on, at settings where each one does something audible. */
FxParams allOn()
{
    FxParams p = fxParamsFrom (defaultParamValues(), Settings {}, 120.0);
    p.distortion.on = true;
    p.distortion.drive = 60.0f;
    p.eq.on = true;
    p.eq.loMidGainDb = 6.0f;
    p.eq.highGainDb = -6.0f;
    p.delay.on = true;
    p.delay.rightNum = 5;
    p.delay.spreadPct = 40.0f;
    p.reverb.on = true;
    p.reverb.mixPct = 35.0f;
    p.chorus.on = true;
    p.chorus.fatnessPct = 50.0f;
    p.limiter.on = true;
    p.limiter.thresholdDb = -12.0f;
    p.limiter.gainDb = 6.0f;
    p.limiter.softClip = true;
    return p;
}

/** Every field of FxParams as a flat list of numbers. A field left out here would make
    the parameter walk below report its row as unconsumed, so the list is self-checking. */
std::vector<double> fingerprint (const FxParams& p)
{
    const auto& d = p.distortion;
    const auto& e = p.eq;
    const auto& y = p.delay;
    const auto& r = p.reverb;
    const auto& c = p.chorus;
    const auto& l = p.limiter;
    const auto f = [] (auto v) { return static_cast<double> (v); };
    std::vector<double> v {
        f (d.on), f (d.model), f (d.dynamics), f (d.dynPost), f (d.drive), f (d.color), f (d.clipper), f (d.preMin),
        f (d.preMax), f (d.knee), f (d.bias), f (d.lowKeep), f (d.preHp), f (d.preEqOn), f (d.preEqGain), f (d.preEqFreq),
        f (d.preEqWidth), f (d.tiltOn), f (d.tiltAmt), f (d.tiltFreq), f (d.colorFreq), f (d.colorReso), f (d.colorRange),
        f (d.postHp), f (d.volume),
        f (e.on), f (e.lowGainDb), f (e.lowFreqHz), f (e.lowWidthOct), f (e.loMidGainDb), f (e.loMidFreqHz),
        f (e.loMidWidthOct), f (e.hiMidGainDb), f (e.hiMidFreqHz), f (e.hiMidWidthOct), f (e.highGainDb),
        f (e.highFreqHz), f (e.highWidthOct),
        f (y.on), f (y.sync), f (y.timeSec), f (y.leftNum), f (y.leftDen), f (static_cast<int> (y.leftScale)),
        f (y.rightNum), f (y.rightDen), f (static_cast<int> (y.rightScale)), f (y.feedbackPct),
        f (static_cast<int> (y.filterType)), f (y.filterCutoffHz), f (y.filterResoPct), f (y.spreadPct), f (y.mixPct),
        f (y.bpm),
        f (r.on), f (r.preDelayMs), f (r.earlySizeM), f (r.lateSizeM), f (r.earlyDiffPct), f (r.lateDiffPct),
        f (r.earlyBrightPct), f (r.lateBrightPct), f (r.feedbackPct), f (r.earlyLatePct), f (r.modPct), f (r.mixPct),
        f (r.lowCutHz),
        f (c.on), f (c.offsetPct), f (c.fatnessPct), f (c.hpPct), f (c.volumeDb), f (c.depthPct), f (c.ratePct),
        f (c.spatialPct), f (c.mixPct),
        f (l.on), f (l.kneePct), f (l.ratio), f (l.attackMs), f (l.releaseMs), f (l.thresholdDb), f (l.gainDb),
        f (l.softClip),
        f (p.bypass), f (p.masterVolumeDb), f (p.bpm)
    };
    for (const auto s : p.order)
        v.push_back (f (s));
    return v;
}

bool isFxGroup (const std::string& g)
{
    return g == "Distortion" || g == "Distortion Voicing" || g == "EQ" || g == "Delay" || g == "Reverb"
           || g == "Chorus" || g == "Limiter" || g == "Master";
}

/** A value of the row clearly different from v and inside the row's range. */
float perturb (const ParamInfo& info, float v)
{
    switch (info.kind)
    {
        case ParamKind::Bool: return v >= 0.5f ? 0.0f : 1.0f;
        case ParamKind::Int:
        case ParamKind::Choice: return v + 1.0f <= info.max ? v + 1.0f : v - 1.0f;
        case ParamKind::Float: break;
    }
    const float span = info.max - info.min;
    const float a = info.min + 0.37f * span;
    return std::abs (a - v) > 0.05f * span ? a : info.min + 0.73f * span;
}

void setValue (ParamValues& v, ParamId id, float x)
{
    v[static_cast<std::size_t> (id)] = x;
}

/** Values that put every block on with every control in play. */
ParamValues walkBase (bool sync)
{
    ParamValues v = defaultParamValues();
    for (const ParamId id : { kDistOn, kEqOn, kDelayOn, kRevOn, kChoOn, kLimOn, kDistPreEqOn, kDistTiltOn })
        setValue (v, id, 1.0f);
    setValue (v, kDistDynamics, 50.0f);
    setValue (v, kDistColor, 40.0f);
    setValue (v, kDistPreEqGain, 6.0f);
    setValue (v, kDistTiltAmt, 6.0f);
    setValue (v, kEqLowGain, 3.0f);
    setValue (v, kEqLoMidGain, -3.0f);
    setValue (v, kEqHiMidGain, 4.0f);
    setValue (v, kEqHighGain, -4.0f);
    setValue (v, kDelaySync, sync ? 1.0f : 0.0f);
    setValue (v, kDelayTime, 0.1f);
    setValue (v, kDelayLNum, 1.0f);
    setValue (v, kDelayRNum, 1.0f);
    setValue (v, kDelayFiltType, 1.0f);
    setValue (v, kDelayFiltCutoff, 2000.0f);
    setValue (v, kDelayFeedback, 60.0f);
    setValue (v, kChoFatness, 50.0f);
    setValue (v, kLimThreshold, -18.0f);
    setValue (v, kLimGain, 16.0f);   // limited output near -2 dBFS: the soft clipper is in play
    return v;
}
}

//==============================================================================
TEST_CASE ("FxChain: order helpers", "[fx][order]")
{
    CHECK (isValidFxOrder (kDefaultFxOrder));
    CHECK (isValidFxOrder ({ 5, 4, 3, 2, 1, 0 }));
    CHECK_FALSE (isValidFxOrder ({ 0, 0, 1, 2, 3, 4 }));
    CHECK_FALSE (isValidFxOrder ({ 0, 1, 2, 3, 4, 6 }));
    CHECK_FALSE (isValidFxOrder ({ 0, 1, 2, 3, 4, 255 }));
    CHECK (sanitiseFxOrder ({ 1, 1, 1, 1, 1, 1 }) == kDefaultFxOrder);
    CHECK (sanitiseFxOrder ({ 3, 0, 1, 2, 4, 5 }) == FxOrder { 3, 0, 1, 2, 4, 5 });
}

TEST_CASE ("FxChain: FxParams defaults are the table defaults", "[fx][params]")
{
    const FxParams fromTable = fxParamsFrom (defaultParamValues(), Settings {}, 120.0);
    const FxParams plain {};
    CHECK (fingerprint (fromTable) == fingerprint (plain));
}

TEST_CASE ("FxChain: fxParamsFrom consumes every effect and master row", "[fx][params]")
{
    // Walks ParamTable.h: every row of an effects group must reach FxParams, and every row
    // except the two that are carried for others (dist_model for the editor, master_volume
    // for the engine) must change what the chain outputs.
    const auto in = disttest::acidLine (kFs, 0.3);
    const auto render = [&in] (const ParamValues& v)
    {
        auto chain = makeChain (fxParamsFrom (v, Settings {}, 120.0));
        Stereo s = fromMono (in);
        renderConst (*chain, s, fxParamsFrom (v, Settings {}, 120.0));
        return s;
    };

    const ParamValues bases[2] = { walkBase (true), walkBase (false) };
    const Stereo baseOut[2] = { render (bases[0]), render (bases[1]) };

    int rows = 0;
    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamInfo& info = kParamTable[static_cast<std::size_t> (i)];
        if (! isFxGroup (info.group)) continue;
        ++rows;
        INFO ("row " << info.id);

        bool paramsChanged = false, audioChanged = false;
        for (int b = 0; b < 2; ++b)
        {
            ParamValues v = bases[b];
            const float before = v[static_cast<std::size_t> (i)];
            v[static_cast<std::size_t> (i)] = perturb (info, before);
            REQUIRE (v[static_cast<std::size_t> (i)] != before);

            if (fingerprint (fxParamsFrom (v, Settings {}, 120.0)) != fingerprint (fxParamsFrom (bases[b], Settings {}, 120.0)))
                paramsChanged = true;
            if (! audioChanged && maxAbsDiff (render (v), baseOut[b]) > 1.0e-5)
                audioChanged = true;
        }

        CHECK (paramsChanged);
        const std::string id = info.id;
        if (id != "dist_model" && id != "master_volume")
            CHECK (audioChanged);
    }
    CHECK (rows == 6 + 19 + 13 + 15 + 12 + 9 + 8 + 2);

    // The order comes from Settings, the tempo from the argument.
    Settings s;
    s.fxOrder = { 5, 4, 3, 2, 1, 0 };
    const auto p = fxParamsFrom (defaultParamValues(), s, 133.0);
    CHECK (p.order == s.fxOrder);
    CHECK (p.bpm == 133.0);
    CHECK (p.delay.bpm == 133.0);
    s.fxOrder = { 0, 1, 2, 3, 4, 4 };
    CHECK (fxParamsFrom (defaultParamValues(), s, 120.0).order == kDefaultFxOrder);
}

//==============================================================================
TEST_CASE ("FxChain: everything off in the default order is bit-transparent", "[fx][transparent]")
{
    for (const double fs : kRates)
    {
        INFO (fs << " Hz");
        const FxParams p {};
        auto chain = makeChain (p, fs);
        CHECK (chain->latencySamples() == 0);
        const Stereo in { disttest::acidLine (fs, 0.5), disttest::sine (311.0, 0.4, fs, static_cast<int> (fs * 0.5)) };
        Stereo out = in;
        renderConst (*chain, out, p, 333);
        CHECK (bitIdentical (in, out));
    }

    // Also after the blocks have been used and switched off again.
    const FxParams on = allOn();
    auto chain = makeChain (on);
    Stereo warm = fromMono (disttest::acidLine (kFs, 0.5));
    renderConst (*chain, warm, on);
    const FxParams off {};
    Stereo settle = fromMono (disttest::acidLine (kFs, 0.2));
    renderConst (*chain, settle, off);
    const Stereo in = fromMono (disttest::acidLine (kFs, 0.5));
    Stereo out = in;
    renderConst (*chain, out, off);
    CHECK (bitIdentical (in, out));
}

TEST_CASE ("FxChain: bypass is transparent, with the latency kept", "[fx][bypass]")
{
    for (const int factor : { 1, 2, 4, 8 })
    {
        INFO ("oversampling " << factor);
        FxParams p = allOn();
        p.bypass = true;
        auto chain = makeChain (p, kFs, factor);
        const int latency = chain->latencySamples();
        if (factor == 1) CHECK (latency == 0);
        else             CHECK (latency > 0);

        const Stereo in { disttest::acidLine (kFs, 0.5), disttest::sine (97.0, 0.3, kFs, 24000) };
        Stereo out = in;
        renderConst (*chain, out, p, 100);

        bool exact = true;
        for (std::size_t i = 0; i < in.size(); ++i)
        {
            const float l = i >= static_cast<std::size_t> (latency) ? in.l[i - static_cast<std::size_t> (latency)] : 0.0f;
            const float r = i >= static_cast<std::size_t> (latency) ? in.r[i - static_cast<std::size_t> (latency)] : 0.0f;
            exact = exact && out.l[i] == l && out.r[i] == r;
        }
        CHECK (exact);
        CHECK (chain->meters().limiterReductionDb == 0.0f);
    }
}

TEST_CASE ("FxChain: the latency is the distortion's, on or off", "[fx][latency]")
{
    for (const int factor : { 1, 2, 4, 8 })
    {
        FxParams p {};
        auto chain = makeChain (p, kFs, factor);
        Distortion d;
        d.prepare (kFs, 512);
        d.setOversampling (factor);
        CHECK (chain->latencySamples() == d.latencySamples());

        // With everything off the chain is a pure delay of that many samples.
        Stereo s = fromMono (disttest::acidLine (kFs, 0.25));
        const Stereo in = s;
        renderConst (*chain, s, p);
        const auto lat = static_cast<std::size_t> (chain->latencySamples());
        bool exact = true;
        for (std::size_t i = lat; i < s.size(); ++i)
            exact = exact && s.l[i] == in.l[i - lat];
        CHECK (exact);
    }
}

//==============================================================================
TEST_CASE ("FxChain: all 720 orders render finite output", "[fx][order]")
{
    const auto in = disttest::acidLine (kFs, 0.12);
    FxParams p = allOn();
    auto chain = makeChain (p);

    FxOrder order = kDefaultFxOrder;
    int count = 0;
    double loudest = 0.0, quietest = 1.0e9;
    do
    {
        p.order = order;
        chain->setParams (p);
        chain->reset();
        REQUIRE (chain->activeOrder() == order);
        Stereo s = fromMono (in);
        renderConst (*chain, s, p, 512);
        INFO ("order " << int (order[0]) << int (order[1]) << int (order[2]) << int (order[3]) << int (order[4]) << int (order[5]));
        REQUIRE (allFinite (s));
        const double pk = std::max (disttest::peak (s.l), disttest::peak (s.r));
        CHECK (pk < 4.0);
        loudest = std::max (loudest, pk);
        quietest = std::min (quietest, pk);
        ++count;
    } while (std::next_permutation (order.begin(), order.end()));

    CHECK (count == 720);
    CHECK (quietest > 0.01);
}

TEST_CASE ("FxChain: the order changes the sound where it should, and only there", "[fx][order]")
{
    const auto in = disttest::acidLine (kFs, 0.6);

    // Distortion and reverb on, everything else off.
    FxParams p {};
    p.distortion.on = true;
    p.distortion.drive = 80.0f;
    p.reverb.on = true;
    p.reverb.mixPct = 50.0f;

    const auto renderOrder = [&] (const FxOrder& order)
    {
        FxParams q = p;
        q.order = order;
        auto chain = makeChain (q);
        Stereo s = fromMono (in);
        renderConst (*chain, s, q);
        return s;
    };

    const Stereo distFirst = renderOrder ({ 0, 1, 2, 3, 4, 5 });
    const Stereo reverbFirst = renderOrder ({ 3, 0, 1, 2, 4, 5 });
    CHECK (maxAbsDiff (distFirst, reverbFirst) > 0.01);

    // Moving the blocks that are off does nothing at all, bit for bit.
    CHECK (bitIdentical (distFirst, renderOrder ({ 5, 4, 0, 2, 3, 1 })));
    CHECK (bitIdentical (reverbFirst, renderOrder ({ 1, 3, 2, 0, 5, 4 })));

    // A distortion after the stereo reverb works on both channels: the right channel
    // differs from what it would be with the distortion off.
    FxParams off = p;
    off.distortion.on = false;
    off.order = { 3, 0, 1, 2, 4, 5 };
    auto chain = makeChain (off);
    Stereo dry = fromMono (in);
    renderConst (*chain, dry, off);
    CHECK (maxAbsDiff (dry, reverbFirst) > 0.01);
    double rightDiff = 0.0, leftRightDiff = 0.0;
    for (std::size_t i = 0; i < dry.size(); ++i)
    {
        rightDiff = std::max (rightDiff, static_cast<double> (std::abs (dry.r[i] - reverbFirst.r[i])));
        leftRightDiff = std::max (leftRightDiff, static_cast<double> (std::abs (reverbFirst.l[i] - reverbFirst.r[i])));
    }
    CHECK (rightDiff > 0.01);
    CHECK (leftRightDiff > 1.0e-3);   // the reverb made the signal stereo
}

TEST_CASE ("FxChain: an invalid order falls back to the default", "[fx][order]")
{
    FxParams p = allOn();
    p.order = { 2, 2, 3, 4, 5, 0 };
    auto chain = makeChain (p);
    CHECK (chain->activeOrder() == kDefaultFxOrder);

    FxParams q = allOn();
    auto reference = makeChain (q);
    Stereo a = fromMono (disttest::acidLine (kFs, 0.3)), b = a;
    renderConst (*chain, a, p);
    renderConst (*reference, b, q);
    CHECK (bitIdentical (a, b));
}

//==============================================================================
TEST_CASE ("FxChain: reordering while running is click-free", "[fx][order][transition]")
{
    // A smooth input, so any discontinuity the chain makes stands out.
    const int n = static_cast<int> (kFs);
    const auto mono = disttest::sine (110.0, 0.3, kFs, n);
    const std::size_t at = static_cast<std::size_t> (n / 2);

    FxParams a = allOn();
    a.delay.on = false;            // keep the signal steady so the step bound is tight
    a.distortion.drive = 40.0f;
    FxParams b = a;
    b.order = { 5, 3, 4, 2, 1, 0 };   // limiter first, distortion last

    // Steady state in each order sets the yardstick.
    double steady = 0.0;
    for (const auto* q : { &a, &b })
    {
        auto chain = makeChain (*q);
        Stereo s = fromMono (mono);
        renderConst (*chain, s, *q, 64);
        steady = std::max (steady, maxStep (s, at - 4800, at + 4800));
    }

    auto chain = makeChain (a);
    Stereo s = fromMono (mono);
    renderBlocks (*chain, s, 64, [&] (FxChain& c, int pos) { c.setParams (static_cast<std::size_t> (pos) >= at ? b : a); });

    // The order really did change, and the switch never steps further than the music does.
    CHECK (chain->activeOrder() == b.order);
    const double transition = maxStep (s, at - 64, at + static_cast<std::size_t> (0.2 * kFs));
    // A fade between two signals may step by the fade's own slope times their difference
    // on top of the signals' own steps: smoothstep peaks at 1.5 / fade length.
    const double allowance = 1.5 / (FxChain::kReorderFadeSeconds * kFs) * 1.0;
    INFO ("steady max step " << steady << ", during the reorder " << transition << ", allowance " << allowance);
    CHECK (transition <= steady + allowance);

    // A plain swap without the gate does click: the yardstick is sharp enough to see it.
    double swapStep = 0.0;
    {
        auto x = makeChain (a);
        Stereo s1 = fromMono (mono);
        renderConst (*x, s1, a, 64);
        auto y = makeChain (b);
        Stereo s2 = fromMono (mono);
        renderConst (*y, s2, b, 64);
        swapStep = std::abs (s2.l[at] - s1.l[at - 1]);
        swapStep = std::max (swapStep, static_cast<double> (std::abs (s2.r[at] - s1.r[at - 1])));
    }
    INFO ("a hard swap would step " << swapStep);
    CHECK (swapStep > steady * 2.0);

    // The gate shuts once and reopens: the output dips to silence for a moment, no longer
    // than about two fades.
    std::size_t silent = 0;
    for (std::size_t i = at; i < at + static_cast<std::size_t> (0.05 * kFs); ++i)
        if (std::abs (s.l[i]) < 1.0e-6f) ++silent;
    CHECK (silent >= 1);
    CHECK (silent < static_cast<std::size_t> (0.012 * kFs));
    dumpWav ("fx_reorder.wav", s, kFs);
}

TEST_CASE ("FxChain: reorders that keep the running sequence are instant and exact", "[fx][order][transition]")
{
    const auto mono = disttest::acidLine (kFs, 0.5);
    FxParams a {};
    a.distortion.on = true;
    a.reverb.on = true;
    a.order = { 0, 1, 2, 3, 4, 5 };
    FxParams b = a;
    b.order = { 4, 1, 0, 5, 3, 2 };   // distortion still before reverb

    auto ref = makeChain (a);
    Stereo r = fromMono (mono);
    renderConst (*ref, r, a, 128);

    auto chain = makeChain (a);
    Stereo s = fromMono (mono);
    renderBlocks (*chain, s, 128, [&] (FxChain& c, int pos) { c.setParams (pos >= 9600 ? b : a); });
    CHECK (bitIdentical (r, s));
    CHECK (chain->activeOrder() == b.order);
}

TEST_CASE ("FxChain: moving an oversampled distortion that is off is click-free", "[fx][order][transition]")
{
    // Off, but at 4x it still delays the signal, so where it sits matters.
    const auto mono = disttest::sine (110.0, 0.3, kFs, 24000);
    FxParams a {};
    a.reverb.on = true;
    a.reverb.preDelayMs = 0.0f;
    a.order = { 0, 3, 1, 2, 4, 5 };
    FxParams b = a;
    b.order = { 3, 0, 1, 2, 4, 5 };

    double steady = 0.0;
    for (const auto* q : { &a, &b })
    {
        auto chain = makeChain (*q, kFs, 4);
        Stereo s = fromMono (mono);
        renderConst (*chain, s, *q, 64);
        steady = std::max (steady, maxStep (s, 1, s.size()));
    }

    auto chain = makeChain (a, kFs, 4);
    Stereo s = fromMono (mono);
    renderBlocks (*chain, s, 64, [&] (FxChain& c, int pos) { c.setParams (pos >= 9600 ? b : a); });
    CHECK (chain->activeOrder() == b.order);
    const double allowance = 1.5 / (FxChain::kReorderFadeSeconds * kFs) * 1.0;
    const double transition = maxStep (s, 9500, s.size());
    INFO ("steady " << steady << ", transition " << transition);
    CHECK (transition <= steady + allowance);
}

TEST_CASE ("FxChain: bypass on and off is click-free", "[fx][bypass][transition]")
{
    const int n = static_cast<int> (kFs);
    const auto mono = disttest::sine (110.0, 0.3, kFs, n);
    FxParams p = allOn();
    p.delay.on = false;
    p.distortion.drive = 40.0f;

    for (const int factor : { 1, 4 })
    {
        INFO ("oversampling " << factor);
        double steady = 0.0;
        for (const bool bypass : { false, true })
        {
            FxParams q = p;
            q.bypass = bypass;
            auto chain = makeChain (q, kFs, factor);
            Stereo s = fromMono (mono);
            renderConst (*chain, s, q, 64);
            steady = std::max (steady, maxStep (s, 1, 36000));   // from a cold start: the chain's own start-up counts
        }

        // Off at 0.25 s, back on at 0.5 s.
        auto chain = makeChain (p, kFs, factor);
        Stereo s = fromMono (mono);
        renderBlocks (*chain, s, 64, [&] (FxChain& c, int pos)
        {
            FxParams q = p;
            q.bypass = pos >= 12000 && pos < 24000;
            c.setParams (q);
        });
        const double transition = std::max (maxStep (s, 11900, 13000), maxStep (s, 23900, 33600));
        const double allowance = 1.5 / (FxChain::kBypassFadeSeconds * kFs) * (1.0 + 0.3);
        INFO ("steady " << steady << ", transitions " << transition << ", allowance " << allowance);
        CHECK (allFinite (s));
        CHECK (transition <= steady + allowance);

        // In the middle of the bypassed stretch the output is exactly the delayed input.
        const auto lat = static_cast<std::size_t> (chain->latencySamples());
        bool exact = true;
        for (std::size_t i = 14000; i < 23000; ++i)
            exact = exact && s.l[i] == mono[i - lat] && s.r[i] == mono[i - lat];
        CHECK (exact);
        if (factor == 4) dumpWav ("fx_bypass_x4.wav", s, kFs);
    }
}

TEST_CASE ("FxChain: changing the oversampling while bypassed is click-free", "[fx][bypass][transition]")
{
    const auto mono = disttest::sine (110.0, 0.3, kFs, 24000);
    FxParams p {};
    p.bypass = true;
    auto chain = makeChain (p, kFs, 1);
    Stereo s = fromMono (mono);
    renderBlocks (*chain, s, 64, [&] (FxChain& c, int pos)
    {
        c.setParams (p);
        if (pos == 9600) c.setDistortionOversampling (8);
    });
    CHECK (chain->latencySamples() > 0);
    const double steady = maxStep (s, 1000, 9000);
    CHECK (maxStep (s, 9500, 12000) <= steady * 1.1 + 1.0e-4);
}

//==============================================================================
TEST_CASE ("FxChain: the tempo reaches the delay", "[fx][delay]")
{
    FxParams p {};
    p.delay.on = true;
    p.delay.mixPct = 100.0f;
    p.delay.feedbackPct = 0.0f;
    p.delay.leftNum = p.delay.rightNum = 1;
    p.delay.leftDen = p.delay.rightDen = 4;   // a quarter note

    for (const double bpm : { 90.0, 120.0, 174.0 })
    {
        INFO (bpm << " BPM");
        p.bpm = bpm;
        p.delay.bpm = 999.0;   // ignored: the chain's tempo wins
        auto chain = makeChain (p);
        const auto expected = static_cast<std::size_t> (std::llround (60.0 / bpm * kFs));
        CHECK (chain->delayBlock().delaySamples (0) == expected);

        Stereo s { std::vector<float> (expected + 2000, 0.0f), std::vector<float> (expected + 2000, 0.0f) };
        s.l[0] = s.r[0] = 1.0f;
        renderConst (*chain, s, p);
        const auto echo = static_cast<std::size_t> (std::max_element (s.l.begin() + 10, s.l.end()) - s.l.begin());
        CHECK (echo == expected);
    }

    // A tempo change while running moves the tap.
    p.bpm = 120.0;
    auto chain = makeChain (p);
    Stereo s = fromMono (std::vector<float> (48000, 0.0f));
    renderBlocks (*chain, s, 256, [&] (FxChain& c, int pos)
    {
        FxParams q = p;
        q.bpm = pos < 24000 ? 120.0 : 140.0;
        c.setParams (q);
    });
    CHECK (chain->delayBlock().delaySamples (0) == static_cast<std::size_t> (std::llround (60.0 / 140.0 * kFs)));
}

TEST_CASE ("FxChain: meters", "[fx][meters]")
{
    // Limiter reduction and peaks.
    FxParams p {};
    p.limiter.on = true;
    p.limiter.thresholdDb = -20.0f;
    auto chain = makeChain (p);
    Stereo s = fromMono (disttest::sine (200.0, 0.5, kFs, 4800));
    renderConst (*chain, s, p, 4800);
    const auto m = chain->meters();
    CHECK (m.limiterReductionDb > 10.0f);
    CHECK (m.peakLeft == static_cast<float> (disttest::peak (s.l)));
    CHECK (m.peakRight == static_cast<float> (disttest::peak (s.r)));
    CHECK_FALSE (m.delayClamped);

    // The delay clamp flag: 32/1 at 40 BPM is far beyond the buffer.
    FxParams d {};
    d.delay.on = true;
    d.delay.leftNum = 32;
    d.delay.leftDen = 1;
    d.bpm = 40.0;
    chain->setParams (d);
    CHECK (chain->meters().delayClamped);
    d.bpm = 120.0;
    d.delay.leftNum = 3;
    d.delay.leftDen = 16;
    chain->setParams (d);
    CHECK_FALSE (chain->meters().delayClamped);

    // Distortion dynamics gain.
    FxParams g {};
    g.distortion.on = true;
    g.distortion.dynamics = 100.0f;
    auto dyn = makeChain (g);
    Stereo t = fromMono (disttest::sine (200.0, 0.05, kFs, 9600));
    renderConst (*dyn, t, g);
    CHECK (dyn->meters().dynamicsGainDb > 1.0f);   // a quiet signal is lifted
}

//==============================================================================
TEST_CASE ("FxChain: output does not depend on the block size", "[fx][blocksize]")
{
    const auto mono = disttest::acidLine (kFs, 1.0);
    FxParams a = allOn();
    FxParams b = a;
    b.order = { 3, 2, 1, 0, 5, 4 };
    FxParams c = b;
    c.bypass = true;

    // Changes land on sample positions every block size divides.
    const auto paramsAt = [&] (int pos) -> const FxParams& { return pos < 16384 ? a : (pos < 32768 ? b : c); };

    std::vector<Stereo> outs;
    for (const int block : { 1, 64, 4096 })
    {
        auto chain = makeChain (a, kFs, 4);
        Stereo s = fromMono (mono);
        renderBlocks (*chain, s, block, [&] (FxChain& ch, int pos) { ch.setParams (paramsAt (pos)); });
        REQUIRE (allFinite (s));
        outs.push_back (std::move (s));
    }
    CHECK (bitIdentical (outs[0], outs[1]));
    CHECK (bitIdentical (outs[0], outs[2]));
}

TEST_CASE ("FxChain: every sample rate works", "[fx][rates]")
{
    for (const double fs : kRates)
        for (const int factor : { 1, 8 })
        {
            INFO (fs << " Hz, x" << factor);
            const FxParams p = allOn();
            auto chain = makeChain (p, fs, factor);
            Stereo s = fromMono (disttest::acidLine (fs, 0.5));
            renderConst (*chain, s, p, 480);
            CHECK (allFinite (s));
            CHECK (std::max (disttest::peak (s.l), disttest::peak (s.r)) <= 1.0);   // soft clip on
            CHECK (disttest::rms (s.l) > 0.01);
        }
}

TEST_CASE ("FxChain: nothing allocates on the audio path", "[fx][realtime]")
{
    FxParams p = allOn();
    auto chain = makeChain (p, kFs, 2);
    Stereo s = fromMono (disttest::acidLine (kFs, 1.0));
    FxOrder order = kDefaultFxOrder;

    acid::test::ScopedAllocGuard guard;
    const int n = static_cast<int> (s.size());
    int block = 0;
    for (int pos = 0; pos < n; pos += 300, ++block)
    {
        FxParams q = p;
        if (block % 7 == 3)
        {
            std::next_permutation (order.begin(), order.end());
            q.order = order;
        }
        q.bypass = (block / 20) % 2 == 1;
        q.bpm = 100.0 + block;
        q.distortion.clipper = block % kNumClipperFamilies;
        q.reverb.lateSizeM = 10.0f + static_cast<float> (block % 20);
        if (block % 13 == 5) chain->setDistortionOversampling (1 << (block % 4));
        chain->setParams (q);
        chain->process (s.l.data() + pos, s.r.data() + pos, std::min (300, n - pos));
        (void) chain->meters();
        (void) chain->latencySamples();
    }
    chain->reset();
    CHECK (guard.count() == 0);
    CHECK (allFinite (s));
}

TEST_CASE ("FxChain: process before prepare leaves the buffer alone", "[fx][realtime]")
{
    FxChain chain;
    chain.setParams (allOn());
    std::vector<float> l (64, 0.5f), r (64, -0.5f);
    chain.process (l.data(), r.data(), 64);
    CHECK (l == std::vector<float> (64, 0.5f));
    CHECK (r == std::vector<float> (64, -0.5f));
}

TEST_CASE ("FxChain: timing benchmark, everything on, distortion at 4x", "[fx][.benchmark]")
{
    const double seconds = 4.0;
    const auto mono = disttest::acidLine (kFs, seconds);
    FxParams p = allOn();
    p.distortion.dynamics = 50.0f;
    auto chain = makeChain (p, kFs, 4);
    Stereo s = fromMono (mono);

    const auto t0 = std::chrono::steady_clock::now();
    renderConst (*chain, s, p, 256);
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double> (t1 - t0).count();
    const double realtime = seconds / elapsed;
    std::printf ("[benchmark] full chain, everything on, distortion x4, 48 kHz: %.1f x real time (%.0f ns/stereo sample)\n",
                 realtime, elapsed / (seconds * kFs) * 1.0e9);
    CHECK (allFinite (s));
    CHECK (realtime > 10.0);
    dumpWav ("fx_all_on_x4.wav", s, kFs);
}
