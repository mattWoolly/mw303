#pragma once
// Feed-forward stereo limiter/compressor with a soft knee and an optional soft clipper.
//
// Detection is stereo-linked: level = max(|L|, |R|) in dB, through a 16 ms peak hold (a
// new peak at or above the held one restarts the hold; when the hold runs out the level
// drops to the current sample). The hold spans the rectified period of a 31 Hz tone, so
// on steady tones the detector reads the crest and the gain sits still. The static
// curve (threshold T, ratio R, knee width W, all in dB) is
//
//     x - T <= -W/2       : y = x
//     |x - T| < W/2       : y = x + (1/R - 1) (x - T + W/2)^2 / (2 W)
//     x - T >= W/2        : y = T + (x - T) / R
//
// with W = 24 dB * (1 - knee/100): knee 100 % is a hard knee, 0 % the softest.
// The gain change y - x is smoothed in dB by a branching one-pole: attack time constant
// when the reduction deepens, release when it recovers (each the time to 63 %; a release
// starts once the hold has let go).
// No look-ahead, so latency is zero and very fast transients can briefly pass the
// threshold; the soft clipper catches them.
//
// After the make-up gain, the optional soft clipper is the identity up to kClipKnee
// (-3.1 dBFS) and bends smoothly (tanh) towards 1.0, so with it on the output never
// exceeds 0 dBFS. The clipper is not oversampled: driven hard it aliases like any
// static waveshaper at the base rate.
// Bypass: 5 ms crossfade; the detector restarts from no reduction on switch-on.
#include "acid/engine/ParamTable.h"
#include "acid/fx/InsertUtil.h"

namespace acid::fx
{
/** One field per row of the Limiter group in ParamTable.h. */
struct LimiterParams
{
    bool on = false;
    float kneePct = 100.0f;            // 0 = 24 dB soft knee, 100 = hard knee
    float ratio = 40.0f;               // 2..40 (:1)
    float attackMs = 1.0f;             // 0.1..100
    float releaseMs = 150.0f;          // 25..500
    float thresholdDb = 0.0f;          // -48..0
    float gainDb = 0.0f;               // make-up, 0..24
    bool softClip = false;
};

/** Reads the Limiter rows of a parameter snapshot. */
inline LimiterParams limiterParamsFrom (const ParamValues& v) noexcept
{
    const auto f = [&v] (ParamId id) { return v[static_cast<std::size_t> (id)]; };
    LimiterParams p;
    p.on = paramAsBool (v, kLimOn);
    p.kneePct = f (kLimKnee);
    p.ratio = f (kLimRatio);
    p.attackMs = f (kLimAttack);
    p.releaseMs = f (kLimRelease);
    p.thresholdDb = f (kLimThreshold);
    p.gainDb = f (kLimGain);
    p.softClip = paramAsBool (v, kLimSoftClip);
    return p;
}

class Limiter
{
public:
    static constexpr double kMaxKneeDb = 24.0;
    static constexpr double kClipKnee = 0.7;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;
    /** Once per block, before process(). */
    void setParams (const LimiterParams& p) noexcept;
    /** In place, any numSamples >= 0. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Deepest gain reduction during the last process() call, in dB, >= 0 (0 when off).
        Read it on the audio thread after process() and publish it to the UI yourself. */
    float gainReductionDb() const noexcept { return lastReductionDb; }

    /** The static curve: output level in dB for an input level in dB. */
    static double staticCurveDb (double inDb, double thresholdDb, double ratio, double kneeDb) noexcept;
    /** The soft clipper alone. */
    static double softClip (double x) noexcept { return insert::softLimit (x, kClipKnee, 1.0); }

private:
    double sampleRate = 48000.0;
    insert::Smoother threshold, ratioS, kneeDb, makeup;
    double attackCoeff = 0.0, releaseCoeff = 0.0;
    double gainDb = 0.0;               // smoothed gain change, <= 0
    double held = 0.0;                 // peak hold of the detector
    int holdLeft = 0, holdSamples = 1;
    bool clipOn = false;
    float lastReductionDb = 0.0f;
    insert::OnOffFade fade;
    bool snapNext = true;
};
}
