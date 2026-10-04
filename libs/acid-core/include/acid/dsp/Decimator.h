#pragma once
// Decimator for the voice's oversampled loop (docs/research/04-dsp-modeling.md section 5).
//
// Cascaded polyphase IIR half-band stages, each two parallel chains of allpass sections
// (Valenzuela and Constantinides). The coefficients are designed in prepare() from each
// stage's transition width with the closed-form elliptic half-band design; nothing is
// tabulated for a particular rate.
//
// Total factors 1, 2, 4 and 8 (0, 1, 2 or 3 stages). The pass band is 0 to 20 kHz (or to
// 0.4535 of the base rate when that is lower). Each stage's stop band is the mirror of its
// pass band, which is exactly the band that would fold onto 0 to 20 kHz. A stage whose
// transition is wide (0.2 of its input rate or more: every stage except the last one at
// 44.1 and 48 kHz) uses 3 coefficients, a narrow one 8; the Ultra quality uses 4 and 10.
//
// Stop-band rejection per stage, as designed and as measured from the impulse response:
//
//   base 48 kHz     8x->4x 146 dB    4x->2x 102 dB    2x->1x 128 dB    (Ultra 189 / 133 / 159)
//   base 44.1 kHz   8x->4x 140 dB    4x->2x  96 dB    2x->1x 104 dB    (Ultra 182 / 125 / 130)
//
// The weakest stage sets the figure for the whole cascade: 102 dB at 48 kHz and 96 dB at
// 44.1 kHz for factors 4 and 8 (Ultra 133 and 125 dB).
//
// Pass-band ripple is the power complement of the stop band (about 1e-9 dB at worst) and
// the DC gain is exactly 1. The phase is not linear: the delay is about 2.6 base-rate
// samples at low frequencies (factor 4 at 48 kHz) and grows towards 20 kHz. latency()
// reports the low-frequency figure.
//
// The input may carry any DC level, but the filter state starts at zero, so the first few
// output samples after prepare() or reset() are the step response to whatever level the
// input starts at. Feed it the voice's output (which rests at zero), not raw oscillator
// volts.
//
// Threading: one instance, one thread. Nothing here allocates, locks or throws, prepare()
// included, so the factor or quality may be changed from the audio thread (the states are
// cleared when it does).
#include <array>
#include <cstddef>

namespace acid::dsp
{
class Decimator
{
public:
    enum class Quality { Standard, Ultra };

    static constexpr int kMaxStages = 3;
    static constexpr int kMaxCoefficients = 12;
    static constexpr double kPassbandHz = 20000.0;
    static constexpr double kPassbandOverBaseRate = 0.4535;     // 20 kHz at 44.1 kHz
    static constexpr double kWideTransition = 0.2;

    /** baseSampleRate is the output rate; the input rate is factor times that. factor is
        1, 2, 4 or 8 (anything else is rounded down to one of those). Designs the stages and
        clears the state. */
    void prepare (double baseSampleRate, int factor, Quality quality = Quality::Standard) noexcept;

    /** Clears the filter state. */
    void reset() noexcept;

    int factor() const noexcept { return totalFactor; }

    /** Consumes factor() input samples from `in`, returns one base-rate sample. */
    double processSample (const double* in) noexcept;

    /** Block form: reads numOut * factor() samples from `in`, writes numOut samples to
        `out`. `in` and `out` must not overlap unless factor() is 1 and the types match. */
    void process (const double* in, double* out, int numOut) noexcept;
    void process (const double* in, float* out, int numOut) noexcept;
    void process (const float* in, float* out, int numOut) noexcept;

    /** Group delay at low frequency, in base-rate samples, measured from the last input
        sample of each group of factor() to the output sample it produces. 0 for factor 1.
        At 48 kHz: 2.08, 2.62 and 2.90 for factors 2, 4 and 8 (Ultra 2.57, 3.26, 3.62).
        At 44.1 kHz: 1.84, 2.38 and 2.65 (Ultra 2.27, 2.96, 3.32). */
    double latency() const noexcept { return latencyBase; }

    /** latency() rounded to the nearest whole sample: what to report to a host. */
    int latencySamples() const noexcept { return static_cast<int> (latencyBase + 0.5); }

    // Design details, for reports and tests. Stage 0 runs at the highest rate.
    int numStages() const noexcept { return stageCount; }
    int numCoefficients (int stage) const noexcept { return stages[static_cast<std::size_t> (stage)].numCoefs; }
    double transitionWidth (int stage) const noexcept { return stages[static_cast<std::size_t> (stage)].transition; }
    double designedRejectionDb (int stage) const noexcept { return stages[static_cast<std::size_t> (stage)].rejectionDb; }
    double coefficient (int stage, int index) const noexcept
    {
        return stages[static_cast<std::size_t> (stage)].coef[static_cast<std::size_t> (index)];
    }

    /** Elliptic half-band design: fills coefs[0..numCoefs) for a transition band of
        `transition` (as a fraction of the stage's input rate, centred on a quarter of it;
        0 < transition < 0.5). The even-indexed coefficients belong to the branch fed with
        the later sample of each input pair, the odd-indexed ones to the other. Returns the
        stop-band rejection in dB. */
    static double designHalfBand (double* coefs, int numCoefs, double transition) noexcept;

private:
    struct Stage
    {
        int numCoefs = 0;
        double transition = 0.0;
        double rejectionDb = 0.0;
        std::array<double, kMaxCoefficients> coef {};
        std::array<double, kMaxCoefficients> x {};      // each section's previous input
        std::array<double, kMaxCoefficients> y {};      // each section's previous output

        double process (double earlier, double later) noexcept
        {
            // Two allpass chains at the stage's output rate; section i is
            // y[n] = c * (x[n] - y[n-1]) + x[n-1].
            double p0 = later, p1 = earlier;
            int i = 0;
            for (; i + 1 < numCoefs; i += 2)
            {
                const auto a = static_cast<std::size_t> (i), b = a + 1;
                const double y0 = coef[a] * (p0 - y[a]) + x[a];
                const double y1 = coef[b] * (p1 - y[b]) + x[b];
                x[a] = p0; y[a] = y0; p0 = y0;
                x[b] = p1; y[b] = y1; p1 = y1;
            }
            if (i < numCoefs)
            {
                const auto a = static_cast<std::size_t> (i);
                const double y0 = coef[a] * (p0 - y[a]) + x[a];
                x[a] = p0; y[a] = y0; p0 = y0;
            }
            return 0.5 * (p0 + p1);
        }
    };

    template <typename In, typename Out>
    void processBlock (const In* in, Out* out, int numOut) noexcept;

    std::array<Stage, kMaxStages> stages {};
    int stageCount = 0;
    int totalFactor = 1;
    double latencyBase = 0.0;
};
}
