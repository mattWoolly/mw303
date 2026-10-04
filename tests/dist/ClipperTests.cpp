// The eight clipper families, tested at the circuit-model level: the implicit solvers
// against bisection, transfer curves, symmetry, harmonic content, knee and bias, and
// agreement between sample rates.
#include "DistTestUtil.h"
#include "acid/common/Rng.h"
#include "acid/fx/DistortionClippers.h"
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::fx;
using namespace acid::fx::detail;
using namespace disttest;

namespace
{
constexpr double kFsHigh = 192000.0;
const char* const kNames[kNumClipperFamilies] = { "DiodePair", "OpAmpHard", "AsymSoft", "ScoopHard",
                                                  "MidHump",   "GatedFuzz", "Console",  "Fold" };

ClipperCoeffs coeffsFor (int family, double gainDb, double knee = 1.0, double bias = 0.0, double fs = kFsHigh)
{
    ClipperControls ctl;
    ctl.family = static_cast<ClipperFamily> (family);
    ctl.gain = dbToGain (gainDb);
    ctl.knee = knee;
    ctl.bias = bias;
    ClipperCoeffs c;
    c.update (ctl, fs);
    return c;
}

std::vector<double> runClipper (const ClipperCoeffs& c, std::vector<double> x)
{
    ClipperState st;
    processClipper (c, st, x.data(), static_cast<int> (x.size()));
    return x;
}

std::vector<double> sineD (double freq, double amp, double fs, int n)
{
    std::vector<double> x (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t> (i)] = amp * std::sin (kTwoPi * freq * i / fs);
    return x;
}

/** One sample from rest: the instantaneous transfer curve of a family with slow state.
    An ADAA stage outputs the mean of its curve over the step from its last input, so the
    fuzz's two ADAA stages are first primed on x itself (from a throwaway run); the slow
    state still starts from rest. */
double fromRest (const ClipperCoeffs& c, double x)
{
    ClipperState primed;
    double v = x;
    processClipper (c, primed, &v, 1);

    ClipperState st;
    st.gatedFuzz.drive = primed.gatedFuzz.drive;
    st.gatedFuzz.collector = primed.gatedFuzz.collector;
    v = x;
    processClipper (c, st, &v, 1);
    return v;
}

/** Harmonic h of a steady 220 Hz tone, relative to the fundamental, in dB. */
struct Harmonics
{
    double fundamental = 0.0;
    double rel[12] = {};   // rel[h] for h = 2..11, dB
    double oddSum = 0.0, evenSum = 0.0;
};

Harmonics harmonicsOf (const ClipperCoeffs& c, double fs = kFsHigh, double freq = 220.0, double amp = kNominalLevel)
{
    const int n = static_cast<int> (fs);   // one second: every harmonic sits on a bin
    const auto y = runClipper (c, sineD (freq, amp, fs, n));
    const std::size_t from = static_cast<std::size_t> (n / 2);
    Harmonics h;
    h.fundamental = tone (y, freq, fs, from);
    for (int k = 2; k < 12; ++k)
    {
        const double a = tone (y, freq * k, fs, from);
        h.rel[k] = db (a / h.fundamental);
        ((k & 1) ? h.oddSum : h.evenSum) += a * a;
    }
    h.oddSum = db (std::sqrt (h.oddSum) / h.fundamental);
    h.evenSum = db (std::sqrt (h.evenSum) / h.fundamental);
    return h;
}

bool isSymmetricFamily (int f)
{
    return f == 0 || f == 1 || f == 4 || f == 7;   // the console's class-A stage is not
}
}

//==============================================================================
TEST_CASE ("solvers: diode pair agrees with bisection, warm or cold", "[dist][clipper][solver]")
{
    Rng rng (11);
    for (int trial = 0; trial < 60; ++trial)
    {
        DiodeLaw d;
        const double scale = 0.6 * expMap (rng.nextDouble(), 0.02, 0.2);
        d.set (expMap (rng.nextDouble(), 1.0, 500.0), expMap (rng.nextDouble(), 1.0e-12, 1.0) * (1.0 + 0.6 / std::sinh (0.6 / scale)),
               scale);

        SolverState st;
        double rhs = 0.0;
        for (int i = 0; i < 400; ++i)
        {
            // Mostly small moves, as in audio, with the occasional leap across the whole range.
            if (rng.chance (0.1)) rhs = (rng.nextDouble() * 2.0 - 1.0) * expMap (rng.nextDouble(), 1.0e-6, 1.0e5);
            else                  rhs += (rng.nextDouble() * 2.0 - 1.0) * 0.05;

            const double v = solveDiodePair (d, rhs, st);

            double lo = 0.0, hi = std::abs (rhs) / d.lin;
            for (int k = 0; k < 200; ++k)
            {
                const double mid = 0.5 * (lo + hi);
                (d.lin * mid + d.sat * std::sinh (mid / d.scale) > std::abs (rhs) ? hi : lo) = mid;
            }
            const double expected = std::copysign (0.5 * (lo + hi), rhs);

            REQUIRE (std::isfinite (v));
            REQUIRE (std::abs (v - expected) <= 1.0e-6 * d.scale + 1.0e-12 * std::abs (expected));
        }
    }
}

TEST_CASE ("solvers: negating the input negates the result exactly", "[dist][clipper][solver]")
{
    Rng rng (5);
    DiodeLaw d;
    d.set (1.0, 1.0e-5, 0.045);
    SolverState a, b, ta, tb;
    for (int i = 0; i < 2000; ++i)
    {
        const double rhs = (rng.nextDouble() * 2.0 - 1.0) * expMap (rng.nextDouble(), 1.0e-4, 100.0);
        REQUIRE (solveDiodePair (d, rhs, a) == -solveDiodePair (d, -rhs, b));
        REQUIRE (solveTanhLoop (3.0, 7.0, rhs, ta) == -solveTanhLoop (3.0, 7.0, -rhs, tb));
    }
}

TEST_CASE ("solvers: single junction agrees with bisection", "[dist][clipper][solver]")
{
    Rng rng (23);
    for (int trial = 0; trial < 40; ++trial)
    {
        const double sat = expMap (rng.nextDouble(), 1.0e-14, 0.1);
        const double scale = kThermalVoltage;
        SolverState st;
        double rhs = 0.0;
        for (int i = 0; i < 400; ++i)
        {
            if (rng.chance (0.1)) rhs = (rng.nextDouble() * 2.0 - 1.0) * expMap (rng.nextDouble(), 1.0e-4, 1.0e3);
            else                  rhs += (rng.nextDouble() * 2.0 - 1.0) * 0.02;

            const double v = solveJunction (sat, scale, 1.0 / scale, rhs, st);

            double lo = std::min (rhs, 0.0) - 1.0, hi = std::max (rhs, 0.0) + sat;
            for (int k = 0; k < 200; ++k)
            {
                const double mid = 0.5 * (lo + hi);
                (mid + sat * std::expm1 (mid / scale) > rhs ? hi : lo) = mid;
            }
            REQUIRE (std::isfinite (v));
            REQUIRE (std::abs (v - 0.5 * (lo + hi)) <= 1.0e-6 * scale);
        }
    }
}

TEST_CASE ("solvers: tanh loop agrees with bisection", "[dist][clipper][solver]")
{
    Rng rng (37);
    for (int trial = 0; trial < 40; ++trial)
    {
        const double c = expMap (rng.nextDouble(), 1.0e-3, 10.0);
        const double k = expMap (rng.nextDouble(), 0.5, 200.0);
        SolverState st;
        double rhs = 0.0;
        for (int i = 0; i < 400; ++i)
        {
            if (rng.chance (0.15)) rhs = (rng.nextDouble() * 2.0 - 1.0) * expMap (rng.nextDouble(), 1.0e-5, 1.0e3);
            else                   rhs += (rng.nextDouble() * 2.0 - 1.0) * 0.02;

            const double e = solveTanhLoop (c, k, rhs, st);

            double lo = 0.0, hi = std::abs (rhs);
            for (int j = 0; j < 200; ++j)
            {
                const double mid = 0.5 * (lo + hi);
                (mid + c * std::tanh (k * mid) > std::abs (rhs) ? hi : lo) = mid;
            }
            REQUIRE (std::isfinite (e));
            REQUIRE (std::abs (e - std::copysign (0.5 * (lo + hi), rhs)) <= 1.0e-9 / k + 1.0e-12 * std::abs (rhs));
        }
    }
}

TEST_CASE ("clipper: drive compensation", "[dist][clipper]")
{
    CHECK (driveCompensation (1.0) == 1.0);
    double previous = 1.0;
    for (double g = 1.1; g < 1000.0; g *= 1.3)
    {
        const double c = driveCompensation (g);
        CHECK (c < previous);
        CHECK (c * g >= 1.0);   // more drive never makes a small signal quieter
        previous = c;
    }
    CHECK (std::abs (driveCompensation (1000.0) - 1.0 / kClipHeadroom) < 0.01);
}

//==============================================================================
TEST_CASE ("clipper: unity gain for small signals at unity drive", "[dist][clipper]")
{
    // GatedFuzz is the exception by design: below its gate it passes nothing.
    // ScoopHard has its tone stage: unity at the extremes of the band, a scoop in the middle.
    for (const int family : { 0, 1, 2, 4, 6, 7 })
    {
        const auto c = coeffsFor (family, 0.0);
        const auto y = runClipper (c, sineD (1000.0, 0.002, kFsHigh, 19200));
        INFO (kNames[family]);
        CHECK (std::abs (db (tone (y, 1000.0, kFsHigh, 9600) / 0.002)) < 0.05);
    }

    const auto scoop = coeffsFor (3, 0.0);
    const auto low = runClipper (scoop, sineD (40.0, 0.002, kFsHigh, 96000));
    const auto mid = runClipper (scoop, sineD (500.0, 0.002, kFsHigh, 19200));
    CHECK (std::abs (db (tone (low, 40.0, kFsHigh, 48000) / 0.002)) < 0.5);
    const double scoopDb = db (tone (mid, 500.0, kFsHigh, 9600) / 0.002);
    INFO ("scoop at 500 Hz: " << scoopDb << " dB");
    CHECK (scoopDb < -6.0);
    CHECK (scoopDb > -12.0);

    const auto gate = coeffsFor (5, 0.0);
    const auto gated = runClipper (gate, sineD (1000.0, 0.002, kFsHigh, 19200));
    CHECK (db (rms (gated, 9600) / 0.002) < -40.0);
}

TEST_CASE ("clipper: diode pair transfer curve", "[dist][clipper][curve]")
{
    for (const double gainDb : { 0.0, 20.0, 40.0, 60.0 })
        for (const double knee : { 0.0, 0.5, 1.0 })
        {
            const auto c = coeffsFor (0, gainDb, knee);
            DiodePairState up, down;
            double previous = -1.0e9, largest = 0.0;
            INFO ("gain " << gainDb << " dB, knee " << knee);
            for (int i = -4000; i <= 4000; ++i)
            {
                const double x = i / 1000.0;
                const double y = diodePairTick (c.diodePair, up, x);
                REQUIRE (y >= previous - 1.0e-9);                                    // monotonic
                REQUIRE (y == -diodePairTick (c.diodePair, down, -x));               // odd
                previous = y;
                largest = std::max (largest, std::abs (y));
            }
            CHECK (largest < 4.0 * kNominalLevel);                                   // bounded
            if (gainDb >= 40.0) CHECK (largest > 0.8 * kNominalLevel);               // and it does settle near nominal
        }

    // The knee is a soft one: at the clip level the curve has left the straight line but
    // is still rising.
    const auto c = coeffsFor (0, 0.0, 1.0);
    DiodePairState st;
    const double atClip = diodePairTick (c.diodePair, st, 2.0 * kNominalLevel);
    CHECK (atClip < 0.9 * 2.0 * kNominalLevel);
    CHECK (atClip > 0.5 * 2.0 * kNominalLevel);
    CHECK (diodePairTick (c.diodePair, st, 4.0 * kNominalLevel) > atClip * 1.05);

    // A hard knee stays straight for longer and then flattens more abruptly.
    const auto hard = coeffsFor (0, 0.0, 0.0);
    DiodePairState hs;
    CHECK (diodePairTick (hard.diodePair, hs, 0.5 * kNominalLevel) > 0.499 * kNominalLevel);
    const double h1 = diodePairTick (hard.diodePair, hs, 4.0 * kNominalLevel);
    const double h2 = diodePairTick (hard.diodePair, hs, 8.0 * kNominalLevel);
    CHECK (h2 / h1 < 1.03);
}

TEST_CASE ("clipper: wavefolder transfer curve folds, then stops", "[dist][clipper][curve]")
{
    for (const double knee : { 0.0, 1.0 })
    {
        const auto c = coeffsFor (7, 0.0, knee);
        FoldState up, down;
        int reversals = 0;
        double previous = 0.0, previousSlope = 1.0, largest = 0.0;
        INFO ("knee " << knee);
        for (int i = 1; i <= 16000; ++i)
        {
            const double x = i / 1000.0;   // up to 64 times the nominal level
            const double y = foldTick (c.fold, up, x);
            REQUIRE (std::abs (y + foldTick (c.fold, down, -x)) < 1.0e-12);   // odd
            const double slope = y - previous;
            if (slope * previousSlope < 0.0) ++reversals;
            if (slope != 0.0) previousSlope = slope;
            previous = y;
            largest = std::max (largest, std::abs (y));
        }
        // Four cells, each folding once (slope +1, then -1): composed, they turn the curve
        // round at T, 3T, 5T and 7T (down, up, down, up), then the end stage holds it.
        CHECK (reversals >= 4);
        CHECK (reversals <= 5);
        CHECK (largest < 3.0 * kNominalLevel);

        // Far past the last fold the end stage holds the output: no further folding, only
        // the logarithmic creep of a clipping junction (about n Vt ln 2 per doubling).
        const double far1 = foldTick (c.fold, up, 100.0);
        const double far2 = foldTick (c.fold, up, 200.0);
        CHECK (std::abs (far2 - far1) < 0.03 * std::abs (far1));
    }
}

TEST_CASE ("clipper: asymmetric soft stage transfer curve", "[dist][clipper][curve]")
{
    for (const double knee : { 0.0, 0.5, 1.0 })
    {
        const auto c = coeffsFor (2, 20.0, knee);
        double previous = -1.0e9;
        INFO ("knee " << knee);
        for (int i = -2000; i <= 2000; ++i)
        {
            const double y = fromRest (c, i / 1000.0);
            REQUIRE (y >= previous - 1.0e-12);   // monotonic
            previous = y;
        }

        const double top = fromRest (c, 2.0), bottom = fromRest (c, -2.0);
        CHECK (top > 0.0);
        CHECK (bottom < 0.0);
        // Cutoff is a hard floor; bottoming is a different, softer limit at a different level.
        CHECK (fromRest (c, -1.0) == bottom);
        CHECK (std::abs (top + bottom) > 0.05 * top);
        CHECK (std::max (top, -bottom) < 3.0 * kNominalLevel);
    }

    // The square law: at low level the curve is already bent, and bent one way.
    const auto c = coeffsFor (2, 0.0, 1.0);
    const double plus = fromRest (c, 0.1), minus = fromRest (c, -0.1);
    CHECK (plus > -minus * 1.05);
}

TEST_CASE ("clipper: gated fuzz transfer curve", "[dist][clipper][curve]")
{
    const auto c = coeffsFor (5, 0.0, 1.0);
    const double swing = 2.0 * kNominalLevel;

    // Monotonic, flat below the gate, flat again once the transistor saturates.
    double previous = -1.0e9;
    for (int i = -2000; i <= 8000; ++i)
    {
        const double y = fromRest (c, i / 1000.0);
        REQUIRE (y >= previous - 1.0e-12);
        previous = y;
    }
    const double rest = fromRest (c, 0.0);
    CHECK (std::abs (fromRest (c, -2.0) - rest) < 0.01 * swing);      // nothing on the negative side
    CHECK (std::abs (fromRest (c, 0.05) - rest) < 0.02 * swing);      // the gate: small inputs do not open it
    CHECK (fromRest (c, 1.0) - rest > 0.9 * swing);                   // large ones open it fully
    CHECK (fromRest (c, 8.0) == fromRest (c, 4.0));                   // saturation

    // The bias control moves the gate.
    const auto open = coeffsFor (5, 0.0, 1.0, 1.0);
    const auto shut = coeffsFor (5, 0.0, 1.0, -1.0);
    CHECK (fromRest (open, 0.05) - fromRest (open, 0.0) > 0.05 * swing);
    CHECK (std::abs (fromRest (shut, 0.2) - fromRest (shut, 0.0)) < 0.02 * swing);
}

TEST_CASE ("clipper: level in against level out is compressive and bounded", "[dist][clipper][curve]")
{
    for (int family = 0; family < kNumClipperFamilies; ++family)
    {
        if (family == 7) continue;   // the folder is not a compressor: see its own test
        for (const double gainDb : { 0.0, 20.0, 50.0 })
        {
            const auto c = coeffsFor (family, gainDb);
            double previousPeak = 0.0, firstRatio = 0.0, lastRatio = 0.0;
            INFO (kNames[family] << ", gain " << gainDb << " dB");
            for (int step = 0; step <= 12; ++step)
            {
                const double amp = kNominalLevel * dbToGain (-36.0 + 4.0 * step);   // -36 to +12 dB re nominal
                const auto y = runClipper (c, sineD (1000.0, amp, kFsHigh, 38400));
                const double pk = peak (y, 19200) ;
                CHECK (pk >= previousPeak * 0.97);     // louder in is never quieter out
                CHECK (pk < 4.0 * kNominalLevel);      // bounded
                previousPeak = pk;
                if (step == 0) firstRatio = pk / amp;
                lastRatio = pk / amp;
            }
            // The fuzz gates quiet signals instead. The mid-hump stage at unity drive has its
            // feedback resistor shorted (gain 1 + Rf / R1 with Rf = 0), so its diodes never
            // conduct: it is linear there, as the circuit is.
            if (family != 5 && ! (family == 4 && gainDb == 0.0))
                CHECK (lastRatio < firstRatio);
        }
    }
}

//==============================================================================
TEST_CASE ("clipper: symmetric families are exactly odd, the others are not", "[dist][clipper][symmetry]")
{
    const auto in = [] {
        auto x = sineD (220.0, 0.3, kFsHigh, 9600);
        const auto y = sineD (1730.0, 0.2, kFsHigh, 9600);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
        return x;
    }();
    auto negated = in;
    for (auto& v : negated) v = -v;

    for (int family = 0; family < kNumClipperFamilies; ++family)
        for (const double gainDb : { 6.0, 30.0 })
        {
            const auto c = coeffsFor (family, gainDb);
            const auto a = runClipper (c, in);
            const auto b = runClipper (c, negated);
            double worst = 0.0;
            for (std::size_t i = 0; i < a.size(); ++i)
                worst = std::max (worst, std::abs (a[i] + b[i]));

            INFO (kNames[family] << ", gain " << gainDb << " dB: asymmetry " << worst);
            if (isSymmetricFamily (family)) CHECK (worst < 1.0e-12);
            else if (family == 3)           CHECK (worst > 1.0e-4);   // the booster: slight
            else                            CHECK (worst > 0.05);     // JFET stage and fuzz: strong
        }
}

TEST_CASE ("clipper: harmonic content at 20 dB of drive", "[dist][clipper][harmonics]")
{
    for (int family = 0; family < kNumClipperFamilies; ++family)
    {
        const auto h = harmonicsOf (coeffsFor (family, 20.0));
        INFO (kNames[family] << ": H2 " << h.rel[2] << " H3 " << h.rel[3] << " H4 " << h.rel[4] << " H5 " << h.rel[5]
                             << " dB, odd " << h.oddSum << " even " << h.evenSum);

        CHECK (h.oddSum > -45.0);   // every family is audibly distorting at this drive

        if (isSymmetricFamily (family))
        {
            CHECK (h.evenSum < -120.0);   // odd harmonics only
        }
        else if (family == 3)
        {
            CHECK (h.oddSum > h.evenSum + 10.0);   // odd-dominant, with a trace of even from the booster
            CHECK (h.evenSum > -80.0);
        }
        else
        {
            CHECK (h.rel[2] > -35.0);   // strong second harmonic without any bias
        }
    }

    // The fuzz is the one whose second harmonic rivals its fundamental.
    CHECK (harmonicsOf (coeffsFor (5, 12.0)).rel[2] > -10.0);
}

TEST_CASE ("clipper: bias adds even harmonics in every family", "[dist][clipper][harmonics]")
{
    for (int family = 0; family < kNumClipperFamilies; ++family)
        for (const double bias : { -0.4, 0.4 })
        {
            const auto plain = harmonicsOf (coeffsFor (family, 20.0, 1.0, 0.0));
            const auto biased = harmonicsOf (coeffsFor (family, 20.0, 1.0, bias));
            INFO (kNames[family] << ", bias " << bias << ": even " << plain.evenSum << " -> " << biased.evenSum << " dB");
            CHECK (biased.evenSum > -40.0);
            if (isSymmetricFamily (family))
                CHECK (biased.evenSum > plain.evenSum + 60.0);
        }

    // In the two asymmetric stages the bias moves the operating point, which changes the
    // balance of even harmonics rather than creating them.
    for (const int family : { 2, 5 })
    {
        const auto low = harmonicsOf (coeffsFor (family, 12.0, 1.0, -0.6));
        const auto high = harmonicsOf (coeffsFor (family, 12.0, 1.0, 0.6));
        INFO (kNames[family] << ": H2 " << low.rel[2] << " and " << high.rel[2]);
        CHECK (std::abs (low.rel[2] - high.rel[2]) > 1.0);
    }
}

TEST_CASE ("clipper: a harder knee means more high harmonics", "[dist][clipper][harmonics]")
{
    for (int family = 0; family < kNumClipperFamilies; ++family)
    {
        // Enough drive for a 220 Hz tone to reach the clipping element. The op-amp and
        // mid-hump stages give a bass tone only part of their gain (their networks favour the
        // mids), so they need more; the folder is just reaching its first fold. The JFET
        // stage's knee is the subtlest of the eight: about 1 dB of upper harmonics.
        const double drive = family == 7 ? 6.0 : (family == 1 ? 26.0 : (family == 2 || family == 4 ? 20.0 : 14.0));
        const auto soft = runClipper (coeffsFor (family, drive, 1.0), sineD (220.0, kNominalLevel, kFsHigh, 192000));
        const auto hard = runClipper (coeffsFor (family, drive, 0.0), sineD (220.0, kNominalLevel, kFsHigh, 192000));

        const auto upper = [] (const std::vector<double>& y) {
            double sum = 0.0;
            for (int k = 9; k <= 25; ++k)
            {
                const double a = tone (y, 220.0 * k, kFsHigh, 96000);
                sum += a * a;
            }
            return db (std::sqrt (sum) / tone (y, 220.0, kFsHigh, 96000));
        };
        const double softDb = upper (soft), hardDb = upper (hard);
        INFO (kNames[family] << ": harmonics 9 to 25, soft " << softDb << " dB, hard " << hardDb << " dB");
        CHECK (hardDb > softDb + 1.0);
    }
}

//==============================================================================
TEST_CASE ("clipper: the model does not depend on the rate it runs at", "[dist][clipper][rates]")
{
    for (int family = 0; family < kNumClipperFamilies; ++family)
        for (const double gainDb : { 6.0, 26.0 })
        {
            const auto reference = harmonicsOf (coeffsFor (family, gainDb, 1.0, 0.0, 768000.0), 768000.0);
            for (const double fs : { 88200.0, 96000.0, 192000.0, 384000.0 })
            {
                const auto h = harmonicsOf (coeffsFor (family, gainDb, 1.0, 0.0, fs), fs);
                INFO (kNames[family] << ", gain " << gainDb << " dB, " << fs << " Hz: fundamental "
                                     << db (h.fundamental) << " against " << db (reference.fundamental) << ", H3 "
                                     << h.rel[3] << " against " << reference.rel[3]);
                CHECK (std::abs (db (h.fundamental / reference.fundamental)) < 0.5);
                if (reference.rel[3] > -60.0)
                    CHECK (std::abs (h.rel[3] - reference.rel[3]) < 1.5);
            }
        }
}

TEST_CASE ("clipper: coefficients and output stay finite at the extremes", "[dist][clipper]")
{
    Rng rng (99);
    for (int family = 0; family < kNumClipperFamilies; ++family)
        for (const double fs : { 44100.0, 384000.0, 1536000.0 })
            for (const double gainDb : { 0.0, 60.0 })
                for (const double knee : { 0.0, 1.0 })
                    for (const double bias : { -1.0, 0.0, 1.0 })
                    {
                        const auto c = coeffsFor (family, gainDb, knee, bias, fs);
                        std::vector<double> x (4096);
                        for (std::size_t i = 0; i < x.size(); ++i)
                        {
                            // Noise, full-scale steps and a huge spike.
                            x[i] = rng.nextDouble() * 2.0 - 1.0;
                            if ((i / 512) % 2 == 1) x[i] = (i / 64) % 2 == 0 ? 4.0 : -4.0;
                        }
                        x[2000] = 1.0e6;
                        x[2001] = -1.0e6;
                        const auto y = runClipper (c, x);
                        INFO (kNames[family] << ", " << fs << " Hz, gain " << gainDb << ", knee " << knee << ", bias " << bias);
                        REQUIRE (allFinite (y));
                        REQUIRE (peak (y) < 8.0);
                    }
}

TEST_CASE ("clipper: bass against treble, the frequency-dependent families", "[dist][clipper]")
{
    const auto gainAt = [] (int family, double freq, double gainDb) {
        const auto y = runClipper (coeffsFor (family, gainDb), sineD (freq, 1.0e-5, kFsHigh, 192000));
        return db (tone (y, freq, kFsHigh, 96000) / 1.0e-5) - db (driveCompensation (dbToGain (gainDb)));
    };

    // Op-amp stage: the gain network leaves the deep bass near unity, gives the mids part
    // of the gain, and the finite gain-bandwidth takes it away again at the top.
    CHECK (gainAt (1, 10.0, 40.0) < 20.0);
    CHECK (gainAt (1, 1000.0, 40.0) > 34.0);
    CHECK (gainAt (1, 1000.0, 40.0) > gainAt (1, 100.0, 40.0) + 3.0);
    // 316 kHz / 1 kHz is 50 dB of open-loop gain; the network's phase at 1 kHz lifts the
    // closed-loop gain about 3 dB above that (|A / (1 + A beta)| with A beta at -140 degrees).
    CHECK (gainAt (1, 1000.0, 60.0) < 55.0);
    CHECK (gainAt (1, 10000.0, 60.0) < 31.0);                             // 30 dB at 10 kHz
    CHECK (gainAt (1, 10000.0, 60.0) > 27.0);

    // Mid-hump stage: unity in the deep bass, the full gain above the 720 Hz corner, and the
    // small feedback capacitor rolling the top off at high gain. The rise starts well below
    // the corner, where Rf / |Z_leg| = 1 (23 Hz at 30 dB), so "unity" means 10 Hz.
    CHECK (std::abs (gainAt (4, 10.0, 30.0)) < 1.0);
    CHECK (gainAt (4, 3000.0, 30.0) > 28.0);
    CHECK (gainAt (4, 720.0, 30.0) < gainAt (4, 3000.0, 30.0) - 2.0);
    CHECK (gainAt (4, 15000.0, 40.0) < gainAt (4, 3000.0, 40.0) - 3.0);

    // Scoop stage: bass under 72 Hz only gets the booster's share of the gain.
    CHECK (gainAt (3, 20.0, 40.0) < gainAt (3, 2000.0, 40.0) - 6.0);
}
