// Calibration units and the effect presets: distortion, EQ, delay, reverb, chorus, limiter.
// Synth presets and scenes reuse these by slug ("+delay=dotted-eighth"), so a sound and the
// effect preset it names are always the same settings.
#include "Common.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace factory
{
namespace
{
struct EffectRecipe
{
    const char* name;
    const char* tags;
    const char* spec;
    const char* description;
};

// ---- Calibration: only the trimmers under the cover ---------------------------------------
// Groups: the reference unit, unit-to-unit spread of new units, worn units (aged capacitors,
// drift, noise, a lazy gate), and extended units (trims past where a factory would set them).
const EffectRecipe kCalibration[] = {
    { "Reference Unit", "Stock Unit,Clean", "",
      "The trimmers where the service notes put them. The starting point for everything else." },
    { "Bench Ideal", "Stock Unit,Clean", "cal_vca_clicks=0 cal_drift=0 cal_vca_noise=0 cal_gate_delay=3",
      "A reference unit with the VCA clicks smoothed out, no pitch drift and a quick gate." },
    { "Low Trim Unit", "Unit Variation,Dark", "cal_tm3=-80 cal_c13=0.94",
      "Cutoff trim set a little low, as many units leave the factory. Darker at every knob position." },
    { "Bright Trim Unit", "Unit Variation,Bright", "cal_tm3=60 cal_accenv=2050",
      "Cutoff trim set high, with a touch more accent sweep. Opens up sooner on the knob." },
    { "Hot Accent Unit", "Unit Variation,Screaming", "cal_accenv=2450 cal_accamp=1.6 cal_c13=0.9",
      "More filter and more volume from every accent, with a slightly quicker sweep capacitor." },
    { "Soft Accent Unit", "Unit Variation,Smooth", "cal_accenv=1500 cal_accamp=0.8 cal_c13=1.15",
      "Gentle accents: less sweep, less jump in level, a slower capacitor." },
    { "Long Tail Unit", "Unit Variation,Talking", "cal_tm3=-70 cal_accenv=3300 cal_accamp=1.5 cal_c13=1.4",
      "Big accent gain and a large sweep capacitor, so accent runs climb and hang on." },
    { "Wide Square Unit", "Unit Variation,Hollow", "cal_pw=40",
      "Square wave shifted towards a wider pulse. Fuller and more hollow." },
    { "Narrow Square Unit", "Unit Variation,Bright", "cal_pw=-45",
      "Square wave shifted towards a narrow pulse. Thinner and more nasal." },
    { "Sub Heavy Unit", "Unit Variation,Deep", "cal_tm3=-350 cal_c21=40 cal_accenv=3200 cal_accamp=1.4",
      "Low cutoff trim and a large coupling capacitor that lets the lowest octave through." },
    { "Thin Coupling Unit", "Unit Variation,Bright", "cal_c21=8",
      "Small coupling capacitor: the bass corner moves up and the low end tightens." },
    { "Noisy Veteran", "Worn,Noisy,Vintage", "cal_tm3=-60 cal_c21=11.3 cal_vca_noise=24 cal_drift=35 cal_cpu_jitter=1 cal_gate_delay=6",
      "Decades of gigs: hiss under the notes, a wandering pitch and a gate that lands a little late." },
    { "Dusty Unit", "Worn,Vintage", "cal_vca_noise=10 cal_drift=25 cal_c13=1.2 cal_kmax=18.3",
      "Aged electrolytics and a little hiss. Slightly tamer resonance than new." },
    { "Tired Capacitors", "Worn,Smooth", "cal_c13=1.7 cal_c21=22 cal_accenv=1750 cal_drift=20",
      "The accent capacitor has grown with age: slower, rounder accent sweeps that stack more." },
    { "Stretched Octave Unit", "Worn,Vintage", "cal_tm5=1.015 cal_drift=20",
      "The octave width trim has drifted wide, so octave jumps land a little sharp." },
    { "Squeezed Octave Unit", "Worn,Vintage", "cal_tm5=0.988 cal_drift=15",
      "The octave width trim has drifted narrow, so octave jumps land a little flat." },
    { "Late Gate Unit", "Worn,Vintage", "cal_gate_delay=6 cal_cpu_jitter=1 cal_vca_clicks=1",
      "Slow gate, jittery timing from the processor and the full VCA click." },
    { "High Loop Unit", "Extended Range,Whistling", "cal_kmax=19.6 cal_rb=27",
      "Loop gain and ladder load trimmed up: the filter rings sooner and whistles at the top." },
    { "Tame Loop Unit", "Extended Range,Smooth", "cal_kmax=17.4 cal_rb=14",
      "Loop gain trimmed down: full resonance stays musical and never quite takes off." },
    { "Wide Sweep Unit", "Extended Range,Squelchy", "cal_cv_scale=1.02 cal_tm3=-40",
      "Filter CV scale trimmed up, so the envelope sweeps further for the same Env Mod." },
    { "Fast Sweep Unit", "Extended Range,Plucky", "cal_c13=0.45 cal_accenv=2200",
      "A small accent capacitor: accents snap open and shut, with little stacking." },
    { "Slow Sweep Unit", "Extended Range,Talking", "cal_c13=3.3 cal_accenv=2600",
      "A large accent capacitor: each accent in a run lifts the filter higher than the last." },
};

// ---- Distortion: built-in voicings with their own front panel, plus custom voicings -------
const EffectRecipe kDistortion[] = {
    { "Warm Edge", "Subtle", "voicing=Glass dist_drive=30",
      "Symmetric diodes just past clean. Rounds off the accents and thickens the saw." },
    { "Glass Lift", "Subtle,Clean Low", "voicing=Glass_Low_Lift dist_drive=45 dist_color=10",
      "Diodes with a low shelf lift ahead of the clip. Weight and bite at once." },
    { "Pedal Classic", "Heavy", "voicing=Grind dist_drive=55 dist_color=-10",
      "Op-amp gain into hard diodes, the familiar sound of a bass line through a pedal." },
    { "Tight Grind", "Heavy,Glue", "voicing=Grind_Tight dist_drive=60 dist_dynamics=30 dist_dyn_post=1",
      "Hard clip with the low end trimmed before it, glued by the compressor after." },
    { "Deep Grind", "Heavy,Clean Low", "voicing=Grind_Deep dist_drive=70",
      "Hard clip with a clean low band kept aside, so the bass stays whole under the grit." },
    { "Amber Push", "Subtle", "voicing=Amber dist_drive=45",
      "Asymmetric soft overdrive. Warm, even harmonics, never harsh." },
    { "Shaded Drive", "Heavy,Filtered", "voicing=Amber_Shade dist_drive=60 dist_color=-25",
      "Soft overdrive into a dark resonant colour filter. Thick and smoky." },
    { "Scooped Saw", "Heavy,Scoop", "voicing=Hollow dist_drive=65",
      "Hard clip with a mid scoop: loud lows and highs, the middle carved out." },
    { "Anchored Scoop", "Heavy,Scoop,Clean Low", "voicing=Hollow_Anchor dist_drive=75 dist_dynamics=20",
      "The scooped stomp box with a clean low band, built for loud hard lines." },
    { "Mid Honk", "Boost", "voicing=Honk_Push dist_drive=55",
      "Mid-hump soft clipper with a mid push ahead of it. Cuts through a busy mix." },
    { "Seam Ripper", "Heavy", "voicing=Rip_Seam dist_drive=70",
      "Gated transistor fuzz. Notes tear at the start and sputter as they fade." },
    { "Fur Coat", "Heavy,Clean Low", "voicing=Thick_Fur dist_drive=60 dist_color=-15",
      "Biased fuzz with a clean low band. Furry and enormous." },
    { "Desk Hot", "Subtle", "voicing=Desk_Red dist_drive=60",
      "A console input pushed into the red. Saturation rather than distortion." },
    { "Iron Floor", "Subtle,Clean Low", "voicing=Iron_Sub dist_drive=50",
      "Console stage with a low shelf before it, for subs that need a little grain." },
    { "Origami", "Heavy", "voicing=Paper_Fold dist_drive=45",
      "Wavefolder: past halfway the peaks fold back into new, glassy harmonics." },
    { "Scrap Metal", "Heavy,Filtered", "voicing=Scrapyard dist_drive=75 dist_color=15",
      "Everything at once: hard clip, high gain, a resonant colour filter. Use with care." },
    { "Split Band Crunch", "Heavy,Clean Low,Custom Voicing",
      "voicing=Grind dist_model=0 dist_low_keep=160 dist_pre_hp=60 dist_tilt_on=1 dist_tilt_amt=6 dist_tilt_freq=900 "
      "dist_color_freq=3000 dist_color_reso=30 dist_post_hp=30 dist_drive=65",
      "A custom voicing: hard clip on everything above 160 Hz, a tilt into the clipper and a clean sub underneath." },
    { "Warm Tape Bend", "Subtle,Custom Voicing",
      "voicing=Desk_Warm dist_model=0 dist_bias=12 dist_pre_eq_on=1 dist_pre_eq_gain=3 dist_pre_eq_freq=120 "
      "dist_pre_eq_width=2 dist_color_freq=4500 dist_color_reso=10 dist_drive=55",
      "A custom voicing that leans on the console stage with a little bias and a low lift. Tape-like." },
};

// ---- EQ: four peaking bands ------------------------------------------------------------
const EffectRecipe kEq[] = {
    { "Low Trim", "Cut", "eq_on=1 eq_low_gain=-9 eq_low_freq=30 eq_low_width=1.5",
      "Clears the rumble below the fundamental so the kick has room." },
    { "Mid Scoop", "Scoop", "eq_on=1 eq_low_gain=1.5 eq_low_freq=90 eq_lomid_gain=-4.5 eq_lomid_freq=500 eq_lomid_width=1.5 eq_high_gain=3 eq_high_freq=6000",
      "Takes out the boxy middle, lifts the top a little and gives the fundamental a touch of weight." },
    { "Presence Lift", "Boost", "eq_on=1 eq_himid_gain=4 eq_himid_freq=2500 eq_himid_width=1.5",
      "Brings the resonant peak forward in a full mix." },
    { "Sub Weight", "Boost", "eq_on=1 eq_low_gain=4 eq_low_freq=70 eq_low_width=1.5 eq_lomid_gain=-3 eq_lomid_freq=300",
      "Adds weight at the fundamental and tidies the low mids." },
    { "Air And Bite", "Boost", "eq_on=1 eq_himid_gain=3 eq_himid_freq=3500 eq_high_gain=4 eq_high_freq=9000 eq_high_width=2",
      "Opens the top end. Good after heavy distortion." },
    { "Telephone", "Cut,Filtered", "eq_on=1 eq_low_gain=-18 eq_low_freq=60 eq_low_width=3 eq_lomid_gain=10 eq_lomid_freq=1100 eq_lomid_width=2.5 eq_high_gain=-14 eq_high_freq=9000 eq_high_width=3",
      "Narrow band, for breakdowns and intros." },
    { "Smile", "Boost,Scoop", "eq_on=1 eq_low_gain=3 eq_low_freq=80 eq_lomid_gain=-4.5 eq_lomid_freq=700 eq_high_gain=3.5 eq_high_freq=8000",
      "Low and high up, middle down. Big and glossy." },
    { "Nasal Cut", "Cut", "eq_on=1 eq_himid_gain=-5 eq_himid_freq=1400 eq_himid_width=1",
      "Tames the honk of a high-resonance square." },
    { "Squelch Focus", "Boost", "eq_on=1 eq_lomid_gain=3 eq_lomid_freq=900 eq_lomid_width=2 eq_low_gain=-4 eq_low_freq=45",
      "A broad lift where the filter sweeps live, with the sub trimmed." },
    { "Dark Tape", "Cut,Filtered", "eq_on=1 eq_high_gain=-8 eq_high_freq=6000 eq_high_width=3 eq_himid_gain=-2 eq_himid_freq=3000 eq_low_gain=2 eq_low_freq=100",
      "Rolls off the top like an old cassette." },
};

// ---- Delay --------------------------------------------------------------------------------
const EffectRecipe kDelay[] = {
    { "Dotted Eighth", "Dotted,Tempo Sync,Filtered",
      "delay_on=1 delay_sync=1 delay_l_num=3 delay_l_den=16 delay_r_num=1 delay_r_den=4 delay_feedback=35 delay_filt_type=3 delay_filt_cutoff=450 delay_spread=40 delay_mix=22",
      "Three sixteenths left, a quarter right, high-passed in the loop so the bass stays clean." },
    { "Dub Echo", "Long,Filtered,Tempo Sync",
      "delay_on=1 delay_sync=1 delay_l_num=3 delay_l_den=8 delay_r_num=3 delay_r_den=8 delay_feedback=62 delay_filt_type=2 delay_filt_cutoff=900 delay_filt_reso=35 delay_spread=60 delay_mix=30",
      "Long band-passed repeats that ring on the filter resonance. For space and dub." },
    { "Triplet Bounce", "Tempo Sync,Wide",
      "delay_on=1 delay_sync=1 delay_l_num=1 delay_l_den=8 delay_l_mod=2 delay_r_num=1 delay_r_den=4 delay_r_mod=2 delay_feedback=30 delay_filt_type=3 delay_filt_cutoff=300 delay_spread=70 delay_mix=20",
      "Triplet repeats against a straight line, spread wide." },
    { "Short Slap", "Short,Free Time",
      "delay_on=1 delay_sync=0 delay_time=0.07 delay_feedback=10 delay_filt_type=1 delay_filt_cutoff=5000 delay_spread=30 delay_mix=18",
      "One quick slap, not in time. Thickens without smearing." },
    { "Ping Wide", "Wide,Tempo Sync",
      "delay_on=1 delay_sync=1 delay_l_num=1 delay_l_den=8 delay_r_num=1 delay_r_den=4 delay_feedback=40 delay_filt_type=3 delay_filt_cutoff=500 delay_spread=100 delay_mix=22",
      "Eighth left, quarter right, fully spread." },
    { "Quarter Space", "Long,Tempo Sync,Filtered",
      "delay_on=1 delay_sync=1 delay_l_num=1 delay_l_den=4 delay_r_num=1 delay_r_den=2 delay_feedback=45 delay_filt_type=1 delay_filt_cutoff=2500 delay_spread=50 delay_mix=20",
      "Quarter and half note repeats, darkening as they go." },
    { "Filtered Haze", "Filtered,Long,Tempo Sync",
      "delay_on=1 delay_sync=1 delay_l_num=3 delay_l_den=16 delay_r_num=5 delay_r_den=16 delay_feedback=55 delay_filt_type=2 delay_filt_cutoff=1400 delay_filt_reso=50 delay_spread=80 delay_mix=26",
      "Uneven sixteenth repeats through a resonant band-pass. A cloud behind the line." },
    { "Eighth Tight", "Short,Tempo Sync",
      "delay_on=1 delay_sync=1 delay_l_num=1 delay_l_den=8 delay_r_num=1 delay_r_den=8 delay_feedback=20 delay_filt_type=3 delay_filt_cutoff=600 delay_mix=15",
      "A single tidy eighth repeat for drive and movement." },
    { "Long Dark Tail", "Long,Filtered,Tempo Sync",
      "delay_on=1 delay_sync=1 delay_l_num=1 delay_l_den=2 delay_l_mod=1 delay_r_num=1 delay_r_den=2 delay_feedback=58 delay_filt_type=1 delay_filt_cutoff=1100 delay_filt_reso=15 delay_spread=40 delay_mix=24",
      "Dotted half notes that fade into the dark. For slow lines." },
    { "Sixteenth Ripple", "Short,Tempo Sync,Wide",
      "delay_on=1 delay_sync=1 delay_l_num=1 delay_l_den=16 delay_r_num=3 delay_r_den=32 delay_feedback=38 delay_filt_type=3 delay_filt_cutoff=800 delay_spread=90 delay_mix=16",
      "Tiny repeats that blur the attack into a shimmer." },
    { "Dotted Wide Dub", "Dotted,Long,Filtered,Tempo Sync",
      "delay_on=1 delay_sync=1 delay_l_num=1 delay_l_den=8 delay_l_mod=1 delay_r_num=1 delay_r_den=4 delay_r_mod=1 delay_feedback=52 delay_filt_type=1 delay_filt_cutoff=1800 delay_filt_reso=25 delay_spread=75 delay_mix=26",
      "Dotted eighth and dotted quarter repeats, low-passed and wide." },
};

// ---- Reverb -------------------------------------------------------------------------------
const EffectRecipe kReverb[] = {
    { "Tight Room", "Room,Short,Subtle",
      "rev_on=1 rev_predelay=5 rev_early_size=4 rev_late_size=9 rev_early_diff=60 rev_late_diff=50 rev_early_bright=55 rev_late_bright=40 rev_feedback=10 rev_early_late=30 rev_mod=5 rev_mix=12",
      "A small, quick room. Glue without a tail." },
    { "Small Plate", "Plate,Short",
      "rev_on=1 rev_predelay=10 rev_early_size=3 rev_late_size=14 rev_early_diff=80 rev_late_diff=75 rev_early_bright=70 rev_late_bright=65 rev_feedback=30 rev_early_late=60 rev_mod=15 rev_mix=15",
      "Bright, dense and short." },
    { "Club Room", "Room,Subtle",
      "rev_on=1 rev_predelay=12 rev_early_size=8 rev_late_size=16 rev_early_diff=50 rev_late_diff=55 rev_early_bright=50 rev_late_bright=45 rev_feedback=20 rev_early_late=45 rev_mod=10 rev_mix=14",
      "About half a second of room, the old digital box kind." },
    { "Dark Hall", "Hall,Long",
      "rev_on=1 rev_predelay=30 rev_early_size=14 rev_late_size=40 rev_early_diff=60 rev_late_diff=70 rev_early_bright=30 rev_late_bright=20 rev_feedback=65 rev_early_late=75 rev_mod=20 rev_mix=20",
      "A big hall with the highs damped. Space without glare." },
    { "Deep Space", "Hall,Long,Heavy",
      "rev_on=1 rev_predelay=60 rev_early_size=18 rev_late_size=50 rev_early_diff=70 rev_late_diff=85 rev_early_bright=35 rev_late_bright=25 rev_feedback=82 rev_early_late=85 rev_mod=35 rev_mix=17",
      "A huge, dark, slowly moving tail for minimal and slow lines." },
    { "Stone Chamber", "Hall",
      "rev_on=1 rev_predelay=20 rev_early_size=12 rev_late_size=28 rev_early_diff=55 rev_late_diff=60 rev_early_bright=50 rev_late_bright=40 rev_feedback=45 rev_early_late=60 rev_mod=10 rev_mix=17",
      "Medium chamber with clear early reflections." },
    { "Bright Chamber", "Plate",
      "rev_on=1 rev_predelay=15 rev_early_size=9 rev_late_size=22 rev_early_diff=75 rev_late_diff=70 rev_early_bright=80 rev_late_bright=70 rev_feedback=40 rev_early_late=55 rev_mod=15 rev_mix=14",
      "Airy and bright, for melodic lines." },
    { "Short Ambience", "Room,Short,Subtle",
      "rev_on=1 rev_predelay=0 rev_early_size=2.5 rev_late_size=7 rev_early_diff=40 rev_late_diff=40 rev_early_bright=60 rev_late_bright=50 rev_feedback=5 rev_early_late=20 rev_mod=0 rev_mix=10",
      "Early reflections only, to put a dry line in a space." },
    { "Endless Void", "Hall,Long,Heavy",
      "rev_on=1 rev_predelay=90 rev_early_size=20 rev_late_size=51 rev_early_diff=80 rev_late_diff=95 rev_early_bright=25 rev_late_bright=15 rev_feedback=92 rev_early_late=95 rev_mod=45 rev_mix=13",
      "A tail that barely ends. Drones and ambient lines." },
    { "Spring Box", "Short,Motion",
      "rev_on=1 rev_predelay=8 rev_early_size=5 rev_late_size=12 rev_early_diff=30 rev_late_diff=35 rev_early_bright=65 rev_late_bright=55 rev_feedback=50 rev_early_late=50 rev_mod=40 rev_mix=14",
      "Low diffusion and lots of movement: a bouncy, metallic small tank." },
};

// ---- Chorus -------------------------------------------------------------------------------
const EffectRecipe kChorus[] = {
    { "Gentle Width", "Subtle,Wide",
      "cho_on=1 cho_depth=25 cho_rate=25 cho_spatial=60 cho_hp=40 cho_mix=30",
      "Slow, light widening that keeps the bass centred." },
    { "Fat Double", "Heavy,Wide",
      "cho_on=1 cho_fatness=70 cho_depth=40 cho_rate=30 cho_spatial=70 cho_hp=30 cho_mix=45",
      "Two lines, the second loud, for a doubled sound." },
    { "Slow Swirl", "Motion",
      "cho_on=1 cho_depth=60 cho_rate=12 cho_spatial=80 cho_hp=35 cho_mix=45",
      "Deep and slow. The line seems to turn in place." },
    { "Fast Shimmer", "Motion",
      "cho_on=1 cho_depth=20 cho_rate=75 cho_spatial=50 cho_hp=55 cho_mix=30",
      "A quick, shallow wobble on the upper harmonics." },
    { "Wide Fatness", "Wide,Heavy",
      "cho_on=1 cho_fatness=50 cho_offset=40 cho_depth=35 cho_rate=35 cho_spatial=100 cho_hp=45 cho_mix=40",
      "Both lines, spread as far as they go." },
    { "Thin Detune", "Subtle",
      "cho_on=1 cho_depth=12 cho_rate=40 cho_spatial=40 cho_hp=20 cho_mix=35",
      "Barely moving: a slight detune rather than a chorus." },
    { "Deep Throb", "Motion,Heavy",
      "cho_on=1 cho_fatness=30 cho_depth=85 cho_rate=20 cho_spatial=60 cho_hp=50 cho_mix=50",
      "Big pitch movement for slow, liquid lines." },
    { "Subtle Air", "Subtle",
      "cho_on=1 cho_depth=18 cho_rate=20 cho_spatial=50 cho_hp=55 cho_mix=25",
      "Only the top end moves." },
    { "Liquid Motion", "Motion,Wide",
      "cho_on=1 cho_fatness=40 cho_offset=25 cho_depth=50 cho_rate=28 cho_spatial=85 cho_hp=40 cho_mix=42",
      "The square-wave lead chorus: wide, moving, smooth." },
    { "Wet Wash", "Heavy,Motion",
      "cho_on=1 cho_fatness=60 cho_depth=70 cho_rate=45 cho_spatial=90 cho_hp=25 cho_mix=65",
      "Mostly wet. For pads made from a bass line." },
};

// ---- Limiter ------------------------------------------------------------------------------
const EffectRecipe kLimiter[] = {
    { "Safety Clip", "Safety",
      "lim_on=1 lim_knee=100 lim_ratio=40 lim_attack=0.5 lim_release=80 lim_threshold=-1.5 lim_gain=0 lim_soft_clip=1",
      "Catches stray peaks and soft clips the rest. Leave it on." },
    { "Glue", "Glue,Subtle",
      "lim_on=1 lim_knee=100 lim_ratio=3 lim_attack=10 lim_release=150 lim_threshold=-12 lim_gain=1.5 lim_soft_clip=0",
      "Gentle compression that evens out accents a little." },
    { "Hard Wall", "Heavy,Safety",
      "lim_on=1 lim_knee=10 lim_ratio=40 lim_attack=0.1 lim_release=50 lim_threshold=-6 lim_gain=4 lim_soft_clip=1",
      "Nothing gets past. Loud and flat." },
    { "Soft Catch", "Safety,Subtle",
      "lim_on=1 lim_knee=100 lim_ratio=8 lim_attack=2 lim_release=120 lim_threshold=-4 lim_gain=1 lim_soft_clip=1",
      "Rounds off the accent peaks only." },
    { "Pump", "Pump,Heavy",
      "lim_on=1 lim_knee=50 lim_ratio=12 lim_attack=8 lim_release=220 lim_threshold=-16 lim_gain=3 lim_soft_clip=0",
      "Slow release that breathes with the pattern." },
    { "Peak Shave", "Safety,Subtle",
      "lim_on=1 lim_knee=60 lim_ratio=20 lim_attack=0.3 lim_release=40 lim_threshold=-3 lim_gain=0 lim_soft_clip=0",
      "Fast and transparent, only on the highest peaks." },
    { "Loud Squash", "Heavy,Glue",
      "lim_on=1 lim_knee=80 lim_ratio=20 lim_attack=1 lim_release=90 lim_threshold=-14 lim_gain=4 lim_soft_clip=1",
      "Heavy squash into a soft clipper. Every note the same loudness." },
    { "Gentle Leveller", "Glue,Subtle",
      "lim_on=1 lim_knee=100 lim_ratio=2 lim_attack=20 lim_release=300 lim_threshold=-18 lim_gain=4 lim_soft_clip=0",
      "Slow, low-ratio levelling for long lines." },
    { "Slow Ride", "Glue,Pump",
      "lim_on=1 lim_knee=100 lim_ratio=6 lim_attack=30 lim_release=500 lim_threshold=-10 lim_gain=3 lim_soft_clip=0",
      "Lets the attack through, then holds the level down." },
    { "Accent Tamer", "Safety,Subtle",
      "lim_on=1 lim_knee=80 lim_ratio=20 lim_attack=0.2 lim_release=60 lim_threshold=-10 lim_gain=0 lim_soft_clip=1",
      "Fast and low: catches the spike at the start of an accent and leaves the rest of the note alone." },
    { "Punch Keeper", "Glue",
      "lim_on=1 lim_knee=70 lim_ratio=4 lim_attack=15 lim_release=100 lim_threshold=-9 lim_gain=2 lim_soft_clip=1",
      "Keeps the snap of each note while taming the body." },
};

std::map<std::string, Patch>& registry (acid::PresetType type)
{
    static std::map<acid::PresetType, std::map<std::string, Patch>> all;
    return all[type];
}

/** The block's own output trim, which levels.tsv moves so an effect preset matches the sound
    without it in loudness (qa.py --calibrate), or -1 for a type that has none. EQ, delay and
    reverb have no output trim: their recipes are matched by hand (band gains, mix). The chorus
    has only its wet level: its mix is a linear crossfade and the two lines are decorrelated
    from the dry signal, so a chorus at 25 to 50 % mix loses 2 to 3 dB unless the wet is
    raised, which is what the trim does. */
int levelParam (acid::PresetType type)
{
    switch (type)
    {
        case acid::PresetType::Chorus: return acid::kChoVolume;
        case acid::PresetType::Distortion: return acid::kDistVolume;
        case acid::PresetType::Limiter: return acid::kLimGain;
        default: return -1;
    }
}

template <std::size_t N>
void addGroup (Library& lib, acid::PresetType type, const EffectRecipe (&recipes)[N])
{
    const std::string dir = typeDirectory (type);
    for (const EffectRecipe& r : recipes)
    {
        Patch patch = baseSound();
        const std::string context = dir + "/" + r.name;
        applySpec (patch, r.spec, context);
        const std::string slug = slugify (r.name);

        Entry e;
        if (const int trimParam = levelParam (type); trimParam >= 0)
        {
            // Synth presets and scenes that name this preset copy it with the trim applied.
            e.levelKey = dir + "/" + slug;
            e.levelParam = trimParam;
            const auto id = static_cast<acid::ParamId> (trimParam);
            const acid::ParamInfo& info = acid::paramInfo (id);
            const float v = std::round ((param (patch.values, id) + lib.levelTrims().trim (e.levelKey)) * 10.0f) / 10.0f;
            setParam (patch.values, id, std::clamp (v, info.min, info.max));
        }
        if (! registry (type).emplace (slug, patch).second)
            fail ("duplicate " + context);

        e.data = makeParamPreset (type, patch, r.name, r.tags, r.description);
        e.path = dir + "/" + slug + ".mw12f";
        e.group = dir;
        lib.add (std::move (e));
    }
}
}

const Patch& findEffect (acid::PresetType type, std::string_view slug)
{
    const auto& reg = registry (type);
    const auto it = reg.find (std::string (slug));
    if (it == reg.end())
        fail ("unknown " + typeDirectory (type) + " preset '" + std::string (slug) + "'");
    return it->second;
}

void addEffects (Library& lib)
{
    addGroup (lib, acid::PresetType::Calibration, kCalibration);
    addGroup (lib, acid::PresetType::Distortion, kDistortion);
    addGroup (lib, acid::PresetType::Eq, kEq);
    addGroup (lib, acid::PresetType::Delay, kDelay);
    addGroup (lib, acid::PresetType::Reverb, kReverb);
    addGroup (lib, acid::PresetType::Chorus, kChorus);
    addGroup (lib, acid::PresetType::Limiter, kLimiter);
}
}
