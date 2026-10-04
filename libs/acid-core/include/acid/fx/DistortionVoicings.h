#pragma once
// The distortion voicing table. A voicing is a named set of values for the Distortion
// Voicing parameters of ParamTable.h; choosing one copies those values into the
// parameters, after which every one of them can still be edited (the "Custom" model).
#include "acid/engine/ParamTable.h"
#include "acid/fx/Distortion.h"
#include <span>

namespace acid::fx
{
/** One value per Distortion Voicing parameter, in the units of ParamTable.h. */
struct DistortionVoicing
{
    const char* name;
    int clipper;                // dist_clipper
    float preMin, preMax;       // dist_pre_min, dist_pre_max, %
    float knee;                 // dist_knee, %
    float bias;                 // dist_bias, %
    float lowKeep;              // dist_low_keep, Hz
    float preHp;                // dist_pre_hp, Hz
    bool preEqOn;               // dist_pre_eq_on
    float preEqGain;            // dist_pre_eq_gain, dB
    float preEqFreq;            // dist_pre_eq_freq, Hz
    float preEqWidth;           // dist_pre_eq_width, octaves
    bool tiltOn;                // dist_tilt_on
    float tiltAmt;              // dist_tilt_amt, dB
    float tiltFreq;             // dist_tilt_freq, Hz
    float colorFreq;            // dist_color_freq, Hz
    float colorReso;            // dist_color_reso, %
    float colorRange;           // dist_color_range, %
    float postHp;               // dist_post_hp, Hz
    float volume;               // dist_volume, dB
};

/** Index 0 is "Custom": a neutral starting point equal to the parameter defaults. The
    rest are the factory voicings. The indices are what `dist_model` stores, so entries are
    only ever appended. */
std::span<const DistortionVoicing> distortionVoicings() noexcept;

/** Copies a voicing into the voicing fields of a parameter struct. The front-panel fields
    (on, model, dynamics, dynPost, drive, color) are left alone. */
void applyVoicing (DistortionParams&, const DistortionVoicing&) noexcept;

/** Copies a voicing into the Distortion Voicing rows of a parameter snapshot: what a
    plugin does when the user picks a model. */
void writeVoicing (ParamValues&, const DistortionVoicing&) noexcept;
}
