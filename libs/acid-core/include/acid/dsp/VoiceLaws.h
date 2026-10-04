#pragma once
// Knob laws and component values of the voice's control path
// (docs/research/03 sections 3 to 7 and 11, docs/research/02 section 3.8).
//
// Every function here maps a control position to the physical quantity the circuit sees.
// Positions are fractions of travel, 0..1 (the parameter table's percent divided by 100),
// unless a name says otherwise. Nothing depends on a sample rate.
//
// Where the stock instrument sits on each knob:
//   cutoff      0..100 % is the stock pot: 314 Hz to 2394 Hz nominal, linear in Hz through
//               an audio taper (50 % is 626 Hz nominal, about 520 Hz resting)
//   envmod      0..100 % is the stock pot: a sixth of the envelope to all of it; the depth
//               also rises by 6 % from the bottom of the cutoff knob to the top
//   decay       0..100 % is the stock pot: time constant 80 ms to 1.08 s
//   acc_decay   0 % is the stock accent decay (80 ms); up to 1.3 s at 100 %
//   slide_time  50 % is the stock 22 ms; 3.7 ms at 0 %, 132 ms at 100 %
//   resonance   0..100 % is the stock pot rotation (linear, with the loading bow)
//   accent      0..100 % is the stock pot rotation (linear)
//   env_attack  0 % is the stock 0.1 ms charge; up to 30 ms at 100 %
#include "acid/common/Math.h"

namespace acid::dsp::laws
{
//==============================================================================
// Component values (service-notes designators).

inline constexpr double kVq9 = 3.3;                 // the filter control bus, volts
inline constexpr double kMegPeakVolts = 10.0;       // main envelope peak at the buffer output
inline constexpr double kKEnv = 0.54;               // octaves per volt, Env Mod wiper to cutoff
inline constexpr double kKAcc = 1.19;               // octaves per volt, accent sweep wiper to cutoff
inline constexpr double kKEnvExtended = 1.69;       // R63 paralleled with 100 k (extended range)

inline constexpr double kR46 = 47.0e3;              // accent sweep series resistor
inline constexpr double kR72 = 102.1e3;             // sweep wiper to the summing node, plus the node itself
inline constexpr double kResPotOhms = 50.0e3;       // VR4b, second gang of the resonance pot
inline constexpr double kAccentPotOhms = 50.0e3;    // VR7
inline constexpr double kAccentVcaOhms = 69.0e3;    // R119 + R120
inline constexpr double kAccentDiodeVolts = 0.45;   // D24 at the currents involved
inline constexpr double kAccentVcaThresholdVolts = 1.2;     // D27 plus the control pin
inline constexpr double kAccentVcaLagSeconds = 0.7e-3;      // R119 with C36, diode conducting

inline constexpr double kVegFullAmps = 47.7e-6;     // VCA control current of a plain note at onset
inline constexpr double kVegSeconds = 1.5;          // R123 * C42
inline constexpr double kVegFloor = -0.118;         // where the decay would settle (below zero)
inline constexpr double kGateVolts = 11.9;          // analog gate, fully on
inline constexpr double kGateToVegVolts = 10.6;     // gate volts that swing the control current fully
inline constexpr double kGateRecoverySeconds = 0.100;       // C72 through R142
inline constexpr double kGateDeadSeconds = 0.017;           // release time before C72 starts to recover
inline constexpr double kGateHoldSeconds = 0.008;
inline constexpr double kGateFallSeconds = 0.0045;
inline constexpr double kGateMinDelaySeconds = 0.0003;

inline constexpr double kSlideStockSeconds = 0.022; // 100 k * C35
inline constexpr double kMegBaseSeconds = 0.080;    // decay with the pot shorted (SPEC section 7)
inline constexpr double kMegPotSeconds = 1.0;       // VR6 1 M * C62 1 uF
inline constexpr double kMegAttackSeconds = 0.0001; // R152 * C62

inline constexpr double kCutoffMinHz = 314.0;       // nominal cutoff, stock pot fully down
inline constexpr double kCutoffMaxHz = 2394.0;      // and fully up
inline constexpr double kCutoffKnobOctaves = 2.930586688051875;    // log2 (2394 / 314)
inline constexpr double kCutoffExtMinHz = 100.0;    // extended range (R47 reduced)
inline constexpr double kCutoffExtMaxHz = 5000.0;   // an octave above stock
inline constexpr double kCutoffKneeHz = 20000.0;    // where Q11 starts to run out of headroom
inline constexpr double kCutoffCeilingHz = 30000.0; // and where it saturates

// Envelope and accent depth against the cutoff knob. The unit the cutoff range was measured
// on sweeps further at high cutoff settings than at low ones: 5.06 octaves with Cutoff and
// Env Mod at maximum, which is 0.935 of the schematic's 5.41, against the 0.88 that
// reproduces its envelope peak at the bottom of the knob (the CV scale trim's default).
inline constexpr double kDepthRiseScaleAtTop = 5.059 / 5.41;
inline constexpr double kDepthRiseScaleAtBottom = 0.88;
inline constexpr double kDepthRiseTop = kDepthRiseScaleAtTop / kDepthRiseScaleAtBottom;    // 1.0627

inline constexpr double kTuneReferenceHz = 110.0;   // A2
inline constexpr double kTuneReferenceVolts = 2.75; // pitch CV of A2 (MIDI 45)

//==============================================================================
/** Audio ("A") taper: resistance fraction for a rotation of 0..1, (b^x - 1) / (b - 1)
    with b = 32, which puts 15 % of the track at half rotation. */
inline double taperA (double x) noexcept
{
    constexpr double log2b = 5.0, b = 32.0;
    return (std::exp2 (log2b * clamp (x, 0.0, 1.0)) - 1.0) / (b - 1.0);
}

/** Pitch CV in volts for a MIDI note: 1 V per octave, MIDI 24 (C1) = 1.000 V,
    MIDI 45 (A2) = 2.750 V. Fractional notes allowed. */
constexpr double pitchVolts (double midiNote) noexcept { return (midiNote - 12.0) / 12.0; }

/** Oscillator frequency. cvVolts is the (slid) pitch CV, octaveWidth the TM5 trim in
    octaves per volt (1.0 calibrated), tuneSemitones the tuning knob. The converter is
    trimmed so A2 stays at 110 Hz whatever the octave width. */
inline double oscillatorHz (double cvVolts, double octaveWidth, double tuneSemitones) noexcept
{
    return kTuneReferenceHz * std::exp2 (octaveWidth * (cvVolts - kTuneReferenceVolts) + tuneSemitones / 12.0);
}

/** Cutoff knob: the nominal resonant frequency in Hz (summing node at the 3.3 V bus).
    The pot feeds a current, so the law is linear in Hz through the audio taper.
    Stock: 314 Hz to 2394 Hz. Extended: 100 Hz to 5 kHz. */
inline double cutoffNominalHz (double x, bool extended = false) noexcept
{
    const double a = taperA (x);
    return extended ? kCutoffExtMinHz + (kCutoffExtMaxHz - kCutoffExtMinHz) * a
                    : kCutoffMinHz + (kCutoffMaxHz - kCutoffMinHz) * a;
}

/** TM3 expressed as an equivalent offset in millivolts: 500 mV is one octave, as a
    multiplier on the nominal cutoff. */
inline double tm3Multiplier (double millivolts) noexcept { return std::exp2 (millivolts / 500.0); }

/** Multiplier on the octaves per volt of the envelope and the accent sweep, as a function
    of the nominal cutoff (knob and TM3, summing node at the 3.3 V bus). 1 at the bottom of
    the stock knob (314 Hz) and below, rising linearly in octaves of nominal cutoff to
    kDepthRiseTop (1.063) at the top of it (2394 Hz) and above. With the CV scale trim at
    its default this takes the envelope peak with Cutoff and Env Mod at maximum from
    21.7 kHz to 24.6 kHz, where the measured unit has it, and leaves the bottom of the
    knob alone. */
inline double depthRise (double nominalHz) noexcept
{
    const double c = clamp (std::log2 (std::max (nominalHz, 1.0) / kCutoffMinHz) / kCutoffKnobOctaves, 0.0, 1.0);
    return 1.0 + (kDepthRiseTop - 1.0) * c;
}

/** Env Mod knob: octaves of cutoff per volt of (envelope - 3.3 V), before the CV scale
    trim. Stock: the pot (audio taper) leaves a sixth of the envelope at minimum
    (R61 10 k under the 50 k track), and the wiper's source resistance loads R63 by up to
    6 % mid-track: 0.087 oct/V at 0 %, 0.54 oct/V at 100 %. Extended: true zero at
    minimum and 3.1 times the depth at maximum. */
inline double envModOctavesPerVolt (double x, bool extended = false) noexcept
{
    const double a = taperA (x);
    if (extended)
    {
        const double source = a * (1.0 - a) * 50.0e3;
        return kKEnvExtended * a * 68.75e3 / (68.75e3 + source);
    }
    const double g = (10.0e3 + a * 50.0e3) / 60.0e3;
    const double source = (10.0e3 + a * 50.0e3) * (1.0 - a) * 50.0e3 / 60.0e3;
    return kKEnv * g * 220.0e3 / (220.0e3 + source);
}

/** Decay knob: main envelope time constant in seconds on a plain note,
    80 ms + 1 s * taper: 80 ms, 230 ms at half, 1.08 s (10 % times 184 ms to 2.49 s). */
inline double decaySeconds (double x) noexcept { return kMegBaseSeconds + kMegPotSeconds * taperA (x); }

/** Accent Decay knob: envelope time constant while the accent latch is set.
    0 % is the stock 80 ms (the Decay pot shorted); exponential up to 1.3 s. */
inline double accentDecaySeconds (double x) noexcept { return expMap (x, kMegBaseSeconds, 1.3); }

/** Env Attack knob: charge time constant of the envelope capacitor. 0 % is the stock
    0.1 ms; exponential up to 30 ms. The envelope reaches its peak after 1.6 of these. */
inline double envAttackSeconds (double x) noexcept { return expMap (x, kMegAttackSeconds, 0.030); }

/** Slide Time knob: pitch lag time constant. 50 % is the stock 22 ms; a factor of six
    either way (3.7 ms to 132 ms, the latter being a 500 k slide pot). */
inline double slideSeconds (double x) noexcept { return kSlideStockSeconds * std::pow (6.0, 2.0 * clamp (x, 0.0, 1.0) - 1.0); }

/** Vibrato Speed knob: how long each of the two alternating notes lasts.
    200 ms at 0 %, 50 ms at 50 % (a 10 Hz trill), 12.5 ms at 100 %. */
inline double vibratoHalfPeriodSeconds (double x) noexcept { return expMap (x, 0.200, 0.0125); }

/** Vibrato Depth knob: distance between the two alternating notes in semitones,
    centred on the played note. 0 to 2 semitones, linear. */
inline double vibratoSpreadSemitones (double x) noexcept { return 2.0 * clamp (x, 0.0, 1.0); }

/** Accent pot wiper voltage per volt of envelope: the pot fraction, loaded by a branch of
    loadOhms hanging on the wiper. */
inline double accentWiperGain (double x, double loadOhms = kAccentVcaOhms) noexcept
{
    const double k = clamp (x, 0.0, 1.0);
    return k / (1.0 + k * (1.0 - k) * kAccentPotOhms / loadOhms);
}

/** Filter Overdrive (Mods): gain on the oscillator into the filter. 0 % is stock (1.0);
    exponential up to 66.6 (the 220 k input resistor replaced by 3.3 k). */
inline double overdriveGain (double x) noexcept { return std::pow (66.6, clamp (x, 0.0, 1.0)); }

/** Filter Tracking (Mods): Hz of nominal cutoff per volt of pitch CV above 2 V (C2).
    Linear, 0 to 2700 Hz per volt. */
inline double trackingHzPerVolt (double x) noexcept { return 2700.0 * clamp (x, 0.0, 1.0); }

/** Filter FM (Mods): Hz of cutoff per unit of voice output (1.0 = full scale), squared
    taper, 0 to 20 kHz. A plain note peaks near 0.25, a full accent near 0.9. */
inline double filterFmHzPerUnit (double x) noexcept
{
    const double k = clamp (x, 0.0, 1.0);
    return 20000.0 * k * k;
}

/** Soft ceiling on the resonant frequency: unchanged up to 20 kHz, then bending over to
    saturate at 30 kHz, as the tail-current transistor runs out of headroom. */
inline double cutoffCeiling (double hz) noexcept
{
    if (! (hz > kCutoffKneeHz))
        return hz;
    const double span = kCutoffCeilingHz - kCutoffKneeHz;
    return kCutoffKneeHz + span * std::tanh ((hz - kCutoffKneeHz) / span);
}

/** Gate turn-on delay in seconds after the gate has been low for offSeconds:
    maxDelay * (1 - exp (-(off - 17 ms) / 100 ms)), never under 0.3 ms (or maxDelay if
    that is smaller). 5.4 ms after a long pause, 1.9 ms in 16ths at 125 BPM. */
inline double gateTurnOnDelay (double offSeconds, double maxDelaySeconds) noexcept
{
    const double floorSeconds = std::min (kGateMinDelaySeconds, std::max (maxDelaySeconds, 0.0));
    if (! (offSeconds > kGateDeadSeconds))
        return floorSeconds;
    const double d = maxDelaySeconds * (1.0 - std::exp (-(offSeconds - kGateDeadSeconds) / kGateRecoverySeconds));
    return std::max (d, floorSeconds);
}
}
