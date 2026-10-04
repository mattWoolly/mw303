#pragma once
// Fast scalar math for the diode ladder solver: a table-driven tanh and a rational tan.
// Both are branch-free on the hot path, allocate nothing and never throw.
#include <cmath>

namespace acid::dsp::ladder
{
/** Piecewise cubic Taylor table for tanh on [0, 12], 64 knots per unit, half a cache line
    per knot. The coefficients are pre-scaled so the polynomial runs on the knot-relative
    offset in knot units (-0.5..0.5). Built once from std::tanh (see tanhTable());
    read-only afterwards. */
struct TanhTable
{
    static constexpr int kKnotsPerUnit = 64;
    static constexpr double kMaxArg = 12.0;
    static constexpr int kNumKnots = 12 * kKnotsPerUnit + 1;

    alignas (32) double coeff[kNumKnots][4];
};

/** The shared table. The first call builds it (25 kB, a few tens of microseconds) and is
    thread-safe; call it once off the audio thread (DiodeLadder's constructor does) and
    keep the reference. */
const TanhTable& tanhTable() noexcept;

/** tanh (x) with absolute error below 1e-9, and relative error below 1e-9 near zero.
    Odd-symmetric by construction. Arguments beyond +-12 return +-tanh (12) = +-(1 - 7.6e-11).
    NaN returns a finite value of magnitude near 1, so a NaN never propagates through here;
    callers that care must test their own arguments. */
inline double fastTanh (const TanhTable& table, double x) noexcept
{
    double a = std::abs (x);
    a = a < TanhTable::kMaxArg ? a : TanhTable::kMaxArg;                // also maps NaN to the last knot
    const double scaled = a * static_cast<double> (TanhTable::kKnotsPerUnit);
    const int knot = static_cast<int> (scaled + 0.5);
    const double d = scaled - static_cast<double> (knot);
    const double* c = table.coeff[knot];
    const double t = (c[0] + d * c[1]) + (d * d) * (c[2] + d * c[3]);
    return std::copysign (t, x);
}

/** tan (x) for 0 <= x <= 0.45 pi, relative error below 3e-11 (an 8-level continued fraction
    written as one rational). Outside that range the result is not meaningful. */
inline double fastTan (double x) noexcept
{
    const double x2 = x * x;
    const double num = 2027025.0 + x2 * (-270270.0 + x2 * (6930.0 - 36.0 * x2));
    const double den = 2027025.0 + x2 * (-945945.0 + x2 * (51975.0 + x2 * (-630.0 + x2)));
    return x * num / den;
}
}
