#pragma once
// Nonlinear zero-delay-feedback diode ladder (docs/research/04-dsp-modeling.md, section 1.7).
//
// Four capacitor states (18 nF at the bottom against 33 nF), five tanh junction pairs, the
// output differential amplifier (Q21) as a resistive load on the top capacitor with its
// coupling capacitor state, and the four-section feedback network, all inside one implicit
// trapezoidal solve per sample (Newton on a 4x4 system). Double precision throughout.
//
// Units, in the normalised form of the research document:
//   input x     differential voltage at the input pair divided by 2 V_T (51.4 mV). 1.0 is
//               51.4 mV at the input pair, which is 5.19 V on the oscillator side of the
//               220 k / 2.2 k input divider. A stock saw peaks near 0.53, the square near 0.33.
//   fRes        the RESONANT frequency in Hz (where the loop phase reaches 180 degrees and
//               the peak sits at high resonance), proportional to the ladder tail current,
//               64.9 Hz per microamp. It is never the -3 dB point, which is 3.25 octaves lower.
//   r           resonance wiper fraction. Loop gain K = kMax * r. 0..1 is the stock pot,
//               up to 2 is the high-resonance mod.
//   output w    the output amplifier's normalised output, tanh (vb). Volts at the output
//               follower (the service test point) = 106 * 2 V_T * w = 5.45 V * w.
//
// Threading: one instance, one thread. prepare() and the constructor are not real-time
// safe in the strict sense only because the constructor touches a shared table once;
// everything else (setTrims, reset, process) is allocation-free, lock-free and noexcept.
// The caller should run with flush-to-zero and denormals-are-zero set, as for any IIR
// code; the filter also snaps its own states to exact zero below 1e-60 so that silence
// costs the same as signal either way.
#include "acid/common/Math.h"
#include "acid/dsp/DiodeLadderMath.h"

#include <cstdint>

namespace acid::dsp
{
/** Physical trims of the ladder. Defaults are the parameter table's (cal_rb, cal_kmax):
    ladder load 20 k, loop gain 18.7, which is acceptanceReference(). With those the
    full-resonance peak stands 7.1, 13.5 and 21.5 dB above the passband at 250 Hz, 500 Hz
    and 1 kHz (document 02 target F12: 5 to 10, 11 to 18, 19 to 31), 21.2 dB at 975 Hz (F9:
    19 +- 3), the zero-resonance level is within 0.8 dB of the unloaded block model (F8
    allows 1.5), self-oscillation starts at 2.7 kHz (F13: none below 1.2 kHz) and the
    sub-audio guard is idle (F11b). The build spec's section 7 table still lists 13.6 k, the
    loaded model of document 04 section 1.4 with a transistor gain of 160; at that setting
    three of F12's points and both of F8's fall 0.5 to 1.9 dB outside their bands (the
    review tests in tests/filter pin both calibrations). */
struct DiodeLadderTrims
{
    /** Q21 input resistance per side, ohms. Loads the top capacitor and, with the 0.1 uF
        couplers, forms the high-pass inside the loop. Calibration range 10 k to 35 k
        (13.6 k is a transistor gain of 160, 20 k about 255, 28 k is 400). Sets how fast
        resonance fades at low cutoff and where full resonance starts to self-oscillate.
        Values outside the calibration range are accepted (1 k to 1 G) for the ideal mode
        and for analysis. */
    double rbOhms = 20.0e3;

    /** Loop gain at r = 1. Calibration range 17 to 20; accepted range 0 to 40. */
    double kMax = 18.7;

    /** Ladder capacitor over bottom capacitor, C / C18. 33 nF / 18 nF as built; 2 is the
        textbook half-value capacitor, 1 is equal capacitors. Accepted range 0.5 to 8.
        The resonant frequency stays at fRes whatever the ratio. */
    double capRatio = 33.0 / 18.0;

    /** Scales the resistive load of Q21 on the top capacitor. 1 is the circuit. 0 buffers
        the ladder from the output amplifier (the coupler then is a fixed high-pass at
        1 / (2 pi rbOhms 0.1 uF), as in the published block model). */
    double loadScale = 1.0;

    /** DC offset added to x, in units of 2 V_T: models input-pair and ladder mismatch and
        is the only source of even harmonics in the filter. 0.02 is about 1 mV. */
    double inputOffset = 0.0;

    /** True: feedback passes through the four sub-20 Hz sections (3.18, 1.18, 6.164 Hz
        high-passes and the 7.408 / 15.517 Hz shelf). False: feedback is taken straight
        from the output amplifier (unity network). */
    bool subAudioNetwork = true;

    /** True (always, in a shipped calibration): when kMax * r is large enough for the
        sub-audio loop gain to exceed 0.85, the feedback network is blended towards unity
        by just enough to hold it there, so the loop cannot motorboat at 5 to 7 Hz. Inactive
        (the model is exactly the stock circuit) below that. False is for model verification. */
    bool subAudioGuard = true;

    /** The calibration that meets every small-signal acceptance target of research
        document 02 at once (F8, F9, F11b, F12, F13): ladder load 20 k (a transistor gain of
        about 255), loop gain 18.7. It is also the default; kept as a named function so the
        tests say which calibration they mean. */
    static DiodeLadderTrims acceptanceReference() noexcept;

    /** The comparison target for other emulations: no load, half-value bottom capacitor,
        no sub-audio network, and one high-pass at couplerHz inside the loop. */
    static DiodeLadderTrims idealLadder (double kMaxValue = 17.0, double couplerHz = 1.0) noexcept;
};

/** Solver health counters since the last resetStats(). */
struct DiodeLadderStats
{
    std::uint64_t samples = 0;
    std::uint64_t iterations = 0;        // Newton iterations, summed
    std::uint64_t nonConverged = 0;      // solves that hit the iteration cap above tolerance
    std::uint64_t stateResets = 0;       // non-finite guard trips (target: zero, always)
    std::uint32_t maxIterations = 0;     // worst single solve

    double meanIterations() const noexcept
    {
        return samples == 0 ? 0.0 : static_cast<double> (iterations) / static_cast<double> (samples);
    }
};

class DiodeLadder
{
public:
    static constexpr double kTwoVt = 0.0514;                  // 2 V_T in volts at 25 C
    static constexpr double kInputDivider = 2.2 / 222.2;      // R70 / (R62 + R70)
    static constexpr double kOutputAmpGain = 106.0;           // Q21 differential gain
    static constexpr double kHzPerMicroamp = 64.9;            // fRes per uA of tail current
    static constexpr double kCouplerFarads = 100.0e-9;        // C25, C27
    static constexpr double kLadderFarads = 33.0e-9;          // C19, C24, C26
    static constexpr double kMinFresHz = 10.0;
    static constexpr double kMaxFresOverFs = 0.45;
    static constexpr double kMaxResonance = 2.0;
    static constexpr double kMaxInput = 100.0;                // |x| clamp, about 5 V at the pair
    static constexpr double kGuardLoopGain = 0.85;            // sub-audio loop gain ceiling
    static constexpr int kDefaultMaxIterations = 8;
    static constexpr double kDefaultTolerance = 1.0e-7;

    DiodeLadder() noexcept;

    /** Derives every coefficient from the rate the filter will be called at (the voice's
        oversampled rate) and resets. Rates from 8 kHz up are accepted. */
    void prepare (double sampleRate) noexcept;

    /** All nine states to rest (zero). Counters are kept. */
    void reset() noexcept;

    /** Real-time safe. Cheap when nothing changed; about 20 microseconds when rbOhms
        changed (it re-derives the sub-audio guard). May be called before prepare(). */
    void setTrims (const DiodeLadderTrims& newTrims) noexcept;
    void setTrims (double rbOhms, double kMax) noexcept;
    const DiodeLadderTrims& getTrims() const noexcept { return trims; }

    /** Newton iteration cap, 1 to 8 (default 8). 1 is the single-step economy solver;
        with a cap below 8 the nonConverged counter counts cap hits and is not a fault. */
    void setMaxIterations (int cap) noexcept;

    /** Newton stops when the largest state correction is below this (default 1e-7). */
    void setTolerance (double stepTolerance) noexcept;

    /** One sample. x, fResHz and r as described at the top of this file.
        fResHz is clamped to [10 Hz, 0.45 fs], r to [0, 2], x to +-100; NaN becomes
        10 Hz, 0 and 0 respectively, infinities clamp. May change every sample with no
        extra handling: the coefficient scales the integrator input, as the tail current does.
        Returns w. */
    inline double process (double x, double fResHz, double r) noexcept;

    /** w of the last sample: the output amplifier (test point) node. */
    double output() const noexcept { return lastOut; }

    /** The node the amplifier input mix is fed from: w after the 1 uF output coupler into
        the 50 k resonance pot (high-pass 3.18 Hz), i.e. the top of the pot. The direct
        path takes this through 220 k; the resonance-wiper path takes
        wiperFraction * potTop() through 100 k. Same scale as w. */
    double potTop() const noexcept { return lastPotTop; }

    /** Fraction by which the guard is bypassing the sub-audio network at resonance r
        with the current trims: 0 is the stock circuit, 1 is a unity network. */
    double subAudioBypass (double r) const noexcept;

    /** Lossless resonant frequency over the integrator rate f0, for a capacitor ratio:
        sqrt ((4 + 6 rho) / (6 + rho)). 1.3838 as built, sqrt (2) for rho = 2. */
    static double resonanceRatio (double capRatio) noexcept;

    /** Loop gain at which the unloaded core self-oscillates: 17.019 as built, 17 for rho = 2. */
    static double criticalGain (double capRatio) noexcept;

    /** Electrical wiper fraction of the linear 50 k resonance pot at a rotation of 0..1,
        with the wiper loaded by loadOhms (about 102 k at low audio frequencies, about 51 k
        around the resonant peak): rotation / (1 + rotation (1 - rotation) 50 k / load). */
    static double wiperFraction (double rotation, double loadOhms = 102.0e3) noexcept;

    /** Volts at the input pair to x, and oscillator-side volts to x. */
    static constexpr double inputFromPairVolts (double volts) noexcept { return volts / kTwoVt; }
    static constexpr double inputFromOscVolts (double volts) noexcept { return volts * kInputDivider / kTwoVt; }
    /** w to volts at the output follower. */
    static constexpr double outputVolts (double w) noexcept { return w * kOutputAmpGain * kTwoVt; }

    double getSampleRate() const noexcept { return sampleRate; }
    double maxFresHz() const noexcept { return fResMax; }

    const DiodeLadderStats& stats() const noexcept { return counters; }
    void resetStats() noexcept { counters = {}; }

private:
    void updateCoefficients() noexcept;
    void updateGuard() noexcept;
    double recoverFromBadState() noexcept;

    static constexpr double kRestLevel = 1.0e-60;
    static constexpr double kLimiterGate = 0.05;        // steps below this are never limited
    static constexpr double kExtrapolationLimit = 0.3;  // start-point error above which to hold
    static double snap (double v) noexcept { return std::abs (v) < kRestLevel ? 0.0 : v; }

    const ladder::TanhTable* table = nullptr;

    DiodeLadderTrims trims;
    double sampleRate = 192000.0;
    int maxIterations = kDefaultMaxIterations;
    double tolerance = kDefaultTolerance;

    // Rate- and trim-derived coefficients.
    double piOverFs = kPi / 192000.0;
    double fResMax = kMaxFresOverFs * 192000.0;
    double rho = 33.0 / 18.0;
    double invCRes = 1.0;
    double kMaxGain = 18.7;
    double inputOffset = 0.0;
    double gq = 0.0, aq = 1.0, twoGq = 0.0;     // coupler: vb = aq * y4 - aq * sq
    double gL = 0.0, gLaq = 0.0;                // load on the top capacitor
    double fbG[4] {};                           // G = g / (1 + g) per feedback section
    double fbC[3] {};                           // 1 - G for the three high-passes
    double shelfC1 = 1.0, shelfC2 = 0.0;        // shelf: out = c1 * in - c2 * z
    double fbAlpha = 1.0;                       // instantaneous gain of the whole network
    double fbQ[4] {}, fbQw = 0.0;               // next sample's be from this sample's states and w
    double guardK0 = 1.0e300;                   // K above which the guard engages
    double guardScale = 0.0;                    // bypass = guardScale * (1 - guardK0 / K)
    double guardWq = -1.0;                      // coupler rate the guard was derived for
    double guardG0 = 0.0, guardSlope = 0.0;     // crossing gain and its relative slope

    // States.
    double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;      // ladder integrators (trapezoidal)
    double sq = 0.0;                                    // coupling capacitors
    double zf[4] {};                                    // feedback network
    double y1 = 0.0, y2 = 0.0, y3 = 0.0, y4 = 0.0;      // last solution
    double p1 = 0.0, p2 = 0.0, p3 = 0.0, p4 = 0.0;      // the one before, for the predictor

    double fbBeta = 0.0;                                // be for the coming sample
    double lastOut = 0.0, prevOut = 0.0, lastPotTop = 0.0;
    double startError = 0.0;                            // first Newton step of the last solve
    DiodeLadderStats counters;
};

//==============================================================================
inline double DiodeLadder::process (double x, double fResHz, double r) noexcept
{
    const ladder::TanhTable& tab = *table;

    // 0. Sanitise. Comparisons are written so that NaN falls into the safe branch.
    if (! (std::abs (x) <= kMaxInput)) x = (x != x) ? 0.0 : std::copysign (kMaxInput, x);
    if (! (fResHz >= kMinFresHz)) fResHz = kMinFresHz;
    if (fResHz > fResMax) fResHz = fResMax;
    if (! (r >= 0.0)) r = 0.0;
    if (r > kMaxResonance) r = kMaxResonance;
    x += inputOffset;

    // 1. Coefficients. Prewarped at the resonant frequency, so the peak is tuned exactly.
    const double g = ladder::fastTan (fResHz * piOverFs) * invCRes;
    const double g1 = rho * g;
    const double K = kMaxGain * r;

    // 2. Affine maps of the linear parts: vb = aq * Y4 + bq, e = al * w + be.
    // be was folded from the feedback states at the end of the previous sample.
    const double bq = -sq * aq;
    double al = fbAlpha;
    double be = fbBeta;
    if (K > guardK0)
    {
        const double bypass = guardScale * (1.0 - guardK0 / K);
        al = bypass + (1.0 - bypass) * al;
        be = (1.0 - bypass) * be;
    }
    const double kAl = K * al;
    const double xe = x - K * be;                       // u = xe - kAl * w
    const double cw = g1 * kAl;

    // The part of the next sample's be that needs only the old feedback states.
    const double beBase = (fbQ[0] * zf[0] + fbQ[1] * zf[1]) + (fbQ[2] * zf[2] + fbQ[3] * zf[3]);

    // 3. Newton. The unknowns are y1..y4; the amplifier output w rides along as a fifth,
    // eliminated analytically, so that tanh (u) does not have to wait for tanh (vb). The
    // linear solve stays 4x4 and the fixed point is the same.
    //
    // Start point: a linear extrapolation of the last two solutions, which is within
    // about 1e-3 for any signal that is smooth at this rate. When the previous sample's
    // extrapolation missed badly (the filter is self-oscillating within a few samples per
    // cycle, or the input just jumped), extrapolating would throw the start deep into
    // saturation, so start from the previous solution instead.
    double Y1 = y1, Y2 = y2, Y3 = y3, Y4 = y4, W = lastOut;
    if (startError < kExtrapolationLimit)
    {
        Y1 += y1 - p1; Y2 += y2 - p2; Y3 += y3 - p3; Y4 += y4 - p4;
        W = clamp (W + (lastOut - prevOut), -1.0, 1.0);
    }
    double vb, w, t0, t1, t2, t3, t4;
    int iterations = 0;
    bool converged = false;

    for (;;)
    {
        vb = aq * Y4 + bq;
        w = ladder::fastTanh (tab, vb);                 // amplifier output at this iterate
        const double u = xe - kAl * W;
        t0 = ladder::fastTanh (tab, u);
        t1 = ladder::fastTanh (tab, Y1 - Y2);
        t2 = ladder::fastTanh (tab, Y2 - Y3);
        t3 = ladder::fastTanh (tab, Y3 - Y4);
        t4 = ladder::fastTanh (tab, Y4);

        const double d1 = 1.0 - t1 * t1, d2 = 1.0 - t2 * t2, d3 = 1.0 - t3 * t3, d4 = 1.0 - t4 * t4;
        const double gd1 = g * d1, gd2 = g * d2, gd3 = g * d3, gd4 = g * d4;

        // Negated residuals of the trapezoidal equations (row 1 follows below).
        const double r2 = s2 + g * (t1 - t2) - Y2;
        const double r3 = s3 + g * (t2 - t3) - Y3;
        const double r4 = s4 + g * (t3 - t4) - gL * vb - Y4;

        // Jacobian: tridiagonal plus the corner term that closes the feedback loop.
        //   [  a    b    .    c  ]    a = 1 + rho gd1, b = -rho gd1, c = g1 d0 K al dw aq
        //   [-gd1   e  -gd2   .  ]    e = 1 + gd1 + gd2
        //   [  .  -gd2   h  -gd3 ]    h = 1 + gd2 + gd3
        //   [  .    .  -gd3   k  ]    k = 1 + gd3 + gd4 + gL aq
        // Elimination runs upward from row 4 as in the reference algorithm, but
        // fraction-free, so every reciprocal is independent of the others and all of this
        // block is independent of t0 and w.
        const double b = -rho * gd1;
        const double a = 1.0 - b;
        const double e = 1.0 + gd1 + gd2;
        const double h = 1.0 + gd2 + gd3;
        const double k = 1.0 + gd3 + gd4 + gLaq;
        const double A3 = h * k - gd3 * gd3;            // row 3 pivot times k
        const double B3 = r3 * k + gd3 * r4;
        const double G3 = -gd2 * k;
        const double A2 = e * A3 + gd2 * G3;            // row 2 pivot times A3
        const double B2 = r2 * A3 + gd2 * B3;
        const double G2 = -gd1 * A3;
        const double kA3 = k * A3;
        const double jG3 = -gd3 * G3;
        const double invA2 = 1.0 / A2, invA3 = 1.0 / A3, invK = 1.0 / k;
        const double X = kA3 * A2;
        const double P = a * X - b * kA3 * G2;
        const double Q = jG3 * G2;
        const double N0 = -b * kA3 * B2;
        const double N1 = A2 * (A3 * r4 + gd3 * B3) + jG3 * B2;

        // The part that waits for t0 and w.
        const double dw = 1.0 - w * w;
        const double d0 = 1.0 - t0 * t0;
        const double wGap = w - W;
        const double cd = cw * d0;
        const double r1 = (s1 - Y1) + g1 * (t0 - t1) - cd * wGap;
        const double c = cd * dw * aq;
        const double den = P - c * Q;
        const double num = r1 * X + N0 - c * N1;
        const double invDen = 1.0 / den;
        const double n2 = (B2 * den - G2 * num) * invA2;
        const double n3 = (B3 * den - G3 * n2) * invA3;
        const double n4 = (r4 * den + gd3 * n3) * invK;
        double D1 = num * invDen, D2 = n2 * invDen, D3 = n3 * invDen, D4 = n4 * invDen;

        double m = std::max (std::max (std::abs (D1), std::abs (D2)),
                             std::max (std::abs (D3), std::abs (D4)));
        double newW = w + dw * aq * D4;

        if (m > kLimiterGate)
        {
            // Step limiter. Two rules, both scaling the whole step:
            //  - at most 2 units (about 100 mV) per iteration on any state;
            //  - the input pair may not jump from one saturated side to the other. With a
            //    loop gain of 20 to 40 a small move of y4 can throw u across the whole
            //    active region, where the Jacobian no longer sees the feedback, and plain
            //    Newton then flips back and forth. Such a step is cut to land on u = 0,
            //    the middle of the active region, from where it converges.
            double scale = m > 2.0 ? 2.0 / m : 1.0;
            const double uNext = u - scale * kAl * (newW - W);
            if ((u > 1.0 && uNext < -1.0) || (u < -1.0 && uNext > 1.0))
                scale *= u / (u - uNext);
            D1 *= scale; D2 *= scale; D3 *= scale; D4 *= scale;
            newW = W + scale * (newW - W);
        }

        Y1 += D1; Y2 += D2; Y3 += D3; Y4 += D4;
        const double dvb = aq * D4;
        newW = clamp (newW, -1.0, 1.0);
        m = std::max (m, std::abs (wGap));
        if (iterations == 0) startError = m;
        ++iterations;

        const bool small = m < tolerance;
        if (small || (iterations >= maxIterations && m < 0.01))
        {
            // 4a. Converged (or out of iterations close to the solution): bring the
            // junction values to the new iterate to first order. The neglected term is
            // of order m squared: below 1e-14 when converged.
            converged = small;
            vb += dvb;
            t0 -= d0 * kAl * (newW - W);
            w = newW;
            t1 += d1 * (D1 - D2);
            t2 += d2 * (D2 - D3);
            t3 += d3 * (D3 - D4);
            t4 += d4 * D4;
            break;
        }

        W = newW;

        if (iterations >= maxIterations || ! (m < 1.0e30))
        {
            // 4b. Out of iterations far from the solution, or a non-finite step:
            // evaluate everything at the last iterate.
            vb = aq * Y4 + bq;
            w = ladder::fastTanh (tab, vb);
            t0 = ladder::fastTanh (tab, xe - kAl * w);
            t1 = ladder::fastTanh (tab, Y1 - Y2);
            t2 = ladder::fastTanh (tab, Y2 - Y3);
            t3 = ladder::fastTanh (tab, Y3 - Y4);
            t4 = ladder::fastTanh (tab, Y4);
            break;
        }
    }

    ++counters.samples;
    counters.iterations += static_cast<std::uint64_t> (iterations);
    if (static_cast<std::uint32_t> (iterations) > counters.maxIterations)
        counters.maxIterations = static_cast<std::uint32_t> (iterations);
    if (! converged) ++counters.nonConverged;

    // Non-finite guard. The states are bounded by the physics (a few units), so anything
    // large or NaN here means the solve failed: return to rest and carry on.
    const double magnitude = std::abs (Y1) + std::abs (Y2) + std::abs (Y3) + std::abs (Y4);
    if (! (magnitude < 1.0e3))
        return recoverFromBadState();

    // 5. Commit (trapezoidal).
    fbBeta = beBase + fbQw * w;
    prevOut = lastOut;
    lastOut = w;

    if (magnitude > kRestLevel)
    {
        p1 = y1; p2 = y2; p3 = y3; p4 = y4;
        y1 = Y1; y2 = Y2; y3 = Y3; y4 = Y4;
        s1 = Y1 + g1 * (t0 - t1);
        s2 = Y2 + g * (t1 - t2);
        s3 = Y3 + g * (t2 - t3);
        s4 = Y4 + g * (t3 - t4) - gL * vb;
        sq += twoGq * vb;
    }
    else
    {
        // The ladder has decayed to nothing: park it at exact zero so that silence never
        // drifts into denormal arithmetic, whatever the caller's floating-point mode. The
        // coupling capacitors keep their charge until it has drained too: with the ladder
        // buffered from the amplifier (loadScale 0) they outlast the ladder states by far,
        // and clearing them early would cut the low end of the response short.
        p1 = p2 = p3 = p4 = 0.0;
        y1 = y2 = y3 = y4 = 0.0;
        s1 = s2 = s3 = s4 = 0.0;
        sq = snap (sq + twoGq * vb);
    }

    // Feedback network: three one-pole high-passes and the shelf, as TPT sections.
    double in = w;
    double v = (in - zf[0]) * fbG[0];
    double lp = v + zf[0];
    zf[0] = snap (lp + v);
    in -= lp;
    lastPotTop = in;                                    // after the 3.18 Hz output coupler
    v = (in - zf[1]) * fbG[1];
    lp = v + zf[1];
    zf[1] = snap (lp + v);
    in -= lp;
    v = (in - zf[2]) * fbG[2];
    lp = v + zf[2];
    zf[2] = snap (lp + v);
    in -= lp;
    v = (in - zf[3]) * fbG[3];
    zf[3] = snap ((v + zf[3]) + v);

    return w;
}
}
