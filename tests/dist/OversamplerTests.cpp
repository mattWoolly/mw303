// The half-band resampler round trip: exact integer latency, flat passband, image and
// alias rejection, at every factor and base rate.
#include "AllocGuard.h"
#include "DistTestUtil.h"
#include "acid/fx/Oversampler.h"
#include <catch2/catch_test_macros.hpp>
#include <type_traits>

using namespace acid;
using namespace acid::fx;
using namespace disttest;

namespace
{
constexpr int kBlock = 16;

/** Up then down with nothing in between. */
std::vector<double> roundTrip (Oversampler& os, const std::vector<double>& in)
{
    std::vector<double> out (in.size());
    double high[kBlock * Oversampler::kMaxFactor];
    for (std::size_t i = 0; i + kBlock <= in.size(); i += kBlock)
    {
        os.upsample (in.data() + i, high, kBlock);
        os.downsample (high, out.data() + i, kBlock);
    }
    return out;
}

std::vector<double> sineD (double freq, double fs, int n)
{
    std::vector<double> x (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<std::size_t> (i)] = std::sin (kTwoPi * freq * i / fs);
    return x;
}
}

static_assert (std::is_trivially_copyable_v<Oversampler>, "the distortion copies channel state without allocating");

TEST_CASE ("oversampler: round trip is a pure integer delay", "[dist][oversampler]")
{
    for (const double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        Oversampler os;
        os.prepare (fs);
        CHECK (os.latencyForFactor (1) == 0);

        for (const int factor : { 1, 2, 4, 8 })
        {
            os.setFactor (factor);
            REQUIRE (os.factor() == factor);
            const int latency = os.latencySamples();
            CHECK (latency == os.latencyForFactor (factor));
            INFO ("fs " << fs << ", x" << factor << ", latency " << latency);
            CHECK (latency < 128);
            if (factor > 1) CHECK (latency > 0);

            std::vector<double> impulse (512, 0.0);
            impulse[0] = 1.0;
            const auto ir = roundTrip (os, impulse);

            std::size_t peakIndex = 0;
            for (std::size_t i = 0; i < ir.size(); ++i)
                if (std::abs (ir[i]) > std::abs (ir[peakIndex])) peakIndex = i;
            CHECK (static_cast<int> (peakIndex) == latency);

            // Linear phase: the response is symmetric about the latency.
            double asymmetry = 0.0;
            for (int k = 1; k <= latency; ++k)
                asymmetry = std::max (asymmetry, std::abs (ir[peakIndex + static_cast<std::size_t> (k)]
                                                           - ir[peakIndex - static_cast<std::size_t> (k)]));
            CHECK (asymmetry < 1.0e-12);

            // Unity gain at DC.
            double sum = 0.0;
            for (const double v : ir) sum += v;
            CHECK (std::abs (sum - 1.0) < 1.0e-9);
        }
    }
}

TEST_CASE ("oversampler: passband is flat to 20 kHz (or 0.45 fs)", "[dist][oversampler]")
{
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Oversampler os;
        os.prepare (fs);
        for (const int factor : { 2, 4, 8 })
        {
            os.setFactor (factor);
            for (const double freq : { 40.0, 1000.0, 10000.0, 18000.0, std::min (20000.0, 0.45 * fs) })
            {
                os.reset();
                const auto out = roundTrip (os, sineD (freq, fs, 16384));
                const double gainDb = db (tone (out, freq, fs, 2048));
                INFO ("fs " << fs << ", x" << factor << ", " << freq << " Hz: " << gainDb << " dB");
                CHECK (std::abs (gainDb) < 0.01);
            }
        }
    }
}

TEST_CASE ("oversampler: images are rejected on the way up", "[dist][oversampler]")
{
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
    {
        Oversampler os;
        os.prepare (fs);
        for (const int factor : { 2, 4, 8 })
        {
            os.setFactor (factor);
            const double freq = std::min (15000.0, 0.34 * fs);
            const auto in = sineD (freq, fs, 8192);

            std::vector<double> high;
            double block[kBlock * Oversampler::kMaxFactor];
            for (std::size_t i = 0; i + kBlock <= in.size(); i += kBlock)
            {
                os.upsample (in.data() + i, block, kBlock);
                high.insert (high.end(), block, block + kBlock * factor);
            }

            const double highRate = fs * factor;
            const std::size_t skip = high.size() / 8;
            CHECK (std::abs (db (tone (high, freq, highRate, skip))) < 0.01);
            for (int image = 1; image < factor; ++image)
            {
                for (const double f : { image * fs - freq, image * fs + freq })
                {
                    if (f >= 0.5 * highRate) continue;
                    INFO ("fs " << fs << ", x" << factor << ", image at " << f << " Hz");
                    CHECK (db (tone (high, f, highRate, skip)) < -80.0);
                }
            }
        }
    }
}

TEST_CASE ("oversampler: out-of-band content is rejected on the way down", "[dist][oversampler]")
{
    for (const double fs : { 44100.0, 48000.0, 96000.0 })
    {
        Oversampler os;
        os.prepare (fs);
        for (const int factor : { 2, 4, 8 })
        {
            os.setFactor (factor);
            const double highRate = fs * factor;

            // Tones that would alias onto 3 kHz at the base rate.
            for (int k = 1; k <= factor / 2; ++k)
            {
                const double freq = k * fs - 3000.0;
                if (freq >= 0.5 * highRate) continue;
                const auto high = sineD (freq, highRate, 8192 * factor);
                std::vector<double> out (8192);
                os.reset();
                for (std::size_t i = 0; i + kBlock <= out.size(); i += kBlock)
                    os.downsample (high.data() + i * static_cast<std::size_t> (factor), out.data() + i, kBlock);

                INFO ("fs " << fs << ", x" << factor << ", " << freq << " Hz in");
                CHECK (db (rms (out, 1024)) < -80.0);
            }
        }
    }
}

TEST_CASE ("oversampler: switching the factor does not allocate and clears state", "[dist][oversampler]")
{
    Oversampler os;
    os.prepare (48000.0);

    const auto in = sineD (1000.0, 48000.0, 256);
    std::vector<double> out (256);
    double high[kBlock * Oversampler::kMaxFactor];

    acid::test::ScopedAllocGuard guard;
    for (const int factor : { 8, 1, 4, 2, 8, 2 })
    {
        os.setFactor (factor);
        for (std::size_t i = 0; i + kBlock <= in.size(); i += kBlock)
        {
            os.upsample (in.data() + i, high, kBlock);
            os.downsample (high, out.data() + i, kBlock);
        }
        // After a switch the first output samples are silent for the whole latency.
        os.setFactor (factor);
        double zero[kBlock] = {}, silent[kBlock];
        os.upsample (zero, high, kBlock);
        os.downsample (high, silent, kBlock);
        for (const double v : silent)
            REQUIRE (v == 0.0);
    }
    REQUIRE (guard.count() == 0);
}
