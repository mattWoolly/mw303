#include "acid/dsp/Decimator.h"

#include "acid/common/Math.h"

#include <cmath>

namespace acid::dsp
{
double Decimator::designHalfBand (double* coefs, int numCoefs, double transition) noexcept
{
    // Closed-form elliptic half-band design in the allpass-sum form (Valenzuela and
    // Constantinides 1983; the series are the theta-function expansions used by the
    // public reference designer for this filter family).
    numCoefs = clamp (numCoefs, 1, kMaxCoefficients);
    transition = clamp (transition, 1.0e-3, 0.499);

    // Selectivity k and its elliptic nome q.
    double k = std::tan ((1.0 - 2.0 * transition) * kPi * 0.25);
    k *= k;
    const double kk = std::sqrt (std::sqrt (1.0 - k * k));
    const double e = 0.5 * (1.0 - kk) / (1.0 + kk);
    const double e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

    const int order = 2 * numCoefs + 1;
    const double q14 = std::sqrt (std::sqrt (q));

    for (int index = 0; index < numCoefs; ++index)
    {
        const double c = static_cast<double> (index + 1) * kPi / static_cast<double> (order);

        double num = 0.0, den = 0.5, sign = 1.0;
        for (int i = 0; i < 32; ++i)
        {
            const double term = std::pow (q, static_cast<double> (i * (i + 1))) * sign;
            num += term * std::sin (static_cast<double> (2 * i + 1) * c);
            sign = -sign;
            if (std::abs (term) < 1.0e-30) break;
        }
        sign = -1.0;
        for (int i = 1; i < 32; ++i)
        {
            const double term = std::pow (q, static_cast<double> (i * i)) * sign;
            den += term * std::cos (static_cast<double> (2 * i) * c);
            sign = -sign;
            if (std::abs (term) < 1.0e-30) break;
        }

        double ww = num * q14 / den;
        ww *= ww;
        const double x = std::sqrt ((1.0 - ww * k) * (1.0 - ww / k)) / (1.0 + ww);
        coefs[index] = (1.0 - x) / (1.0 + x);
    }

    // Stop-band rejection: the discrimination factor is 4 * sqrt (q ^ order), and for a
    // power-complementary pair that is the stop-band power gain.
    return -10.0 * std::log10 (4.0) - 5.0 * static_cast<double> (order) * std::log10 (q);
}

void Decimator::prepare (double baseSampleRate, int factor, Quality quality) noexcept
{
    totalFactor = factor >= 8 ? 8 : (factor >= 4 ? 4 : (factor >= 2 ? 2 : 1));
    stageCount = totalFactor == 8 ? 3 : (totalFactor == 4 ? 2 : (totalFactor == 2 ? 1 : 0));

    const double baseRate = std::max (baseSampleRate, 1.0);
    const double passband = std::min (kPassbandHz, kPassbandOverBaseRate * baseRate);
    latencyBase = 0.0;

    for (int s = 0; s < stageCount; ++s)
    {
        Stage& st = stages[static_cast<std::size_t> (s)];
        const double oversampling = static_cast<double> (totalFactor >> s);     // of this stage's input
        const double inRate = baseRate * oversampling;

        st.transition = 2.0 * (0.25 - passband / inRate);
        const bool wide = st.transition >= kWideTransition;
        st.numCoefs = quality == Quality::Ultra ? (wide ? 4 : 10) : (wide ? 3 : 8);
        st.coef.fill (0.0);
        st.rejectionDb = designHalfBand (st.coef.data(), st.numCoefs, st.transition);

        // Low-frequency group delay in this stage's input samples: the mean of its two
        // branches, the odd one carrying the earlier sample (one more sample of delay).
        double d0 = 0.0, d1 = 1.0;
        for (int i = 0; i < st.numCoefs; ++i)
        {
            const double c = st.coef[static_cast<std::size_t> (i)];
            ((i & 1) == 0 ? d0 : d1) += 2.0 * (1.0 - c) / (1.0 + c);
        }
        latencyBase += 0.5 * (d0 + d1) / oversampling;
    }

    for (int s = stageCount; s < kMaxStages; ++s)
        stages[static_cast<std::size_t> (s)] = Stage {};

    reset();
}

void Decimator::reset() noexcept
{
    for (auto& st : stages)
    {
        st.x.fill (0.0);
        st.y.fill (0.0);
    }
}

namespace
{
template <typename In, typename StageArray>
inline double decimateOne (StageArray& stages, int stageCount, const In* in) noexcept
{
    switch (stageCount)
    {
        case 1:
            return stages[0].process (static_cast<double> (in[0]), static_cast<double> (in[1]));

        case 2:
        {
            const double a0 = stages[0].process (static_cast<double> (in[0]), static_cast<double> (in[1]));
            const double a1 = stages[0].process (static_cast<double> (in[2]), static_cast<double> (in[3]));
            return stages[1].process (a0, a1);
        }

        case 3:
        {
            const double a0 = stages[0].process (static_cast<double> (in[0]), static_cast<double> (in[1]));
            const double a1 = stages[0].process (static_cast<double> (in[2]), static_cast<double> (in[3]));
            const double a2 = stages[0].process (static_cast<double> (in[4]), static_cast<double> (in[5]));
            const double a3 = stages[0].process (static_cast<double> (in[6]), static_cast<double> (in[7]));
            const double b0 = stages[1].process (a0, a1);
            const double b1 = stages[1].process (a2, a3);
            return stages[2].process (b0, b1);
        }

        default:
            return static_cast<double> (in[0]);
    }
}
}

double Decimator::processSample (const double* in) noexcept
{
    return decimateOne (stages, stageCount, in);
}

template <typename In, typename Out>
void Decimator::processBlock (const In* in, Out* out, int numOut) noexcept
{
    const int step = totalFactor;
    for (int n = 0; n < numOut; ++n, in += step)
        out[n] = static_cast<Out> (decimateOne (stages, stageCount, in));
}

void Decimator::process (const double* in, double* out, int numOut) noexcept { processBlock (in, out, numOut); }
void Decimator::process (const double* in, float* out, int numOut) noexcept { processBlock (in, out, numOut); }
void Decimator::process (const float* in, float* out, int numOut) noexcept { processBlock (in, out, numOut); }
}
