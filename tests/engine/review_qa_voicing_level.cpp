// QA review (sound): the distortion voicings are documented as level-matched to "Custom"
// (DistortionVoicings.cpp: +-2.6 dB on the calibration lines; tests/dist holds them within
// 2.5 dB), but those lines are synthetic. Played by the real voice, several voicings come
// out 6 to 10 dB quieter than Custom at the same drive, so stepping through the Distortion
// Model list during playback jumps in level by up to 11 dB. Fails until the trims are refit
// on the instrument's own output.
#include "EngineTestSupport.h"
#include "acid/fx/DistortionVoicings.h"
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using namespace etest;

namespace
{
/** RMS in dB of two bars of a classic line at 125 BPM, voice at its defaults except the
    filter (cutoff 50, resonance 70, Env Mod 55), distortion on at drive 50 with the given
    voicing, every other effect off. The first beat is skipped. */
double lineLevelDb (int model)
{
    Session s;
    s.setPattern (0, "C:A C+:A C:AS C+ ~ . D#:S D#+:A C . G-:AS G C+:A C+:A A#-:S C");
    s.params[kSeqRun] = 1.0f;
    s.params[kCutoff] = 50.0f;
    s.params[kResonance] = 70.0f;
    s.params[kEnvMod] = 55.0f;
    s.params[kDistOn] = 1.0f;
    s.params[kDistDrive] = 50.0f;
    s.params[kDistModel] = static_cast<float> (model);
    fx::writeVoicing (s.params, fx::distortionVoicings()[static_cast<std::size_t> (model)]);
    s.host.bpm = 125.0;
    s.host.play();
    s.prepare (kFs, 512);
    const auto total = static_cast<std::int64_t> (2 * 4 * 60.0 / 125.0 * kFs);
    const Rendered r = s.render (total, 512);
    double sum = 0.0;
    const auto from = static_cast<std::size_t> (60.0 / 125.0 * kFs);
    for (std::size_t i = from; i < r.left.size(); ++i)
        sum += static_cast<double> (r.left[i]) * static_cast<double> (r.left[i]);
    return 10.0 * std::log10 (sum / static_cast<double> (r.left.size() - from) + 1.0e-30);
}
}

TEST_CASE ("review_qa: voicings stay level-matched to Custom on the real voice", "[engine][review_qa]")
{
    const double custom = lineLevelDb (0);
    const auto voicings = fx::distortionVoicings();
    // Measured before the fix (dB against Custom): Tin Whistle -11.5, Lopsided -7.6,
    // Grind Tight -6.6, Thin Air -6.7; Glass and Desk Warm pass.
    for (const char* name : { "Tin Whistle", "Lopsided", "Grind Tight", "Thin Air", "Glass", "Desk Warm" })
    {
        int index = -1;
        for (std::size_t i = 0; i < voicings.size(); ++i)
            if (std::string (voicings[i].name) == name) index = static_cast<int> (i);
        REQUIRE (index > 0);
        const double off = lineLevelDb (index) - custom;
        INFO (name << ": " << off << " dB against Custom");
        CHECK (std::abs (off) <= 3.0);
    }
}
