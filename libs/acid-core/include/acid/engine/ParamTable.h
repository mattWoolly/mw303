#pragma once
// The parameter table: the one definition of every host-visible parameter.
//
// The engine reads values from a ParamValues snapshot indexed by ParamId; the plugin
// builds its host parameters from the same rows; presets and MIDI maps store the string
// ids. Values are always in the natural units given here (percent, Hz, dB, ms...), never
// normalised 0..1. Mapping a percentage to a physical quantity (a knob law) belongs to the
// module that owns the physics, not to this table.
//
// The string ids are serialised in presets and host sessions. Once released, an id is
// never renamed or reused; new parameters are appended.
#include <array>
#include <cstdint>
#include <string_view>

namespace acid
{
enum class ParamKind : std::uint8_t { Float, Int, Bool, Choice };

/** How a UI or host should spread the range across a control. Values stay in natural units. */
enum class ParamScale : std::uint8_t { Linear, Log };

struct ParamInfo
{
    const char* id;       // stable snake_case id
    const char* name;     // display name
    const char* group;    // UI/host group
    ParamKind kind;
    float min, max, def;
    ParamScale scale;
    const char* unit;     // "" when none
    const char* choices;  // Choice only: labels separated by '|'
};

// X(Enum, id, name, group, kind, min, max, default, scale, unit, choices)
#define ACID_PARAM_TABLE(X) \
    /* ---- Synth: the front panel ------------------------------------------------------ */ \
    X(Wave,          "wave",           "Waveform",          "Synth", Choice, 0, 1, 0, Linear, "", "Saw|Square") \
    X(TuneExt,       "tune_ext",       "Tuning Ext Mode",   "Synth", Bool,   0, 1, 0, Linear, "", "") \
    X(Tune,          "tune",           "Tuning",            "Synth", Float, -12, 12, 0, Linear, "st", "") \
    X(TuneCoarse,    "tune_coarse",    "Tuning Coarse",     "Synth", Int,   -12, 12, 0, Linear, "st", "") \
    X(TuneFine,      "tune_fine",      "Tuning Fine",       "Synth", Float, -100, 100, 0, Linear, "ct", "") \
    X(Cutoff,        "cutoff",         "Cutoff",            "Synth", Float, 0, 100, 35, Linear, "%", "") \
    X(Resonance,     "resonance",      "Resonance",         "Synth", Float, 0, 100, 75, Linear, "%", "") \
    X(EnvMod,        "envmod",         "Env Mod",           "Synth", Float, 0, 100, 45, Linear, "%", "") \
    X(Decay,         "decay",          "Decay",             "Synth", Float, 0, 100, 40, Linear, "%", "") \
    X(Accent,        "accent",         "Accent",            "Synth", Float, 0, 100, 70, Linear, "%", "") \
    X(SlideTime,     "slide_time",     "Slide Time",        "Synth", Float, 0, 100, 50, Linear, "%", "") \
    X(SweepCouple,   "sweep_couple",   "Resonance/Sweep Couple", "Synth", Bool, 0, 1, 1, Linear, "", "") \
    X(SweepAmt,      "sweep_amt",      "Sweep Amount",      "Synth", Float, 0, 100, 100, Linear, "%", "") \
    X(EnvAttack,     "env_attack",     "Env Attack",        "Synth", Float, 0, 100, 0, Linear, "%", "") \
    X(AccDecay,      "acc_decay",      "Accent Decay",      "Synth", Float, 0, 100, 0, Linear, "%", "") \
    X(AccVolCouple,  "accvol_couple",  "Accent/Volume Couple", "Synth", Bool, 0, 1, 1, Linear, "", "") \
    X(AccVol,        "acc_vol",        "Accent Volume",     "Synth", Float, 0, 100, 100, Linear, "%", "") \
    X(VibSpeed,      "vib_speed",      "Vibrato Speed",     "Synth", Float, 0, 100, 50, Linear, "%", "") \
    X(VibDepth,      "vib_depth",      "Vibrato Depth",     "Synth", Float, 0, 100, 50, Linear, "%", "") \
    /* ---- Calibration: the trimmers under the cover ----------------------------------- */ \
    X(CalTm3,        "cal_tm3",        "TM3 Cutoff Trim",   "Calibration", Float, -500, 500, 0, Linear, "mV", "") \
    X(CalC21,        "cal_c21",        "C21 Coupling Cap",  "Calibration", Float, 7.9f, 31800, 15.9f, Log, "nF", "") \
    X(CalAccEnv,     "cal_accenv",     "Accent Filter Gain","Calibration", Float, 0, 4000, 1951, Linear, "mV", "") \
    X(CalAccAmp,     "cal_accamp",     "Accent Amp Gain",   "Calibration", Float, 0, 3.5f, 1.1f, Linear, "V", "") \
    X(CalC13,        "cal_c13",        "C13 Accent Cap",    "Calibration", Float, 0.33f, 4.7f, 1.0f, Log, "uF", "") \
    X(CalTm5,        "cal_tm5",        "TM5 Octave Width",  "Calibration", Float, 0.9f, 1.1f, 1.0f, Linear, "oct/V", "") \
    X(CalPw,         "cal_pw",         "Square PW Shift",   "Calibration", Float, -100, 100, 0, Linear, "%", "") \
    X(CalVcaClicks,  "cal_vca_clicks", "VCA Clicks",        "Calibration", Bool,  0, 1, 1, Linear, "", "") \
    X(CalVcaNoise,   "cal_vca_noise",  "VCA Noise",         "Calibration", Float, 0, 100, 0, Linear, "%", "") \
    X(CalRb,         "cal_rb",         "Ladder Load",       "Calibration", Float, 10, 35, 20.0f, Linear, "kOhm", "") \
    X(CalKmax,       "cal_kmax",       "Loop Gain",         "Calibration", Float, 17, 20, 18.7f, Linear, "", "") \
    X(CalCvScale,    "cal_cv_scale",   "Filter CV Scale",   "Calibration", Float, 0.75f, 1.05f, 0.88f, Linear, "", "") \
    X(CalGateDelay,  "cal_gate_delay", "Gate Delay",        "Calibration", Float, 0, 6, 5.4f, Linear, "ms", "") \
    X(CalDrift,      "cal_drift",      "Analog Drift",      "Calibration", Float, 0, 100, 10, Linear, "%", "") \
    X(CalCpuJitter,  "cal_cpu_jitter", "CPU Gate Jitter",   "Calibration", Bool,  0, 1, 0, Linear, "", "") \
    /* ---- Mods: circuit modifications beyond the stock unit --------------------------- */ \
    X(ModOn,         "mod_on",         "Mods Enable",       "Mods", Bool,  0, 1, 0, Linear, "", "") \
    X(ModOverdrive,  "mod_overdrive",  "Filter Overdrive",  "Mods", Float, 0, 100, 0, Linear, "%", "") \
    X(ModFilterFm,   "mod_filter_fm",  "Filter FM",         "Mods", Float, 0, 100, 0, Linear, "%", "") \
    X(ModTracking,   "mod_tracking",   "Filter Tracking",   "Mods", Float, 0, 100, 0, Linear, "%", "") \
    X(ModAmpDecay,   "mod_amp_decay",  "Amp Decay",         "Mods", Float, 7, 1500, 1500, Log, "ms", "") \
    X(ModAmpSustain, "mod_amp_sustain","Amp Sustain",       "Mods", Float, 0, 100, 0, Linear, "%", "") \
    X(ModSoftAttack, "mod_soft_attack","Soft Attack",       "Mods", Float, 0.3f, 30, 0.5f, Log, "ms", "") \
    X(ModHiRes,      "mod_hires",      "High Resonance",    "Mods", Bool,  0, 1, 0, Linear, "", "") \
    X(ModSweepSpeed, "mod_sweep_speed","Accent Sweep Speed","Mods", Choice, 0, 2, 0, Linear, "", "Normal|Fast|Slow") \
    X(ModMuffler,    "mod_muffler",    "Muffler",           "Mods", Choice, 0, 2, 0, Linear, "", "Off|Soft|Hard") \
    X(ModExtRange,   "mod_ext_range",  "Extended Ranges",   "Mods", Bool,  0, 1, 0, Linear, "", "") \
    X(ModSubLevel,   "mod_sub_level",  "Sub Osc Level",     "Mods", Float, 0, 100, 0, Linear, "%", "") \
    X(ModSubOct,     "mod_sub_oct",    "Sub Osc Octave",    "Mods", Choice, 0, 1, 0, Linear, "", "-1 Oct|-2 Oct") \
    /* ---- Distortion: front panel ------------------------------------------------------ */ \
    X(DistOn,        "dist_on",        "Distortion On",     "Distortion", Bool,  0, 1, 0, Linear, "", "") \
    X(DistModel,     "dist_model",     "Distortion Model",  "Distortion", Int,   0, 63, 1, Linear, "", "") \
    X(DistDynamics,  "dist_dynamics",  "Dynamics",          "Distortion", Float, 0, 100, 0, Linear, "%", "") \
    X(DistDynPost,   "dist_dyn_post",  "Dynamics Post",     "Distortion", Bool,  0, 1, 0, Linear, "", "") \
    X(DistDrive,     "dist_drive",     "Drive",             "Distortion", Float, 0, 100, 30, Linear, "%", "") \
    X(DistColor,     "dist_color",     "Color",             "Distortion", Float, -100, 100, 0, Linear, "%", "") \
    /* ---- Distortion: the voicing a model loads (editable as a custom model) ---------- */ \
    X(DistClipper,   "dist_clipper",   "Clipper",           "Distortion Voicing", Choice, 0, 7, 0, Linear, "", "Diode|Op-amp Hard|Overdrive|Stomp|Mid Hump|Fuzz|Console|Fold") \
    X(DistPreMin,    "dist_pre_min",   "Preamp Min",        "Distortion Voicing", Float, 0, 100, 0, Linear, "%", "") \
    X(DistPreMax,    "dist_pre_max",   "Preamp Max",        "Distortion Voicing", Float, 0, 100, 75, Linear, "%", "") \
    X(DistKnee,      "dist_knee",      "Knee",              "Distortion Voicing", Float, 0, 100, 100, Linear, "%", "") \
    X(DistBias,      "dist_bias",      "Bias",              "Distortion Voicing", Float, -100, 100, 0, Linear, "%", "") \
    X(DistLowKeep,   "dist_low_keep",  "Clean Low Band",    "Distortion Voicing", Float, 0, 300, 0, Linear, "Hz", "") \
    X(DistPreHp,     "dist_pre_hp",    "Pre High-pass",     "Distortion Voicing", Float, 0, 1000, 0, Linear, "Hz", "") \
    X(DistPreEqOn,   "dist_pre_eq_on", "Pre EQ On",         "Distortion Voicing", Bool,  0, 1, 0, Linear, "", "") \
    X(DistPreEqGain, "dist_pre_eq_gain","Pre EQ Gain",      "Distortion Voicing", Float, -24, 24, 0, Linear, "dB", "") \
    X(DistPreEqFreq, "dist_pre_eq_freq","Pre EQ Freq",      "Distortion Voicing", Float, 20, 1000, 200, Log, "Hz", "") \
    X(DistPreEqWidth,"dist_pre_eq_width","Pre EQ Width",    "Distortion Voicing", Float, 0.5f, 4, 1.5f, Linear, "oct", "") \
    X(DistTiltOn,    "dist_tilt_on",   "Tilt On",           "Distortion Voicing", Bool,  0, 1, 0, Linear, "", "") \
    X(DistTiltAmt,   "dist_tilt_amt",  "Tilt Amount",       "Distortion Voicing", Float, 0, 24, 0, Linear, "dB", "") \
    X(DistTiltFreq,  "dist_tilt_freq", "Tilt Freq",         "Distortion Voicing", Float, 100, 10000, 1000, Log, "Hz", "") \
    X(DistColorFreq, "dist_color_freq","Color Freq",        "Distortion Voicing", Float, 20, 20000, 2000, Log, "Hz", "") \
    X(DistColorReso, "dist_color_reso","Color Resonance",   "Distortion Voicing", Float, 0, 100, 20, Linear, "%", "") \
    X(DistColorRange,"dist_color_range","Color Range",      "Distortion Voicing", Float, 0, 100, 100, Linear, "%", "") \
    X(DistPostHp,    "dist_post_hp",   "Post High-pass",    "Distortion Voicing", Float, 0, 1000, 0, Linear, "Hz", "") \
    X(DistVolume,    "dist_volume",    "Distortion Trim",   "Distortion Voicing", Float, -60, 24, 0, Linear, "dB", "") \
    /* ---- EQ --------------------------------------------------------------------------- */ \
    X(EqOn,          "eq_on",          "EQ On",             "EQ", Bool,  0, 1, 0, Linear, "", "") \
    X(EqLowGain,     "eq_low_gain",    "EQ Low Gain",       "EQ", Float, -24, 24, 0, Linear, "dB", "") \
    X(EqLowFreq,     "eq_low_freq",    "EQ Low Freq",       "EQ", Float, 20, 16000, 100, Log, "Hz", "") \
    X(EqLowWidth,    "eq_low_width",   "EQ Low Width",      "EQ", Float, 0.5f, 4, 2, Linear, "oct", "") \
    X(EqLoMidGain,   "eq_lomid_gain",  "EQ Mid-Low Gain",   "EQ", Float, -24, 24, 0, Linear, "dB", "") \
    X(EqLoMidFreq,   "eq_lomid_freq",  "EQ Mid-Low Freq",   "EQ", Float, 20, 16000, 550, Log, "Hz", "") \
    X(EqLoMidWidth,  "eq_lomid_width", "EQ Mid-Low Width",  "EQ", Float, 0.5f, 4, 1.5f, Linear, "oct", "") \
    X(EqHiMidGain,   "eq_himid_gain",  "EQ Mid-High Gain",  "EQ", Float, -24, 24, 0, Linear, "dB", "") \
    X(EqHiMidFreq,   "eq_himid_freq",  "EQ Mid-High Freq",  "EQ", Float, 20, 16000, 3500, Log, "Hz", "") \
    X(EqHiMidWidth,  "eq_himid_width", "EQ Mid-High Width", "EQ", Float, 0.5f, 4, 2, Linear, "oct", "") \
    X(EqHighGain,    "eq_high_gain",   "EQ High Gain",      "EQ", Float, -24, 24, 0, Linear, "dB", "") \
    X(EqHighFreq,    "eq_high_freq",   "EQ High Freq",      "EQ", Float, 20, 16000, 10000, Log, "Hz", "") \
    X(EqHighWidth,   "eq_high_width",  "EQ High Width",     "EQ", Float, 0.5f, 4, 2, Linear, "oct", "") \
    /* ---- Delay ------------------------------------------------------------------------ */ \
    X(DelayOn,       "delay_on",       "Delay On",          "Delay", Bool,  0, 1, 0, Linear, "", "") \
    X(DelaySync,     "delay_sync",     "Delay Sync",        "Delay", Bool,  0, 1, 1, Linear, "", "") \
    X(DelayTime,     "delay_time",     "Delay Time",        "Delay", Float, 0.01f, 1, 0.2f, Log, "s", "") \
    X(DelayLNum,     "delay_l_num",    "Delay L Numerator", "Delay", Int,   1, 32, 3, Linear, "", "") \
    X(DelayLDen,     "delay_l_den",    "Delay L Denominator","Delay", Int,  1, 32, 16, Linear, "", "") \
    X(DelayLMod,     "delay_l_mod",    "Delay L Scale",     "Delay", Choice, 0, 2, 0, Linear, "", "Full|Dotted|Triplet") \
    X(DelayRNum,     "delay_r_num",    "Delay R Numerator", "Delay", Int,   1, 32, 3, Linear, "", "") \
    X(DelayRDen,     "delay_r_den",    "Delay R Denominator","Delay", Int,  1, 32, 16, Linear, "", "") \
    X(DelayRMod,     "delay_r_mod",    "Delay R Scale",     "Delay", Choice, 0, 2, 0, Linear, "", "Full|Dotted|Triplet") \
    X(DelayFeedback, "delay_feedback", "Delay Feedback",    "Delay", Float, 0, 100, 35, Linear, "%", "") \
    X(DelayFiltType, "delay_filt_type","Delay Filter Type", "Delay", Choice, 0, 3, 0, Linear, "", "Off|LP|BP|HP") \
    X(DelayFiltCutoff,"delay_filt_cutoff","Delay Filter Cutoff","Delay", Float, 10, 16000, 8000, Log, "Hz", "") \
    X(DelayFiltReso, "delay_filt_reso","Delay Filter Reso", "Delay", Float, 0, 100, 0, Linear, "%", "") \
    X(DelaySpread,   "delay_spread",   "Delay Stereo Spread","Delay", Float, -100, 100, 0, Linear, "%", "") \
    X(DelayMix,      "delay_mix",      "Delay Mix",         "Delay", Float, 0, 100, 30, Linear, "%", "") \
    /* ---- Reverb ----------------------------------------------------------------------- */ \
    X(RevOn,         "rev_on",         "Reverb On",         "Reverb", Bool,  0, 1, 0, Linear, "", "") \
    X(RevPreDelay,   "rev_predelay",   "Reverb Pre-delay",  "Reverb", Float, 0, 1000, 0, Linear, "ms", "") \
    X(RevEarlySize,  "rev_early_size", "Reverb Early Size", "Reverb", Float, 1.1f, 20.5f, 10.4f, Linear, "m", "") \
    X(RevLateSize,   "rev_late_size",  "Reverb Late Size",  "Reverb", Float, 5.1f, 51.1f, 27.9f, Linear, "m", "") \
    X(RevEarlyDiff,  "rev_early_diff", "Reverb Early Diffusion","Reverb", Float, 0, 100, 50, Linear, "%", "") \
    X(RevLateDiff,   "rev_late_diff",  "Reverb Late Diffusion","Reverb", Float, 0, 100, 50, Linear, "%", "") \
    X(RevEarlyBright,"rev_early_bright","Reverb Early Brightness","Reverb", Float, 0, 100, 50, Linear, "%", "") \
    X(RevLateBright, "rev_late_bright","Reverb Late Brightness","Reverb", Float, 0, 100, 50, Linear, "%", "") \
    X(RevFeedback,   "rev_feedback",   "Reverb Feedback",   "Reverb", Float, 0, 100, 30, Linear, "%", "") \
    X(RevEarlyLate,  "rev_early_late", "Reverb Early/Late", "Reverb", Float, 0, 100, 50, Linear, "%", "") \
    X(RevMod,        "rev_mod",        "Reverb Tail Mod",   "Reverb", Float, 0, 100, 10, Linear, "%", "") \
    X(RevMix,        "rev_mix",        "Reverb Mix",        "Reverb", Float, 0, 100, 25, Linear, "%", "") \
    /* ---- Chorus ----------------------------------------------------------------------- */ \
    X(ChoOn,         "cho_on",         "Chorus On",         "Chorus", Bool,  0, 1, 0, Linear, "", "") \
    X(ChoOffset,     "cho_offset",     "Chorus Offset",     "Chorus", Float, 0, 100, 0, Linear, "%", "") \
    X(ChoFatness,    "cho_fatness",    "Chorus Fatness",    "Chorus", Float, 0, 100, 0, Linear, "%", "") \
    X(ChoHp,         "cho_hp",         "Chorus High-pass",  "Chorus", Float, 0, 100, 0, Linear, "%", "") \
    X(ChoVolume,     "cho_volume",     "Chorus Volume",     "Chorus", Float, -60, 12, 0, Linear, "dB", "") \
    X(ChoDepth,      "cho_depth",      "Chorus Depth",      "Chorus", Float, 0, 100, 35, Linear, "%", "") \
    X(ChoRate,       "cho_rate",       "Chorus Rate",       "Chorus", Float, 0, 100, 35, Linear, "%", "") \
    X(ChoSpatial,    "cho_spatial",    "Chorus Spatial",    "Chorus", Float, 0, 100, 50, Linear, "%", "") \
    X(ChoMix,        "cho_mix",        "Chorus Mix",        "Chorus", Float, 0, 100, 50, Linear, "%", "") \
    /* ---- Limiter ---------------------------------------------------------------------- */ \
    X(LimOn,         "lim_on",         "Limiter On",        "Limiter", Bool,  0, 1, 0, Linear, "", "") \
    X(LimKnee,       "lim_knee",       "Limiter Knee",      "Limiter", Float, 0, 100, 100, Linear, "%", "") \
    X(LimRatio,      "lim_ratio",      "Limiter Ratio",     "Limiter", Float, 2, 40, 40, Log, ":1", "") \
    X(LimAttack,     "lim_attack",     "Limiter Attack",    "Limiter", Float, 0.1f, 100, 1, Log, "ms", "") \
    X(LimRelease,    "lim_release",    "Limiter Release",   "Limiter", Float, 25, 500, 150, Log, "ms", "") \
    X(LimThreshold,  "lim_threshold",  "Limiter Threshold", "Limiter", Float, -48, 0, 0, Linear, "dB", "") \
    X(LimGain,       "lim_gain",       "Limiter Gain",      "Limiter", Float, 0, 24, 0, Linear, "dB", "") \
    X(LimSoftClip,   "lim_soft_clip",  "Limiter Soft Clip", "Limiter", Bool,  0, 1, 0, Linear, "", "") \
    /* ---- Master ----------------------------------------------------------------------- */ \
    X(MasterVolume,  "master_volume",  "Output Volume",     "Master", Float, -60, 12, 0, Linear, "dB", "") \
    X(FxBypass,      "fx_bypass",      "Bypass FX",         "Master", Bool,  0, 1, 0, Linear, "", "") \
    /* ---- Performance: play mode, pattern selection, live overrides ------------------- */ \
    X(PlayMode,      "play_mode",      "Play Mode",         "Performance", Choice, 0, 2, 1, Linear, "", "EXT|SEQ|ARP") \
    X(SeqRun,        "seq_run",        "Run",               "Performance", Bool,  0, 1, 0, Linear, "", "") \
    X(PatternBank,   "pattern_bank",   "Pattern Bank",      "Performance", Choice, 0, 3, 0, Linear, "", "A|B|C|D") \
    X(PatternSlot,   "pattern_slot",   "Pattern",           "Performance", Int,   1, 12, 1, Linear, "", "") \
    X(LiveTranspose, "live_transpose", "Live Transpose",    "Performance", Int,   -24, 24, 0, Linear, "st", "") \
    X(OvrNoteSet,    "ovr_note_set",   "Override Note Set", "Performance", Bool,  0, 1, 0, Linear, "", "") \
    X(OvrNoteClr,    "ovr_note_clr",   "Override Note Clear","Performance", Bool, 0, 1, 0, Linear, "", "") \
    X(OvrSlideSet,   "ovr_slide_set",  "Override Slide Set","Performance", Bool,  0, 1, 0, Linear, "", "") \
    X(OvrSlideClr,   "ovr_slide_clr",  "Override Slide Clear","Performance", Bool, 0, 1, 0, Linear, "", "") \
    X(OvrAccentSet,  "ovr_accent_set", "Override Accent Set","Performance", Bool, 0, 1, 0, Linear, "", "") \
    X(OvrAccentClr,  "ovr_accent_clr", "Override Accent Clear","Performance", Bool, 0, 1, 0, Linear, "", "") \
    X(OvrVibSet,     "ovr_vib_set",    "Override Vibrato Set","Performance", Bool, 0, 1, 0, Linear, "", "") \
    X(OvrVibClr,     "ovr_vib_clr",    "Override Vibrato Clear","Performance", Bool, 0, 1, 0, Linear, "", "")

enum ParamId : int
{
#define ACID_PARAM_ENUM(Enum, ...) k##Enum,
    ACID_PARAM_TABLE (ACID_PARAM_ENUM)
#undef ACID_PARAM_ENUM
    kNumParams
};

inline constexpr std::array<ParamInfo, kNumParams> kParamTable { {
#define ACID_PARAM_ROW(Enum, id, name, group, kind, min, max, def, scale, unit, choices) \
    ParamInfo { id, name, group, ParamKind::kind, static_cast<float> (min), static_cast<float> (max), \
                static_cast<float> (def), ParamScale::scale, unit, choices },
    ACID_PARAM_TABLE (ACID_PARAM_ROW)
#undef ACID_PARAM_ROW
} };

/** One value per parameter, in natural units. Bool: 0 or 1. Choice/Int: the integer as a float. */
using ParamValues = std::array<float, kNumParams>;

constexpr ParamValues defaultParamValues() noexcept
{
    ParamValues v {};
    for (int i = 0; i < kNumParams; ++i)
        v[static_cast<std::size_t> (i)] = kParamTable[static_cast<std::size_t> (i)].def;
    return v;
}

constexpr const ParamInfo& paramInfo (ParamId id) noexcept { return kParamTable[static_cast<std::size_t> (id)]; }

/** Looks a parameter up by its string id. Returns -1 when unknown. */
constexpr int findParam (std::string_view id) noexcept
{
    for (int i = 0; i < kNumParams; ++i)
        if (id == kParamTable[static_cast<std::size_t> (i)].id)
            return i;
    return -1;
}

constexpr bool paramAsBool (const ParamValues& v, ParamId id) noexcept { return v[static_cast<std::size_t> (id)] >= 0.5f; }
constexpr int paramAsInt (const ParamValues& v, ParamId id) noexcept
{
    const float f = v[static_cast<std::size_t> (id)];
    return static_cast<int> (f < 0.0f ? f - 0.5f : f + 0.5f);
}
}
