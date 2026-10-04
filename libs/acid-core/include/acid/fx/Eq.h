#pragma once
// Four-band parametric EQ: four peaking (bell) bands, each with frequency, width in
// octaves and gain. Each band is the bilinear transform of the analog peaking prototype
//
//     H(s) = (s^2 + s A / Q + 1) / (s^2 + s / (A Q) + 1),   A = 10^(gain / 40),
//     1 / Q = 2 sinh (ln 2 / 2 * width),
//
// with the centre frequency prewarped, so the gain at the centre is exact and the band
// edges (centre * 2^(+-width / 2)) sit at half the gain in dB, apart from bilinear
// compression near Nyquist. It runs as a trapezoidal state variable filter whose g, k and
// mix coefficients are smoothed per sample (20 ms), so automation never zippers.
#include "acid/engine/ParamTable.h"
#include "acid/fx/InsertUtil.h"
#include <array>

namespace acid::fx
{
/** One field per row of the EQ group in ParamTable.h, in the same natural units. */
struct EqParams
{
    bool on = false;
    float lowGainDb = 0.0f,   lowFreqHz = 100.0f,   lowWidthOct = 2.0f;
    float loMidGainDb = 0.0f, loMidFreqHz = 550.0f, loMidWidthOct = 1.5f;
    float hiMidGainDb = 0.0f, hiMidFreqHz = 3500.0f, hiMidWidthOct = 2.0f;
    float highGainDb = 0.0f,  highFreqHz = 10000.0f, highWidthOct = 2.0f;
};

/** Reads the EQ rows of a parameter snapshot. */
inline EqParams eqParamsFrom (const ParamValues& v) noexcept
{
    const auto f = [&v] (ParamId id) { return v[static_cast<std::size_t> (id)]; };
    EqParams p;
    p.on = paramAsBool (v, kEqOn);
    p.lowGainDb = f (kEqLowGain);     p.lowFreqHz = f (kEqLowFreq);     p.lowWidthOct = f (kEqLowWidth);
    p.loMidGainDb = f (kEqLoMidGain); p.loMidFreqHz = f (kEqLoMidFreq); p.loMidWidthOct = f (kEqLoMidWidth);
    p.hiMidGainDb = f (kEqHiMidGain); p.hiMidFreqHz = f (kEqHiMidFreq); p.hiMidWidthOct = f (kEqHiMidWidth);
    p.highGainDb = f (kEqHighGain);   p.highFreqHz = f (kEqHighFreq);   p.highWidthOct = f (kEqHighWidth);
    return p;
}

class Eq
{
public:
    static constexpr int kNumBands = 4;

    /** Allocates nothing; may be called again to change the rate. Leaves the EQ reset. */
    void prepare (double sampleRate, int maxBlockSize) noexcept;
    /** Clears filter state and jumps every smoothed value to its target. */
    void reset() noexcept;
    /** Once per block, before process(). Values outside the table ranges are clamped. */
    void setParams (const EqParams& p) noexcept;
    /** In place, any numSamples >= 0. On/off crossfades over 5 ms. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** The analytic magnitude of one band (as designed for this sample rate) at hz, linear.
        For tests and for drawing the curve in the editor. */
    static double bandMagnitude (double freqHz, double widthOct, double gainDb, double hz, double sampleRate) noexcept;

private:
    struct Band
    {
        insert::Smoother g, k, m;           // SVF g, SVF k = 1 / (Q A), band-pass mix k (A^2 - 1)
        insert::SvfCoeffs c;
        insert::SvfState s[2];
        bool active = false;                // false: settled at 0 dB, state cleared, skipped
    };

    void setBandTargets (Band& b, double freqHz, double widthOct, double gainDb) noexcept;

    double sampleRate = 48000.0;
    std::array<Band, kNumBands> bands {};
    insert::OnOffFade fade;
    bool snapNext = true;
};
}
