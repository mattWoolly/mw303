// Review tests: defects found in review, encoded as tests that fail until they are fixed.
#include "acid/fx/FxChain.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

using namespace acid;
using namespace acid::fx;

// Amended (fix pass): the absolute check below only means something in an optimised build
// without sanitisers, so it skips itself elsewhere; debug and sanitiser runs of the default
// test set must pass. FxTransitionTests.cpp checks the same transitions everywhere, as cost
// relative to a steady block, against a quarter of the block budget.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ACID_REVIEW_SANITISED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define ACID_REVIEW_SANITISED 1
#endif
#endif

namespace
{
/** Worst process() time in microseconds over `blocks` blocks of 64 samples. */
double worstBlockMicros (FxChain& c, const FxParams& p, int blocks)
{
    std::vector<float> l (64), r (64);
    double worst = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 64; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = static_cast<float> (0.1 * std::sin ((b * 64 + i) * 0.05));
        c.setParams (p);
        const auto t0 = std::chrono::steady_clock::now();
        c.process (l.data(), r.data(), 64);
        worst = std::max (worst, std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
    }
    return worst;
}
}

// Releasing fx_bypass, or switching the delay back on, clears the delay and reverb lines
// inside one process() call. At 192 kHz that call took 2.6 ms for a 64-sample block whose
// budget is 0.33 ms (0.64 ms at 48 kHz): a guaranteed dropout. The clear has to be spread
// over many blocks (or done while the block is idle). Timing test: run it on an idle machine.
TEST_CASE ("review: no single block blows the real-time budget when blocks come back on", "[fx][realtime][review]")
{
#if ! defined(NDEBUG) || defined(ACID_REVIEW_SANITISED)
    SKIP ("absolute timing needs an optimised build without sanitisers");
#endif
    const double fs = 192000.0;
    const double budget = 64.0 / fs * 1.0e6;
    FxChain c;
    c.prepare (fs, 64);
    FxParams p;
    p.delay.on = true;
    p.reverb.on = true;
    c.setParams (p);
    c.reset();
    const int tenSeconds = static_cast<int> (10.0 * fs / 64.0);
    const int fifty = static_cast<int> (0.05 * fs / 64.0);

    worstBlockMicros (c, p, tenSeconds);   // fill every line
    auto bypassed = p;
    bypassed.bypass = true;
    worstBlockMicros (c, bypassed, fifty);
    const double release = worstBlockMicros (c, p, 8);

    worstBlockMicros (c, p, tenSeconds);
    auto delayOff = p;
    delayOff.delay.on = false;
    worstBlockMicros (c, delayOff, fifty);
    const double reEnable = worstBlockMicros (c, p, 8);

    INFO ("budget " << budget << " us; bypass release " << release << " us; delay re-enable " << reEnable << " us");
    CHECK (release < 0.5 * budget);
    CHECK (reEnable < 0.5 * budget);
}

// A single NaN from upstream (with the distortion off, nothing sanitises the input) leaves
// the whole chain outputting NaN until the host resets it.
TEST_CASE ("review: the chain recovers from one non-finite input sample", "[fx][review]")
{
    const double fs = 48000.0;
    FxParams p;
    p.eq.on = true;
    p.eq.lowGainDb = 3.0f;
    p.delay.on = true;
    p.reverb.on = true;
    FxChain c;
    c.prepare (fs, 256);
    c.setParams (p);
    c.reset();
    const auto n = static_cast<std::size_t> (fs * 3.0);
    std::vector<float> l (n), r (n);
    for (std::size_t i = 0; i < n; ++i)
        l[i] = r[i] = static_cast<float> (0.25 * std::sin (2.0 * kPi * 110.0 * static_cast<double> (i) / fs));
    l[static_cast<std::size_t> (fs)] = std::numeric_limits<float>::quiet_NaN();
    for (std::size_t i = 0; i < n; i += 256)
    {
        c.setParams (p);
        c.process (l.data() + i, r.data() + i, static_cast<int> (std::min<std::size_t> (256, n - i)));
    }
    bool finite = true;
    for (std::size_t i = static_cast<std::size_t> (fs * 2.5); i < n; ++i)
        finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
    CHECK (finite);
}
