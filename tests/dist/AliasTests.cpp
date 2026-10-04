// Aliasing of the distortion block, per clipper family and oversampling factor.
//
// Method: a sine on an exact bin of a 16384-point spectrum (so every harmonic and every
// alias also sits on a bin, with no window), through the block at full drive, after the
// block has settled. The figure is the power of every component below 19 kHz that is not a
// harmonic, relative to the power of the harmonics. Above 19 kHz the oversampler's own
// transition band lets a little through on purpose; that is inaudible and excluded.
//
// The budgets are the measured values (October 2026, after first-order ADAA went into the
// fold, fuzz and console stages) plus 3 dB, so any change that makes a family alias more
// fails here.
#include "DistTestUtil.h"
#include "acid/fx/Distortion.h"
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::fx;
using namespace disttest;

namespace
{
constexpr double kFs = 48000.0;
constexpr std::size_t kN = 16384;
constexpr std::size_t kBin1k = 337;    // 987 Hz; prime, so aliases miss the harmonic bins
constexpr std::size_t kBin3k = 1021;   // 2991 Hz
const char* const kNames[kNumClipperFamilies] = { "DiodePair", "OpAmpHard", "AsymSoft", "ScoopHard",
                                                  "MidHump",   "GatedFuzz", "Console",  "Fold" };

/** Alias power against harmonic power in dB, for a tone at the nominal level. */
double aliasDb (int family, int factor, std::size_t bin, float drive)
{
    const double hz = static_cast<double> (bin) * kFs / static_cast<double> (kN);
    DistortionParams p;
    p.on = true;
    p.clipper = family;
    p.drive = drive;   // the Custom voicing otherwise: preamp 0 to 45 dB
    Distortion d;
    d.prepare (kFs, 256);
    d.setOversampling (factor);
    d.setParams (p);
    d.reset();
    const auto out = render (d, sine (hz, kNominalLevel, kFs, static_cast<int> (3 * kN)));
    return aliasRatioDb (out, 2 * kN, kN, bin, kFs, 19000.0);
}

constexpr int kFactors[4] = { 1, 2, 4, 8 };

// Measured at drive 100, in dB, for factors 1, 2, 4, 8.
constexpr double kMeasured3k[kNumClipperFamilies][4] = {
    { -18.4, -26.5, -35.1, -45.3 },   // diode pair
    { -19.3, -28.3, -48.6, -63.6 },   // op-amp hard
    { -15.4, -22.6, -30.2, -39.9 },   // asymmetric soft
    { -15.1, -24.3, -40.7, -56.8 },   // scoop hard
    { -24.4, -35.7, -56.1, -85.0 },   // mid hump
    { -15.4, -22.3, -29.2, -37.1 },   // gated fuzz (was -14.1, -27.2 at 4x, -33.8 at 8x)
    { -28.1, -41.7, -54.9, -70.3 },   // console (was -14.9, -29.4 at 4x, -39.3 at 8x)
    { -26.7, -27.8, -40.8, -59.7 },   // fold (was -13.6, -19.9 at 4x, -45.9 at 8x)
};
constexpr double kMeasured1k[kNumClipperFamilies][4] = {
    { -25.8, -35.7, -48.2, -70.7 },
    { -26.2, -39.8, -58.2, -86.6 },
    { -21.6, -30.5, -43.5, -56.6 },
    { -20.3, -40.2, -56.3, -71.7 },
    { -34.7, -54.1, -88.5, -128.9 },
    { -21.6, -28.8, -37.7, -48.5 },
    { -32.9, -47.8, -68.3, -92.5 },
    { -34.7, -43.1, -73.5, -87.3 },
};
constexpr double kMarginDb = 3.0;
}

TEST_CASE ("aliasing: every family and factor stays inside its budget at full drive", "[dist][aliasing]")
{
    for (int family = 0; family < kNumClipperFamilies; ++family)
        for (int f = 0; f < 4; ++f)
        {
            const double a3 = aliasDb (family, kFactors[f], kBin3k, 100.0f);
            const double a1 = aliasDb (family, kFactors[f], kBin1k, 100.0f);
            INFO (kNames[family] << " at " << kFactors[f] << "x: 3 kHz " << a3 << " dB (budget "
                                 << kMeasured3k[family][f] + kMarginDb << "), 1 kHz " << a1 << " dB (budget "
                                 << kMeasured1k[family][f] + kMarginDb << ")");
            CHECK (a3 <= kMeasured3k[family][f] + kMarginDb);
            CHECK (a1 <= kMeasured1k[family][f] + kMarginDb);
        }
}

TEST_CASE ("aliasing: minimumOversamplingFor keeps every family 30 dB clean", "[dist][aliasing]")
{
    for (int family = 0; family < kNumClipperFamilies; ++family)
    {
        const int factor = minimumOversamplingFor (family);
        INFO (kNames[family] << " at its minimum of " << factor << "x");
        CHECK ((factor == 1 || factor == 2 || factor == 4 || factor == 8));
        CHECK (aliasDb (family, factor, kBin3k, 100.0f) <= -30.0);
        CHECK (aliasDb (family, factor, kBin1k, 100.0f) <= -36.0);
        // ... and it is the minimum: half the factor misses the 3 kHz figure.
        if (factor > 1)
            CHECK (aliasDb (family, factor / 2, kBin3k, 100.0f) > -30.0);
    }
    CHECK (minimumOversamplingFor (-1) == 8);
    CHECK (minimumOversamplingFor (kNumClipperFamilies) == 8);
}

TEST_CASE ("aliasing: moderate drive is clean at 4x", "[dist][aliasing]")
{
    // Drive 50 (22.5 dB of preamp): what most voicings sit near.
    for (int family = 0; family < kNumClipperFamilies; ++family)
    {
        const double a3 = aliasDb (family, 4, kBin3k, 50.0f);
        const double a1 = aliasDb (family, 4, kBin1k, 50.0f);
        INFO (kNames[family] << ": 3 kHz " << a3 << " dB, 1 kHz " << a1 << " dB");
        CHECK (a3 <= -50.0);
        CHECK (a1 <= -55.0);
    }
}

TEST_CASE ("aliasing: drive automation does not add a tick at the control rate", "[dist][aliasing]")
{
    // Moving the drive refreshes the clipper coefficients every 16 samples. An ADAA stage
    // cannot difference antiderivatives of two different curves, but drive only scales what
    // a stage is fed, so its curve, and the ADAA, carry on undisturbed. Here the drive
    // dithers by 0.01 % every block, so the coefficients change at every control tick; the
    // non-harmonic floor must stay where it is without automation. (Treating every refresh
    // as a new curve put a control-rate tick at -66 to -71 dB in the fold and the console.)
    // The folder alone rises to about -86 dB: its output really does move with the gain.
    for (const int family : { 0, 1, 4, 5, 6, 7 })
    {
        const double hz = static_cast<double> (kBin1k) * kFs / static_cast<double> (kN);
        const auto in = sine (hz, kNominalLevel, kFs, static_cast<int> (3 * kN));
        const auto renderWith = [&] (bool dither)
        {
            DistortionParams p;
            p.on = true;
            p.clipper = family;
            p.drive = 70.0f;
            Distortion d;
            d.prepare (kFs, 64);
            d.setOversampling (4);
            d.setParams (p);
            d.reset();
            auto out = in;
            for (std::size_t i = 0; i < out.size(); i += 64)
            {
                if (dither)
                {
                    p.drive = (i / 64) % 2 == 0 ? 70.0f : 70.01f;
                    d.setParams (p);
                }
                d.process (out.data() + i, nullptr, 64);
            }
            return aliasRatioDb (out, 2 * kN, kN, kBin1k, kFs, 19000.0);
        };
        const double still = renderWith (false), moving = renderWith (true);
        INFO (kNames[family] << ": " << still << " dB still, " << moving << " dB with the drive moving");
        CHECK (moving < std::max (still + 3.0, -80.0));
    }
}
