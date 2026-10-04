#include "acid/fx/DistortionVoicings.h"

namespace acid::fx
{
namespace
{
constexpr int kDiode = static_cast<int> (ClipperFamily::DiodePair);
constexpr int kOpAmp = static_cast<int> (ClipperFamily::OpAmpHard);
constexpr int kAsym = static_cast<int> (ClipperFamily::AsymSoft);
constexpr int kScoop = static_cast<int> (ClipperFamily::ScoopHard);
constexpr int kHump = static_cast<int> (ClipperFamily::MidHump);
constexpr int kFuzz = static_cast<int> (ClipperFamily::GatedFuzz);
constexpr int kDesk = static_cast<int> (ClipperFamily::Console);
constexpr int kFold = static_cast<int> (ClipperFamily::Fold);

// The trims in the last column were set by measurement on the instrument's own output
// (tests/engine/VoicingLevelTests.cpp, "voicing level: trim calibration on real voice
// lines", run with acid_engine_tests "[.calibrate]" and CAL_GRID=core): four voice lines
// rendered through the Engine (the first-load line, a classic accented line, an all-accent
// line, a sparse low line), cutoff 40 to 60, resonance 60 to 80, drive 25 to 75, each
// voicing picked as the plugin picks it (writeVoicing), in BS.1770 gated loudness. Each
// trim puts the middle of its voicing's spread against "Custom" on Custom. The block's own
// family make-up (Distortion.cpp) does the drive-dependent part, so the trims carry what the
// voicing's filters (and its family, on a real line) do to the level. On that grid the fit
// leaves at most +-2.3 LU, except Thin Air (+-3.9), Grind Tight (+-3.6) and Nasal (+-2.8):
// a trim is one number, and what a 950 Hz pre high-pass or a +22 dB tilt takes away depends
// on how dark the line is and how hard the clipper is driven. Further out (drive 0, cutoff
// 20 or 80, resonance 30 or 95) those voicings drift further; tests/engine holds the result
// on lines and settings the fit never saw. An earlier fit on synthetic bass lines left
// voicings up to 11 LU from Custom on the real voice. Re-run after any change to a clipper
// model, the family make-up, the voice or a voicing, then re-check the factory distortion
// presets (tools/factory/qa.py), which compensate on top of these.
//
// name, clipper, preMin, preMax, knee, bias, lowKeep, preHp,
//   preEq on/gain/freq/width, tilt on/amount/freq, color freq/reso/range, postHp, volume
constexpr DistortionVoicing kVoicings[] = {
    { "Custom",          kDiode,  0.0f,  75.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 20.0f, 100.0f,   0.0f,  0.0f },

    // ---- Diode pair: from barely there to a whistling lead ----
    { "Glass",           kDiode,  0.0f,  72.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2200.0f, 40.0f, 100.0f,   0.0f,      0.0f },
    { "Glass Low Lift",  kDiode,  4.0f,  66.0f, 100.0f,   0.0f,   0.0f,  45.0f,
      true,   3.5f, 160.0f, 1.9f,  false,  0.0f, 1000.0f,  3600.0f, 20.0f, 100.0f,  18.0f,      -0.5f },
    { "Glass Tight",     kDiode,  0.0f,  74.0f,  80.0f,   0.0f,   0.0f,  90.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 45.0f, 100.0f,  55.0f,      1.0f },
    { "Mid Fist",        kDiode,  0.0f,  65.0f,  70.0f,   0.0f,   0.0f,   0.0f,
      true,   6.0f, 380.0f, 3.0f,  true,   5.0f,  430.0f,  3200.0f, 35.0f, 100.0f,  25.0f, 0.4f },
    { "Lopsided",        kDiode,  0.0f,  66.0f, 100.0f, -14.0f,   0.0f,  70.0f,
      true,   8.0f, 105.0f, 1.3f,  true,   5.0f, 1000.0f,  2200.0f, 18.0f,  82.0f, 240.0f,      7.2f },
    { "Tin Whistle",     kDiode,  6.0f,  64.0f,  45.0f,   0.0f,   0.0f,   0.0f,
      true,   6.5f, 175.0f, 1.5f,  true,  22.0f,  680.0f,  2500.0f, 20.0f, 100.0f, 300.0f, 5.7f },
    { "Thin Air",        kDiode,  3.0f,  82.0f, 100.0f,   0.0f,   0.0f, 950.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,   800.0f,  5.0f, 100.0f,   0.0f,      7.0f },

    // ---- Op-amp into hard diodes ----
    { "Grind",           kOpAmp,  0.0f,  75.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 20.0f, 100.0f,   0.0f,    2.6f },
    { "Grind Tight",     kOpAmp,  5.0f,  72.0f,  50.0f,   0.0f,   0.0f, 100.0f,
      true,  -5.0f, 550.0f, 1.5f,  true,   7.5f,  290.0f,  2600.0f, 20.0f, 100.0f,  25.0f,  2.2f },
    { "Grind Deep",      kOpAmp, 10.0f,  85.0f,  80.0f,   0.0f, 110.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  1100.0f, 50.0f, 100.0f,   0.0f,      1.0f },

    // ---- Asymmetric soft overdrive ----
    { "Amber",           kAsym,   4.0f,  92.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 20.0f, 100.0f,   0.0f,     0.7f },
    { "Amber Shade",     kAsym,  30.0f, 100.0f, 100.0f,   0.0f,   0.0f, 215.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,   260.0f, 66.0f,  55.0f,  45.0f,     2.4f },
    { "Grit Ramp",       kAsym,  10.0f, 100.0f,  50.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  true,   3.0f,  575.0f,  1600.0f, 25.0f, 100.0f,   0.0f,   1.8f },
    { "Leaning",         kAsym,   9.0f,  82.0f, 100.0f, -35.0f,   0.0f,  90.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  3100.0f, 26.0f, 100.0f, 105.0f,      2.1f },
    { "Amber Tilt",      kAsym,   6.0f,  95.0f,  60.0f,   0.0f,   0.0f,  80.0f,
      true,   3.5f, 127.0f, 1.25f, true,  11.0f, 1000.0f,  3900.0f, 20.0f, 100.0f,   0.0f,      3.8f },

    // ---- Hard clip with the mid scoop ----
    { "Hollow",          kScoop,  0.0f,  80.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 20.0f, 100.0f,   0.0f,      0.6f },
    { "Hollow Anchor",   kScoop,  5.0f,  85.0f,  80.0f,   0.0f, 120.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2400.0f, 30.0f, 100.0f,   0.0f,      -0.9f },
    { "Razor Scoop",     kScoop, 10.0f,  90.0f,  30.0f,   0.0f,   0.0f,  60.0f,
      false,  0.0f, 200.0f, 1.5f,  true,   9.0f, 1400.0f,  4200.0f, 35.0f, 100.0f,  40.0f,      2.9f },

    // ---- Mid-hump soft clipper ----
    { "Honk",            kHump,   0.0f,  68.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 20.0f, 100.0f,   0.0f,      2.5f },
    { "Honk Push",       kHump,  10.0f,  80.0f,  70.0f,   0.0f,   0.0f,  40.0f,
      true,   5.0f, 700.0f, 2.0f,  false,  0.0f, 1000.0f,  2800.0f, 30.0f, 100.0f,   0.0f,  -0.6f },
    { "Nasal",           kHump,   8.0f,  75.0f,  40.0f,   8.0f,   0.0f, 120.0f,
      false,  0.0f, 200.0f, 1.5f,  true,   4.0f,  900.0f,  1200.0f, 60.0f,  80.0f,  60.0f,  1.4f },

    // ---- Gated fuzz ----
    { "Rip Seam",        kFuzz,  28.0f,  70.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 20.0f, 100.0f,   0.0f,   2.0f },
    { "Sputter",         kFuzz,  30.0f,  60.0f,  40.0f, -25.0f,   0.0f,  60.0f,
      false,  0.0f, 200.0f, 1.5f,  true,   4.0f,  800.0f,  3000.0f, 25.0f, 100.0f,  30.0f,      0.6f },
    { "Thick Fur",       kFuzz,  36.0f,  80.0f, 100.0f,  40.0f, 100.0f,   0.0f,
      true,   4.0f, 250.0f, 2.5f,  false,  0.0f, 1000.0f,  1500.0f, 35.0f, 100.0f,   0.0f,     0.4f },

    // ---- Console input stage ----
    { "Desk Warm",       kDesk,   0.0f,  45.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 10.0f, 100.0f,   0.0f,      -0.4f },
    { "Desk Red",        kDesk,  12.0f,  70.0f,  55.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  true,   2.0f,  900.0f,  3000.0f, 15.0f, 100.0f,   0.0f,     0.9f },
    { "Iron Sub",        kDesk,   6.0f,  55.0f, 100.0f,  10.0f,   0.0f,   0.0f,
      true,   5.0f,  70.0f, 1.6f,  false,  0.0f, 1000.0f,  1800.0f, 10.0f, 100.0f,   0.0f,      -1.1f },

    // ---- Wavefolder ----
    { "Paper Fold",      kFold,   0.0f,  42.0f, 100.0f,   0.0f,   0.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  2000.0f, 20.0f, 100.0f,   0.0f,    -0.3f },
    { "Paper Cut",       kFold,   5.0f,  45.0f,  30.0f,   0.0f,   0.0f, 110.0f,
      false,  0.0f, 200.0f, 1.5f,  true,   6.0f, 1200.0f,  4500.0f, 25.0f, 100.0f,  50.0f,    0.9f },
    { "Crease Anchor",   kFold,   6.0f,  48.0f,  80.0f,  12.0f, 150.0f,   0.0f,
      false,  0.0f, 200.0f, 1.5f,  false,  0.0f, 1000.0f,  1600.0f, 40.0f, 100.0f,   0.0f,      -1.3f },

    // ---- Wrecked ----
    { "Scrapyard",       kOpAmp, 60.0f, 100.0f,   0.0f,  10.0f,   0.0f,  50.0f,
      true,   8.0f, 900.0f, 2.0f,  true,   6.0f,  700.0f,  3000.0f, 55.0f, 100.0f,  40.0f, -0.2f },
    { "Meltdown",        kFold,  40.0f,  75.0f,   0.0f,  20.0f,   0.0f,   0.0f,
      true,   6.0f, 300.0f, 3.0f,  false,  0.0f, 1000.0f,  2400.0f, 45.0f, 100.0f,  30.0f, -1.7f },
};
}

std::span<const DistortionVoicing> distortionVoicings() noexcept
{
    return kVoicings;
}

void applyVoicing (DistortionParams& p, const DistortionVoicing& v) noexcept
{
    p.clipper = v.clipper;
    p.preMin = v.preMin;
    p.preMax = v.preMax;
    p.knee = v.knee;
    p.bias = v.bias;
    p.lowKeep = v.lowKeep;
    p.preHp = v.preHp;
    p.preEqOn = v.preEqOn;
    p.preEqGain = v.preEqGain;
    p.preEqFreq = v.preEqFreq;
    p.preEqWidth = v.preEqWidth;
    p.tiltOn = v.tiltOn;
    p.tiltAmt = v.tiltAmt;
    p.tiltFreq = v.tiltFreq;
    p.colorFreq = v.colorFreq;
    p.colorReso = v.colorReso;
    p.colorRange = v.colorRange;
    p.postHp = v.postHp;
    p.volume = v.volume;
}

void writeVoicing (ParamValues& values, const DistortionVoicing& v) noexcept
{
    const auto set = [&values] (ParamId id, float x) { values[static_cast<std::size_t> (id)] = x; };
    set (kDistClipper, static_cast<float> (v.clipper));
    set (kDistPreMin, v.preMin);
    set (kDistPreMax, v.preMax);
    set (kDistKnee, v.knee);
    set (kDistBias, v.bias);
    set (kDistLowKeep, v.lowKeep);
    set (kDistPreHp, v.preHp);
    set (kDistPreEqOn, v.preEqOn ? 1.0f : 0.0f);
    set (kDistPreEqGain, v.preEqGain);
    set (kDistPreEqFreq, v.preEqFreq);
    set (kDistPreEqWidth, v.preEqWidth);
    set (kDistTiltOn, v.tiltOn ? 1.0f : 0.0f);
    set (kDistTiltAmt, v.tiltAmt);
    set (kDistTiltFreq, v.tiltFreq);
    set (kDistColorFreq, v.colorFreq);
    set (kDistColorReso, v.colorReso);
    set (kDistColorRange, v.colorRange);
    set (kDistPostHp, v.postHp);
    set (kDistVolume, v.volume);
}
}
