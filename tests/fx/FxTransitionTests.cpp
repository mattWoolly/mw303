// Transitions of the whole chain: the cost of the block in which something switches on,
// reorders that leave the delay lines alone, and bad input through every block.
#include "../dist/DistTestUtil.h"
#include "acid/fx/FxChain.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <vector>

using namespace acid;
using namespace acid::fx;

namespace
{
/** process() time of each of `blocks` blocks of 64 samples, in microseconds. */
std::vector<double> blockMicros (FxChain& c, const FxParams& p, int blocks, std::int64_t& clock)
{
    std::vector<float> l (64), r (64);
    std::vector<double> t (static_cast<std::size_t> (blocks));
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 64; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = static_cast<float> (0.2 * std::sin (static_cast<double> (clock++) * 0.05));
        c.setParams (p);
        const auto t0 = std::chrono::steady_clock::now();
        c.process (l.data(), r.data(), 64);
        t[static_cast<std::size_t> (b)] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
    }
    return t;
}

double median (std::vector<double> v)
{
    std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end());
    return v[v.size() / 2];
}

double maxOf (const std::vector<double>& v)
{
    return *std::max_element (v.begin(), v.end());
}

/** Extra time of the worst of the first eight blocks after `to` takes over from `from`, over
    the median block in steady state with `to`. The smallest of three tries, so a
    pre-emption by the scheduler in one try does not count. */
double switchSpikeMicros (double fs, const FxParams& from, const FxParams& to, int oversampling)
{
    double best = 1.0e9;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        FxChain c;
        c.setDistortionOversampling (oversampling);
        c.prepare (fs, 64);
        c.setParams (to);
        c.reset();
        std::int64_t clock = 0;
        const int second = static_cast<int> (fs / 64.0);
        blockMicros (c, to, second / 4, clock);                   // lines written, caches warm
        const double steady = median (blockMicros (c, to, second / 10, clock));
        blockMicros (c, from, second / 10, clock);                // long enough to go idle
        const double worst = maxOf (blockMicros (c, to, 8, clock));
        best = std::min (best, worst - steady);
    }
    return best;
}
}

TEST_CASE ("FxChain: switching any block on, or releasing the bypass, costs no more than a block", "[fx][realtime]")
{
    // At 192 kHz a 64-sample block lasts 333 us. Clearing the delay and reverb lines inside
    // one process() call used to take 2.2 to 2.6 ms there (17 MB of memset). The lines now
    // clear in constant time; the switch-on block may cost at most a quarter of a block
    // more than a steady one. The figure is relative to steady state, so it holds in debug
    // and sanitiser builds as well as release.
    const double fs = 192000.0;
    const double budget = 64.0 / fs * 1.0e6;

    FxParams everything {};
    everything.distortion.on = true;
    everything.eq.on = true;
    everything.eq.lowGainDb = 3.0f;
    everything.delay.on = true;
    everything.reverb.on = true;
    everything.chorus.on = true;
    everything.limiter.on = true;

    {
        auto bypassed = everything;
        bypassed.bypass = true;
        const double spike = switchSpikeMicros (fs, bypassed, everything, 4);
        INFO ("bypass release: " << spike << " us over a steady block (budget " << 0.25 * budget << " us)");
        CHECK (spike < 0.25 * budget);
    }

    for (int slot = 0; slot < kNumFxSlots; ++slot)
    {
        auto off = everything;
        switch (static_cast<FxSlot> (slot))
        {
            case FxSlot::Distortion: off.distortion.on = false; break;
            case FxSlot::Eq:         off.eq.on = false; break;
            case FxSlot::Delay:      off.delay.on = false; break;
            case FxSlot::Reverb:     off.reverb.on = false; break;
            case FxSlot::Chorus:     off.chorus.on = false; break;
            case FxSlot::Limiter:    off.limiter.on = false; break;
        }
        for (const int factor : { 1, 8 })
        {
            const double spike = switchSpikeMicros (fs, off, everything, factor);
            INFO ("slot " << slot << " on, distortion at " << factor << "x: " << spike << " us over a steady block (budget "
                          << 0.25 * budget << " us)");
            CHECK (spike < 0.25 * budget);
        }
    }
}

TEST_CASE ("FxChain: a reorder behind the delay leaves its echoes whole", "[fx][order][transition]")
{
    // A steady tone into a quarter-note delay (0.5 s at 120 BPM, 60 % feedback). Swapping
    // the chorus and the limiter after it gates only their inputs and the output, so the
    // delay line takes no hole and the echoes 0.5 and 1 s later carry no dip. (When every
    // running block was gated, the dip came back 2.6 dB deep at 0.5 s and 1.2 dB at 1 s.)
    const double fs = 48000.0;
    FxParams a {};
    a.delay.on = true;
    a.delay.leftNum = a.delay.rightNum = 1;
    a.delay.leftDen = a.delay.rightDen = 4;
    a.delay.feedbackPct = 60.0f;
    a.delay.mixPct = 50.0f;
    a.chorus.on = true;
    a.chorus.mixPct = 0.0f;           // running, but transparent
    a.limiter.on = true;
    a.limiter.thresholdDb = 0.0f;     // running, but never limiting this tone
    a.bpm = 120.0;
    FxParams b = a;
    b.order = { 0, 1, 2, 3, 5, 4 };

    const auto n = static_cast<std::size_t> (4.0 * fs);
    const auto at = static_cast<std::size_t> (2.0 * fs);
    const auto mono = disttest::sine (500.0, 0.2, fs, static_cast<int> (n));
    std::vector<float> l = mono, r = mono;
    FxChain c;
    c.prepare (fs, 64);
    c.setParams (a);
    c.reset();
    for (std::size_t pos = 0; pos < n; pos += 64)
    {
        c.setParams (pos >= at ? b : a);
        c.process (l.data() + pos, r.data() + pos, 64);
    }
    REQUIRE (c.activeOrder() == b.order);

    // The deepest 2 ms window near each echo of the reorder, against the level just before.
    const auto window = static_cast<std::size_t> (0.002 * fs);
    const auto level = [&] (std::size_t from) { std::vector<float> w (l.begin() + static_cast<long> (from), l.begin() + static_cast<long> (from + window)); return disttest::rms (w); };
    const double before = level (at - 10 * window);
    for (const double echo : { 0.5, 1.0 })
    {
        double deepest = 1.0e9;
        const auto centre = at + static_cast<std::size_t> (echo * fs);
        for (std::size_t from = centre - 10 * window; from < centre + 10 * window; from += window / 2)
            deepest = std::min (deepest, level (from));
        INFO ("echo at +" << echo << " s: deepest window " << disttest::db (deepest / before) << " dB");
        CHECK (disttest::db (deepest / before) > -0.3);
    }
}

TEST_CASE ("FxChain: bad samples through every block in any order", "[fx][robust]")
{
    const double fs = 48000.0;
    FxParams p {};
    p.distortion.on = true;
    p.distortion.drive = 80.0f;
    p.eq.on = true;
    p.eq.lowGainDb = p.eq.highGainDb = 24.0f;
    p.delay.on = true;
    p.delay.feedbackPct = 100.0f;
    p.reverb.on = true;
    p.reverb.feedbackPct = 100.0f;
    p.chorus.on = true;
    p.limiter.on = true;
    for (const FxOrder order : { kDefaultFxOrder, FxOrder { 5, 4, 3, 2, 1, 0 }, FxOrder { 1, 2, 3, 4, 5, 0 } })
        for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::max(), 1.0e30f })
            for (const bool off : { false, true })
            {
                auto q = p;
                q.order = order;
                if (off) q.distortion.on = q.eq.on = q.limiter.on = false;
                FxChain c;
                c.setDistortionOversampling (4);
                c.prepare (fs, 256);
                c.setParams (q);
                c.reset();
                const auto n = static_cast<std::size_t> (3.0 * fs);
                auto l = disttest::sine (110.0, 0.25, fs, static_cast<int> (n));
                auto r = l;
                l[static_cast<std::size_t> (fs)] = bad;
                r[static_cast<std::size_t> (fs) + 7] = bad;
                for (std::size_t pos = 0; pos < n; pos += 256)
                {
                    c.setParams (q);
                    c.process (l.data() + pos, r.data() + pos, static_cast<int> (std::min<std::size_t> (256, n - pos)));
                }
                INFO ("order " << int (order[0]) << int (order[1]) << int (order[2]) << int (order[3]) << int (order[4]) << int (order[5])
                               << ", bad " << bad << (off ? ", distortion, EQ and limiter off" : ""));
                CHECK (disttest::allFinite (l));
                CHECK (disttest::allFinite (r));
                std::vector<float> tail (l.begin() + static_cast<long> (2.5 * fs), l.end());
                CHECK (disttest::rms (tail) > 0.01);
                CHECK (disttest::peak (tail) < 4.0);
            }
}
