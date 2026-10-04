#pragma once
// Stereo tempo-synced delay with a multimode filter inside the feedback loop.
//
// Times. Synced: num/den of a whole note (4 beats) at params.bpm, times 1.5 for Dotted
// and 2/3 for Triplet. Free: timeSec for both channels. Each time is rounded to the nearest
// sample (error <= 0.5 sample), so echoes land on exact sample positions and repeats are
// never dulled by interpolation. The buffer holds kMaxDelaySeconds; longer requests are
// clamped and timeClamped() reports it for the UI's warning.
//
// Time changes crossfade (50 ms, smoothstep) from the old read tap to the new one, like
// a digital delay: no pitch glide and no click. A change that arrives mid-fade is applied
// when the running fade ends.
//
// Loop, per channel: line input = softLimit(input + feedback); feedback = gain * filter(
// crossfed taps), with a 5 Hz DC blocker. Feedback 100 % is loop gain 1: echoes sustain
// and the soft limit (linear to 1.0, ceiling 2.0) keeps any resonant filter setting
// bounded. The filter is a trapezoidal SVF; Off/LP/BP/HP are crossfaded outputs of the
// same state, so changing the type does not click. Every output is normalised to unity
// peak gain (LP and HP by their resonant peak, BP at its centre), so the loop gain is at
// most the feedback amount: below 100 % echoes always decay, whatever the resonance, and
// resonance shapes the echoes rather than adding gain. Non-finite input is read as 0.
//
// Spread s in -1..+1 is a ping-pong amount. 0: two independent lines. |s| > 0: the lines
// cross-feed by |s| and the input is steered by |s| towards one line (the mid signal goes
// to L for s > 0, to R for s < 0), so a mono source bounces between the sides; at +-100 %
// it is a full ping-pong whose first echo lands left (+) or right (-).
//
// Mix is equal power: dry cos(m pi/2), wet sin(m pi/2).
// Bypass: on/off crossfades over 5 ms; switching off cuts the tail (the lines are cleared
// before the next switch-on).
#include "acid/engine/ParamTable.h"
#include "acid/fx/InsertUtil.h"
#include <cstdint>

namespace acid::fx
{
enum class DelayScale : std::uint8_t { Full = 0, Dotted = 1, Triplet = 2 };
enum class DelayFilter : std::uint8_t { Off = 0, LowPass = 1, BandPass = 2, HighPass = 3 };

/** One field per row of the Delay group in ParamTable.h, plus the tempo. */
struct DelayParams
{
    bool on = false;
    bool sync = true;
    float timeSec = 0.2f;              // free time, both channels, 0.01..1 s
    int leftNum = 3, leftDen = 16;     // 1..32 each
    DelayScale leftScale = DelayScale::Full;
    int rightNum = 3, rightDen = 16;
    DelayScale rightScale = DelayScale::Full;
    float feedbackPct = 35.0f;         // 0..100
    DelayFilter filterType = DelayFilter::Off;
    float filterCutoffHz = 8000.0f;    // 10..16000
    float filterResoPct = 0.0f;        // 0..100: Q 0.707..12
    float spreadPct = 0.0f;            // -100..100
    float mixPct = 30.0f;              // 0..100
    double bpm = 120.0;                // tempo for synced times; invalid values fall back to 120
};

/** Reads the Delay rows of a parameter snapshot. */
inline DelayParams delayParamsFrom (const ParamValues& v, double bpm) noexcept
{
    const auto f = [&v] (ParamId id) { return v[static_cast<std::size_t> (id)]; };
    const auto scale = [&v] (ParamId id) { return static_cast<DelayScale> (clamp (paramAsInt (v, id), 0, 2)); };
    DelayParams p;
    p.on = paramAsBool (v, kDelayOn);
    p.sync = paramAsBool (v, kDelaySync);
    p.timeSec = f (kDelayTime);
    p.leftNum = paramAsInt (v, kDelayLNum);
    p.leftDen = paramAsInt (v, kDelayLDen);
    p.leftScale = scale (kDelayLMod);
    p.rightNum = paramAsInt (v, kDelayRNum);
    p.rightDen = paramAsInt (v, kDelayRDen);
    p.rightScale = scale (kDelayRMod);
    p.feedbackPct = f (kDelayFeedback);
    p.filterType = static_cast<DelayFilter> (clamp (paramAsInt (v, kDelayFiltType), 0, 3));
    p.filterCutoffHz = f (kDelayFiltCutoff);
    p.filterResoPct = f (kDelayFiltReso);
    p.spreadPct = f (kDelaySpread);
    p.mixPct = f (kDelayMix);
    p.bpm = bpm;
    return p;
}

class Delay
{
public:
    static constexpr double kMaxDelaySeconds = 8.0;

    /** Allocates the lines (kMaxDelaySeconds per channel). Not real-time safe. */
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    /** Once per block, before process(). */
    void setParams (const DelayParams& p) noexcept;
    /** In place, any numSamples >= 0. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** True when the last setParams asked for a time longer than kMaxDelaySeconds. */
    bool timeClamped() const noexcept { return clamped; }
    /** Current delay of each channel in samples (after rounding and clamping). */
    std::size_t delaySamples (int channel) const noexcept { return taps.current (channel); }

    /** Synced time in seconds: num/den of a whole note at bpm, with the scale applied. */
    static double syncedSeconds (int num, int den, DelayScale scale, double bpm) noexcept;

private:
    void clearLines() noexcept;

    double sampleRate = 48000.0;
    std::size_t maxSamples = 1;
    insert::DelayLine line[2];
    insert::CrossfadeTaps taps;
    insert::OnOffFade fade;

    insert::Smoother feedback, cross, steerL, steerR, dryGain, wetGain;
    insert::Smoother filterG, filterK;
    insert::Smoother wDirect, wLow, wBand, wHigh;
    insert::SvfCoeffs filterC;
    insert::SvfState filterS[2];
    double peakNorm = 1.0;              // 1 / peak gain of the LP and HP outputs at filterC.k
    double dcX[2] {}, dcY[2] {}, dcCoeff = 0.0;

    bool needsClear = false;
    bool snapNext = true;
    bool clamped = false;
};
}
