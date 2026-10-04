#include "acid/fx/Eq.h"
#include <complex>

namespace acid::fx
{
namespace
{
constexpr double kSmoothSeconds = 0.02;
constexpr double kFadeSeconds = 0.005;

struct BandDesign
{
    double g, k, m;
};

BandDesign designBand (double freqHz, double widthOct, double gainDb, double sampleRate) noexcept
{
    const double f = insert::finiteOr (freqHz, 1000.0, 20.0, std::min (16000.0, 0.45 * sampleRate));
    const double w = insert::finiteOr (widthOct, 1.0, 0.5, 4.0);
    const double db = insert::finiteOr (gainDb, 0.0, -24.0, 24.0);
    const double a = std::pow (10.0, db / 40.0);
    const double invQ = 2.0 * std::sinh (0.5 * std::numbers::ln2 * w);
    const double k = invQ / a;
    return { insert::SvfCoeffs::gFor (f, sampleRate), k, k * (a * a - 1.0) };
}
}

void Eq::prepare (double sr, int) noexcept
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    for (auto& b : bands)
    {
        b.g.setTimeConstant (kSmoothSeconds, sampleRate);
        b.k.setTimeConstant (kSmoothSeconds, sampleRate);
        b.m.setTimeConstant (kSmoothSeconds, sampleRate);
        b.g.setEpsilon (1.0e-9);
        b.k.setEpsilon (1.0e-9);
        b.m.setEpsilon (1.0e-9);
    }
    fade.prepare (sampleRate, kFadeSeconds);
    reset();
}

void Eq::reset() noexcept
{
    for (auto& b : bands)
    {
        b.s[0].reset();
        b.s[1].reset();
        b.g.snap (b.g.targetValue());
        b.k.snap (b.k.targetValue());
        b.m.snap (b.m.targetValue());
        b.c.set (b.g.current(), b.k.current());
        b.active = b.m.current() != 0.0;
    }
    fade.reset (fade.isOn());
    snapNext = true;
}

void Eq::setBandTargets (Band& b, double freqHz, double widthOct, double gainDb) noexcept
{
    const auto d = designBand (freqHz, widthOct, gainDb, sampleRate);
    b.g.setTarget (d.g);
    b.k.setTarget (d.k);
    b.m.setTarget (d.m);
}

void Eq::setParams (const EqParams& p) noexcept
{
    const bool wasIdle = fade.idle();
    fade.setOn (p.on);

    // Idle and staying off: nothing to compute. Switching on recomputes and snaps everything.
    if (wasIdle && ! p.on) return;

    setBandTargets (bands[0], p.lowFreqHz, p.lowWidthOct, p.lowGainDb);
    setBandTargets (bands[1], p.loMidFreqHz, p.loMidWidthOct, p.loMidGainDb);
    setBandTargets (bands[2], p.hiMidFreqHz, p.hiMidWidthOct, p.hiMidGainDb);
    setBandTargets (bands[3], p.highFreqHz, p.highWidthOct, p.highGainDb);

    // Nothing to glide from while idle or straight after a reset.
    if (snapNext || wasIdle)
    {
        for (auto& b : bands)
        {
            b.g.snap (b.g.targetValue());
            b.k.snap (b.k.targetValue());
            b.m.snap (b.m.targetValue());
            b.c.set (b.g.current(), b.k.current());
            b.active = b.m.current() != 0.0;
            if (! b.active) { b.s[0].reset(); b.s[1].reset(); }
        }
        snapNext = false;
    }
}

void Eq::process (float* left, float* right, int numSamples) noexcept
{
    if (fade.idle() || numSamples <= 0) return;

    const bool fading = ! fade.fullyOn();

    for (int i = 0; i < numSamples; ++i)
    {
        const double inL = insert::finiteSample (left[i]), inR = insert::finiteSample (right[i]);
        double l = inL, r = inR;

        for (auto& b : bands)
        {
            const bool moving = ! (b.g.settled() && b.k.settled() && b.m.settled());
            if (moving)
            {
                b.g.next();
                b.k.next();
                b.m.next();
                b.c.set (b.g.current(), b.k.current());
                b.active = true;
            }
            else if (b.m.current() == 0.0)
            {
                if (b.active) { b.s[0].reset(); b.s[1].reset(); b.active = false; }
                continue;
            }

            const double m = b.m.current();
            double band = 0.0;
            b.s[0].tick (b.c, l, band);
            l += m * band;
            b.s[1].tick (b.c, r, band);
            r += m * band;
        }

        const double w = fading ? fade.next() : 1.0;
        left[i] = static_cast<float> (insert::OnOffFade::mix (inL, l, w));
        right[i] = static_cast<float> (insert::OnOffFade::mix (inR, r, w));
        if (fading && fade.idle()) break;   // the rest of the block is already dry
    }

    if (fade.idle())
        for (auto& b : bands) { b.s[0].reset(); b.s[1].reset(); }
}

double Eq::bandMagnitude (double freqHz, double widthOct, double gainDb, double hz, double sr) noexcept
{
    const auto d = designBand (freqHz, widthOct, gainDb, sr);
    // Bilinear map of the analog prototype normalised to the prewarped centre.
    const double wa = std::tan (kPi * clamp (hz, 0.0, 0.4999 * sr) / sr) / d.g;
    const std::complex<double> s (0.0, wa);
    const double a2 = d.m / d.k + 1.0;     // A^2
    const double kA2 = d.k * a2;           // A / Q
    const auto h = (s * s + kA2 * s + 1.0) / (s * s + d.k * s + 1.0);
    return std::abs (h);
}
}
