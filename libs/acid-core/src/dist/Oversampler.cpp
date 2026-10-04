#include "acid/fx/Oversampler.h"
#include "acid/common/Math.h"
#include <cmath>

namespace acid::fx
{
namespace
{
constexpr double kStopbandDb = 90.0;

/** Modified Bessel function of the first kind, order zero (power series). */
double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 64; ++k)
    {
        term *= q / (static_cast<double> (k) * static_cast<double> (k));
        sum += term;
        if (term < 1.0e-18 * sum) break;
    }
    return sum;
}

/** Dot product with four independent accumulators: a fixed summation order (so the result
    is reproducible) that still lets the CPU overlap the additions. */
inline double dot (const double* a, const double* b, int n) noexcept
{
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    int i = 0;
    for (; i + 3 < n; i += 4)
    {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    for (; i < n; ++i)
        s0 += a[i] * b[i];
    return (s0 + s1) + (s2 + s3);
}
}

//==============================================================================
void Oversampler::Stage::design (double transition, double attenuationDb) noexcept
{
    // Kaiser estimate for the length of a windowed-sinc half-band filter whose
    // transition band is `transition` (a fraction of the stage's output rate).
    const double width = clamp (transition, 0.01, 0.45);
    const double estimate = (attenuationDb - 7.95) / (14.36 * width) + 1.0;

    // The filter has 2M + 1 taps with M odd, so the end taps are not the zero ones.
    int m = static_cast<int> (std::ceil ((estimate - 1.0) * 0.5));
    if ((m & 1) == 0) ++m;
    m = clamp (m, 3, kMaxPhaseTaps - 1);
    taps = m + 1;

    const double beta = 0.1102 * (attenuationDb - 8.7);
    const double i0Beta = besselI0 (beta);

    // Odd offsets -M, -M + 2, ..., M. h (o) = 0.5 sinc (o / 2) w (o); the even offsets are
    // zero except the centre, which is exactly 0.5 and is applied as a pure delay.
    double sum = 0.0;
    for (int k = 0; k < taps; ++k)
    {
        const int offset = 2 * k - m;
        const double x = 0.5 * kPi * static_cast<double> (offset);
        const double ratio = static_cast<double> (offset) / static_cast<double> (m);
        const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - ratio * ratio))) / i0Beta;
        const double h = 0.5 * std::sin (x) / x * window;
        coef[static_cast<std::size_t> (k)] = 2.0 * h;
        sum += 2.0 * h;
    }

    // Unity gain at DC on both phases.
    for (int k = 0; k < taps; ++k)
        coef[static_cast<std::size_t> (k)] /= sum;
    for (int k = taps; k < kMaxPhaseTaps; ++k)
        coef[static_cast<std::size_t> (k)] = 0.0;
}

void Oversampler::Stage::clear() noexcept
{
    upHist.fill (0.0);
    downHist.fill (0.0);
    downCentre.fill (0.0);
    upPos = downPos = centrePos = 0;
}

void Oversampler::Stage::up (const double* in, double* out, int n) noexcept
{
    const int centre = taps / 2 - 1;   // (M - 1) / 2
    for (int i = 0; i < n; ++i)
    {
        upPos = (upPos == 0 ? taps : upPos) - 1;
        upHist[static_cast<std::size_t> (upPos)] = in[i];
        upHist[static_cast<std::size_t> (upPos + taps)] = in[i];
        const double* h = upHist.data() + upPos;   // h[k] is the input k samples ago

        out[2 * i] = dot (coef.data(), h, taps);   // the half-sample point
        out[2 * i + 1] = h[centre];                // the input itself, delayed to line up
    }
}

void Oversampler::Stage::down (const double* in, double* out, int n) noexcept
{
    const int centreDelay = taps / 2;   // (M + 1) / 2 pairs
    for (int i = 0; i < n; ++i)
    {
        downPos = (downPos == 0 ? taps : downPos) - 1;
        downHist[static_cast<std::size_t> (downPos)] = in[2 * i];
        downHist[static_cast<std::size_t> (downPos + taps)] = in[2 * i];

        const double centre = downCentre[static_cast<std::size_t> (centrePos)];
        downCentre[static_cast<std::size_t> (centrePos)] = in[2 * i + 1];
        if (++centrePos >= centreDelay) centrePos = 0;

        // coef holds the taps times two, hence the 0.5 on the first term.
        out[i] = 0.5 * (dot (coef.data(), downHist.data() + downPos, taps) + centre);
    }
}

//==============================================================================
int Oversampler::stagesForFactor (int factor) noexcept
{
    return factor >= 8 ? 3 : (factor >= 4 ? 2 : (factor >= 2 ? 1 : 0));
}

void Oversampler::prepare (double sampleRate) noexcept
{
    const double fs = std::max (sampleRate, 1000.0);
    const double passEdge = std::min (20000.0, 0.45 * fs);

    for (int s = 0; s < kNumStages; ++s)
    {
        // Stage s runs at fs * 2^(s + 1). Stage 0 must pass the audio band. Later stages
        // also cover stage 0's transition band (up to fs - passEdge) where that is cheap,
        // and at high base rates stop a quarter above the audio band instead: what they
        // then let through lies above 20 kHz and stage 0 attenuates it again.
        const double outRate = fs * static_cast<double> (2 << s);
        const double edge = (s == 0) ? passEdge : std::min (fs - passEdge, 1.25 * passEdge);
        const double transition = (0.5 * outRate - 2.0 * edge) / outRate;
        stages[static_cast<std::size_t> (s)].design (transition, kStopbandDb);
    }

    // Each stage delays by M samples of its input rate on the way up plus down. The sum is
    // not a whole number of base-rate samples at 4x and 8x, so a short high-rate delay pads
    // it to the next integer.
    for (int n = 0; n <= kNumStages; ++n)
    {
        const int factor = 1 << n;
        int total = 0;   // in high-rate samples
        for (int s = 0; s < n; ++s)
            total += (stages[static_cast<std::size_t> (s)].taps - 1) * (factor >> s);
        const int pad = (factor - total % factor) % factor;
        padSamples[static_cast<std::size_t> (n)] = pad;
        latency[static_cast<std::size_t> (n)] = (total + pad) / factor;
    }

    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& s : stages)
        s.clear();
    padLine.fill (0.0);
    padPos = 0;
}

void Oversampler::setFactor (int factor) noexcept
{
    numActive = stagesForFactor (factor);
    reset();
}

int Oversampler::latencyForFactor (int factor) const noexcept
{
    return latency[static_cast<std::size_t> (stagesForFactor (factor))];
}

void Oversampler::upsample (const double* in, double* out, int numSamples) noexcept
{
    if (numActive == 0)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = in[i];
        return;
    }

    double scratch[2][kMaxBlock * kMaxFactor / 2];
    const double* src = in;
    int len = numSamples;
    for (int s = 0; s < numActive; ++s)
    {
        double* dst = (s == numActive - 1) ? out : scratch[s & 1];
        stages[static_cast<std::size_t> (s)].up (src, dst, len);
        src = dst;
        len *= 2;
    }

    const int pad = padSamples[static_cast<std::size_t> (numActive)];
    if (pad > 0)
    {
        for (int i = 0; i < len; ++i)
        {
            const double delayed = padLine[static_cast<std::size_t> (padPos)];
            padLine[static_cast<std::size_t> (padPos)] = out[i];
            if (++padPos >= pad) padPos = 0;
            out[i] = delayed;
        }
    }
}

void Oversampler::downsample (const double* in, double* out, int numSamples) noexcept
{
    if (numActive == 0)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = in[i];
        return;
    }

    double scratch[2][kMaxBlock * kMaxFactor / 2];
    const double* src = in;
    int len = numSamples << numActive;
    for (int s = numActive - 1; s >= 0; --s)
    {
        len /= 2;
        double* dst = (s == 0) ? out : scratch[s & 1];
        stages[static_cast<std::size_t> (s)].down (src, dst, len);
        src = dst;
    }
}
}
