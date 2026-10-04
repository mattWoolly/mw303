#include "acid/fx/Delay.h"

namespace acid::fx
{
namespace
{
constexpr double kTimeFadeSeconds = 0.05;
constexpr double kOnOffSeconds = 0.005;
constexpr double kSmoothSeconds = 0.02;
constexpr double kDcBlockHz = 5.0;
constexpr double kLoopKnee = 1.0;
constexpr double kLoopCeiling = 2.0;
constexpr double kMinQ = 0.70710678118654752;
constexpr double kMaxQ = 12.0;

/** 1 / (peak gain) of the SVF low-pass and high-pass outputs for damping k = 1/Q <= sqrt 2.
    The analog peak is Q / sqrt (1 - 1 / (4 Q^2)); the bilinear map keeps the peak value
    (it only moves the frequency), so this holds for the trapezoidal filter too. */
double resonantPeakNorm (double k) noexcept
{
    return k >= std::numbers::sqrt2 ? 1.0 : k * std::sqrt (1.0 - 0.25 * k * k);
}
}

double Delay::syncedSeconds (int num, int den, DelayScale scale, double bpm) noexcept
{
    const double n = clamp (num, 1, 32);
    const double d = clamp (den, 1, 32);
    const double tempo = (std::isfinite (bpm) && bpm > 0.0) ? clamp (bpm, 1.0, 999.0) : 120.0;
    const double m = scale == DelayScale::Dotted ? 1.5 : (scale == DelayScale::Triplet ? 2.0 / 3.0 : 1.0);
    return n / d * (240.0 / tempo) * m;
}

void Delay::prepare (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    maxSamples = static_cast<std::size_t> (std::ceil (kMaxDelaySeconds * sampleRate));
    line[0].prepare (maxSamples + 1);
    line[1].prepare (maxSamples + 1);
    taps.prepare (sampleRate, kTimeFadeSeconds);
    fade.prepare (sampleRate, kOnOffSeconds);

    for (auto* s : { &feedback, &cross, &steerL, &steerR, &dryGain, &wetGain, &filterG, &filterK,
                     &wDirect, &wLow, &wBand, &wHigh })
    {
        s->setTimeConstant (kSmoothSeconds, sampleRate);
        s->setEpsilon (1.0e-9);
    }
    dcCoeff = std::exp (-kTwoPi * kDcBlockHz / sampleRate);
    needsClear = false;
    reset();
}

void Delay::clearLines() noexcept
{
    line[0].clear();   // constant time: see insert::DelayLine
    line[1].clear();
    filterS[0].reset();
    filterS[1].reset();
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;
    needsClear = false;
}

void Delay::reset() noexcept
{
    clearLines();
    for (auto* s : { &feedback, &cross, &steerL, &steerR, &dryGain, &wetGain, &filterG, &filterK,
                     &wDirect, &wLow, &wBand, &wHigh })
        s->snap (s->targetValue());
    filterC.set (filterG.current(), filterK.current());
    peakNorm = resonantPeakNorm (filterC.k);
    filterS[0].reset();
    filterS[1].reset();
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;
    taps.snap (taps.current (0), taps.current (1));
    fade.reset (fade.isOn());
    snapNext = true;
}

void Delay::setParams (const DelayParams& p) noexcept
{
    const bool wasIdle = fade.idle();
    fade.setOn (p.on);

    double secs[2];
    if (p.sync)
    {
        secs[0] = syncedSeconds (p.leftNum, p.leftDen, p.leftScale, p.bpm);
        secs[1] = syncedSeconds (p.rightNum, p.rightDen, p.rightScale, p.bpm);
    }
    else
    {
        secs[0] = secs[1] = insert::finiteOr (p.timeSec, 0.2, 0.01, 1.0);
    }

    clamped = false;
    std::size_t samples[2];
    for (int c = 0; c < 2; ++c)
    {
        const double want = secs[c] * sampleRate;
        if (secs[c] > kMaxDelaySeconds) clamped = true;
        samples[c] = static_cast<std::size_t> (clamp (std::llround (want), 1ll, static_cast<long long> (maxSamples)));
    }
    taps.setTarget (samples[0], samples[1]);

    // Idle and staying off: nothing to compute. Switching on recomputes and snaps everything.
    if (wasIdle && ! p.on)
    {
        taps.snap (samples[0], samples[1]);
        return;
    }

    feedback.setTarget (insert::finiteOr (p.feedbackPct, 0.0, 0.0, 100.0) * 0.01);
    const double s = insert::finiteOr (p.spreadPct, 0.0, -100.0, 100.0) * 0.01;
    cross.setTarget (std::abs (s));
    steerL.setTarget (std::max (s, 0.0));
    steerR.setTarget (std::max (-s, 0.0));
    const auto mix = insert::MixGains::equalPower (insert::finiteOr (p.mixPct, 0.0, 0.0, 100.0) * 0.01);
    dryGain.setTarget (mix.dry);
    wetGain.setTarget (mix.wet);

    const double cutoff = insert::finiteOr (p.filterCutoffHz, 8000.0, 10.0, 16000.0);
    const double reso = insert::finiteOr (p.filterResoPct, 0.0, 0.0, 100.0) * 0.01;
    filterG.setTarget (insert::SvfCoeffs::gFor (cutoff, sampleRate));
    filterK.setTarget (1.0 / (kMinQ * std::pow (kMaxQ / kMinQ, reso)));
    const auto type = static_cast<int> (p.filterType);
    wDirect.setTarget (type == 0 || type > 3 ? 1.0 : 0.0);
    wLow.setTarget (type == 1 ? 1.0 : 0.0);
    wBand.setTarget (type == 2 ? 1.0 : 0.0);
    wHigh.setTarget (type == 3 ? 1.0 : 0.0);

    if (snapNext || wasIdle)
    {
        for (auto* sm : { &feedback, &cross, &steerL, &steerR, &dryGain, &wetGain, &filterG, &filterK,
                          &wDirect, &wLow, &wBand, &wHigh })
            sm->snap (sm->targetValue());
        filterC.set (filterG.current(), filterK.current());
        peakNorm = resonantPeakNorm (filterC.k);
        taps.snap (samples[0], samples[1]);
        snapNext = false;
    }
}

void Delay::process (float* left, float* right, int numSamples) noexcept
{
    if (fade.idle() || numSamples <= 0) return;
    if (needsClear) clearLines();

    const bool fading = ! fade.fullyOn();

    for (int i = 0; i < numSamples; ++i)
    {
        // A non-finite sample from upstream is taken as silence, here and in the dry path.
        const double inL = insert::finiteSample (left[i]), inR = insert::finiteSample (right[i]);
        // While fading, the lines also take a faded input so echoes start and stop smoothly.
        const double w = fading ? fade.next() : 1.0;

        const double tw = taps.next();
        const double yL = taps.read (line[0], 0, tw);
        const double yR = taps.read (line[1], 1, tw);

        const double fb = feedback.next();
        const double a = cross.next();
        const double sL = steerL.next(), sR = steerR.next();
        const double dry = dryGain.next(), wet = wetGain.next();
        if (! (filterG.settled() && filterK.settled()))
        {
            filterC.set (filterG.next(), filterK.next());
            peakNorm = resonantPeakNorm (filterC.k);
        }
        const double wd = wDirect.next(), wl = wLow.next(), wb = wBand.next(), wh = wHigh.next();

        const double src[2] = { yL + a * (yR - yL), yR + a * (yL - yR) };
        double back[2];
        for (int c = 0; c < 2; ++c)
        {
            double band = 0.0;
            const double low = filterS[c].tick (filterC, src[c], band);
            const double bandN = filterC.k * band;
            const double high = src[c] - bandN - low;
            // Every filter output is scaled to unity peak gain, so the loop gain never
            // exceeds the feedback amount: below 100 % every echo dies away.
            const double filtered = wd * src[c] + peakNorm * (wl * low + wh * high) + wb * bandN;
            const double hp = filtered - dcX[c] + dcCoeff * dcY[c];
            dcX[c] = filtered;
            dcY[c] = insert::flushDenormal (hp);
            back[c] = fb * hp;
        }

        const double sendL = w * inL, sendR = w * inR;
        const double mid = 0.5 * (sendL + sendR);
        const double keep = 1.0 - sL - sR;
        line[0].push (insert::softLimit (keep * sendL + sL * mid + back[0], kLoopKnee, kLoopCeiling));
        line[1].push (insert::softLimit (keep * sendR + sR * mid + back[1], kLoopKnee, kLoopCeiling));

        left[i] = static_cast<float> (insert::OnOffFade::mix (inL, dry * inL + wet * yL, w));
        right[i] = static_cast<float> (insert::OnOffFade::mix (inR, dry * inR + wet * yR, w));
        if (fading && fade.idle()) break;   // the rest of the block is already dry
    }

    if (fade.idle()) needsClear = true;
}
}
