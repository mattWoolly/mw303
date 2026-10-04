#include "acid/fx/Reverb.h"

namespace acid::fx
{
namespace
{
constexpr double kSpeedOfSound = 343.0;     // m/s
constexpr double kSmoothSeconds = 0.02;
constexpr double kSizeSmoothSeconds = 0.15;
constexpr double kOnOffSeconds = 0.005;
constexpr double kPreFadeSeconds = 0.05;
constexpr double kMaxPreDelaySeconds = 1.0;
constexpr double kMinLateSizeM = 5.1;
constexpr double kMaxEarlySizeM = 20.5;
constexpr double kMaxLateSizeM = 51.1;
constexpr double kSizeExponent = 0.6;       // line length grows as size^0.6
constexpr double kMaxModSeconds = 0.001;
constexpr double kMinSmallRoomModPct = 10.0;  // tail modulation floor at the smallest late size ...
constexpr double kModFloorEndM = 15.0;        // ... falling to none at this size
constexpr double kDiffusionScale = 0.7;     // input allpass coefficient at 100 % diffusion
constexpr double kLoopDiffusionScale = 0.6; // in-loop allpass coefficient at 100 % diffusion
constexpr double kLoopGuard = 8.0;          // far above any signal level: a guard only

// Early tap distances in units of the early size, and their signs. Two different
// patterns so the left and right reflections are decorrelated.
constexpr std::array<double, Reverb::kNumTaps> kTapDistL { 0.31, 0.47, 0.63, 0.81, 0.97, 1.18, 1.39, 1.62, 1.88, 2.17, 2.51, 2.93 };
constexpr std::array<double, Reverb::kNumTaps> kTapDistR { 0.37, 0.53, 0.71, 0.86, 1.07, 1.27, 1.49, 1.74, 2.01, 2.33, 2.68, 3.00 };
constexpr std::array<double, Reverb::kNumTaps> kTapSignL { 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, 1, -1 };
constexpr std::array<double, Reverb::kNumTaps> kTapSignR { 1, 1, -1, 1, -1, -1, 1, -1, 1, 1, -1, -1 };
constexpr double kMaxTapDist = 3.0;

// Diffuser lengths in seconds (left, right): mutually prime-ish, fixed at prepare.
constexpr double kEarlyApSec[2][Reverb::kNumEarlyAllpass] { { 0.00173, 0.00291, 0.00431 }, { 0.00191, 0.00313, 0.00467 } };
constexpr double kLateApSec[2][Reverb::kNumLateAllpass] { { 0.00477, 0.00359, 0.01273, 0.00930 }, { 0.00503, 0.00381, 0.01197, 0.00869 } };

// FDN line lengths relative to the mean: primes / 1000, log-spaced.
constexpr std::array<double, Reverb::kNumLines> kLineRatio {
    0.557, 0.607, 0.661, 0.709, 0.769, 0.827, 0.883, 0.953,
    1.009, 1.069, 1.129, 1.193, 1.259, 1.319, 1.381, 1.451 };
constexpr double kMaxLineRatio = 1.451;

// In-loop allpass lengths in seconds, one per line, spread so no two coincide.
constexpr std::array<double, Reverb::kNumLines> kLoopApSec {
    0.00293, 0.00131, 0.00347, 0.00179, 0.00229, 0.00397, 0.00107, 0.00311,
    0.00163, 0.00373, 0.00211, 0.00257, 0.00139, 0.00331, 0.00191, 0.00277 };

// Output sign patterns (orthogonal) and input sign pattern.
constexpr std::array<double, Reverb::kNumLines> kOutL { 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1 };
constexpr std::array<double, Reverb::kNumLines> kOutR { 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1 };
constexpr std::array<double, Reverb::kNumLines> kInSign { 1, 1, 1, 1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1 };

// Tail modulation: four rotating phasors, each used in sine and cosine phase, drive the
// eight lines with an odd number of set bits (both inputs, both output sign groups).
// -1 marks an unmodulated line.
constexpr double kLfoHz[4] { 0.31, 0.53, 0.79, 1.07 };
constexpr std::array<int, Reverb::kNumLines> kLfoOf { -1, 0, 1, -1, 2, -1, -1, 3, 4, -1, -1, 5, -1, 6, 7, -1 };

double meanLineSeconds (double sizeM) noexcept
{
    const double largest = (2.0 / 3.0) * kMaxLateSizeM / kSpeedOfSound;
    return largest * std::pow (sizeM / kMaxLateSizeM, kSizeExponent);
}

double tapNorm (const std::array<double, Reverb::kNumTaps>& dist, std::size_t i) noexcept
{
    return std::pow (dist[0] / dist[i], 0.8);
}

template <std::size_t H>
inline void hadamardStage (std::array<double, Reverb::kNumLines>& x) noexcept
{
    for (std::size_t i = 0; i < Reverb::kNumLines; i += 2 * H)
        for (std::size_t j = i; j < i + H; ++j)
        {
            const double a = x[j], b = x[j + H];
            x[j] = a + b;
            x[j + H] = a - b;
        }
}

/** In-place orthonormal 16-point Hadamard transform. */
inline void hadamard16 (std::array<double, Reverb::kNumLines>& x) noexcept
{
    hadamardStage<1> (x);
    hadamardStage<2> (x);
    hadamardStage<4> (x);
    hadamardStage<8> (x);
    for (auto& v : x) v *= 0.25;
}
}

double Reverb::designT60 (double lateSizeM, double feedbackPct) noexcept
{
    const double size = insert::finiteOr (lateSizeM, 27.9, kMinLateSizeM, kMaxLateSizeM);
    const double fb = insert::finiteOr (feedbackPct, 30.0, 0.0, 100.0) * 0.01;
    const double alpha = 0.02 * std::pow (50.0, 1.0 - fb);
    return 0.161 * size / (6.0 * alpha);
}

void Reverb::prepare (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;

    preMax = static_cast<std::size_t> (std::ceil (kMaxPreDelaySeconds * sampleRate)) + 1;
    pre[0].prepare (preMax + 1);
    pre[1].prepare (preMax + 1);
    preTaps.prepare (sampleRate, kPreFadeSeconds);

    const auto earlyMax = static_cast<std::size_t> (std::ceil (kMaxTapDist * kMaxEarlySizeM / kSpeedOfSound * sampleRate)) + 4;
    for (std::size_t c = 0; c < 2; ++c)
    {
        early[c].prepare (earlyMax);
        const auto& dist = c == 0 ? kTapDistL : kTapDistR;
        const auto& sign = c == 0 ? kTapSignL : kTapSignR;
        double sum = 0.0;
        for (std::size_t i = 0; i < kNumTaps; ++i) sum += tapNorm (dist, i) * tapNorm (dist, i);
        const double norm = 1.0 / std::sqrt (sum);
        for (std::size_t i = 0; i < kNumTaps; ++i) tapGain[c][i] = sign[i] * tapNorm (dist, i) * norm;

        for (std::size_t i = 0; i < kNumEarlyAllpass; ++i)
        {
            auto& ap = earlyAp[c][i];
            ap.delay = std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (kEarlyApSec[c][i] * sampleRate)));
            ap.line.prepare (ap.delay + 1);
        }
        for (std::size_t i = 0; i < kNumLateAllpass; ++i)
        {
            auto& ap = lateAp[c][i];
            ap.delay = std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (kLateApSec[c][i] * sampleRate)));
            ap.line.prepare (ap.delay + 1);
        }
    }

    const auto lineMax = static_cast<std::size_t> (std::ceil ((kMaxLineRatio * meanLineSeconds (kMaxLateSizeM) + kMaxModSeconds) * sampleRate)) + 8;
    for (auto& l : lines) l.prepare (lineMax);
    for (std::size_t i = 0; i < kNumLines; ++i)
    {
        loopAp[i].delay = std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (kLoopApSec[i] * sampleRate)));
        loopAp[i].line.prepare (loopAp[i].delay + 1);
    }

    for (std::size_t i = 0; i < kNumLfos; ++i)
    {
        const double w = kTwoPi * kLfoHz[i] / sampleRate;
        lfoCw[i] = std::cos (w);
        lfoSw[i] = std::sin (w);
    }

    for (auto* s : { &earlyDiff, &earlyG, &lateDiff, &loopDiff, &modDepth, &earlyMix, &lateMix, &dryGain, &wetGain })
    {
        s->setTimeConstant (kSmoothSeconds, sampleRate);
        s->setEpsilon (1.0e-9);
    }
    earlyUnit.setTimeConstant (kSizeSmoothSeconds, sampleRate);
    earlyUnit.setEpsilon (1.0e-6);
    for (std::size_t i = 0; i < kNumLines; ++i)
    {
        lineLen[i].setTimeConstant (kSizeSmoothSeconds, sampleRate);
        lineLen[i].setEpsilon (1.0e-6);
        gainDc[i].setTimeConstant (kSizeSmoothSeconds, sampleRate);
        gainDc[i].setEpsilon (1.0e-12);
        pole[i].setTimeConstant (kSizeSmoothSeconds, sampleRate);
        pole[i].setEpsilon (1.0e-12);
    }

    fade.prepare (sampleRate, kOnOffSeconds);
    lowCutHz = -1.0;
    tapUnit = -1.0;

    // Usable defaults until the first setParams.
    setParams (ReverbParams {});
    reset();
}

void Reverb::clearState() noexcept
{
    const auto clearLine = [] (insert::DelayLine& l) { l.clear(); };   // constant time: see insert::DelayLine
    for (std::size_t c = 0; c < 2; ++c)
    {
        clearLine (pre[c]);
        clearLine (early[c]);
        for (auto& ap : earlyAp[c]) clearLine (ap.line);
        for (auto& ap : lateAp[c]) clearLine (ap.line);
        lowCutS[c].reset();
        earlyLp[c].reset();
    }
    for (auto& l : lines) clearLine (l);
    for (auto& ap : loopAp) clearLine (ap.line);
    damp.fill (0.0);
    for (std::size_t i = 0; i < kNumLfos; ++i)
    {
        const double ph = 2.39996322972865332 * static_cast<double> (i);   // golden angle
        lfoC[i] = std::cos (ph);
        lfoS[i] = std::sin (ph);
    }
    needsClear = false;
}

void Reverb::snapAll() noexcept
{
    for (auto* s : { &earlyDiff, &earlyG, &lateDiff, &loopDiff, &modDepth, &earlyMix, &lateMix, &dryGain, &wetGain,
                     &earlyUnit })
        s->snap (s->targetValue());
    for (std::size_t i = 0; i < kNumLines; ++i)
    {
        lineLen[i].snap (lineLen[i].targetValue());
        gainDc[i].snap (gainDc[i].targetValue());
        pole[i].snap (pole[i].targetValue());
    }
    coeffsMoving = false;
}

void Reverb::updateTaps (double unit) noexcept
{
    tapUnit = unit;
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto& dist = c == 0 ? kTapDistL : kTapDistR;
        for (std::size_t i = 0; i < kNumTaps; ++i)
        {
            const double d = 1.0 + dist[i] * unit;          // >= 1: read after push
            const auto whole = static_cast<std::int64_t> (d);
            tapIdx[c][i] = static_cast<std::size_t> (whole);
            tapFrac[c][i] = d - static_cast<double> (whole);
        }
    }
}

void Reverb::reset() noexcept
{
    clearState();
    snapAll();
    updateTaps (earlyUnit.current());
    preTaps.snap (preTaps.current (0), preTaps.current (1));
    fade.reset (fade.isOn());
    snapNext = true;
}

void Reverb::setParams (const ReverbParams& p) noexcept
{
    const bool wasIdle = fade.idle();
    fade.setOn (p.on);

    // Idle and staying off: nothing to compute. Switching on recomputes and snaps everything.
    if (wasIdle && ! p.on) return;

    const double preMs = insert::finiteOr (p.preDelayMs, 0.0, 0.0, 1000.0);
    const auto preSamples = std::min (static_cast<std::size_t> (std::llround (preMs * 0.001 * sampleRate)) + 1, preMax);
    preTaps.setTarget (preSamples, preSamples);

    const double cut = insert::finiteOr (p.lowCutHz, 60.0, 1.0, 1000.0);
    if (cut != lowCutHz)
    {
        lowCutHz = cut;
        lowCutC.set (insert::SvfCoeffs::gFor (cut, sampleRate), std::numbers::sqrt2);
    }

    const double earlySize = insert::finiteOr (p.earlySizeM, 10.4, 1.1, kMaxEarlySizeM);
    earlyUnit.setTarget (earlySize / kSpeedOfSound * sampleRate);
    earlyDiff.setTarget (kDiffusionScale * insert::finiteOr (p.earlyDiffPct, 50.0, 0.0, 100.0) * 0.01);
    const double eb = insert::finiteOr (p.earlyBrightPct, 50.0, 0.0, 100.0) * 0.01;
    earlyG.setTarget (insert::SvfCoeffs::gFor (600.0 * std::exp2 (5.5 * eb), sampleRate));

    const double diff01 = insert::finiteOr (p.lateDiffPct, 50.0, 0.0, 100.0) * 0.01;
    lateDiff.setTarget (kDiffusionScale * diff01);
    loopDiff.setTarget (kLoopDiffusionScale * diff01);
    const double lateSize = insert::finiteOr (p.lateSizeM, 27.9, kMinLateSizeM, kMaxLateSizeM);
    const double mean = meanLineSeconds (lateSize) * sampleRate;

    const double t60 = designT60 (lateSize, p.feedbackPct);
    const double lb = insert::finiteOr (p.lateBrightPct, 50.0, 0.0, 100.0) * 0.01;
    const double t60High = t60 * expMap (lb, 0.08, 1.0);
    for (std::size_t i = 0; i < kNumLines; ++i)
    {
        const double lineSamples = std::max (4.0, std::round (mean * kLineRatio[i]));
        lineLen[i].setTarget (lineSamples);
        // One pass round the loop: the line plus its allpass.
        const double len = lineSamples + static_cast<double> (loopAp[i].delay);
        const double gdc = std::pow (10.0, -3.0 * len / (sampleRate * t60));
        const double ghf = std::pow (10.0, -3.0 * len / (sampleRate * t60High));
        gainDc[i].setTarget (gdc);
        pole[i].setTarget ((gdc - ghf) / (gdc + ghf));
        if (! (gainDc[i].settled() && pole[i].settled() && lineLen[i].settled())) coeffsMoving = true;
    }

    // Small rooms keep a floor of tail modulation: with few, short lines their modes are
    // sparse enough that one would otherwise ring steadily (a narrow peak that never moves).
    const double modFloor = kMinSmallRoomModPct * clamp ((kModFloorEndM - lateSize) / (kModFloorEndM - kMinLateSizeM), 0.0, 1.0);
    const double modPct = std::max (insert::finiteOr (p.modPct, 10.0, 0.0, 100.0), modFloor);
    modDepth.setTarget (modPct * 0.01 * kMaxModSeconds * sampleRate);

    const auto el = insert::MixGains::equalPower (insert::finiteOr (p.earlyLatePct, 50.0, 0.0, 100.0) * 0.01);
    earlyMix.setTarget (el.dry);
    lateMix.setTarget (el.wet);
    const auto mix = insert::MixGains::equalPower (insert::finiteOr (p.mixPct, 25.0, 0.0, 100.0) * 0.01);
    dryGain.setTarget (mix.dry);
    wetGain.setTarget (mix.wet);

    if (snapNext || wasIdle)
    {
        snapAll();
        updateTaps (earlyUnit.current());
        preTaps.snap (preSamples, preSamples);
        snapNext = false;
    }
}

void Reverb::process (float* left, float* right, int numSamples) noexcept
{
    if (fade.idle() || numSamples <= 0) return;
    if (needsClear) clearState();

    const bool fading = ! fade.fullyOn();
    constexpr double inGain = 0.35355339059327376;   // 1 / sqrt (8): eight lines per channel
    constexpr double outGain = 0.35355339059327376;

    std::array<double, kNumLines> z {};

    for (int n = 0; n < numSamples; ++n)
    {
        const double inL = insert::finiteSample (left[n]), inR = insert::finiteSample (right[n]);
        // While fading, the network also takes a faded input so the wet onset is smooth.
        const double fw = fading ? fade.next() : 1.0;

        // Pre-delay (push first, so a tap of 1 is no delay) and wet low cut.
        pre[0].push (fw * inL);
        pre[1].push (fw * inR);
        const double pw = preTaps.next();
        double x[2] = { preTaps.read (pre[0], 0, pw), preTaps.read (pre[1], 1, pw) };
        for (std::size_t c = 0; c < 2; ++c)
        {
            double band = 0.0;
            const double low = lowCutS[c].tick (lowCutC, x[c], band);
            x[c] = x[c] - lowCutC.k * band - low;
        }

        // Early reflections.
        const double unit = earlyUnit.next();
        if (unit != tapUnit) updateTaps (unit);
        const double eDiff = earlyDiff.next();
        const double eG = earlyG.next();
        double e[2];
        for (std::size_t c = 0; c < 2; ++c)
        {
            early[c].push (x[c]);
            double sum = 0.0;
            for (std::size_t i = 0; i < kNumTaps; ++i)
            {
                const double a = early[c].read (tapIdx[c][i]);
                const double b = early[c].read (tapIdx[c][i] + 1);
                sum += tapGain[c][i] * (a + tapFrac[c][i] * (b - a));
            }
            for (auto& ap : earlyAp[c]) sum = ap.tick (sum, eDiff);
            e[c] = earlyLp[c].tick (sum, eG);
        }

        // Late input diffusion.
        const double lDiff = lateDiff.next();
        double d[2] = { x[0], x[1] };
        for (std::size_t c = 0; c < 2; ++c)
            for (auto& ap : lateAp[c]) d[c] = ap.tick (d[c], lDiff);

        // Feedback delay network. While sizes glide every line reads fractionally; once
        // settled the unmodulated lines read whole samples (the same values, cheaper).
        const double depth = modDepth.next();
        const double loopG = loopDiff.next();
        const bool gliding = coeffsMoving;
        if (coeffsMoving)
        {
            bool moving = false;
            for (std::size_t i = 0; i < kNumLines; ++i)
            {
                lineLen[i].next();
                gainDc[i].next();
                pole[i].next();
                moving = moving || ! (gainDc[i].settled() && pole[i].settled() && lineLen[i].settled());
            }
            coeffsMoving = moving;
        }

        double lfo[2 * kNumLfos];
        for (std::size_t k = 0; k < kNumLfos; ++k)
        {
            // Rotating phasor, renormalised every sample.
            const double c0 = lfoC[k], s0 = lfoS[k];
            const double c1 = c0 * lfoCw[k] - s0 * lfoSw[k];
            const double s1 = s0 * lfoCw[k] + c0 * lfoSw[k];
            const double r = 1.5 - 0.5 * (c1 * c1 + s1 * s1);
            lfoC[k] = c1 * r;
            lfoS[k] = s1 * r;
            lfo[2 * k] = depth * lfoS[k];
            lfo[2 * k + 1] = depth * lfoC[k];
        }

        double outL = 0.0, outR = 0.0;
        for (std::size_t i = 0; i < kNumLines; ++i)
        {
            const double len = lineLen[i].current();
            const double raw = kLfoOf[i] >= 0 ? lines[i].readCubic (len + lfo[kLfoOf[i]])
                             : gliding ? lines[i].readCubic (len)
                             : lines[i].read (static_cast<std::size_t> (len));
            const double y = loopAp[i].tick (raw, loopG);
            const double g = gainDc[i].current(), pl = pole[i].current();
            damp[i] = insert::flushDenormal (g * (1.0 - pl) * y + pl * damp[i]);
            z[i] = damp[i];
            outL += kOutL[i] * y;
            outR += kOutR[i] * y;
        }
        hadamard16 (z);
        for (std::size_t i = 0; i < kNumLines; ++i)
            lines[i].push (clamp (z[i] + kInSign[i] * inGain * d[i & 1u], -kLoopGuard, kLoopGuard));

        const double em = earlyMix.next(), lm = lateMix.next();
        const double wetL = em * e[0] + lm * outGain * outL;
        const double wetR = em * e[1] + lm * outGain * outR;
        const double dry = dryGain.next(), wet = wetGain.next();
        left[n] = static_cast<float> (insert::OnOffFade::mix (inL, dry * inL + wet * wetL, fw));
        right[n] = static_cast<float> (insert::OnOffFade::mix (inR, dry * inR + wet * wetR, fw));
        if (fading && fade.idle()) break;   // the rest of the block is already dry
    }

    if (fade.idle()) needsClear = true;
}
}
