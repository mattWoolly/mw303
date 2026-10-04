#include "editor/ParamHelp.h"

#include <string_view>
#include <utility>

namespace acid::ui
{
namespace
{
using Help = std::pair<std::string_view, const char*>;

// Keyed by the parameter's string id. Short: a tooltip is read in passing.
constexpr Help kHelp[] = {
    { "wave", "Oscillator waveform, saw or square." },
    { "tune_ext", "Extended tuning: swaps the tuning knob for a semitone transpose and a fine trim." },
    { "tune", "Pitch of the whole voice, plus or minus an octave." },
    { "tune_coarse", "Transpose in semitones (extended tuning)." },
    { "tune_fine", "Fine tuning in cents (extended tuning)." },
    { "cutoff", "Filter cutoff frequency." },
    { "resonance", "Filter resonance. With the amber couple LEDs on, it also sets how far an accent sweeps the filter." },
    { "envmod", "How far the envelope opens the filter." },
    { "decay", "Envelope decay of unaccented notes (accented notes always decay fast)." },
    { "accent", "Accent strength. With the cyan couple LEDs on, it also sets the accent's loudness." },
    { "slide_time", "Glide time of a slide (50 % is the original 22 ms)." },
    { "sweep_couple", "Amber couple: on, RESONANCE also sets the accent sweep and the SWEEP knob is out of use. Click to uncouple." },
    { "sweep_amt", "Accent sweep amount. Active only with the amber couple LEDs off; on, RESONANCE sets it." },
    { "env_attack", "Envelope attack." },
    { "acc_decay", "Decay of accented notes." },
    { "accvol_couple", "Cyan couple: on, ACCENT drives both the filter and the loudness accent and VOLUME is out of use. Click to uncouple." },
    { "acc_vol", "Loudness of accented notes. Active only with the cyan couple LEDs off; on, ACCENT sets it." },
    { "vib_speed", "Vibrato rate (steps flagged VIB)." },
    { "vib_depth", "Vibrato depth (steps flagged VIB)." },

    { "cal_tm3", "Service trimmer: filter cutoff offset of this unit." },
    { "cal_c21", "Coupling capacitor at the filter: shapes the low end." },
    { "cal_accenv", "Accent's push on the filter for this unit." },
    { "cal_accamp", "Accent's push on the amplifier for this unit." },
    { "cal_c13", "Accent capacitor: how accents stack up over consecutive accented notes." },
    { "cal_tm5", "Octave width of the oscillator's pitch scaling." },
    { "cal_pw", "Pulse width offset of the square wave." },
    { "cal_vca_clicks", "Amplifier clicks at note starts, as on worn units." },
    { "cal_vca_noise", "Amplifier noise floor." },
    { "cal_rb", "Load on the filter ladder: resonance character." },
    { "cal_kmax", "Filter loop gain: how hard resonance can push." },
    { "cal_cv_scale", "Filter control voltage scaling." },
    { "cal_gate_delay", "Gate delay of this unit." },
    { "cal_drift", "Slow analog drift of pitch and filter." },
    { "cal_cpu_jitter", "Timing jitter of the original's processor on gates." },

    { "mod_on", "Switches the circuit mods on. Off, the voice is stock and the mods are greyed." },
    { "mod_overdrive", "Drives the filter input into saturation." },
    { "mod_filter_fm", "Oscillator modulating the filter frequency." },
    { "mod_tracking", "Filter cutoff follows the note pitch." },
    { "mod_amp_decay", "Amplifier decay time." },
    { "mod_amp_sustain", "Amplifier sustain level." },
    { "mod_soft_attack", "Softens the start of each note." },
    { "mod_hires", "Higher resonance, up to self-oscillation." },
    { "mod_sweep_speed", "Speed of the accent sweep." },
    { "mod_muffler", "Soft clipping after the amplifier." },
    { "mod_ext_range", "Wider ranges for cutoff, decay and env mod." },
    { "mod_sub_level", "Level of the sub oscillator." },
    { "mod_sub_oct", "Sub oscillator one or two octaves down." },

    { "dist_on", "Distortion on or off." },
    { "dist_model", "Distortion voicing: a pedal-style character. Editing its voicing turns it into Custom." },
    { "dist_dynamics", "How much playing dynamics change the drive." },
    { "dist_dyn_post", "Dynamics before (PRE) or after (POST) the clipper." },
    { "dist_drive", "Distortion drive." },
    { "dist_color", "Tone of the distortion." },
    { "dist_clipper", "Clipping circuit of the voicing." },
    { "dist_pre_min", "Preamp gain at drive zero." },
    { "dist_pre_max", "Preamp gain at full drive." },
    { "dist_knee", "Softness of the clipping." },
    { "dist_bias", "Clipping asymmetry." },
    { "dist_low_keep", "Low band kept clean under the distortion." },
    { "dist_pre_hp", "High-pass before the clipper." },
    { "dist_pre_eq_on", "Mid boost before the clipper on or off." },
    { "dist_pre_eq_gain", "Gain of the pre-clipper EQ." },
    { "dist_pre_eq_freq", "Frequency of the pre-clipper EQ." },
    { "dist_pre_eq_width", "Width of the pre-clipper EQ." },
    { "dist_tilt_on", "Tilt EQ after the clipper on or off." },
    { "dist_tilt_amt", "Tilt EQ amount." },
    { "dist_tilt_freq", "Tilt EQ pivot frequency." },
    { "dist_color_freq", "Centre frequency of the colour filter." },
    { "dist_color_reso", "Resonance of the colour filter." },
    { "dist_color_range", "Range the COLOUR knob sweeps." },
    { "dist_post_hp", "High-pass after the clipper." },
    { "dist_volume", "Output trim of the distortion." },

    { "eq_on", "EQ on or off." },
    { "eq_low_gain", "Low band gain." },
    { "eq_low_freq", "Low band frequency." },
    { "eq_low_width", "Low band width." },
    { "eq_lomid_gain", "Low-mid band gain." },
    { "eq_lomid_freq", "Low-mid band frequency." },
    { "eq_lomid_width", "Low-mid band width." },
    { "eq_himid_gain", "High-mid band gain." },
    { "eq_himid_freq", "High-mid band frequency." },
    { "eq_himid_width", "High-mid band width." },
    { "eq_high_gain", "High band gain." },
    { "eq_high_freq", "High band frequency." },
    { "eq_high_width", "High band width." },

    { "delay_on", "Delay on or off. The tab turns amber when a synced time is longer than the delay line holds." },
    { "delay_sync", "Delay time in note values of the tempo, or free time." },
    { "delay_time", "Free delay time." },
    { "delay_l_num", "Left delay: beats (numerator)." },
    { "delay_l_den", "Left delay: note value (denominator)." },
    { "delay_l_mod", "Left delay: straight, dotted or triplet." },
    { "delay_r_num", "Right delay: beats (numerator)." },
    { "delay_r_den", "Right delay: note value (denominator)." },
    { "delay_r_mod", "Right delay: straight, dotted or triplet." },
    { "delay_feedback", "Delay repeats." },
    { "delay_filt_type", "Filter in the delay's feedback." },
    { "delay_filt_cutoff", "Cutoff of the feedback filter." },
    { "delay_filt_reso", "Resonance of the feedback filter." },
    { "delay_spread", "Stereo spread of the repeats." },
    { "delay_mix", "Delay level against the dry signal." },

    { "rev_on", "Reverb on or off." },
    { "rev_predelay", "Gap before the reverb starts." },
    { "rev_early_size", "Size of the early reflections." },
    { "rev_late_size", "Size of the tail." },
    { "rev_early_diff", "Diffusion of the early reflections." },
    { "rev_late_diff", "Diffusion of the tail." },
    { "rev_early_bright", "Brightness of the early reflections." },
    { "rev_late_bright", "Brightness of the tail." },
    { "rev_feedback", "Length of the tail." },
    { "rev_early_late", "Balance of early reflections against the tail." },
    { "rev_mod", "Movement in the tail." },
    { "rev_mix", "Reverb level against the dry signal." },

    { "cho_on", "Chorus on or off." },
    { "cho_offset", "Delay offset of the chorus voices." },
    { "cho_fatness", "Thickness of the chorus." },
    { "cho_hp", "Keeps the low end out of the chorus." },
    { "cho_volume", "Chorus output level." },
    { "cho_depth", "Chorus modulation depth." },
    { "cho_rate", "Chorus modulation rate." },
    { "cho_spatial", "Stereo width of the chorus." },
    { "cho_mix", "Chorus level against the dry signal." },

    { "lim_on", "Limiter on or off." },
    { "lim_knee", "Softness of the limiter's knee." },
    { "lim_ratio", "Limiter ratio." },
    { "lim_attack", "Limiter attack." },
    { "lim_release", "Limiter release." },
    { "lim_threshold", "Level where limiting starts." },
    { "lim_gain", "Gain after the limiter." },
    { "lim_soft_clip", "Soft clipping after the limiter." },

    { "master_volume", "Output volume." },
    { "fx_bypass", "Bypasses the whole effects rack." },

    { "play_mode", "EXT: play the voice from MIDI. SEQ: the pattern sequencer. ARP: the arpeggiator, from held MIDI notes." },
    { "seq_run", "Starts and stops the sequencer." },
    { "pattern_bank", "Pattern bank select (A to D). Right-click a bank key to MIDI-learn it." },
    { "pattern_slot", "Pattern select (1 to 12). Right-click a pattern key to MIDI-learn it." },
    { "live_transpose", "Transposes playback live, without changing the pattern." },
    { "ovr_note_set", "Live override, held: every step plays a note." },
    { "ovr_note_clr", "Live override, held: mutes every step." },
    { "ovr_slide_set", "Live override, held: every note slides." },
    { "ovr_slide_clr", "Live override, held: no slides." },
    { "ovr_accent_set", "Live override, held: every note is accented." },
    { "ovr_accent_clr", "Live override, held: no accents." },
    { "ovr_vib_set", "Live override, held: every note gets vibrato." },
    { "ovr_vib_clr", "Live override, held: no vibrato." },
};
}

juce::String paramTooltip (ParamId id)
{
    const auto& info = paramInfo (id);
    const std::string_view key (info.id);
    const juce::String name (info.name);

    for (const auto& [k, text] : kHelp)
        if (k == key)
            return name + ": " + text;

    return name;
}
}
