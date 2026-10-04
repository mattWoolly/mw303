#include "acid/fx/Chorus.h"

namespace acid::fx
{
namespace
{
constexpr double kSmoothSeconds = 0.02;
constexpr double kDelaySmoothSeconds = 0.05;
constexpr double kOnOffSeconds = 0.005;
constexpr double kMinBaseSeconds = 0.005;
constexpr double kMaxBaseSeconds = 0.025;
constexpr double kMaxSweepSeconds = 0.010;
constexpr double kLine2Ratio = 1.5;
}

double Chorus::baseDelaySeconds (double offsetPct) noexcept
{
    return lerp (kMinBaseSeconds, kMaxBaseSeconds, insert::finiteOr (offsetPct, 0.0, 0.0, 100.0) * 0.01);
}

double Chorus::rateHz (double ratePct) noexcept
{
    return expMap (insert::finiteOr (ratePct, 35.0, 0.0, 100.0) * 0.01, 0.05, 8.0);
}

double Chorus::sweepSeconds (double depthPct, double ratePct) noexcept
{
    const double wanted = insert::finiteOr (depthPct, 35.0, 0.0, 100.0) * 0.01 * kMaxSweepSeconds;
    return std::min (wanted, kMaxPitchDeviation / (kPi * rateHz (ratePct)));
}

double Chorus::highPassHz (double hpPct) noexcept
{
    return expMap (insert::finiteOr (hpPct, 0.0, 0.0, 100.0) * 0.01, 10.0, 2000.0);
}

void Chorus::prepare (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    const auto maxSamples = static_cast<std::size_t> (std::ceil ((kLine2Ratio * kMaxBaseSeconds + kMaxSweepSeconds) * sampleRate)) + 8;
    line[0].prepare (maxSamples);
    line[1].prepare (maxSamples);
    for (auto* s : { &rate, &spatial, &fat, &hpG, &wetGain, &mixWet })
    {
        s->setTimeConstant (kSmoothSeconds, sampleRate);
        s->setEpsilon (1.0e-9);
    }
    base.setTimeConstant (kDelaySmoothSeconds, sampleRate);
    base.setEpsilon (1.0e-6);
    sweep.setTimeConstant (kDelaySmoothSeconds, sampleRate);
    sweep.setEpsilon (1.0e-6);
    fade.prepare (sampleRate, kOnOffSeconds);
    setParams (ChorusParams {});
    reset();
}

void Chorus::clearState() noexcept
{
    for (auto& l : line) l.clear();   // constant time: see insert::DelayLine
    hpS[0].reset();
    hpS[1].reset();
    phase = 0.0;
    needsClear = false;
}

void Chorus::reset() noexcept
{
    clearState();
    for (auto* s : { &base, &sweep, &rate, &spatial, &fat, &hpG, &wetGain, &mixWet })
        s->snap (s->targetValue());
    hpC.set (hpG.current(), std::numbers::sqrt2);
    fade.reset (fade.isOn());
    snapNext = true;
}

void Chorus::setParams (const ChorusParams& p) noexcept
{
    const bool wasIdle = fade.idle();
    fade.setOn (p.on);

    // Idle and staying off: nothing to compute. Switching on recomputes and snaps everything.
    if (wasIdle && ! p.on) return;

    base.setTarget (baseDelaySeconds (p.offsetPct) * sampleRate);
    sweep.setTarget (sweepSeconds (p.depthPct, p.ratePct) * sampleRate);
    rate.setTarget (rateHz (p.ratePct) / sampleRate);
    spatial.setTarget (insert::finiteOr (p.spatialPct, 50.0, 0.0, 100.0) * 0.01 * 0.5);   // cycles
    fat.setTarget (insert::finiteOr (p.fatnessPct, 0.0, 0.0, 100.0) * 0.01);
    hpG.setTarget (insert::SvfCoeffs::gFor (highPassHz (p.hpPct), sampleRate));
    wetGain.setTarget (dbToGain (insert::finiteOr (p.volumeDb, 0.0, -60.0, 12.0)));
    mixWet.setTarget (insert::finiteOr (p.mixPct, 50.0, 0.0, 100.0) * 0.01);

    if (snapNext || wasIdle)
    {
        for (auto* s : { &base, &sweep, &rate, &spatial, &fat, &hpG, &wetGain, &mixWet })
            s->snap (s->targetValue());
        hpC.set (hpG.current(), std::numbers::sqrt2);
        snapNext = false;
    }
}

void Chorus::process (float* left, float* right, int numSamples) noexcept
{
    if (fade.idle() || numSamples <= 0) return;
    if (needsClear) clearState();

    const bool fading = ! fade.fullyOn();

    for (int n = 0; n < numSamples; ++n)
    {
        const double in[2] = { insert::finiteSample (left[n]), insert::finiteSample (right[n]) };
        // While fading, the lines also take a faded input so the wet onset is smooth.
        const double w = fading ? fade.next() : 1.0;
        line[0].push (w * in[0]);
        line[1].push (w * in[1]);

        const double b = base.next(), sw = sweep.next();
        const double inc = rate.next();
        const double sp = spatial.next();
        const double f = fat.next();
        if (! hpG.settled()) hpC.set (hpG.next(), std::numbers::sqrt2);
        const double vol = wetGain.next(), m = mixWet.next();
        const double norm = 1.0 / std::sqrt (1.0 + f * f);

        double out[2];
        for (int c = 0; c < 2; ++c)
        {
            const auto cc = static_cast<std::size_t> (c);
            const double ph = kTwoPi * (phase + (c == 1 ? sp : 0.0));
            const double d1 = b + sw * (0.5 + 0.5 * std::sin (ph));
            double wet = line[cc].readCubic (1.0 + d1);
            if (f > 0.0)
            {
                const double d2 = kLine2Ratio * b + sw * (0.5 + 0.5 * std::cos (ph));
                wet += f * line[cc].readCubic (1.0 + d2);
            }
            double band = 0.0;
            const double low = hpS[cc].tick (hpC, wet * norm, band);
            const double hp = wet * norm - hpC.k * band - low;
            out[c] = in[c] + m * (vol * hp - in[c]);
        }

        phase += inc;
        phase -= std::floor (phase);

        left[n] = static_cast<float> (insert::OnOffFade::mix (in[0], out[0], w));
        right[n] = static_cast<float> (insert::OnOffFade::mix (in[1], out[1], w));
        if (fading && fade.idle()) break;   // the rest of the block is already dry
    }

    if (fade.idle()) needsClear = true;
}
}
