#pragma once
// Reference models and measurement helpers for the diode ladder tests.
//
//   LinearModel     small-signal transfer functions of the continuous circuit equations
//                   (research doc 04, sections 1.1 and 1.3), and of their exact
//                   trapezoidal discretisation, in closed form
//   Rk4Ladder       the nonlinear continuous equations integrated with classic RK4
//   dftBin, fft     single-bin DFT and a radix-2 FFT, Kaiser window
//   AdditiveSaw     band-limited sawtooth, sampled on a fine grid
#include "acid/common/Math.h"
#include "acid/dsp/DiodeLadder.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace acid::test::filter
{
using Complex = std::complex<double>;

inline constexpr double kFbPoleHz[4] = { 3.18, 1.18, 6.164, 15.517 };
inline constexpr double kShelfZeroHz = 7.408;

//==============================================================================
/** Everything the linear model needs, in rad/s. */
struct LinearRates
{
    double w0 = 0.0;            // ladder integrator rate
    double wq = 0.0;            // coupler
    double wL = 0.0;            // load on the top capacitor
    double fbPole[4] {};        // feedback section poles
    double shelfZero = 0.0;
    double rho = 33.0 / 18.0;
    double K = 0.0;
    double bypass = 0.0;        // feedback network blended towards unity by this much
    bool network = true;
};

/** Continuous-time rates for a filter with these trims at fRes and r. */
inline LinearRates analogRates (const dsp::DiodeLadderTrims& t, double fRes, double r, double bypass = 0.0)
{
    LinearRates m;
    m.rho = t.capRatio;
    m.w0 = kTwoPi * fRes / dsp::DiodeLadder::resonanceRatio (t.capRatio);
    m.wq = 1.0 / (t.rbOhms * dsp::DiodeLadder::kCouplerFarads);
    m.wL = t.loadScale / (t.rbOhms * 2.0 * dsp::DiodeLadder::kLadderFarads);
    for (int i = 0; i < 4; ++i) m.fbPole[i] = kTwoPi * kFbPoleHz[i];
    m.shelfZero = kTwoPi * kShelfZeroHz;
    m.K = t.kMax * r;
    m.bypass = bypass;
    m.network = t.subAudioNetwork;
    return m;
}

/** The rates the trapezoidal discretisation realises at sample rate fs: every integrator
    coefficient g maps to an equivalent analogue rate 2 fs g. Evaluating the analogue
    transfer function with these rates at s = j 2 fs tan (pi f / fs) gives the digital
    filter's response exactly. */
inline LinearRates discreteRates (const dsp::DiodeLadderTrims& t, double fRes, double r, double fs, double bypass = 0.0)
{
    LinearRates m = analogRates (t, fRes, r, bypass);
    const double two = 2.0 * fs;
    m.w0 = two * std::tan (kPi * fRes / fs) / dsp::DiodeLadder::resonanceRatio (t.capRatio);
    m.wq = two * std::tan (m.wq / two);
    for (int i = 0; i < 4; ++i) m.fbPole[i] = two * std::tan (kPi * kFbPoleHz[i] / fs);
    m.shelfZero = m.fbPole[3] * kShelfZeroHz / kFbPoleHz[3];
    return m;
}

inline Complex warpedS (double hz, double fs) { return Complex (0.0, 2.0 * fs * std::tan (kPi * hz / fs)); }
inline Complex analogS (double hz) { return Complex (0.0, kTwoPi * hz); }

struct LinearResponse
{
    Complex out;        // w / x
    Complex potTop;     // (w after the 3.18 Hz coupler) / x
    Complex loopGain;   // K * open-loop gain; the loop is unstable where this is -1
};

/** Linearised model (every tanh replaced by its argument) evaluated at complex frequency s. */
inline LinearResponse linearResponse (const LinearRates& m, Complex s)
{
    // Work back from y4 = 1.
    const Complex vb = s / (s + m.wq);
    const Complex t = s / m.w0 + 2.0;
    const Complex y3 = t + (m.wL / m.w0) * vb;
    const Complex y2 = t * y3 - 1.0;
    const Complex y1 = t * y2 - y3;
    const Complex u = (s / (m.rho * m.w0) + 1.0) * y1 - y2;

    const Complex hp0 = s / (s + m.fbPole[0]);
    Complex net (1.0, 0.0);
    if (m.network)
    {
        net = hp0 * (s / (s + m.fbPole[1])) * (s / (s + m.fbPole[2])) * ((s + m.shelfZero) / (s + m.fbPole[3]));
        net = m.bypass + (1.0 - m.bypass) * net;
    }

    const Complex forward = vb / u;
    LinearResponse out;
    out.loopGain = m.K * forward * net;
    out.out = forward / (1.0 + out.loopGain);
    out.potTop = out.out * hp0;
    return out;
}

inline double magDb (Complex h) { return 20.0 * std::log10 (std::max (std::abs (h), 1.0e-300)); }
inline double phaseDeg (Complex h) { return std::arg (h) * 180.0 / kPi; }
inline double wrapDeg (double d) { while (d > 180.0) d -= 360.0; while (d < -180.0) d += 360.0; return d; }

//==============================================================================
/** Sum of x[n] exp (-j 2 pi hz n / fs): one DFT bin at any frequency. */
inline Complex dftBin (const std::vector<double>& x, double hz, double fs)
{
    const double w = kTwoPi * hz / fs;
    const Complex rot = std::polar (1.0, -w);
    Complex acc (0.0, 0.0), ph (1.0, 0.0);
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        if ((n & 1023u) == 0u) ph = std::polar (1.0, -w * static_cast<double> (n));
        acc += x[n] * ph;
        ph *= rot;
    }
    return acc;
}

/** In-place radix-2 FFT. Size must be a power of two. */
inline void fft (std::vector<Complex>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPi / static_cast<double> (len);
        const Complex wLen (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            Complex w (1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                if ((k & 255u) == 0u) w = std::polar (1.0, ang * static_cast<double> (k));
                const Complex u = a[i + k];
                const Complex v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wLen;
            }
        }
    }
}

/** Modified Bessel function of the first kind, order zero. */
inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 200; ++k)
    {
        term *= q / (static_cast<double> (k) * static_cast<double> (k));
        sum += term;
        if (term < sum * 1.0e-17) break;
    }
    return sum;
}

inline std::vector<double> kaiserWindow (std::size_t n, double beta)
{
    std::vector<double> w (n);
    const double norm = besselI0 (beta);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = 2.0 * static_cast<double> (i) / static_cast<double> (n - 1) - 1.0;
        w[i] = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / norm;
    }
    return w;
}

//==============================================================================
/** Impulse response of a filter instance in its linear regime (a 1e-6 impulse), both the
    output and the pot-top node. The filter is reset first. */
struct ImpulseResponse
{
    std::vector<double> out, potTop;
    double fs = 0.0;
    double amplitude = 1.0;

    Complex response (double hz) const { return dftBin (out, hz, fs) / amplitude; }
    Complex potTopResponse (double hz) const { return dftBin (potTop, hz, fs) / amplitude; }
    double db (double hz) const { return 20.0 * std::log10 (std::max (std::abs (response (hz)), 1.0e-300)); }
};

inline ImpulseResponse impulseResponse (dsp::DiodeLadder& f, double fRes, double r, double seconds,
                                        double amplitude = 1.0e-6)
{
    ImpulseResponse ir;
    ir.fs = f.getSampleRate();
    ir.amplitude = amplitude;
    const auto n = static_cast<std::size_t> (seconds * ir.fs);
    ir.out.resize (n);
    ir.potTop.resize (n);
    f.reset();
    for (std::size_t i = 0; i < n; ++i)
    {
        ir.out[i] = f.process (i == 0 ? amplitude : 0.0, fRes, r);
        ir.potTop[i] = f.potTop();
    }
    return ir;
}

struct Peak { double hz = 0.0, db = -1.0e9; };

/** Largest magnitude of the measured response between lo and hi: log scan, then a
    golden-section refinement around the best scan point. */
inline Peak findPeak (const ImpulseResponse& ir, double lo, double hi, int scanPoints = 120)
{
    Peak best;
    const double ratio = std::pow (hi / lo, 1.0 / static_cast<double> (scanPoints - 1));
    double f = lo;
    for (int i = 0; i < scanPoints; ++i, f *= ratio)
    {
        const double d = ir.db (f);
        if (d > best.db) best = { f, d };
    }
    double a = std::max (lo, best.hz / ratio), b = std::min (hi, best.hz * ratio);
    const double phi = 0.6180339887498949;
    double c = b - phi * (b - a), d = a + phi * (b - a);
    double fc = ir.db (c), fd = ir.db (d);
    for (int i = 0; i < 24; ++i)
    {
        if (fc > fd) { b = d; d = c; fd = fc; c = b - phi * (b - a); fc = ir.db (c); }
        else         { a = c; c = d; fc = fd; d = a + phi * (b - a); fd = ir.db (d); }
    }
    const double fBest = 0.5 * (a + b);
    const double dBest = ir.db (fBest);
    return dBest > best.db ? Peak { fBest, dBest } : best;
}

/** Smallest magnitude between lo and hi on a log scan. */
inline Peak findDip (const ImpulseResponse& ir, double lo, double hi, int scanPoints = 80)
{
    Peak best { lo, 1.0e9 };
    const double ratio = std::pow (hi / lo, 1.0 / static_cast<double> (scanPoints - 1));
    double f = lo;
    for (int i = 0; i < scanPoints; ++i, f *= ratio)
    {
        const double d = ir.db (f);
        if (d < best.db) best = { f, d };
    }
    return best;
}

/** Amplitude of the component at hz in y[start, start + length), Hann weighted. */
inline double hannAmplitude (const std::vector<double>& y, std::size_t start, std::size_t length, double hz, double fs)
{
    Complex acc (0.0, 0.0);
    double weightSum = 0.0;
    const double w = kTwoPi * hz / fs;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (kTwoPi * static_cast<double> (i) / static_cast<double> (length - 1));
        acc += y[start + i] * win * std::polar (1.0, -w * static_cast<double> (i));
        weightSum += win;
    }
    return 2.0 * std::abs (acc) / weightSum;
}

/** Runs a whole input buffer through the filter at fixed fRes and r. */
inline std::vector<double> render (dsp::DiodeLadder& f, const std::vector<double>& in, double fRes, double r)
{
    std::vector<double> y (in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
        y[i] = f.process (in[i], fRes, r);
    return y;
}

inline std::vector<double> logSpace (double lo, double hi, int count)
{
    std::vector<double> v (static_cast<std::size_t> (count));
    for (int i = 0; i < count; ++i)
        v[static_cast<std::size_t> (i)] = lo * std::pow (hi / lo, static_cast<double> (i) / static_cast<double> (count - 1));
    return v;
}

//==============================================================================
/** Band-limited sawtooth (harmonics up to maxHz) of ideal amplitude `peak`, sampled at
    gridRate from t = 0. Rising ramp with a falling reset, zero mean. */
inline std::vector<double> additiveSaw (double f0, double peak, double maxHz, double gridRate, std::size_t numSamples)
{
    std::vector<double> out (numSamples, 0.0);
    const int harmonics = static_cast<int> (maxHz / f0);
    for (int k = 1; k <= harmonics; ++k)
    {
        const double w = kTwoPi * f0 * static_cast<double> (k) / gridRate;
        const double amp = peak * (2.0 / kPi) / static_cast<double> (k) * ((k & 1) ? 1.0 : -1.0);
        const Complex rot = std::polar (1.0, w);
        Complex ph (1.0, 0.0);
        for (std::size_t n = 0; n < numSamples; ++n)
        {
            if ((n & 4095u) == 0u) ph = std::polar (1.0, w * static_cast<double> (n));
            out[n] += amp * ph.imag();
            ph *= rot;
        }
    }
    return out;
}

//==============================================================================
/** The continuous equations of research doc 04 section 1.1 with the feedback network of
    section 1.3, integrated with classic fourth-order Runge-Kutta. std::tanh throughout:
    this shares no code with the filter under test. */
class Rk4Ladder
{
public:
    explicit Rk4Ladder (const dsp::DiodeLadderTrims& t, double bypass = 0.0)
        : rho (t.capRatio),
          cRes (dsp::DiodeLadder::resonanceRatio (t.capRatio)),
          wq (1.0 / (t.rbOhms * dsp::DiodeLadder::kCouplerFarads)),
          wL (t.loadScale / (t.rbOhms * 2.0 * dsp::DiodeLadder::kLadderFarads)),
          kMax (t.kMax),
          blend (t.subAudioNetwork ? bypass : 1.0),
          offset (t.inputOffset)
    {
    }

    /** One step of length h. Inputs at the start, middle and end of the step. */
    void step (double h, double xa, double xm, double xb, double fa, double fm, double fb, double r)
    {
        State k1, k2, k3, k4, tmp;
        derivative (st, xa, fa, r, k1);
        axpy (st, k1, 0.5 * h, tmp);  derivative (tmp, xm, fm, r, k2);
        axpy (st, k2, 0.5 * h, tmp);  derivative (tmp, xm, fm, r, k3);
        axpy (st, k3, h, tmp);        derivative (tmp, xb, fb, r, k4);
        for (int i = 0; i < kN; ++i)
            st.v[i] += h / 6.0 * (k1.v[i] + 2.0 * k2.v[i] + 2.0 * k3.v[i] + k4.v[i]);
    }

    /** w = tanh (y4 - q) at the current state. */
    double output() const { return std::tanh (st.v[3] - st.v[4]); }

private:
    static constexpr int kN = 9;                    // y1..y4, q, four feedback low-pass states
    struct State { double v[kN] {}; };

    static void axpy (const State& a, const State& d, double h, State& out)
    {
        for (int i = 0; i < kN; ++i) out.v[i] = a.v[i] + h * d.v[i];
    }

    void derivative (const State& s, double x, double fRes, double r, State& d) const
    {
        const double w0 = kTwoPi * fRes / cRes;
        const double vb = s.v[3] - s.v[4];
        const double w = std::tanh (vb);

        double a = w;
        for (int i = 0; i < 3; ++i)
        {
            const double wp = kTwoPi * kFbPoleHz[i];
            d.v[5 + i] = wp * (a - s.v[5 + i]);
            a -= s.v[5 + i];
        }
        const double wp = kTwoPi * kFbPoleHz[3];
        d.v[8] = wp * (a - s.v[8]);
        const double e = a - (1.0 - kShelfZeroHz / kFbPoleHz[3]) * s.v[8];
        const double fbk = blend * w + (1.0 - blend) * e;

        const double u = x + offset - kMax * r * fbk;
        const double t0 = std::tanh (u);
        const double t1 = std::tanh (s.v[0] - s.v[1]);
        const double t2 = std::tanh (s.v[1] - s.v[2]);
        const double t3 = std::tanh (s.v[2] - s.v[3]);
        const double t4 = std::tanh (s.v[3]);
        d.v[0] = rho * w0 * (t0 - t1);
        d.v[1] = w0 * (t1 - t2);
        d.v[2] = w0 * (t2 - t3);
        d.v[3] = w0 * (t3 - t4) - wL * vb;
        d.v[4] = wq * vb;
    }

    double rho, cRes, wq, wL, kMax, blend, offset;
    State st;
};

inline double rms (const std::vector<double>& v)
{
    double acc = 0.0;
    for (double s : v) acc += s * s;
    return std::sqrt (acc / static_cast<double> (std::max<std::size_t> (v.size(), 1)));
}
}
