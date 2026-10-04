// Switches inside a voicing (tilt on, pre EQ on, dynamics placement, the high-passes and
// the low-keep split switching in or out) must not step the output: picking another voicing
// during playback changes several of them at once. The tilt and the pre EQ glide in dB;
// the rest are made under the block's short duck (Distortion.h, setParams).
#include "DistTestUtil.h"
#include "acid/fx/Distortion.h"
#include "acid/fx/DistortionVoicings.h"
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace acid::fx;

namespace
{
struct Steps
{
    double steady = 0.0;   // largest sample-to-sample change of the settled signal, either side
    double around = 0.0;   // the same over the switch and the 50 ms after it
    int at = 0;            // where the largest change around the switch is, samples after it
};

/** A 100 Hz sine at 0.25 through the block for one second; `change` edits the parameters
    at the block boundary nearest the middle. */
Steps switchSteps (DistortionParams p, const std::function<void (DistortionParams&)>& change, int factor = 4)
{
    const double fs = 48000.0;
    Distortion d;
    d.prepare (fs, 256);
    d.setOversampling (factor);
    d.setParams (p);
    d.reset();

    const int n = static_cast<int> (fs);
    std::vector<float> out = disttest::sine (100.0, 0.25, fs, n);
    const int half = n / 2;
    const int sw = half - half % 256;
    for (int i = 0; i < n; i += 256)
    {
        if (i == sw)
        {
            change (p);
            d.setParams (p);
        }
        d.process (out.data() + i, nullptr, std::min (256, n - i));
    }

    const auto delta = [&out] (int i) {
        return static_cast<double> (std::abs (out[static_cast<std::size_t> (i + 1)] - out[static_cast<std::size_t> (i)]));
    };
    Steps s;
    for (int i = sw - 9600; i < sw - 1; ++i) s.steady = std::max (s.steady, delta (i));
    for (int i = n - 9600; i < n - 1; ++i) s.steady = std::max (s.steady, delta (i));
    for (int i = sw - 8; i < sw + 2400; ++i)
        if (delta (i) > s.around)
        {
            s.around = delta (i);
            s.at = i - sw;
        }
    return s;
}

DistortionParams mild()
{
    DistortionParams p;
    p.on = true;
    p.drive = 30.0f;
    return p;
}
}

TEST_CASE ("switches: tilt amount, pre EQ on and dynamics placement glide or duck", "[dist][switch]")
{
    {
        auto p = mild();
        p.tiltOn = true;
        p.tiltAmt = 0.0f;
        const Steps s = switchSteps (p, [] (DistortionParams& q) { q.tiltAmt = 12.0f; });
        INFO ("tilt amount 0 to 12 dB: around " << s.around << ", steady " << s.steady);
        CHECK (s.around <= 1.5 * s.steady);
    }
    {
        auto p = mild();
        p.preEqGain = 12.0f;
        p.preEqFreq = 100.0f;
        p.preEqOn = false;
        const Steps s = switchSteps (p, [] (DistortionParams& q) { q.preEqOn = true; });
        INFO ("pre EQ +12 dB at 100 Hz switched on: around " << s.around << ", steady " << s.steady);
        CHECK (s.around <= 1.5 * s.steady);
    }
    {
        auto p = mild();
        p.dynamics = 80.0f;
        p.dynPost = false;
        const Steps s = switchSteps (p, [] (DistortionParams& q) { q.dynPost = true; });
        INFO ("dynamics moved after the clipper: around " << s.around << ", steady " << s.steady);
        CHECK (s.around <= 1.5 * s.steady);
    }
    {
        auto p = mild();
        const Steps s = switchSteps (p, [] (DistortionParams& q) { q.preHp = 150.0f; q.postHp = 120.0f; q.lowKeep = 80.0f; });
        INFO ("pre and post high-pass and low keep switched in: around " << s.around << ", steady " << s.steady);
        CHECK (s.around <= 1.5 * s.steady);
    }
}

TEST_CASE ("switches: stepping through the voicing list mid-note does not click", "[dist][switch]")
{
    // Every voicing from the one before it in the list, at drive 30, as a user stepping
    // through the Distortion Model list hears it.
    const auto voicings = distortionVoicings();
    double worstRatio = 0.0;
    for (std::size_t i = 1; i < voicings.size(); ++i)
    {
        DistortionParams p = mild();
        applyVoicing (p, voicings[i - 1]);
        const Steps s = switchSteps (p, [&] (DistortionParams& q) { applyVoicing (q, voicings[i]); });
        const double ratio = s.around / s.steady;
        worstRatio = std::max (worstRatio, ratio);
        INFO (voicings[i - 1].name << " to " << voicings[i].name << ": around " << s.around << " (" << s.at
              << " samples after the switch), steady " << s.steady);
        // 1.5, as for the single switches above: a change of family or filter structure is made
        // silently at the bottom of the duck, where every smoothed control also takes its new
        // value, so what comes back in is the new voicing settled (worst measured 1.4x, the
        // new voicing's own first cycles). A click is far beyond that: before the filter
        // switches, corner glides, the duck hold and the snap, these changes measured up to 42x.
        CHECK (ratio <= 1.5);
    }
    std::printf ("[switch] worst step around a voicing change against the steady signal: %.2fx\n", worstRatio);
}
