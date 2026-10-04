#include "acid/dsp/SquareShaper.h"

#include "acid/common/Math.h"

#include <cmath>

namespace acid::dsp
{
namespace
{
constexpr double kG35 = 1.0 / SquareShaper::kR35;
constexpr double kG34 = 1.0 / SquareShaper::kR34;
constexpr double kG45 = 1.0 / SquareShaper::kR45;
constexpr double kG36 = 1.0 / SquareShaper::kR36;
constexpr double kIs = SquareShaper::kSaturationCurrent;
constexpr double kVt = SquareShaper::kThermalVolts;
constexpr double kInvVt = 1.0 / kVt;
constexpr double kInvBr = 1.0 / SquareShaper::kReverseBeta;
constexpr double kRail = SquareShaper::kCollectorRailVolts;
constexpr double kVee = SquareShaper::kEmitterRailVolts;

// Above this junction voltage a Newton step is damped logarithmically (the classic
// junction limiter): Vt * ln (Vt / (sqrt (2) * Is)).
constexpr double kCriticalVolts = 0.712;

// No junction is ever iterated above this (the limiter keeps them well below it).
constexpr double kMaxJunctionVolts = 1.0;

// Below this a junction's exponential is under 1e-16 and is taken as zero.
constexpr double kDeadJunctionVolts = -1.0;

// Newton stops once the applied correction is below this. Convergence is quadratic, so the
// solution is then good to about d^2 / (2 Vt), 2e-7 V.
constexpr double kStepTolerance = 1.0e-4;

// The exponentials are evaluated exactly once per solve, at the starting point, and carried
// to nearby iterates with a 6th-order Taylor series: relative error under 1.3e-8 inside
// this range (in units of Vt). An iterate that leaves the range gets a fresh evaluation.
constexpr double kTaylorRange = 0.25;

inline double expTaylor (double d) noexcept
{
    // Grouped so the terms evaluate side by side instead of in one long chain.
    const double d2 = d * d;
    const double d4 = d2 * d2;
    return (1.0 + d) + d2 * (0.5 + d * (1.0 / 6.0)) + d4 * ((1.0 / 24.0) + d * (1.0 / 120.0) + d2 * (1.0 / 720.0));
}

inline double junctionExp (double v) noexcept
{
    return v > kDeadJunctionVolts ? std::exp (v * kInvVt) : 0.0;
}

inline double limitJunction (double vNew, double vOld) noexcept
{
    if (vNew > kCriticalVolts && std::abs (vNew - vOld) > 2.0 * kVt)
    {
        if (vOld > 0.0)
        {
            const double arg = 1.0 + (vNew - vOld) * kInvVt;
            return arg > 0.0 ? vOld + kVt * std::log (arg) : kCriticalVolts;
        }
        return kVt * std::log (vNew * kInvVt);
    }
    return vNew;
}
}

void SquareShaper::prepare (double sampleRate) noexcept
{
    samplePeriod = 1.0 / std::max (sampleRate, 1.0);
    updateFullStep();
    reset();
    resetStats();
}

void SquareShaper::reset() noexcept
{
    ve = kEmitterRestVolts;
    vC10 = 0.0;
    iC10 = 0.0;
    jC11 = kG45 * (kVee - ve);
    vb = kVee;
    vc = kRail;
    veb = ve - vb;
    vcb = vc - vb;
    slope1 = slope2 = 0.0;
    c1 = c2 = kDeadJunctionVolts - 1.0;
    e1 = e2 = 0.0;
    off = true;
}

void SquareShaper::setBeta (double beta) noexcept
{
    invBeta = 1.0 / clamp (beta, kMinBeta, kMaxBeta);
}

void SquareShaper::setEmitterCapacitor (double farads) noexcept
{
    const double c = clamp (farads, 0.1e-6, 10.0e-6);
    if (c != c11)
    {
        c11 = c;
        updateFullStep();
    }
}

void SquareShaper::updateFullStep() noexcept
{
    fullStep = coeffsFor (1.0);
}

SquareShaper::StepCoeffs SquareShaper::coeffsFor (double fraction) const noexcept
{
    // Trapezoidal companion models of the two capacitors for a step of fraction * period.
    StepCoeffs k;
    const double halfDt = 0.5 * fraction * samplePeriod;
    k.qC10 = halfDt / kC10;
    const double h = k.qC10 * kG34;
    k.invOnePlusH = 1.0 / (1.0 + h);
    k.bc = h * k.invOnePlusH;
    k.p = halfDt / c11;
    k.invOnePlusPg = 1.0 / (1.0 + k.p * kG45);
    k.be = k.p * k.invOnePlusPg;
    k.gb = kG35 + kG34 * (1.0 - k.bc);
    k.invGb = 1.0 / k.gb;
    return k;
}

double SquareShaper::advance (double fraction, double sawVolts) noexcept
{
    StepCoeffs partial;
    if (fraction != 1.0)
        partial = coeffsFor (fraction);
    const StepCoeffs& k = fraction == 1.0 ? fullStep : partial;

    // History terms. C10: vC10' = ac + bc * (saw' - vb').  C11: ve' = al - be * ie'.
    const double ac = (vC10 + k.qC10 * iC10) * k.invOnePlusH;
    const double al = (ve + k.p * (jC11 + kG45 * kVee)) * k.invOnePlusPg;
    const double iHist = kG34 * ac;

    // Open-circuit solution: no base, collector or emitter current.
    const double vbLin = sawVolts - iHist * k.invGb;
    const double vebLin = al - vbLin;
    const double vcbLin = kRail - vbLin;

    if (vebLin < kOffVolts && vcbLin < kOffVolts)
    {
        ve = al;
        vb = vbLin;
        vc = kRail;
        veb = vebLin;
        vcb = vcbLin;
        slope1 = slope2 = 0.0;
        off = true;
        jC11 = kG45 * (kVee - al);
    }
    else
    {
        // Newton on the two junction voltages. Start from the open-circuit solution when
        // the transistor is just turning on, otherwise from the last solve carried forward
        // along its own trend.
        double x1 = off ? vebLin : veb + slope1 * fraction;
        double x2 = off ? vcbLin : vcb + slope2 * fraction;
        x1 = std::min (x1, kMaxJunctionVolts);
        x2 = std::min (x2, kMaxJunctionVolts);

        // With w = be * ie + veb the node voltages are vb = al - w and vc = al - w + vcb,
        // and the two node equations read
        //   base:       base1 + gb * w + ib = 0
        //   collector:  base2 + g36 * (w - vcb) + ic = 0
        const double base1 = k.gb * (sawVolts - al) - iHist;
        const double base2 = kG36 * (kRail - al);
        const double emitterGain = 1.0 + invBeta;

        double ie = 0.0;
        std::uint32_t iterations = 0;
        bool converged = false;

        while (iterations < static_cast<std::uint32_t> (kMaxIterations))
        {
            ++iterations;

            // The exponentials are evaluated exactly only when a junction has moved out
            // of the Taylor range of its last exact evaluation; a saturated transistor
            // goes through most of its on time without one.
            double ee, ec;
            const double t1 = (x1 - c1) * kInvVt;
            const double t2 = (x2 - c2) * kInvVt;
            if (std::abs (t1) <= kTaylorRange)
                ee = e1 * expTaylor (t1);
            else
            {
                c1 = x1;
                ee = e1 = junctionExp (c1);
            }
            if (std::abs (t2) <= kTaylorRange)
                ec = e2 * expTaylor (t2);
            else
            {
                c2 = x2;
                ec = e2 = junctionExp (c2);
            }

            const double iF = kIs * (ee - 1.0);
            const double iR = kIs * (ec - 1.0);
            const double gF = kIs * ee * kInvVt;
            const double gR = kIs * ec * kInvVt;

            const double ib = iF * invBeta + iR * kInvBr;
            const double ic = iF - iR * (1.0 + kInvBr);
            const double ieNow = iF * emitterGain - iR;
            const double w = k.be * ieNow + x1;

            const double f1 = base1 + k.gb * w + ib;
            const double f2 = base2 + kG36 * (w - x2) + ic;

            const double dW1 = k.be * gF * emitterGain + 1.0;
            const double dW2 = -k.be * gR;

            const double j11 = k.gb * dW1 + gF * invBeta;
            const double j12 = k.gb * dW2 + gR * kInvBr;
            const double j21 = kG36 * dW1 + gF;
            const double j22 = kG36 * (dW2 - 1.0) - gR * (1.0 + kInvBr);

            const double det = j11 * j22 - j12 * j21;
            if (! (std::abs (det) > 0.0))
                break;

            const double invDet = 1.0 / det;
            const double n1 = std::min (limitJunction (x1 + (j12 * f2 - f1 * j22) * invDet, x1), kMaxJunctionVolts);
            const double n2 = std::min (limitJunction (x2 + (j21 * f1 - j11 * f2) * invDet, x2), kMaxJunctionVolts);
            const double d1 = n1 - x1;
            const double d2 = n2 - x2;
            x1 = n1;
            x2 = n2;

            if (std::abs (d1) < kStepTolerance && std::abs (d2) < kStepTolerance)
            {
                // The correction is under 0.4 % of Vt: second order carries the emitter
                // current to the final iterate with a relative error under 1e-8.
                const double dF = gF * d1 * (1.0 + 0.5 * kInvVt * d1);
                const double dR = gR * d2 * (1.0 + 0.5 * kInvVt * d2);
                ie = ieNow + dF * emitterGain - dR;
                converged = true;
                break;
            }
        }

        if (! (std::isfinite (x1) && std::isfinite (x2)))
        {
            // Cannot happen with finite input; keep the circuit alive if it somehow does.
            x1 = std::min (vebLin, kOffVolts);
            x2 = std::min (vcbLin, kOffVolts);
            converged = false;
        }

        if (! converged)
            ie = kIs * ((junctionExp (x1) - 1.0) * emitterGain - (junctionExp (x2) - 1.0));

        const bool wasOff = off;
        ve = al - k.be * ie;
        jC11 = kG45 * (kVee - al) + (kG45 * k.be - 1.0) * ie;       // g45 * (rail - ve) - ie
        vb = ve - x1;
        vc = vb + x2;

        // Trend of the solution per sample, for the next starting point. Not across a jump
        // of the input and not out of the off state, where it means nothing.
        if (wasOff || ! (fraction > 0.0))
            slope1 = slope2 = 0.0;
        else if (fraction == 1.0)
        {
            slope1 = x1 - veb;
            slope2 = x2 - vcb;
        }
        else if (fraction >= 1.0e-2)
        {
            const double inv = 1.0 / fraction;
            slope1 = (x1 - veb) * inv;
            slope2 = (x2 - vcb) * inv;
        }
        // (a sliver of a step keeps the trend it had)

        veb = x1;
        vcb = x2;
        off = false;

        ++stats.solves;
        stats.iterations += iterations;
        stats.maxIterations = std::max (stats.maxIterations, iterations);
        if (! converged)
            ++stats.nonConverged;
    }

    // Commit C10's voltage and current at the new node.
    const double across = sawVolts - vb;
    vC10 = ac + k.bc * across;
    iC10 = kG34 * ((1.0 - k.bc) * across - ac);
    return vc;
}
}
