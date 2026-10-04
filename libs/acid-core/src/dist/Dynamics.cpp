#include "acid/fx/Dynamics.h"
#include "acid/common/Math.h"
#include <cmath>

namespace acid::fx
{
namespace
{
constexpr double kReferenceDb = -12.0412;   // 20 log10 (0.25): the level that passes at unity
constexpr double kMaxSlope = 0.9;           // gain change per dB above threshold at full amount (10:1)
constexpr double kThresholdRange = 20.0;    // dB below the reference at full amount
constexpr double kKneeDb = 8.0;
constexpr double kGateTopDb = -54.0;        // the lift starts to release below this level
constexpr double kGateBottomDb = -66.0;     // and is gone here
constexpr double kFloorDb = -140.0;

constexpr double kHoldSeconds = 0.030;
constexpr double kHoldRenew = 0.98855;      // -0.1 dB: a peak this close to the held one renews the hold
constexpr double kEnvReleaseSeconds = 0.010;
constexpr double kAttackSeconds = 0.003;
constexpr double kReleaseSeconds = 0.120;
constexpr double kAmountSeconds = 0.020;

/** Decibels over the threshold after a quadratic soft knee. */
double kneeOver (double over) noexcept
{
    if (over <= -0.5 * kKneeDb) return 0.0;
    if (over >= 0.5 * kKneeDb) return over;
    const double t = over + 0.5 * kKneeDb;
    return t * t / (2.0 * kKneeDb);
}
}

void Dynamics::prepare (double sampleRate) noexcept
{
    const double fs = std::max (sampleRate, 1000.0);
    holdSamples = std::max (1, static_cast<int> (std::lround (kHoldSeconds * fs)));
    envReleaseCoef = onePoleCoeff (kEnvReleaseSeconds, fs);
    attackCoef = onePoleCoeff (kAttackSeconds, fs);
    releaseCoef = onePoleCoeff (kReleaseSeconds, fs);
    amountCoef = onePoleCoeff (kAmountSeconds, fs);
    reset();
}

void Dynamics::reset() noexcept
{
    amount = amountTarget;
    envelope = 0.0;
    gain = 0.0;
    hold = 0;
}

void Dynamics::setAmount (double percent) noexcept
{
    amountTarget = std::isfinite (percent) ? clamp (percent * 0.01, 0.0, 1.0) : 0.0;
}

double Dynamics::staticGainDb (double levelDb, double amt) const noexcept
{
    const double slope = kMaxSlope * amt;
    const double threshold = kReferenceDb - kThresholdRange * amt;
    const double makeUp = slope * kneeOver (kReferenceDb - threshold);
    double g = makeUp - slope * kneeOver (levelDb - threshold);

    if (levelDb < kGateTopDb)
        g *= clamp ((levelDb - kGateBottomDb) / (kGateTopDb - kGateBottomDb), 0.0, 1.0);
    return g;
}

double Dynamics::processGain (double peak) noexcept
{
    amount += amountCoef * (amountTarget - amount);
    if (amountTarget <= 0.0 && amount < 1.0e-5) amount = 0.0;

    if (! (peak <= 1.0e12)) peak = 0.0;   // also catches NaN, which would otherwise stick

    if (peak >= envelope)
    {
        envelope = peak;
        hold = holdSamples;
    }
    else if (peak >= envelope * kHoldRenew)
    {
        // A sampled tone's peaks wander by a hair from cycle to cycle. Without this a
        // slightly lower peak would let the hold run out and the gain ripple.
        hold = holdSamples;
    }
    else if (hold > 0)
    {
        --hold;
    }
    else
    {
        envelope += envReleaseCoef * (peak - envelope);
    }

    const double levelDb = envelope > 1.0e-7 ? 20.0 * std::log10 (envelope) : kFloorDb;
    const double target = staticGainDb (levelDb, amount);
    gain += (target < gain ? attackCoef : releaseCoef) * (target - gain);
    if (amount <= 0.0 && std::abs (gain) < 1.0e-9) gain = 0.0;

    return std::exp (gain * (std::numbers::ln10 / 20.0));
}

void Dynamics::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || ! isActive()) return;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = static_cast<double> (left[i]);
        const double r = right != nullptr ? static_cast<double> (right[i]) : l;
        const double g = processGain (std::max (std::abs (l), std::abs (r)));
        left[i] = static_cast<float> (l * g);
        if (right != nullptr) right[i] = static_cast<float> (r * g);
    }
}
}
