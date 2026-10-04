#pragma once
// Stereo two-line chorus.
//
// Per channel, two modulated delay lines read with Catmull-Rom interpolation:
//   line 1: base + depth * (0.5 + 0.5 sin phi)
//   line 2: 1.5 * base + depth * (0.5 + 0.5 sin (phi + pi/2))   (level = Fatness)
// The right channel's LFO runs Spatial * 180 degrees ahead of the left.
//
// Knob laws (percent in, physics out):
//   offset  0..100 %  -> base delay 5..25 ms (linear)
//   rate    0..100 %  -> LFO 0.05..8 Hz (exponential)
//   depth   0..100 %  -> sweep 0..10 ms, capped so the pitch deviation pi * rate * sweep
//                        never exceeds kMaxPitchDeviation (3 %, about 51 cents)
//   hp      0..100 %  -> wet high-pass 10 Hz..2 kHz (exponential, 12 dB/oct), so the
//                        chorus can stay off the bass fundamental
//   volume  dB        -> level of the wet signal (-60..+12 dB)
//   fatness 0..100 %  -> line 2 level; wet = (line1 + f line2) / sqrt (1 + f^2)
//   mix     0..100 %  -> linear crossfade: out = (1 - m) dry + m wet
// The mix is linear because the wet signal is strongly correlated with the dry one.
// Bypass: 5 ms crossfade, the 40 ms of line state are cleared before the next switch-on.
#include "acid/engine/ParamTable.h"
#include "acid/fx/InsertUtil.h"

namespace acid::fx
{
/** One field per row of the Chorus group in ParamTable.h. */
struct ChorusParams
{
    bool on = false;
    float offsetPct = 0.0f;
    float fatnessPct = 0.0f;
    float hpPct = 0.0f;
    float volumeDb = 0.0f;
    float depthPct = 35.0f;
    float ratePct = 35.0f;
    float spatialPct = 50.0f;
    float mixPct = 50.0f;
};

/** Reads the Chorus rows of a parameter snapshot. */
inline ChorusParams chorusParamsFrom (const ParamValues& v) noexcept
{
    const auto f = [&v] (ParamId id) { return v[static_cast<std::size_t> (id)]; };
    ChorusParams p;
    p.on = paramAsBool (v, kChoOn);
    p.offsetPct = f (kChoOffset);
    p.fatnessPct = f (kChoFatness);
    p.hpPct = f (kChoHp);
    p.volumeDb = f (kChoVolume);
    p.depthPct = f (kChoDepth);
    p.ratePct = f (kChoRate);
    p.spatialPct = f (kChoSpatial);
    p.mixPct = f (kChoMix);
    return p;
}

class Chorus
{
public:
    static constexpr double kMaxPitchDeviation = 0.03;

    /** Allocates the lines. Not real-time safe. */
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    /** Once per block, before process(). */
    void setParams (const ChorusParams& p) noexcept;
    /** In place, any numSamples >= 0. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Knob laws, exposed for tests and the editor's value display. */
    static double baseDelaySeconds (double offsetPct) noexcept;
    static double rateHz (double ratePct) noexcept;
    /** Sweep in seconds after the pitch-deviation cap. */
    static double sweepSeconds (double depthPct, double ratePct) noexcept;
    static double highPassHz (double hpPct) noexcept;

private:
    void clearState() noexcept;

    double sampleRate = 48000.0;
    insert::DelayLine line[2];
    insert::Smoother base, sweep, rate, spatial, fat, hpG, wetGain, mixWet;
    insert::SvfCoeffs hpC;
    insert::SvfState hpS[2];
    double phase = 0.0;
    insert::OnOffFade fade;
    bool needsClear = false;
    bool snapNext = true;
};
}
