#pragma once
// The eight clipper families of the distortion block. Each one is a small circuit model
// (component values, device laws, implicit integration where a capacitor meets a diode),
// not a waveshaping formula.
//
// Conventions shared by every family:
//   * Signals are in full-scale units. kNominalLevel (0.25, about -12 dBFS) is the peak of
//     a plain note from the voice. With the drive gain at 1 a family has unity gain for
//     small signals and starts to clip 6 dB above the nominal level, so a plain note
//     passes nearly clean. As the drive rises the output gain falls by up to those 6 dB
//     (driveCompensation), so a hard-driven family settles near the nominal level.
//   * `gain` is the linear drive gain (1 to 1000). Where the circuit has a gain control
//     the model changes that component; elsewhere the signal is scaled ahead of the stage.
//   * `knee` is 1 for the softest knee the circuit offers and 0 for the hardest.
//   * `bias` (-1 to 1) is a DC error ahead of the clipping element, so it moves the zero
//     crossing and produces even harmonics. Every family subtracts its own resting output
//     (the output for no input at the current bias), so a bias change alone moves no DC.
//     A biased clipper still turns signal into DC (more on one side than the other); the
//     Distortion class removes that with its DC blocker.
//   * A family runs at the oversampled rate. update() derives every coefficient from that
//     rate; nothing is tuned for one rate.
//
// These types live in `detail` because the tests exercise them directly. Everything else
// should go through acid::fx::Distortion.
#include "acid/common/Math.h"
#include "acid/fx/DistortionFilters.h"

namespace acid::fx
{
/** The order matches the `dist_clipper` choice parameter. */
enum class ClipperFamily : int
{
    DiodePair = 0,   // symmetric diode pair to ground, Shockley soft knee
    OpAmpHard = 1,   // slew-limited op-amp gain stage into hard diode clipping
    AsymSoft = 2,    // single-ended JFET stage: square-law cutoff one way, bottoming the other
    ScoopHard = 3,   // transistor booster, op-amp gain, hard diode clip, mid-scoop tone stage
    MidHump = 4,     // clipping diodes in the op-amp feedback path, bass kept out of the gain
    GatedFuzz = 5,   // starved single transistor: gated, strongly asymmetric
    Console = 6,     // class-A feedback amplifier into its rails, then a saturating output transformer
    Fold = 7         // series diode wavefolder
};

inline constexpr int kNumClipperFamilies = 8;

/** Nominal peak level in full-scale units (see the conventions above). */
inline constexpr double kNominalLevel = 0.25;

/** The smallest oversampling factor (1, 2, 4 or 8) at which a family keeps its aliasing at
    least 30 dB below its harmonics for a 3 kHz tone at the nominal level and full drive
    (preamp gain 45 dB, as tests/dist "aliasing" measures it). The engine raises the
    distortion factor of a quality tier to this when the selected family needs more. The
    gated fuzz needs 8x: its edges come from two hard limits in cascade with a steep
    junction between them, which first-order ADAA cannot fully smooth. Out-of-range
    families get the largest value. */
constexpr int minimumOversamplingFor (int clipperFamily) noexcept
{
    constexpr int factors[kNumClipperFamilies] = { 4,    // diode pair
                                                   4,    // op-amp hard
                                                   4,    // asymmetric soft
                                                   4,    // scoop hard
                                                   2,    // mid hump
                                                   8,    // gated fuzz
                                                   2,    // console
                                                   4 };  // fold
    return clipperFamily >= 0 && clipperFamily < kNumClipperFamilies ? factors[clipperFamily] : 8;
}

namespace detail
{
inline constexpr double kThermalVoltage = 0.02585;        // kT/q at 300 K
inline constexpr double kBiasFullScale = 0.5 * kNominalLevel;   // input-referred offset at bias = 1
inline constexpr double kClipHeadroom = 2.0;              // clip level over nominal level at unity drive
inline constexpr int kSolverMaxIterations = 40;

/** Output gain that goes with a drive gain: unity at unity drive, falling to one over the
    headroom as the drive rises, so a hard-driven family settles near the nominal level
    instead of the clip level. */
double driveCompensation (double gain) noexcept;

//==============================================================================
// Scalar solvers. Every implicit equation here is monotone, with one sign of curvature on
// each side of zero. The iteration is Newton's method started from a first-order
// prediction (the previous solution moved along the previous slope), which at audio rates
// is usually one step from the answer. Whenever an iterate would leave the region where
// convergence is monotone it is pulled back to a bound of the root, so the loops are
// bounded and cannot overflow. The work is done on the magnitude of the right-hand side:
// negating the input negates the result exactly.

/** What a solver remembers between samples. */
struct SolverState
{
    double v = 0.0, rhs = 0.0, slope = 0.0;
};

/** lin * v + sat * sinh (v / scale) = rhs: a resistor (and perhaps a capacitor) feeding an
    anti-parallel diode pair. */
struct DiodeLaw
{
    double lin = 1.0, invLin = 1.0, sat = 1.0, scale = 1.0, invScale = 1.0;

    void set (double linear, double saturation, double junctionScale) noexcept
    {
        lin = linear;
        invLin = 1.0 / linear;
        sat = saturation;
        scale = junctionScale;
        invScale = 1.0 / junctionScale;
    }

    /** Small-signal gain d(v)/d(rhs) at zero. */
    double smallSignal() const noexcept { return 1.0 / (lin + sat * invScale); }
};

/** tolerance is the last step size, relative to the junction scale, below which the
    iteration stops; the error left is about its square. */
inline double solveDiodePair (const DiodeLaw& d, double rhs, SolverState& st, double tolerance = 3.0e-4) noexcept
{
    const double sign = rhs < 0.0 ? -1.0 : 1.0;
    const double r = rhs * sign;
    const double jump = 2.0 * d.scale;
    const double tol = tolerance * d.scale;

    // The resistor alone and the diodes alone each bound the root from above. The second
    // bound costs a logarithm, so it is only taken when an iterate moves far upwards.
    double v = clamp ((st.v + st.slope * (rhs - st.rhs)) * sign, 0.0, r * d.invLin);
    if (v > std::max (st.v * sign, 0.0) + jump)
        v = std::min (v, d.scale * std::asinh (r / d.sat));

    double df = d.lin;
    for (int i = 0; i < kSolverMaxIterations; ++i)
    {
        const double e = std::exp (v * d.invScale);
        const double ei = 1.0 / e;
        const double f = d.lin * v + 0.5 * d.sat * (e - ei) - r;
        df = d.lin + 0.5 * d.sat * d.invScale * (e + ei);
        double next = v - f / df;
        if (next > v + jump)
            next = std::min (next, d.scale * std::asinh (r / d.sat));
        next = std::max (next, 0.0);
        const double moved = std::abs (next - v);
        v = next;
        if (moved < tol) break;
    }

    st.v = v * sign;
    st.rhs = rhs;
    st.slope = 1.0 / df;
    return st.v;
}

/** The same equation from rest, with no memory: for coefficient set-up. */
inline double solveDiodePair (const DiodeLaw& d, double rhs) noexcept
{
    SolverState st;
    return solveDiodePair (d, rhs, st, 1.0e-9);
}

/** v + sat * (exp (v / scale) - 1) = rhs: a resistor feeding one forward junction. */
inline double solveJunction (double sat, double scale, double invScale, double rhs, SolverState& st,
                             double tolerance = 3.0e-4) noexcept
{
    const double jump = 2.0 * scale;
    const double tol = tolerance * scale;
    const double upper = rhs > 0.0 ? rhs : rhs + sat;

    double v = std::min (st.v + st.slope * (rhs - st.rhs), upper);
    if (v > st.v + jump && rhs > 0.0)
        v = std::min (v, scale * std::log1p (rhs / sat));

    double df = 1.0;
    for (int i = 0; i < kSolverMaxIterations; ++i)
    {
        const double e = std::exp (v * invScale);
        const double f = v + sat * (e - 1.0) - rhs;
        df = 1.0 + sat * invScale * e;
        double next = std::min (v - f / df, upper);
        if (next > v + jump && rhs > 0.0)
            next = std::min (next, scale * std::log1p (rhs / sat));
        const double moved = std::abs (next - v);
        v = next;
        if (moved < tol) break;
    }

    st.v = v;
    st.rhs = rhs;
    st.slope = 1.0 / df;
    return v;
}

inline double solveJunction (double sat, double scale, double rhs) noexcept
{
    SolverState st;
    return solveJunction (sat, scale, 1.0 / scale, rhs, st, 1.0e-9);
}

/** e + c * tanh (k * e) = rhs: a saturating transconductance inside a feedback loop
    (c and k positive). */
inline double solveTanhLoop (double c, double k, double rhs, SolverState& st, double tolerance = 1.0e-6) noexcept
{
    const double sign = rhs < 0.0 ? -1.0 : 1.0;
    const double r = rhs * sign;
    const double ck = c * k;
    const double lower = std::max (r / (1.0 + ck), r - c);   // both bound the root from below
    const double tol = tolerance / k;

    double e = clamp ((st.v + st.slope * (rhs - st.rhs)) * sign, lower, r);
    double df = 1.0 + ck;
    for (int i = 0; i < kSolverMaxIterations; ++i)
    {
        const double t = 1.0 - 2.0 / (std::exp (2.0 * k * e) + 1.0);   // tanh (k e), e >= 0
        const double f = e + c * t - r;
        df = 1.0 + ck * (1.0 - t * t);
        const double next = clamp (e - f / df, lower, r);
        const double moved = std::abs (next - e);
        e = next;
        if (moved < tol) break;
    }

    st.v = e * sign;
    st.rhs = rhs;
    st.slope = 1.0 / df;
    return st.v;
}

//==============================================================================
// First-order antiderivative antialiasing (ADAA). A memoryless stage y = f(x) is replaced
// by the mean of f over the straight line between the last input and this one,
// (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1]) with F' = f. That is the stage followed by a
// one-sample box filter in continuous time, so the corners of a clipping curve produce far
// less energy above the Nyquist frequency to fold back. It costs half a sample of delay
// and a gentle top-octave droop (cos (w / 2), under 0.5 dB at 20 kHz once oversampled 4x).
// When the two inputs are too close for the difference quotient to be accurate, the mean
// of the two outputs stands in (it agrees to second order). The same mean is used for the
// first step after the stage's curve changes (the knee moves), where the stored
// antiderivative belongs to the old curve. Drive and bias only scale and shift a stage's
// input, so its curve stays put and automating them costs nothing. The first step after a
// reset, with no previous input at all, is the plain output.

inline constexpr double kAdaaTolerance = 1.0e-7;   // relative step below which the fallback is used

/** What an ADAA stage remembers: the last input, its antiderivative and its plain output. */
struct AdaaState
{
    double x = 0.0, big = 0.0, y = 0.0;
    bool primed = false;    // there is a previous input (false after a reset)
    bool current = false;   // the stored antiderivative belongs to the coefficients in use
};

/** One ADAA step, given the stage's input x, antiderivative F (x) and plain output f (x). */
inline double adaaStep (AdaaState& s, double x, double antiderivative, double plain) noexcept
{
    const double dx = x - s.x;
    const double out = ! s.primed ? plain
                     : s.current && std::abs (dx) > kAdaaTolerance * (1.0 + std::abs (x)) ? (antiderivative - s.big) / dx
                     : 0.5 * (plain + s.y);
    s.x = x;
    s.big = antiderivative;
    s.y = plain;
    s.primed = s.current = true;
    return out;
}

/** ADAA of a hard clip at +-limit (an amplifier stage running into its rails). */
inline double hardClipAdaa (double x, double limit, AdaaState& s) noexcept
{
    const double a = std::abs (x);
    const double antiderivative = a <= limit ? 0.5 * x * x : limit * a - 0.5 * limit * limit;
    return adaaStep (s, x, antiderivative, clamp (x, -limit, limit));
}

/** ADAA of a clamp to 0..limit (a transistor between cutoff and saturation). */
inline double rangeClipAdaa (double x, double limit, AdaaState& s) noexcept
{
    const double antiderivative = x <= 0.0 ? 0.0 : (x <= limit ? 0.5 * x * x : limit * x - 0.5 * limit * limit);
    return adaaStep (s, x, antiderivative, clamp (x, 0.0, limit));
}

/** ADAA of the diode-pair law v (rhs), given v = solveDiodePair (d, rhs). The input and
    the antiderivative are both taken from v (rhs = lin v + sat sinh (v / scale), and
    F = lin v^2 / 2 + sat (v sinh (v / scale) - scale (cosh (v / scale) - 1))), so the pair
    lies exactly on the curve and solver error cannot leak into the difference quotient.
    Exactly odd: negating every input negates every output. */
inline double diodePairAdaa (const DiodeLaw& d, double v, AdaaState& s) noexcept
{
    const double a = std::abs (v);
    const double em = std::expm1 (a * d.invScale);
    const double inv = 1.0 / (1.0 + em);
    const double sh = 0.5 * em * (2.0 + em) * inv;   // sinh (a / scale)
    const double chm1 = 0.5 * em * em * inv;         // cosh (a / scale) - 1
    const double r = d.lin * a + d.sat * sh;
    const double antiderivative = 0.5 * d.lin * a * a + d.sat * (a * sh - d.scale * chm1);
    return adaaStep (s, v < 0.0 ? -r : r, antiderivative, v);
}

/** e + c (tanh (k e + b) - tanh b) = rhs: an unbalanced differential pair (its operating
    point b off centre) inside a feedback loop. c and k positive; tb is tanh (b). The
    left-hand side is monotone but not odd, so Newton runs inside a bracket that shrinks
    every step, and bisects whenever a step would leave it. */
inline double solveOffsetTanhLoop (double c, double k, double b, double tb, double rhs, SolverState& st,
                                   double tolerance = 1.0e-6) noexcept
{
    // The pair's output lies in (-c (1 + tb), c (1 - tb)), which brackets e.
    double lo = rhs - c * (1.0 - tb), hi = rhs + c * (1.0 + tb);
    const double tol = tolerance / k;
    double e = clamp (st.v + st.slope * (rhs - st.rhs), lo, hi);
    double df = 1.0;
    for (int i = 0; i < kSolverMaxIterations; ++i)
    {
        const double t = std::tanh (k * e + b);
        const double f = e + c * (t - tb) - rhs;
        df = 1.0 + c * k * (1.0 - t * t);
        if (f == 0.0) break;
        if (f > 0.0) hi = e;
        else         lo = e;
        double next = e - f / df;
        // Strictly inside: a saturated pair (tanh exactly +-1) sends Newton to a bracket end
        // exactly, and accepting that would let the iterate alternate between the two ends
        // with a bracket that never shrinks.
        if (! (next > lo && next < hi)) next = 0.5 * (lo + hi);
        const double moved = std::abs (next - e);
        e = next;
        if (moved < tol) break;
    }

    st.v = e;
    st.rhs = rhs;
    st.slope = 1.0 / df;
    return e;
}

/** ln (cosh (z)) without overflow. */
inline double logCosh (double z) noexcept
{
    const double a = std::abs (z);
    return a + std::log1p (std::exp (-2.0 * a)) - std::numbers::ln2;
}

/** Diode softness for a knee setting: the junction scale n*Vt as a fraction of the
    clipping voltage. About 0.08 is a silicon signal diode. */
inline double diodeSoftness (double knee, double hardest, double softest) noexcept
{
    return hardest * std::pow (softest / hardest, clamp (knee, 0.0, 1.0));
}

//==============================================================================
/** What a family needs to know, refreshed at control rate. */
struct ClipperControls
{
    ClipperFamily family = ClipperFamily::DiodePair;
    double gain = 1.0;   // linear, 1..1000
    double knee = 1.0;   // 0 hard .. 1 soft
    double bias = 0.0;   // -1..1
};

//==============================================================================
// Family 0. A series resistor into an anti-parallel diode pair to ground. Memoryless.
struct DiodePairCoeffs
{
    double inGain = 1.0, offset = 0.0, outGain = 1.0, rest = 0.0;
    DiodeLaw diode;
    void update (const ClipperControls&) noexcept;
};

struct DiodePairState
{
    SolverState solver;
};

inline double diodePairTick (const DiodePairCoeffs& c, DiodePairState& st, double x) noexcept
{
    return (solveDiodePair (c.diode, x * c.inGain + c.offset, st.solver) - c.rest) * c.outGain;
}

//==============================================================================
// Family 1. Non-inverting op-amp whose gain network has two RC legs to ground (corners near
// 1.5 kHz and 60 Hz), a finite gain-bandwidth product and a slew-rate limit; then a series
// resistor into silicon diodes to ground.
struct OpAmpHardCoeffs
{
    double inScale = 1.0, outGain = 1.0, rest = 0.0, restOut = 0.0;
    double alpha = 1.0, rfG1 = 0.0, rfG2 = 0.0, offset = 0.0;
    double loopC = 0.0, loopK = 1.0;
    double leak1 = 0.0, leak2 = 0.0;
    DiodeLaw diode;
    void update (const ClipperControls&, double sampleRate) noexcept;
};

struct OpAmpHardState
{
    double out = 0.0, s1 = 0.0, s2 = 0.0;
    SolverState loop, diode;
};

inline constexpr double kOpAmpRail = 3.5;   // volts either side of the mid-supply reference

inline double opAmpHardTick (const OpAmpHardCoeffs& c, OpAmpHardState& st, double x) noexcept
{
    // Inverting input: vm = alpha * out + base (the two leg capacitors as companion sources).
    const double base = c.alpha * (c.rfG1 * st.s1 + c.rfG2 * st.s2 - c.offset);
    const double rhs = x * c.inScale - base - c.alpha * st.out;

    // Backward Euler on d(out)/dt = slew * tanh (wu * e / slew), e = v+ - v-.
    const double e = solveTanhLoop (c.loopC, c.loopK, rhs, st.loop);
    st.out = clamp (st.out + (rhs - e) / c.alpha, -kOpAmpRail, kOpAmpRail);

    const double vm = c.alpha * st.out + base;
    st.s1 += c.leak1 * (vm - st.s1);
    st.s2 += c.leak2 * (vm - st.s2);

    return (solveDiodePair (c.diode, st.out, st.diode) - c.rest) * c.outGain;
}

//==============================================================================
// Family 2. Common-source JFET stage with a partly bypassed source resistor. Square-law
// conduction, cutoff on one side, the ohmic region (drain bottoming) on the other. The
// bypass capacitor lets the operating point drift with the average current.
struct AsymSoftCoeffs
{
    double inGain = 1.0, gateBias = 0.0, outGain = 1.0, restDrain = 0.0, restSource = 0.0;
    double rsu = 0.0, rsb = 0.0, betaRsu = 0.0, sourceCoef = 0.0;
    void update (const ClipperControls&, double sampleRate) noexcept;
};

struct AsymSoftState
{
    double vsb = 0.0;   // voltage on the bypassed part of the source resistor, relative to rest
};

inline constexpr double kJfetSupply = 9.0;
inline constexpr double kJfetPinch = -2.0;                   // Vp
inline constexpr double kJfetBeta = 2.0e-3 / 4.0;            // Idss / Vp^2, Idss = 2 mA
inline constexpr double kJfetDrainR = 6800.0;
inline constexpr double kJfetSourceR = 2000.0;

/** Drain current for a gate voltage, with the load line and the unbypassed source
    resistance solved in closed form. */
inline double jfetDrainCurrent (double vg, double vsb, double rsu, double betaRsu) noexcept
{
    const double vov0 = vg - vsb - kJfetPinch;
    if (vov0 <= 0.0) return 0.0;   // cutoff

    const double c = betaRsu * vov0;
    double id = 2.0 * kJfetBeta * vov0 * vov0 / ((1.0 + 2.0 * c) + std::sqrt (1.0 + 4.0 * c));

    const double e = kJfetSupply - vsb;
    const double rt = kJfetDrainR + rsu;
    if (e - id * rt < vov0 - id * rsu)
    {
        // Ohmic region: Id = beta * (2 Vov Vds - Vds^2) with Vov and Vds both falling with Id.
        const double q = kJfetDrainR - rsu;
        const double w = 2.0 * vov0 - e;
        const double b = 1.0 / kJfetBeta - e * q + rt * w;
        const double disc = std::max (0.0, b * b + 4.0 * rt * q * e * w);
        id = (-b + std::sqrt (disc)) / (2.0 * rt * q);
    }
    return clamp (id, 0.0, e / rt);
}

inline double asymSoftTick (const AsymSoftCoeffs& c, AsymSoftState& st, double x) noexcept
{
    const double vsb = st.vsb + c.restSource;
    const double id = jfetDrainCurrent (x * c.inGain + c.gateBias, vsb, c.rsu, c.betaRsu);
    st.vsb += c.sourceCoef * (id * c.rsb - vsb);
    return (c.restDrain - (kJfetSupply - id * kJfetDrainR)) * c.outGain;
}

//==============================================================================
// Family 3. A common-emitter booster (exponential law, emitter degeneration, cutoff and
// saturation), an op-amp gain stage that leaves the bass below 72 Hz at unity, hard diode
// clipping loaded by a capacitor, and the passive tone stage: a 234 Hz low-pass summed
// with a 1063 Hz high-pass, which leaves a scoop near 500 Hz.
struct ScoopHardCoeffs
{
    double inBoost = 1.0, opGain = 0.0, offset = 0.0, outGain = 1.0, rest = 0.0;
    double capK = 0.0;
    DiodeLaw diode;
    OnePoleCoeffs shelf, toneLow, toneHigh;
    void update (const ClipperControls&, double sampleRate) noexcept;
};

struct ScoopHardState
{
    double clip = 0.0;
    OnePoleState shelf, toneLow, toneHigh;
    SolverState booster, diode;
};

inline constexpr double kBoosterRe = 114.0;
inline constexpr double kBoosterRc = 10000.0;
inline constexpr double kBoosterIc0 = 0.4e-3;
inline constexpr double kBoosterIcSat = (9.0 - 0.2) / (kBoosterRc + kBoosterRe);
/** Small-signal gain of the booster: Rc / (re + Re). */
inline constexpr double kBoosterGain = kBoosterRc * kBoosterIc0 / (kThermalVoltage + kBoosterRe * kBoosterIc0);

inline double scoopHardTick (const ScoopHardCoeffs& c, ScoopHardState& st, double x) noexcept
{
    // Booster: dVin = dVbe + Re * (Ic - Ic0), Ic = Ic0 * exp (dVbe / Vt).
    const double dvIn = x * c.inBoost;
    const double dvBe = solveJunction (kBoosterRe * kBoosterIc0, kThermalVoltage, 1.0 / kThermalVoltage, dvIn, st.booster);
    const double dIc = std::min ((dvIn - dvBe) / kBoosterRe, kBoosterIcSat - kBoosterIc0);
    const double v1 = kBoosterRc * dIc;

    // Op-amp stage: unity below the 72 Hz corner, opGain + 1 above it, then the rails.
    const double hp = v1 - onePoleLp (c.shelf, st.shelf, v1);
    const double v2 = clamp (v1 + c.opGain * hp + c.offset, -kOpAmpRail, kOpAmpRail);

    // Series resistor into the diode pair with a capacitor across it (backward Euler).
    st.clip = solveDiodePair (c.diode, c.capK * st.clip + v2, st.diode);

    const double low = onePoleLp (c.toneLow, st.toneLow, st.clip);
    const double high = st.clip - onePoleLp (c.toneHigh, st.toneHigh, st.clip);
    return (low + high - c.rest) * c.outGain;
}

//==============================================================================
// Family 4. Non-inverting op-amp with the diode pair, the gain resistor and a small
// capacitor in parallel in the feedback path, and a 4.7 k + 47 nF leg to ground (720 Hz).
// The output is the input plus the feedback voltage, so the bass passes almost clean and
// the mids are what clips.
struct MidHumpCoeffs
{
    double inScale = 1.0, outGain = 1.0, offset = 0.0, rest = 0.0, restFb = 0.0;
    double capK = 0.0;
    DiodeLaw diode;
    OnePoleCoeffs leg;
    void update (const ClipperControls&, double sampleRate) noexcept;
};

struct MidHumpState
{
    double fb = 0.0;
    OnePoleState leg;
    SolverState diode;
};

inline double midHumpTick (const MidHumpCoeffs& c, MidHumpState& st, double x) noexcept
{
    const double vin = x * c.inScale;
    const double legDrive = vin - onePoleLp (c.leg, st.leg, vin) + c.offset;   // leg current times R1
    st.fb = solveDiodePair (c.diode, c.capK * st.fb + legDrive, st.diode);
    return (clamp (vin + st.fb, -kOpAmpRail, kOpAmpRail) - c.rest) * c.outGain;
}

//==============================================================================
// Family 5. One common-emitter transistor biased near cutoff, driven through a coupling
// capacitor from a source with real resistance. The base junction rectifies the input and
// charges the capacitor, so the bias moves with level: quiet signals do not open the
// transistor at all (the gate) and loud ones come out as a pulse train.
struct GatedFuzzCoeffs
{
    double inGain = 1.0, outGain = 1.0, rest = 0.0;
    double kp = 0.0, biasTerm = 0.0, invA = 1.0, sat = 1.0, ibScale = 0.0, rePrime = 0.0;
    double capStep = 0.0, restCap = 0.0, icSat = 0.0;
    void update (const ClipperControls&, double sampleRate) noexcept;
};

struct GatedFuzzState
{
    double q = 0.0;   // coupling capacitor voltage, relative to its resting value
    SolverState base;
    AdaaState drive, collector;
};

inline constexpr double kFuzzSupply = 9.0;
inline constexpr double kFuzzRc = 10000.0;
inline constexpr double kFuzzBeta = 120.0;
inline constexpr double kFuzzDriveRail = 4.5;   // the stage ahead cannot swing further than this

inline double gatedFuzzTick (const GatedFuzzCoeffs& c, GatedFuzzState& st, double x) noexcept
{
    // The rails of the stage ahead and the collector's range are hard limits: both run
    // with ADAA. The base junction between them is smooth.
    const double vin = hardClipAdaa (x * c.inGain, kFuzzDriveRail, st.drive);
    const double q = st.q + c.restCap;

    // Base node, backward Euler on the coupling capacitor:
    //   kp * (vin - q - vb) = ib + (vb - vbb) / rb,  vb = vbe + re' * ib
    const double rhs = (c.kp * (vin - q) + c.biasTerm) * c.invA;
    const double vbe = solveJunction (c.sat, kThermalVoltage, 1.0 / kThermalVoltage, rhs, st.base);
    const double ib = (rhs - vbe) * c.ibScale;
    const double vb = vbe + c.rePrime * ib;
    st.q += c.kp * (vin - q - vb) * c.capStep;

    const double ic = rangeClipAdaa (kFuzzBeta * ib, c.icSat, st.collector);
    return (ic - c.rest) * c.outGain;
}

//==============================================================================
// Family 6. A console channel: a class-A input amplifier and an output transformer.
//
// The amplifier is a feedback loop round a differential pair whose operating point sits
// off centre (single-ended, class A): its law is tanh (k e + b) - tanh (b), so it clips
// sooner on one side than the other and adds second harmonic even with the bias at zero.
// The loop keeps it clean (unity gain) until it nears its rails; the knee is the loop
// gain. The amplifier is memoryless and runs with ADAA.
//
// The transformer is a core whose flux integrates the amplifier's voltage, drained by a
// magnetising current that rises as a sinh of the flux once the core saturates. Flux is
// voltage over frequency, so the core saturates on low notes first: at the nominal level a
// note near 50 Hz is well into it while the mids pass straight through. Its output, the
// rate of change of flux, is what the next stage sees (backward Euler).
struct ConsoleCoeffs
{
    double inGain = 1.0, offset = 0.0, outGain = 1.0, invBeta = 1.0, restAmp = 0.0;
    double loopC = 0.0, loopK = 1.0, pairB = 0.0, pairTanh = 0.0;
    double dt = 0.0, invDt = 0.0;
    DiodeLaw core;   // the same equation: flux in place of voltage, magnetising current in place of diode current
    void update (const ClipperControls&, double sampleRate) noexcept;
};

struct ConsoleState
{
    double flux = 0.0;
    SolverState loop, core;
    AdaaState amp;
};

/** The amplifier: pair output g for loop input `in`, with ADAA over the step from the last
    input. Returns the closed-loop output (in - e) / beta = g / beta. */
inline double consoleAmp (const ConsoleCoeffs& c, ConsoleState& st, double in) noexcept
{
    const double e = solveOffsetTanhLoop (c.loopC, c.loopK, c.pairB, c.pairTanh, in, st.loop);
    const double z = c.loopK * e + c.pairB;
    const double g = c.loopC * (std::tanh (z) - c.pairTanh);
    // in = e + g (e), so the integral of g over in is the integral of g de plus g^2 / 2.
    const double antiderivative = c.loopC * (logCosh (z) / c.loopK - c.pairTanh * e) + 0.5 * g * g;
    return adaaStep (st.amp, e + g, antiderivative, g) * c.invBeta;
}

inline double consoleTick (const ConsoleCoeffs& c, ConsoleState& st, double x) noexcept
{
    // The amplifier is AC coupled to the transformer: its resting output is removed.
    const double amp = consoleAmp (c, st, x * c.inGain + c.offset) - c.restAmp;

    // d(flux)/dt = amp - R * i_m (flux), with i_m a sinh of the flux. Backward Euler.
    const double flux = solveDiodePair (c.core, st.flux + c.dt * amp, st.core, 1.0e-7);
    const double out = (flux - st.flux) * c.invDt;
    st.flux = flux;
    return out * c.outGain;
}

//==============================================================================
// Family 7. Four series folding cells, each a diode pair inside a difference amplifier
// (slope +1 below the diode knee, -1 above it), and a final amplifier with a hard diode
// pair that bounds the output just above the fold height once the input is past the last
// fold.
struct FoldCoeffs
{
    static constexpr int kCells = 4;
    double inGain = 1.0, offset = 0.0, cellGain = 2.0, endGain = 1.0, outGain = 1.0, rest = 0.0;
    DiodeLaw cell, end;
    void update (const ClipperControls&) noexcept;
};

struct FoldState
{
    SolverState cells[FoldCoeffs::kCells];
    SolverState end;
    AdaaState cellAdaa[FoldCoeffs::kCells], endAdaa;
    double cellIn[FoldCoeffs::kCells] {};
    double restHistory[FoldCoeffs::kCells + 1] {};   // the resting output, last few samples
};

/** The folding chain. Every stage is memoryless, so each runs with ADAA: a cell is
    cellGain * diode (v) - v, whose mean over a step is cellGain * (the diode's ADAA) minus
    the mean of the two inputs. */
inline double foldChain (const FoldCoeffs& c, FoldState& st, double v) noexcept
{
    for (int i = 0; i < FoldCoeffs::kCells; ++i)
    {
        const double meanIn = st.cellAdaa[i].primed ? 0.5 * (v + st.cellIn[i]) : v;
        const double d = diodePairAdaa (c.cell, solveDiodePair (c.cell, v, st.cells[i]), st.cellAdaa[i]);
        st.cellIn[i] = v;
        v = c.cellGain * d - meanIn;
    }
    const double x = v * c.endGain;
    return diodePairAdaa (c.end, solveDiodePair (c.end, x, st.end), st.endAdaa);
}

inline double foldTick (const FoldCoeffs& c, FoldState& st, double x) noexcept
{
    // Each of the five ADAA stages answers a small step half a sample late (a two-tap
    // average), so the chain's resting output follows a bias move through the binomial
    // 1 5 10 10 5 1 over 32. The resting point is subtracted through the same filter, or
    // every control-rate step of a gliding bias would leave a one-sample click.
    static_assert (FoldCoeffs::kCells == 4, "the binomial assumes four cells and the end stage");
    auto& h = st.restHistory;
    const double lagged = (c.rest + 5.0 * h[0] + 10.0 * h[1] + 10.0 * h[2] + 5.0 * h[3] + h[4]) * (1.0 / 32.0);
    for (int i = FoldCoeffs::kCells; i > 0; --i)
        h[i] = h[i - 1];
    h[0] = c.rest;
    return (foldChain (c, st, x * c.inGain + c.offset) - lagged) * c.outGain;
}

//==============================================================================
/** Coefficients for whichever family is selected. Shared by both channels. */
struct ClipperCoeffs
{
    ClipperFamily family = ClipperFamily::DiodePair;
    DiodePairCoeffs diodePair;
    OpAmpHardCoeffs opAmpHard;
    AsymSoftCoeffs asymSoft;
    ScoopHardCoeffs scoopHard;
    MidHumpCoeffs midHump;
    GatedFuzzCoeffs gatedFuzz;
    ConsoleCoeffs console;
    FoldCoeffs fold;

    /** Recomputes the selected family's coefficients. sampleRate is the rate the clipper
        runs at (base rate times the oversampling factor). */
    void update (const ClipperControls&, double sampleRate) noexcept;
};

/** State of one channel, for every family (only the selected one is touched). */
struct ClipperState
{
    DiodePairState diodePair;
    OpAmpHardState opAmpHard;
    AsymSoftState asymSoft;
    ScoopHardState scoopHard;
    MidHumpState midHump;
    GatedFuzzState gatedFuzz;
    ConsoleState console;
    FoldState fold;

    /** Every family's state to zero. Follow with settle() once the coefficients are set. */
    void reset() noexcept { *this = ClipperState {}; }

    /** Puts the selected family's state at its resting point for these coefficients (the
        point it would reach after a long silence), so a block switched on with a bias does
        not start with a DC step. Families whose state is stored relative to rest need
        nothing. */
    void settle (const ClipperCoeffs&) noexcept;

    /** Call after ClipperCoeffs::update when the knee changed: the next ADAA step of every
        stage averages the plain outputs instead of differencing antiderivatives of two
        different curves. Drive and bias only scale and shift what a stage is fed, so the
        stage functions (and their antiderivatives) stay the same and need no call. */
    void coefficientsChanged() noexcept
    {
        for (auto& a : fold.cellAdaa) a.current = false;
        fold.endAdaa.current = false;
        gatedFuzz.drive.current = false;
        gatedFuzz.collector.current = false;
        console.amp.current = false;
    }
};

/** Runs the selected family over a buffer in place. */
void processClipper (const ClipperCoeffs&, ClipperState&, double* buffer, int numSamples) noexcept;
}
}
