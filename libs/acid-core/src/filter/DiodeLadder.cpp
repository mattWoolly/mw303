#include "acid/dsp/DiodeLadder.h"

#include <complex>

namespace acid::dsp
{
namespace ladder
{
namespace
{
struct TanhTableBuilder
{
    TanhTable table;

    TanhTableBuilder() noexcept
    {
        // Cubic Taylor coefficients of tanh about each knot, from T = tanh (x0) and
        // s = sech^2 (x0): first derivative s, second -2 T s, third -2 s (1 - 3 T^2), each
        // scaled by the knot spacing to the matching power. The remainder is bounded by
        // (fourth derivative, at most 4.1) * (1/128)^4 / 24: below 7e-10 everywhere.
        const double step = 1.0 / static_cast<double> (TanhTable::kKnotsPerUnit);
        for (int i = 0; i < TanhTable::kNumKnots; ++i)
        {
            const double x0 = static_cast<double> (i) * step;
            const double T = std::tanh (x0);
            const double ch = std::cosh (x0);
            const double s = 1.0 / (ch * ch);
            double* c = table.coeff[i];
            c[0] = T;
            c[1] = s * step;
            c[2] = -T * s * step * step;
            c[3] = -s * (1.0 - 3.0 * T * T) / 3.0 * step * step * step;
        }
    }
};
}

const TanhTable& tanhTable() noexcept
{
    static const TanhTableBuilder builder;
    return builder.table;
}
}

namespace
{
// Feedback network of the research document, section 1.3: three high-passes and a shelf.
constexpr double kFbPoleHz[4] = { 3.18, 1.18, 6.164, 15.517 };
constexpr double kShelfZeroHz = 7.408;

/** Open-loop gain per unit K of the sub-audio loop at frequency hz, in the limit of a
    large tail current (ladder transparent, no source resistance), which is its worst case:
    coupler high-pass times the feedback network, the latter blended towards unity by bypass. */
std::complex<double> subAudioLoop (double hz, double wq, double bypass) noexcept
{
    const std::complex<double> s (0.0, kTwoPi * hz);
    std::complex<double> net = (s + kTwoPi * kShelfZeroHz) / (s + kTwoPi * kFbPoleHz[3]);
    for (int i = 0; i < 3; ++i)
        net *= s / (s + kTwoPi * kFbPoleHz[i]);
    return (s / (s + wq)) * (bypass + (1.0 - bypass) * net);
}

/** Largest magnitude of subAudioLoop where it is negative real (the 180 degree crossings
    of the loop, which has negative feedback), searched from 0.5 to 60 Hz. 0 if none. */
double crossingGain (double wq, double bypass) noexcept
{
    constexpr int kSteps = 40;
    constexpr double kLoHz = 0.5, kHiHz = 60.0;
    const double ratio = std::pow (kHiHz / kLoHz, 1.0 / kSteps);

    double best = 0.0;
    double f0 = kLoHz;
    std::complex<double> l0 = subAudioLoop (f0, wq, bypass);
    for (int i = 0; i < kSteps; ++i)
    {
        const double f1 = f0 * ratio;
        const std::complex<double> l1 = subAudioLoop (f1, wq, bypass);
        if (l0.imag() * l1.imag() <= 0.0 && (l0.real() < 0.0 || l1.real() < 0.0))
        {
            double lo = f0, hi = f1;
            double imLo = l0.imag();
            std::complex<double> mid = l0;
            for (int n = 0; n < 30; ++n)
            {
                const double fm = std::sqrt (lo * hi);
                mid = subAudioLoop (fm, wq, bypass);
                if (mid.imag() * imLo > 0.0) { lo = fm; imLo = mid.imag(); }
                else                         { hi = fm; }
            }
            if (mid.real() < 0.0)
                best = std::max (best, std::abs (mid));
        }
        f0 = f1;
        l0 = l1;
    }
    return best;
}
}

//==============================================================================
DiodeLadderTrims DiodeLadderTrims::acceptanceReference() noexcept
{
    DiodeLadderTrims t;
    t.rbOhms = 20.0e3;
    t.kMax = 18.7;
    return t;
}

DiodeLadderTrims DiodeLadderTrims::idealLadder (double kMaxValue, double couplerHz) noexcept
{
    DiodeLadderTrims t;
    t.rbOhms = 1.0 / (kTwoPi * std::max (couplerHz, 1.0e-3) * DiodeLadder::kCouplerFarads);
    t.kMax = kMaxValue;
    t.capRatio = 2.0;
    t.loadScale = 0.0;
    t.inputOffset = 0.0;
    t.subAudioNetwork = false;
    t.subAudioGuard = true;
    return t;
}

DiodeLadder::DiodeLadder() noexcept
    : table (&ladder::tanhTable())
{
    updateCoefficients();
}

void DiodeLadder::prepare (double newSampleRate) noexcept
{
    sampleRate = (newSampleRate >= 8000.0 && newSampleRate <= 1.0e7) ? newSampleRate : 192000.0;
    updateCoefficients();
    reset();
}

void DiodeLadder::reset() noexcept
{
    s1 = s2 = s3 = s4 = 0.0;
    sq = 0.0;
    for (double& z : zf) z = 0.0;
    y1 = y2 = y3 = y4 = 0.0;
    p1 = p2 = p3 = p4 = 0.0;
    fbBeta = 0.0;
    lastOut = prevOut = lastPotTop = 0.0;
    startError = 0.0;
}

void DiodeLadder::setTrims (const DiodeLadderTrims& newTrims) noexcept
{
    const auto sane = [] (double v, double lo, double hi, double fallback) noexcept
    {
        return (v == v) ? clamp (v, lo, hi) : fallback;
    };

    DiodeLadderTrims t;
    const DiodeLadderTrims defaults;
    t.rbOhms = sane (newTrims.rbOhms, 1.0e3, 1.0e9, defaults.rbOhms);
    t.kMax = sane (newTrims.kMax, 0.0, 40.0, defaults.kMax);
    t.capRatio = sane (newTrims.capRatio, 0.5, 8.0, defaults.capRatio);
    t.loadScale = sane (newTrims.loadScale, 0.0, 4.0, defaults.loadScale);
    t.inputOffset = sane (newTrims.inputOffset, -10.0, 10.0, defaults.inputOffset);
    t.subAudioNetwork = newTrims.subAudioNetwork;
    t.subAudioGuard = newTrims.subAudioGuard;

    const bool same = t.rbOhms == trims.rbOhms && t.kMax == trims.kMax && t.capRatio == trims.capRatio
                      && t.loadScale == trims.loadScale && t.inputOffset == trims.inputOffset
                      && t.subAudioNetwork == trims.subAudioNetwork && t.subAudioGuard == trims.subAudioGuard;
    if (same) return;

    trims = t;
    updateCoefficients();
}

void DiodeLadder::setTrims (double rbOhms, double kMax) noexcept
{
    DiodeLadderTrims t = trims;
    t.rbOhms = rbOhms;
    t.kMax = kMax;
    setTrims (t);
}

void DiodeLadder::setMaxIterations (int cap) noexcept
{
    maxIterations = clamp (cap, 1, kDefaultMaxIterations);
}

void DiodeLadder::setTolerance (double stepTolerance) noexcept
{
    tolerance = (stepTolerance == stepTolerance) ? clamp (stepTolerance, 1.0e-12, 1.0e-2) : kDefaultTolerance;
}

double DiodeLadder::resonanceRatio (double capRatio) noexcept
{
    return std::sqrt ((4.0 + 6.0 * capRatio) / (6.0 + capRatio));
}

double DiodeLadder::criticalGain (double capRatio) noexcept
{
    // Denominator of the core with s in units of the integrator rate:
    //   D (s) = (s^4 + 6 s^3 + 10 s^2 + 4 s) / rho + s^3 + 5 s^2 + 6 s + 1.
    // At the resonant frequency its imaginary part is zero and K = -Re D.
    const double w2 = (4.0 + 6.0 * capRatio) / (6.0 + capRatio);
    return -((w2 * w2 - 10.0 * w2) / capRatio - 5.0 * w2 + 1.0);
}

double DiodeLadder::wiperFraction (double rotation, double loadOhms) noexcept
{
    const double x = clamp (rotation, 0.0, 1.0);
    return x / (1.0 + x * (1.0 - x) * 50.0e3 / std::max (loadOhms, 1.0));
}

double DiodeLadder::subAudioBypass (double r) const noexcept
{
    const double K = kMaxGain * clamp (r, 0.0, kMaxResonance);
    return K > guardK0 ? guardScale * (1.0 - guardK0 / K) : 0.0;
}

void DiodeLadder::updateCoefficients() noexcept
{
    const double fs = sampleRate;
    piOverFs = kPi / fs;
    fResMax = kMaxFresOverFs * fs;

    rho = trims.capRatio;
    invCRes = 1.0 / resonanceRatio (rho);
    kMaxGain = trims.kMax;
    inputOffset = trims.inputOffset;

    // Coupling capacitors into rbOhms: dq/dt = wq * vb, and the load term -wL * vb on the
    // top capacitor (both arms: 2 C).
    const double wq = 1.0 / (trims.rbOhms * kCouplerFarads);
    const double wL = trims.loadScale / (trims.rbOhms * 2.0 * kLadderFarads);
    gq = std::tan (std::min (wq / (2.0 * fs), 0.49 * kPi));
    aq = 1.0 / (1.0 + gq);
    twoGq = 2.0 * gq;
    gL = wL / (2.0 * fs);
    gLaq = gL * aq;

    // Feedback sections, each prewarped at its own pole.
    for (int i = 0; i < 4; ++i)
    {
        const double gi = std::tan (kPi * kFbPoleHz[i] / fs);
        fbG[i] = gi / (1.0 + gi);
    }
    const double rsh = kShelfZeroHz / kFbPoleHz[3];
    shelfC1 = 1.0 - (1.0 - rsh) * fbG[3];
    shelfC2 = (1.0 - rsh) * (1.0 - fbG[3]);
    fbAlpha = shelfC1;
    for (int i = 0; i < 3; ++i)
    {
        fbC[i] = 1.0 - fbG[i];
        fbAlpha *= fbC[i];
    }

    // The feedback network is linear, so the offset it will present to the next solve is
    // a fixed combination of its current states and the current output. Derive that
    // combination by pushing unit vectors through the section updates.
    const auto offsetOf = [this] (const double* z) noexcept
    {
        double be = -z[0] * fbC[0];
        be = (be - z[1]) * fbC[1];
        be = (be - z[2]) * fbC[2];
        return be * shelfC1 - shelfC2 * z[3];
    };
    const auto advance = [this] (double* z, double w) noexcept
    {
        double in = w;
        for (int i = 0; i < 3; ++i)
        {
            const double v = (in - z[i]) * fbG[i];
            const double lp = v + z[i];
            z[i] = lp + v;
            in -= lp;
        }
        const double v = (in - z[3]) * fbG[3];
        z[3] = (v + z[3]) + v;
    };
    for (int j = 0; j < 4; ++j)
    {
        double z[4] { 0.0, 0.0, 0.0, 0.0 };
        z[j] = 1.0;
        advance (z, 0.0);
        fbQ[j] = offsetOf (z);
    }
    {
        double z[4] { 0.0, 0.0, 0.0, 0.0 };
        advance (z, 1.0);
        fbQw = offsetOf (z);
    }
    fbBeta = offsetOf (zf);

    if (wq != guardWq)
    {
        // The guard depends on the coupler corner only. Crossing gain per unit K with the
        // stock network, and how fast a small bypass lowers it.
        constexpr double kProbe = 0.01;
        guardWq = wq;
        guardG0 = crossingGain (wq, 0.0);
        guardSlope = guardG0 > 0.0 ? (1.0 - crossingGain (wq, kProbe) / guardG0) / kProbe : 0.0;
    }
    updateGuard();
}

void DiodeLadder::updateGuard() noexcept
{
    if (! trims.subAudioNetwork)
    {
        // Unity network: bypass = 1 for every K above zero.
        guardK0 = 0.0;
        guardScale = 1.0;
    }
    else if (trims.subAudioGuard && guardG0 > 0.0 && guardSlope > 0.0)
    {
        // The crossing gain falls at least linearly with the bypass fraction (it is concave),
        // so bypass = (1 - K0 / K) / slope holds K * gain at or below the ceiling.
        guardK0 = kGuardLoopGain / guardG0;
        guardScale = std::min (1.0 / guardSlope, 1.0);
    }
    else
    {
        guardK0 = 1.0e300;
        guardScale = 0.0;
    }
}

double DiodeLadder::recoverFromBadState() noexcept
{
    ++counters.stateResets;
    reset();
    return 0.0;
}
}
