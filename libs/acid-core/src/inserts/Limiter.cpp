#include "acid/fx/Limiter.h"

namespace acid::fx
{
namespace
{
constexpr double kSmoothSeconds = 0.02;
constexpr double kOnOffSeconds = 0.005;
constexpr double kFloorDb = -200.0;
constexpr double kDetectorCeiling = 1000.0;   // +60 dBFS
constexpr double kHoldSeconds = 0.016;        // peak hold: half a period of 31 Hz, the rectified period
}

double Limiter::staticCurveDb (double x, double t, double r, double w) noexcept
{
    const double over = x - t;
    if (2.0 * over <= -w) return x;
    if (2.0 * std::abs (over) < w)
    {
        const double a = over + 0.5 * w;
        return x + (1.0 / r - 1.0) * a * a / (2.0 * w);
    }
    return t + over / r;
}

void Limiter::prepare (double sr, int) noexcept
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    for (auto* s : { &threshold, &ratioS, &kneeDb, &makeup })
    {
        s->setTimeConstant (kSmoothSeconds, sampleRate);
        s->setEpsilon (1.0e-9);
    }
    fade.prepare (sampleRate, kOnOffSeconds);
    holdSamples = std::max (1, static_cast<int> (std::lround (kHoldSeconds * sampleRate)));
    setParams (LimiterParams {});
    reset();
}

void Limiter::reset() noexcept
{
    for (auto* s : { &threshold, &ratioS, &kneeDb, &makeup })
        s->snap (s->targetValue());
    gainDb = 0.0;
    held = 0.0;
    holdLeft = 0;
    lastReductionDb = 0.0f;
    fade.reset (fade.isOn());
    snapNext = true;
}

void Limiter::setParams (const LimiterParams& p) noexcept
{
    const bool wasIdle = fade.idle();
    fade.setOn (p.on);

    // Idle and staying off: nothing to compute. Switching on recomputes and snaps everything.
    if (wasIdle && ! p.on) return;

    threshold.setTarget (insert::finiteOr (p.thresholdDb, 0.0, -48.0, 0.0));
    ratioS.setTarget (insert::finiteOr (p.ratio, 40.0, 2.0, 40.0));
    kneeDb.setTarget (kMaxKneeDb * (1.0 - insert::finiteOr (p.kneePct, 100.0, 0.0, 100.0) * 0.01));
    makeup.setTarget (insert::finiteOr (p.gainDb, 0.0, 0.0, 24.0));
    attackCoeff = std::exp (-1.0 / (insert::finiteOr (p.attackMs, 1.0, 0.1, 100.0) * 0.001 * sampleRate));
    releaseCoeff = std::exp (-1.0 / (insert::finiteOr (p.releaseMs, 150.0, 25.0, 500.0) * 0.001 * sampleRate));
    clipOn = p.softClip;

    if (snapNext || wasIdle)
    {
        for (auto* s : { &threshold, &ratioS, &kneeDb, &makeup })
            s->snap (s->targetValue());
        gainDb = 0.0;
        held = 0.0;
        holdLeft = 0;
        snapNext = false;
    }
}

void Limiter::process (float* left, float* right, int numSamples) noexcept
{
    if (fade.idle() || numSamples <= 0)
    {
        lastReductionDb = 0.0f;
        return;
    }

    const bool fading = ! fade.fullyOn();
    double deepest = 0.0;

    for (int n = 0; n < numSamples; ++n)
    {
        const double inL = insert::finiteSample (left[n]), inR = insert::finiteSample (right[n]);
        const double t = threshold.next(), r = ratioS.next(), w = kneeDb.next(), mk = makeup.next();

        // The detector is capped at +60 dBFS: a wild sample cannot dig a gain hole that
        // would take seconds to release. It holds each peak for kHoldSeconds, so on a tone
        // it reads the crest, not every point of the wave: the gain then sits still (no
        // ripple, no overshoot of the static curve) and the attack time is honest.
        const double peak = std::min (std::max (std::abs (inL), std::abs (inR)), kDetectorCeiling);
        if (peak >= held || holdLeft <= 0)
        {
            held = peak;
            holdLeft = holdSamples;
        }
        else
        {
            --holdLeft;
        }
        const double xDb = held > 1.0e-10 ? 20.0 * std::log10 (held) : kFloorDb;
        const double target = staticCurveDb (xDb, t, r, w) - xDb;
        const double coeff = target < gainDb ? attackCoeff : releaseCoeff;
        gainDb = target + coeff * (gainDb - target);
        deepest = std::min (deepest, gainDb);

        const double g = std::pow (10.0, (gainDb + mk) / 20.0);
        double oL = inL * g, oR = inR * g;
        if (clipOn)
        {
            oL = softClip (oL);
            oR = softClip (oR);
        }
        const double fw = fading ? fade.next() : 1.0;
        left[n] = static_cast<float> (insert::OnOffFade::mix (inL, oL, fw));
        right[n] = static_cast<float> (insert::OnOffFade::mix (inR, oR, fw));
        if (fading && fade.idle()) break;   // the rest of the block is already dry
    }

    lastReductionDb = static_cast<float> (-deepest);
    if (fade.idle())
    {
        gainDb = 0.0;
        held = 0.0;
        holdLeft = 0;
    }
}
}
