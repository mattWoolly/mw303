#pragma once
// The distortion block: one parametric engine, eight clipper families, a voicing table.
//
// Signal path (per channel, the dynamics detector is linked across channels):
//
//   in -> [dynamics, when pre] -> clean low band split (low_keep) ---------------------+
//                                   high band -> pre high-pass -> pre peaking EQ        |
//                                   -> drive -> CLIPPER (oversampled 1x/2x/4x/8x)       | delayed by
//                                   -> tilt -> color filter -> post high-pass           | the oversampler
//                                   -> DC blocker -------------------------------> (+) <+ latency
//                                                                                   |
//   out <- on/off crossfade with the delayed dry input <- [dynamics, when post] <- trim
//
// Color. The knob position times the range gives an amount u in -1..+1. The filter is a
// first-order high shelf (gain from 1 at DC to G at the top, corner placed so G and 1/G
// are exact inverses) followed by a resonant peaking band at the same corner, Q 0.5..8
// from the resonance. The kColorMaxDb budget (18 dB at u = +-1) is split between them:
// the resonance takes 0.6 x reso of it, the shelf the rest. As |u| grows the corner also
// sweeps down from colorFreq by up to one octave. Positive u boosts both, negative u cuts
// both by the same amounts (the cut response is the exact inverse of the boost), so:
// brighter never exceeds kColorMaxDb, darker never boosts any frequency, and the gain at
// every frequency rises monotonically with the knob. At u = 0 the stage is bypassed.
//
// Level. After the clipper the block applies a make-up gain that depends on the family and
// the preamp gain (distortionFamilyLevelDb, measured), so a bass line at the nominal level
// comes out near its clean RMS level at any drive: the drive knob changes the character,
// not the loudness, and voicings level-match at every drive. While the clipper is still
// close to linear (low preamp gain) the level change of the pre-clipper shaping (pre
// high-pass and pre EQ) is also taken back out. dist_volume trims what is left.
//
// Bias glides over 200 ms, slower than the 6 Hz DC blocker follows, and every family
// removes its own resting output, so a bias move in silence moves nothing and with signal
// comes out as a slow drift rather than a thump.
//
// Aliasing. The memoryless stages of the fold, fuzz and console families run with
// first-order antiderivative antialiasing (DistortionClippers.h). minimumOversamplingFor()
// gives the factor each family needs to keep aliasing 30 dB down at full drive.
//
// Bad input. A NaN or infinite input sample is taken as silence (in the dry path too) and
// anything beyond +-1e6 is clamped, so nothing non-finite can reach a filter or the clipper.
//
// Call order: prepare() once (and again when the sample rate changes), then on the audio
// thread setParams() once per block followed by process(). setOversampling() may be called
// on the audio thread at any time. Nothing after prepare() allocates, locks or throws.
//
// Latency: latencySamples() is the delay of the oversampling filters at the requested
// factor (0 at 1x). The block keeps that delay when `on` is false: it then outputs the
// input delayed by latencySamples() and otherwise bit-identical, so a host's delay
// compensation stays valid whether the distortion is on or off. At 1x, or with no latency,
// an off block does not touch the buffers at all.
#include "acid/engine/ParamTable.h"
#include "acid/fx/DistortionClippers.h"
#include "acid/fx/DistortionFilters.h"
#include "acid/fx/Dynamics.h"
#include "acid/fx/Oversampler.h"
#include <array>

namespace acid::fx
{
/** One field per Distortion and Distortion Voicing row of ParamTable.h, in the units of
    that table. Defaults are the table defaults. */
struct DistortionParams
{
    // Front panel
    bool on = false;            // dist_on
    int model = 1;              // dist_model: index into distortionVoicings(). Carried for the UI;
                                // the DSP reads the voicing fields below, never the table.
    float dynamics = 0.0f;      // dist_dynamics, %
    bool dynPost = false;       // dist_dyn_post: false = before the clipper, true = after
    float drive = 30.0f;        // dist_drive, %
    float color = 0.0f;         // dist_color, -100..100 %: up brighter, down darker, 0 exactly neutral

    // Voicing
    int clipper = 0;            // dist_clipper, a ClipperFamily
    float preMin = 0.0f;        // dist_pre_min, %: preamp setting at drive 0
    float preMax = 75.0f;       // dist_pre_max, %: preamp setting at drive 100
    float knee = 100.0f;        // dist_knee, %: 100 the softest knee the family has, 0 the hardest
    float bias = 0.0f;          // dist_bias, -100..100 %
    float lowKeep = 0.0f;       // dist_low_keep, Hz: crossover of the clean low band, 0 = off
    float preHp = 0.0f;         // dist_pre_hp, Hz, 0 = off
    bool preEqOn = false;       // dist_pre_eq_on
    float preEqGain = 0.0f;     // dist_pre_eq_gain, dB
    float preEqFreq = 200.0f;   // dist_pre_eq_freq, Hz
    float preEqWidth = 1.5f;    // dist_pre_eq_width, octaves
    bool tiltOn = false;        // dist_tilt_on
    float tiltAmt = 0.0f;       // dist_tilt_amt, dB: treble up and bass down by half of this each
    float tiltFreq = 1000.0f;   // dist_tilt_freq, Hz: the pivot
    float colorFreq = 2000.0f;  // dist_color_freq, Hz: corner of the color filter at the knob's centre
    float colorReso = 20.0f;    // dist_color_reso, %: share and sharpness of the resonant peak
    float colorRange = 100.0f;  // dist_color_range, %: how far the color knob sweeps the filter
    float postHp = 0.0f;        // dist_post_hp, Hz, 0 = off
    float volume = 0.0f;        // dist_volume, dB
};

/** Copies the Distortion and Distortion Voicing rows out of a parameter snapshot. */
DistortionParams distortionParamsFrom (const ParamValues&) noexcept;

/** Preamp gain in dB for a preamp setting in percent (0 % is unity, 100 % is +60 dB). */
double distortionPreampDb (double preampPercent) noexcept;

/** Make-up gain in dB the block applies after a clipper family at a preamp gain (dB), so
    that every family follows the level of the diode pair across the drive range. 0 for
    the diode pair. Measured, see Distortion.cpp. */
double distortionFamilyLevelDb (int clipperFamily, double preampDb) noexcept;

class Distortion
{
public:
    /** Samples between control updates (smoothing steps and coefficient refreshes). */
    static constexpr int kControlInterval = 16;

    /** Designs every rate-dependent part and resets. maxBlockSize is accepted for symmetry
        with the other blocks: process() takes blocks of any size. Does not allocate. */
    void prepare (double sampleRate, int maxBlockSize);

    /** All state to rest. Parameters and the oversampling factor are kept and applied
        without smoothing on the next process(). */
    void reset() noexcept;

    /** 1, 2, 4 or 8. Real-time safe. While running, the change is made under a few
        milliseconds of fade so it does not click. latencySamples() reflects it at once. */
    void setOversampling (int factor) noexcept;
    int oversampling() const noexcept { return requestedFactor; }

    /** Once per block, before process(). Values are clamped to their table ranges.
        Drive, color, knee, bias, trim, tilt (amount, pivot and its on switch) and pre EQ
        gain (and its on switch) are smoothed. A change of clipper family or dynamics
        placement, or a filter switching in or out (low keep, pre and post high-pass), is
        made under a short fade (kDuckSeconds each way); the rest take effect within
        kControlInterval samples. On the first block after reset() everything applies at
        once. */
    void setParams (const DistortionParams&) noexcept;

    /** In place, any block size. right may be nullptr (or equal to left) for mono. While
        both channels have carried identical samples since the last reset, only one is
        computed, so a mono source costs half. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Delay through the block in samples at the requested oversampling factor.
        Constant for a given sample rate and factor, on or off. */
    int latencySamples() const noexcept;

    /** Gain of the dynamics stage at the last sample, dB (0 when it is not running). */
    double dynamicsGainDb() const noexcept { return dynamics.isActive() ? dynamics.gainDb() : 0.0; }

private:
    static constexpr int kDelaySize = 256;   // power of two, above the largest latency

    struct Channel
    {
        std::array<float, kDelaySize> dry {};
        std::array<double, kDelaySize> low {};
        int dryPos = 0, lowPos = 0;
        detail::SvfState splitA, splitB, preHp, preEq, color, postHp;
        detail::OnePoleState tilt, colorShelf, dcBlock;
        detail::ClipperState clip;
        Oversampler os;

        void resetWet() noexcept;
        void resetAll() noexcept;
    };

    void controlTick() noexcept;
    void applyStructure() noexcept;
    void applySwitches() noexcept;
    bool switchesPending() const noexcept;
    void updateCoefficients (bool force) noexcept;
    void flushDenormals() noexcept;
    void processSpan (float* left, float* right, int n) noexcept;
    void processWet (Channel&, double* buffer, int n) noexcept;

    double sampleRate = 48000.0;
    DistortionParams target {};
    DistortionParams applied {};

    // Smoothed controls. The tilt and the pre EQ glide in dB (an "off" switch is a target of
    // 0 dB), so turning them on or off, or picking a voicing with other values, is a short
    // glide and not a step.
    double driveDb = 0.0, knee = 1.0, bias = 0.0, colorAmt = 0.0, trimDb = 0.0;
    double tiltDb = 0.0, tiltLogHz = 0.0, preEqDb = 0.0, preShapeDb = 0.0;
    double tiltDbApplied = -1.0e9, tiltLogHzApplied = -1.0e9, preEqDbApplied = -1.0e9;
    double splitLog = 0.0, preHpLog = 0.0, postHpLog = 0.0, preEqLog = 0.0, preEqWidth = 1.5;   // corners in log2 Hz
    double splitLogApplied = -1.0e9, preHpLogApplied = -1.0e9, postHpLogApplied = -1.0e9;
    double preEqLogApplied = -1.0e9, preEqWidthApplied = -1.0e9;
    double preShapeTarget = 0.0, preHpHz = 0.0;
    DistortionParams shapingApplied {};    // the pre-clipper shaping preShapeTarget was computed for
    double smoothCoef = 1.0, biasSmoothCoef = 1.0;
    bool snap = true;

    // Structure
    int requestedFactor = 1, activeFactor = 1, activeFamily = 0, latency = 0;
    bool wetActive = false;
    double mix = 0.0, mixStep = 1.0;       // 0 dry .. 1 wet
    double duck = 1.0, duckStep = 1.0;     // output gain during a structural change
    int duckDir = 0;                       // -1 fading out, +1 fading in, 0 idle
    int duckHold = 0;                      // samples to stay silent at the bottom of a duck
    bool rightFollowsLeft = true;
    int samplesToTick = 0;

    // Coefficients
    detail::SvfCoeffs splitCoefs, preHpCoefs, preEqCoefs, colorCoefs, postHpCoefs;
    detail::OnePoleCoeffs tiltCoefs, colorShelfCoefs, dcCoefs;
    bool splitOn = false, preHpOn = false, preEqOn = false, postHpOn = false, colorOn = false;
    double preEqMix = 0.0, tiltDirect = 1.0, tiltMix = 0.0, trimGain = 1.0;
    double colorShelfMix = 0.0, colorBellMix = 0.0;
    double colorAmtApplied = 0.0;
    bool activeDynPost = false;            // where the dynamics stage runs; switched under the duck
    detail::ClipperCoeffs clipCoefs;
    double clipDriveDb = -1.0, clipKnee = -1.0, clipBias = -2.0;
    bool clipFresh = true;   // the clipper state was just reset: settle it on the next refresh

    Dynamics dynamics;
    std::array<Channel, 2> channels {};
};
}
