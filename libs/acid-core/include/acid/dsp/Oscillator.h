#pragma once
// The oscillator: saw core, square shaper and a sub-oscillator divider
// (docs/research/04-dsp-modeling.md section 2.3, 02 sections 2.3 to 2.6).
//
// One relaxation core. The timing capacitor is discharged at a constant rate and snapped
// back up when it reaches its lower threshold, so the saw is a slow linear FALL and a fast
// RISE. A source follower buffers it and rounds the top slightly. The core is kept as the
// fraction of the ramp travelled; volts come from the follower map between the two levels
// in OscillatorParams, which are the levels at the follower output. The square is that saw
// through the Q8 shaper circuit (SquareShaper.h): low and flat at 5.333 V after the reset,
// high and drooping during the last part of each ramp, with a duty cycle that falls as
// pitch rises (0.58 at C1, 0.47 at A2, 0.36 at C5 at the default trims). The
// sub-oscillator is a divider clocked by the saw reset.
//
// The core free-runs. There is no way to set its phase: note-ons must not touch it.
//
// Call tick() once per sample of the voice's oversampled rate, with the frequency in Hz;
// the frequency may change on every call (slide, drift, vibrato).
//
// Units. All three outputs are volts at the waveform switch node, before the filter's
// 220 k / 2.2 k input divider:
//   saw     5.5 V to 11.3 V (5.8 V peak to peak), mean 8.45 V, fundamental 1.88 V
//   square  5.333 V to about 8.8 V (3.3 to 3.6 V peak to peak), mean 6.5 V (C6) to 6.9 V
//           (C1), fundamental 1.9 to 2.05 V
//   sub     +-1.5 V around zero, fundamental 1.91 V
// The stock circuit AC-couples the selected waveform (1 uF into 222 k, 0.72 Hz) and divides
// it by 101. In the filter's normalised units:
//   x = highPass_0.72Hz (volts) * kFilterInputDivider / (2 V_T)          (2 V_T = 51.4 mV)
// which puts the saw at +-0.56 and the square at about 0.67 peak to peak, as the research
// expects. Do not normalise the two waveforms to equal peaks: the level difference is the
// circuit. The sub is meant to be added to the selected waveform before the high-pass,
// scaled by the Mods layer's level (1.0 at 100 %).
// Pre-charge the coupling high-pass with sawMeanVolts() or kSquareTypicalMeanVolts, or the
// 8 V of DC will thump for half a second after prepare. Switching between saw and square
// steps the DC by 1.6 V, which the hardware's switch does too.
//
// Band-limiting. The outputs are the plain sampled waveforms plus corrections at events:
// the reset step of all three outputs gets a 4-point polyBLEP (cubic B-spline kernel) whose
// height is the circuit's actual level on both sides of the reset, and slope changes get
// the matching polyBLAMP. The square's turn-on edge is fast but analytic, not a step: when
// it is too fast for the sample rate it is solved in sub-steps and the corners between the
// sub-steps are corrected the same way. A 3-tap equaliser removes the kernel's
// high-frequency droop (harmonics are level to 0.05 dB at 20 kHz when the rate is 176.4 kHz
// or more); it makes each band-limited step over- and undershoot by 4 % of its height for
// one sample either side, which is the price of a flat pass band and not the circuit.
// The outputs are late by kLatencySamples oversampled samples, all three by the same
// amount. Measured after 4x decimation: non-harmonic components below -100 dBc for the saw
// and below -94 dBc for the square up to 3.5 kHz.
//
// Cost: the square shaper is most of it. Its emitter capacitor is state that a later switch
// to the square must find settled, so it always runs, but OscillatorParams::squareInUse
// lets it idle at a coarse step while the saw is selected.
//
// Threading: one instance, one thread. tick(), setParams() and reset() never allocate,
// lock or throw. prepare() does not allocate either.
#include "acid/common/Math.h"
#include "acid/dsp/SquareShaper.h"

namespace acid::dsp
{
/** Trims of the oscillator. Defaults are the stock circuit as the research has it. */
struct OscillatorParams
{
    static constexpr double kDefaultSawTopVolts = 11.3;
    static constexpr double kDefaultSawBottomVolts = 5.5;
    static constexpr double kPwShiftVoltsPerPercent = 0.01;

    /** Saw level right after the reset, at the follower output. This is the pulse-width
        trim: the square's duty moves by about -0.025 per +0.5 V. Accepted 9.5 to 12.5 V. */
    double sawTopVolts = kDefaultSawTopVolts;

    /** Saw level at which the core resets. Accepted 5.0 to 6.5 V. */
    double sawBottomVolts = kDefaultSawBottomVolts;

    /** Forward current gain of Q8, calibration range 100 to 400. */
    double shaperBeta = SquareShaper::kDefaultBeta;

    /** Emitter capacitor C11 in farads. Scales the square's threshold time constants. */
    double emitterCapFarads = SquareShaper::kC11;

    /** Time the saw is held flat at its top after each reset (the reset latch letting go),
        0 to 50 microseconds. 0 is an ideal reset. */
    double flatTopSeconds = 0.0;

    /** False: the ramp is shortened so the period stays exactly 1 / frequency (a unit whose
        scale trimmer has absorbed the hold). True: the hold adds to the period, as in an
        untrimmed core, and high notes go flat (20 us is 18 cents at 523 Hz). */
    bool flatTopDetunes = false;

    /** Sub-oscillator divider: 1 is one octave below the saw, 2 is two octaves below. */
    int subOctaves = 1;

    /** True while the square output is being listened to. False (the waveform switch is on
        saw) steps the shaper only every 40 microseconds, just enough to keep its emitter
        capacitor where a later switch to the square expects it, and halves the cost of
        tick(). The square output is then a coarse, uncorrected copy and not for listening;
        saw and sub are unaffected, bit for bit. Switching back to true is seamless: the
        first cycle's duty is already within 0.005 of where it would have been. */
    bool squareInUse = true;

    /** The calibration panel's Square PW Shift, -100 to +100 percent, as a saw top level.
        0 is the default 11.3 V. +100 lowers the top by 1.0 V (to 10.3 V), which turns Q8 on
        earlier and widens the high part of the square by 0.04 to 0.06 of the period
        (C3: 0.465 to 0.519); -100 raises the top by 1.0 V (to 12.3 V) and narrows it by
        0.03 to 0.05 (C3: 0.422). That spans the +-0.03 to +-0.05 of duty the research
        gives for unit-to-unit variation. */
    static constexpr double sawTopFromPwShift (double percent) noexcept
    {
        return kDefaultSawTopVolts - kPwShiftVoltsPerPercent * clamp (percent, -100.0, 100.0);
    }
};

/** One oversampled sample of all three outputs, in volts (see the top of this file). */
struct OscillatorOutput
{
    double saw = 0.0;
    double square = 0.0;
    double sub = 0.0;
};

class Oscillator
{
public:
    /** R70 / (R62 + R70): the filter's input divider, 1 / 101. */
    static constexpr double kFilterInputDivider = 2.2 / 222.2;

    /** Peak level of the sub-oscillator square, volts either side of zero. Chosen so its
        fundamental (4 / pi times this, 1.91 V) equals the saw's. */
    static constexpr double kSubAmplitudeVolts = 1.5;

    /** A typical mean of the square output, for pre-charging the coupling capacitor. The
        true mean moves from 6.9 V at C1 to 6.5 V at C6. */
    static constexpr double kSquareTypicalMeanVolts = 6.8;

    /** Output delay in oversampled samples: 2 for the band-limiting kernel, 1 for its
        equaliser. The same for saw, square and sub. */
    static constexpr int kLatencySamples = 3;

    /** Highest frequency accepted, as a fraction of the oversampled rate. */
    static constexpr double kMaxFrequencyOverFs = 0.45;

    static constexpr double kMaxFlatTopSeconds = 50.0e-6;

    /** The turn-on edge of the square is solved in sub-steps short enough that the saw
        moves no more than this between them (a quarter of the thermal voltage). */
    static constexpr double kEdgeResolutionVolts = SquareShaper::kThermalVolts * 0.25;

    /** Refinement starts when Q8's emitter-base voltage is about to pass this (collector
        current still about a five-hundredth of its final value) ... */
    static constexpr double kEdgeStartVolts = 0.45;

    /** ... and lasts for this much saw travel, in volts: until Q8 is saturated and the top
        of the pulse is smooth again. */
    static constexpr double kEdgeWindowVolts = 0.30;

    static constexpr int kMaxSubSteps = 16;

    /** Step of the shaper while the square is not in use. */
    static constexpr double kIdleStepSeconds = 40.0e-6;

    /** Usable at once, prepared for 192 kHz; call prepare() with the real rate. */
    Oscillator() noexcept { prepare (192000.0); }

    /** sampleRate is the rate tick() will be called at (the voice's oversampled rate).
        Derives every constant from it and resets. */
    void prepare (double sampleRate) noexcept;

    /** Power-on state: ramp at its top, shaper at rest, divider cleared, output history
        empty. For prepare and panic only. It is not a note-on action: the core free-runs
        across notes, and resetting it per note is audibly wrong. */
    void reset() noexcept;

    /** Cheap; call once per block or whenever a trim moves. Values are clamped to their
        accepted ranges. A change of level takes effect at once (no smoothing here). */
    void setParams (const OscillatorParams& newParams) noexcept;
    const OscillatorParams& getParams() const noexcept { return params; }

    /** One oversampled sample. frequencyHz is the pitch; it is clamped to
        [0, kMaxFrequencyOverFs * sampleRate] and NaN is taken as 0. */
    OscillatorOutput tick (double frequencyHz) noexcept;

    /** The follower's static map: saw volts for a ramp phase of 0 (just after the reset,
        at the top) to 1 (the reset threshold, at the bottom). A straight fall with the top
        corner rounded off; exposed for reference models and plots. */
    double sawVoltsAtPhase (double phase) const noexcept
    {
        const double l = mapOffset + kMapSlope * (1.0 - phase);
        const double d = l - mapCeiling;
        return bottomVolts + spanVolts * 0.5 * (l + mapCeiling - std::sqrt (d * d + kMapKnee4));
    }

    /** Exact mean of the saw output at the current trims, in volts. */
    double sawMeanVolts() const noexcept { return sawMean; }

    /** Ramp phase of the core right now, 0 to 1 (read-only: there is no setter). */
    double phase() const noexcept { return rampPhase; }

    /** The shaper, for meters, plots and solver statistics. */
    const SquareShaper& getShaper() const noexcept { return shaper; }
    void resetStats() noexcept { shaper.resetStats(); }

private:
    // Follower map shape, as fractions of the saw's peak-to-peak. The linear part of the
    // ramp is steeper than the chord by kMapOvershoot and the top corner is rounded with a
    // hyperbolic knee of kMapKnee, so the top 5 % of the swing is compressed by about 2 %.
    static constexpr double kMapOvershoot = 0.02;
    static constexpr double kMapSlope = 1.0 + kMapOvershoot;
    static constexpr double kMapKnee = 0.02;
    static constexpr double kMapKnee4 = 4.0 * kMapKnee * kMapKnee;

    /** One output's band-limiting state: the plain samples on their way out, plus the
        corrections that events have left for the outputs still in flight. */
    struct Channel
    {
        // A correction stays in flight for four outputs of kernel and two more of equaliser.
        static constexpr int kLiveSamples = 6;

        double nv1 = 0.0, nv2 = 0.0;                    // plain samples n-1, n-2
        double hx1 = 0.0, hx2 = 0.0;                    // band-limited samples n-3, n-4
        double res[4] = { 0.0, 0.0, 0.0, 0.0 };         // corrections for outputs n-2 .. n+1
        int live = 0;                                   // samples with corrections in flight

        void fill (double v) noexcept
        {
            nv1 = nv2 = hx1 = hx2 = v;
            res[0] = res[1] = res[2] = res[3] = 0.0;
            live = 0;
        }

        /** Adds weight * shape[0..3] to the corrections in flight. */
        void deposit (double weight, const double (&shape)[4]) noexcept
        {
            for (int i = 0; i < 4; ++i)
                res[i] += weight * shape[i];
            live = kLiveSamples;
        }

        /** Takes the plain sample for output n, returns the finished output n-3. */
        double next (double sample) noexcept
        {
            double y;
            if (live > 0)
            {
                // Add the correction and run the kernel's 3-tap equaliser.
                --live;
                const double x = nv2 + res[0];
                y = (4.0 / 3.0) * hx1 - (1.0 / 6.0) * (x + hx2);
                hx2 = hx1;
                hx1 = x;
                res[0] = res[1];
                res[1] = res[2];
                res[2] = res[3];
                res[3] = 0.0;
            }
            else
            {
                // Nothing in flight: a pure delay.
                y = hx1;
                hx2 = hx1;
                hx1 = nv2;
            }
            nv2 = nv1;
            nv1 = sample;
            return y;
        }
    };

    void rampTo (double end, double du, double inc, double anchorU, bool endsAtReset) noexcept;
    void segmentTo (double end, double sawEnd) noexcept;
    void flushIdle (double sawVolts) noexcept;
    void fire (double inc) noexcept;
    void updateSubValue() noexcept;

    OscillatorParams params;
    SquareShaper shaper;

    double samplePeriod = 1.0 / 192000.0;
    double maxFrequency = 0.45 * 192000.0;

    // derived from params
    double topVolts = OscillatorParams::kDefaultSawTopVolts;
    double bottomVolts = OscillatorParams::kDefaultSawBottomVolts;
    double spanVolts = topVolts - bottomVolts;
    double mapOffset = 0.0;
    double mapCeiling = 1.0;
    double slopeAtTop = 0.5;        // d(saw) / d(ramp travel) at the two ends, in volts
    double slopeAtBottom = 1.0;
    double sawMean = 0.0;
    double holdSamples = 0.0;
    double flatTop = 0.0;

    // core state
    double rampPhase = 0.0;         // 0 at the top, 1 at the reset threshold
    double holdLeft = 0.0;          // samples of flat top still to run
    bool divideBy2 = false;
    bool divideBy4 = false;
    double subValue = -kSubAmplitudeVolts;

    // coarse stepping while the square is not in use
    double idleStride = 8.0;        // samples per shaper step
    double idleSpan = 0.0;          // time the shaper is behind, in samples

    // edge refinement
    double edgeVoltsLeft = 0.0;
    bool edgeDone = false;          // the edge of this ramp has had its window

    // position inside the current sample and the signal values there
    double u = 0.0;
    double nodeSaw = OscillatorParams::kDefaultSawTopVolts;
    double nodeSquare = 0.0;        // collector volts above its rail
    double squareSlope = 0.0;       // of the last segment, volts per sample
    bool afterPartial = false;      // the last segment was a piece of a sample

    Channel sawOut, squareOut, subOut;
};
}
