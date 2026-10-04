// The voicing table: shape, naming rules, level matching, and a render of every voicing.
#include "AllocGuard.h"
#include "DistTestUtil.h"
#include "Wav.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using namespace acid;
using namespace acid::fx;
using namespace disttest;

namespace
{
constexpr double kFs = 48000.0;
constexpr float kModerateDrive = 50.0f;

/** RMS of a line through a voicing, in dB, skipping the first beat. */
double lineLevelDb (std::size_t index, float drive, const std::vector<float>& in, int factor, double fs = kFs)
{
    Distortion d;
    d.prepare (fs, 256);
    d.setOversampling (factor);
    d.setParams (voicingParams (index, drive));
    d.reset();
    const auto out = render (d, in);
    return db (rms (out, static_cast<std::size_t> (0.48 * fs)));
}

/** The same for the reference bass line. */
double voicingLevelDb (std::size_t index, float drive, double fs, int factor)
{
    return lineLevelDb (index, drive, acidLine (fs, 3.84), factor, fs);
}
}

TEST_CASE ("voicings: table shape", "[dist][voicings]")
{
    const auto voicings = distortionVoicings();
    REQUIRE (voicings.size() >= 25);
    REQUIRE (voicings.size() <= 64);   // dist_model runs 0..63
    REQUIRE (std::string (voicings[0].name) == "Custom");

    SECTION ("Custom equals the parameter defaults")
    {
        DistortionParams fromVoicing;
        applyVoicing (fromVoicing, voicings[0]);
        const DistortionParams fromTable = distortionParamsFrom (defaultParamValues());
        CHECK (fromVoicing.clipper == fromTable.clipper);
        CHECK (fromVoicing.preMin == fromTable.preMin);
        CHECK (fromVoicing.preMax == fromTable.preMax);
        CHECK (fromVoicing.knee == fromTable.knee);
        CHECK (fromVoicing.bias == fromTable.bias);
        CHECK (fromVoicing.lowKeep == fromTable.lowKeep);
        CHECK (fromVoicing.preHp == fromTable.preHp);
        CHECK (fromVoicing.preEqOn == fromTable.preEqOn);
        CHECK (fromVoicing.preEqGain == fromTable.preEqGain);
        CHECK (fromVoicing.preEqFreq == fromTable.preEqFreq);
        CHECK (fromVoicing.preEqWidth == fromTable.preEqWidth);
        CHECK (fromVoicing.tiltOn == fromTable.tiltOn);
        CHECK (fromVoicing.tiltAmt == fromTable.tiltAmt);
        CHECK (fromVoicing.tiltFreq == fromTable.tiltFreq);
        CHECK (fromVoicing.colorFreq == fromTable.colorFreq);
        CHECK (fromVoicing.colorReso == fromTable.colorReso);
        CHECK (fromVoicing.colorRange == fromTable.colorRange);
        CHECK (fromVoicing.postHp == fromTable.postHp);
        CHECK (fromVoicing.volume == fromTable.volume);
    }

    SECTION ("names are unique, short, plain ASCII")
    {
        std::set<std::string> names;
        for (const auto& v : voicings)
        {
            const std::string name = v.name;
            INFO (name);
            CHECK (! name.empty());
            CHECK (name.size() <= 20);
            CHECK (names.insert (name).second);
            for (const char c : name)
                CHECK ((c >= 0x20 && c < 0x7f));   // no em dash or other multi-byte punctuation
        }
    }

    SECTION ("every value is inside its parameter range")
    {
        const auto inRange = [] (float value, ParamId id)
        {
            const auto& info = paramInfo (id);
            return value >= info.min && value <= info.max;
        };
        for (const auto& v : voicings)
        {
            INFO (v.name);
            CHECK ((v.clipper >= 0 && v.clipper < kNumClipperFamilies));
            CHECK (inRange (v.preMin, kDistPreMin));
            CHECK (inRange (v.preMax, kDistPreMax));
            CHECK (v.preMin <= v.preMax);
            CHECK (inRange (v.knee, kDistKnee));
            CHECK (inRange (v.bias, kDistBias));
            CHECK (inRange (v.lowKeep, kDistLowKeep));
            CHECK (inRange (v.preHp, kDistPreHp));
            CHECK (inRange (v.preEqGain, kDistPreEqGain));
            CHECK (inRange (v.preEqFreq, kDistPreEqFreq));
            CHECK (inRange (v.preEqWidth, kDistPreEqWidth));
            CHECK (inRange (v.tiltAmt, kDistTiltAmt));
            CHECK (inRange (v.tiltFreq, kDistTiltFreq));
            CHECK (inRange (v.colorFreq, kDistColorFreq));
            CHECK (inRange (v.colorReso, kDistColorReso));
            CHECK (inRange (v.colorRange, kDistColorRange));
            CHECK (inRange (v.postHp, kDistPostHp));
            CHECK (inRange (v.volume, kDistVolume));
        }
    }

    SECTION ("all eight families are covered, each by at least two voicings")
    {
        int count[kNumClipperFamilies] = {};
        for (std::size_t i = 1; i < voicings.size(); ++i)
            ++count[voicings[i].clipper];
        for (int f = 0; f < kNumClipperFamilies; ++f)
        {
            INFO ("family " << f);
            CHECK (count[f] >= 2);
        }
    }
}

TEST_CASE ("voicings: writeVoicing and distortionParamsFrom round-trip", "[dist][voicings]")
{
    const auto voicings = distortionVoicings();
    for (std::size_t i = 0; i < voicings.size(); ++i)
    {
        ParamValues values = defaultParamValues();
        writeVoicing (values, voicings[i]);
        const DistortionParams viaTable = distortionParamsFrom (values);
        DistortionParams direct;
        applyVoicing (direct, voicings[i]);

        INFO (voicings[i].name);
        CHECK (viaTable.clipper == direct.clipper);
        CHECK (viaTable.preMin == direct.preMin);
        CHECK (viaTable.preMax == direct.preMax);
        CHECK (viaTable.knee == direct.knee);
        CHECK (viaTable.bias == direct.bias);
        CHECK (viaTable.lowKeep == direct.lowKeep);
        CHECK (viaTable.preHp == direct.preHp);
        CHECK (viaTable.preEqOn == direct.preEqOn);
        CHECK (viaTable.preEqGain == direct.preEqGain);
        CHECK (viaTable.preEqFreq == direct.preEqFreq);
        CHECK (viaTable.preEqWidth == direct.preEqWidth);
        CHECK (viaTable.tiltOn == direct.tiltOn);
        CHECK (viaTable.tiltAmt == direct.tiltAmt);
        CHECK (viaTable.tiltFreq == direct.tiltFreq);
        CHECK (viaTable.colorFreq == direct.colorFreq);
        CHECK (viaTable.colorReso == direct.colorReso);
        CHECK (viaTable.colorRange == direct.colorRange);
        CHECK (viaTable.postHp == direct.postHp);
        CHECK (viaTable.volume == direct.volume);
        // The front panel is left alone.
        CHECK (viaTable.on == false);
        CHECK (viaTable.drive == paramInfo (kDistDrive).def);
    }
}

TEST_CASE ("voicings: a voicing's level against Custom holds across drives and lines", "[dist][voicings][level]")
{
    // The trims themselves are fitted on the instrument's own output (tests/engine,
    // VoicingLevelTests.cpp): on these synthetic lines a voicing whose filters take a lot away
    // (a 950 Hz pre high-pass, a +22 dB tilt) sits several dB from Custom, which the voice
    // never does. What the block itself must keep is the shape: the family make-up holds each
    // voicing's level against Custom steady as the drive moves and the line changes. So each
    // voicing is measured against Custom at the same drive, relative to the middle of its own
    // offsets, with the tolerances the absolute check had (the old fit put that middle on
    // 0 dB, so this is the same check without the absolute part). A line away from the nominal level counts only from
    // drive 50, where Custom is driven too: at low drive Custom is clean and its level follows
    // the input, while a voicing whose preamp minimum already clips does not.
    // Before the family make-up: 18.3 dB of spread at drive 0, 5.3 dB at drive 25.
    const auto voicings = distortionVoicings();
    const auto specs = heldOutLines();
    const float drives[5] = { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f };
    std::vector<std::vector<float>> inputs;
    for (const auto& spec : specs) inputs.push_back (bassLine (spec, kFs, 1.92));

    double worstLow = 0.0, worstHigh = 0.0;
    for (std::size_t i = 1; i < voicings.size(); ++i)
    {
        struct Point { float drive; double levelDb, off; };
        std::vector<Point> points;
        double lo = 1.0e9, hi = -1.0e9;
        for (std::size_t s = 0; s < specs.size(); ++s)
            for (const float drive : drives)
            {
                if (specs[s].levelDb != 0.0 && drive < 50.0f) continue;
                const double off = lineLevelDb (i, drive, inputs[s], 2) - lineLevelDb (0, drive, inputs[s], 2);
                points.push_back ({ drive, specs[s].levelDb, off });
                lo = std::min (lo, off);
                hi = std::max (hi, off);
            }
        const double centre = 0.5 * (lo + hi);
        for (const Point& pt : points)
        {
            const double off = pt.off - centre;
            INFO (voicings[i].name << ", drive " << pt.drive << ", line at " << pt.levelDb << " dB: " << off
                                   << " dB from the middle of its offsets against Custom");
            CHECK (std::abs (off) <= (pt.drive == 0.0f ? 4.5 : 3.0));
            double& worst = pt.drive == 0.0f ? worstLow : worstHigh;
            worst = std::max (worst, std::abs (off));
        }
    }
    std::printf ("[voicings] level against Custom, around each voicing's middle: worst %.2f dB at drive 0, %.2f dB at drives 25 to 100\n",
                 worstLow, worstHigh);
}

TEST_CASE ("voicings: the drive knob changes character, not loudness", "[dist][voicings][level]")
{
    // The family make-up holds a nominal-level line near its clean level at every preamp
    // gain: Custom from drive 0 to 100 stays within a few dB (it was 15 dB louder at full
    // drive before the make-up went in).
    const auto in = acidLine (kFs, 1.92);
    const double clean = lineLevelDb (0, 0.0f, in, 2);
    for (const float drive : { 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const double level = lineLevelDb (0, drive, in, 2);
        INFO ("drive " << drive << ": " << level << " dB against " << clean << " dB clean");
        CHECK (std::abs (level - clean) <= 2.0);
    }
}

TEST_CASE ("voicings: level match holds at other rates and factors", "[dist][voicings]")
{
    const auto voicings = distortionVoicings();
    for (const auto& setup : { std::pair { 44100.0, 2 }, std::pair { 96000.0, 2 } })
    {
        // Each voicing's offset from Custom at this rate and factor against the same offset at
        // 48 kHz x4 (the absolute offsets belong to the voice-line fit in tests/engine; this
        // checks that the block keeps them at other rates). The limit is the 2.5 dB the
        // absolute check used to have.
        const double reference = voicingLevelDb (0, kModerateDrive, setup.first, setup.second);
        const double reference48 = voicingLevelDb (0, kModerateDrive, kFs, 4);
        for (std::size_t i = 0; i < voicings.size(); ++i)
        {
            const double off = voicingLevelDb (i, kModerateDrive, setup.first, setup.second) - reference;
            const double off48 = voicingLevelDb (i, kModerateDrive, kFs, 4) - reference48;
            INFO (voicings[i].name << " at " << setup.first << " Hz, x" << setup.second << ": " << off
                                   << " dB against Custom, " << off48 << " dB at 48 kHz x4");
            CHECK (std::abs (off - off48) <= 2.5);
        }
    }
}

TEST_CASE ("voicings: finite, bounded output on a resonant sweep at every factor", "[dist][voicings]")
{
    const auto voicings = distortionVoicings();
    auto in = acidLine (kFs, 1.92);

    // A hot resonant whistle on top: a rising sine sweep, well above the nominal level.
    double phase = 0.0;
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (in.size());
        phase += kTwoPi * 80.0 * std::pow (150.0, t) / kFs;
        in[i] += static_cast<float> (0.6 * std::sin (phase));
    }

    for (const int factor : { 1, 2, 4, 8 })
        for (std::size_t i = 0; i < voicings.size(); ++i)
            for (const float drive : { 0.0f, 50.0f, 100.0f })
            {
                Distortion d;
                d.prepare (kFs, 512);
                d.setOversampling (factor);
                auto p = voicingParams (i, drive);
                p.dynamics = 60.0f;
                p.color = drive - 50.0f;
                d.setParams (p);
                d.reset();
                const auto out = render (d, in, 512);

                INFO (voicings[i].name << ", x" << factor << ", drive " << drive);
                CHECK (allFinite (out));
                CHECK (peak (out) < 8.0);
                CHECK (rms (out) > 1.0e-4);
            }
}

TEST_CASE ("voicings: renders for listening", "[dist][voicings][render]")
{
    const auto voicings = distortionVoicings();
    const auto in = acidLine (kFs, 3.84);
    const std::string dir = ACID_TEST_OUTPUT_DIR;
    REQUIRE (acid::test::writeWav (dir + "/dist_00_input.wav", in, 1, kFs));

    for (std::size_t i = 0; i < voicings.size(); ++i)
    {
        Distortion d;
        d.prepare (kFs, 256);
        d.setOversampling (4);
        d.setParams (voicingParams (i, kModerateDrive));
        d.reset();
        const auto out = render (d, in);

        std::string name = voicings[i].name;
        for (auto& c : name)
            if (c == ' ') c = '_';
        char path[512];
        std::snprintf (path, sizeof path, "%s/dist_%02d_%s.wav", dir.c_str(), static_cast<int> (i), name.c_str());
        CHECK (acid::test::writeWav (path, out, 1, kFs));
    }
}

// Not part of the normal run. Run with: acid_dist_tests "[.calibrate]"
//
// Step 1 prints the rows of kFamilyLevelDb (Distortion.cpp): for each family and preamp
// gain, the make-up that brings the calibration lines back to their clean level (the
// middle of the spread between the lines). They are absolute (the table in force is
// accounted for): paste them in, rebuild. Step 2 prints, for reference only, the trim each
// voicing would need on these synthetic lines: the trims in the table are fitted on the
// instrument's own output instead (acid_engine_tests "[.calibrate]", VoicingLevelTests.cpp),
// because a voicing's filters take a different share of a real voice line. Re-run step 1 after
// any change to a clipper model, then the engine fit.
TEST_CASE ("voicings: level calibration", "[.calibrate]")
{
    const auto specs = calibrationLines();
    std::vector<std::vector<float>> lines;
    for (const auto& spec : specs)
        lines.push_back (bassLine (spec, kFs, 1.92));
    const auto levelOf = [] (const DistortionParams& p, const std::vector<float>& in)
    {
        Distortion d;
        d.prepare (kFs, 256);
        d.setOversampling (4);
        d.setParams (p);
        d.reset();
        return db (rms (render (d, in), static_cast<std::size_t> (0.48 * kFs)));
    };

    std::printf ("Step 1: kFamilyLevelDb rows (current table included)\n");
    for (int f = 0; f < kNumClipperFamilies; ++f)
    {
        std::printf ("    {");
        for (int gi = 0; gi <= 10; ++gi)
        {
            double lo = 1.0e9, hi = -1.0e9;
            for (const auto& in : lines)
            {
                DistortionParams clean;
                clean.on = true;
                clean.preMin = clean.preMax = 0.0f;
                DistortionParams driven = clean;
                driven.clipper = f;
                driven.preMin = driven.preMax = static_cast<float> (gi * 10);
                const double diff = levelOf (clean, in) - levelOf (driven, in)
                                  + distortionFamilyLevelDb (f, 6.0 * gi) - distortionFamilyLevelDb (0, 0.0);
                lo = std::min (lo, diff);
                hi = std::max (hi, diff);
            }
            std::printf (" %5.1f%s", 0.5 * (lo + hi), gi < 10 ? "," : "");
        }
        std::printf (" },\n");
    }

    std::printf ("Step 2: voicing trims on synthetic lines (reference only, see above)\n");
    const auto voicings = distortionVoicings();
    const float drives[5] = { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f };
    for (std::size_t i = 1; i < voicings.size(); ++i)
    {
        double lo = 1.0e9, hi = -1.0e9;
        for (std::size_t s = 0; s < lines.size(); ++s)
            for (const float drive : drives)
            {
                if (specs[s].levelDb != 0.0 && drive < 50.0f) continue;
                const double d = levelOf (voicingParams (i, drive), lines[s]) - levelOf (voicingParams (0, drive), lines[s]);
                lo = std::min (lo, d);
                hi = std::max (hi, d);
            }
        std::printf ("%2d %-16s trim now %5.1f  synthetic-line fit %5.1f  (residual +-%.2f dB)\n", static_cast<int> (i), voicings[i].name,
                     static_cast<double> (voicings[i].volume), static_cast<double> (voicings[i].volume) - 0.5 * (lo + hi),
                     0.5 * (hi - lo));
    }
}
