// Synth presets: the sound library, by family. Each recipe starts from the stock circuit
// (baseSound) and sets what differs. Levels are matched afterwards by measurement
// (levels.tsv, written by qa.py --calibrate), through master_volume only, so the sound
// itself is exactly the recipe.
#include "Common.h"

#include <map>

namespace factory
{
namespace
{
struct Family
{
    const char* name;
    const char* base;           // recipe applied before every member's own
};

struct SoundRecipe
{
    const char* family;
    const char* name;
    const char* character;      // Character tags, comma separated
    const char* spec;
    const char* description;
};

const Family kFamilies[] = {
    { "Stock", "mod_on=0" },
    { "Squelch", "mod_on=0" },
    { "Drive", "mod_on=0" },
    { "Deep Bass", "mod_on=0" },
    { "Scream", "mod_on=0" },
    { "Whistle", "" },
    { "Liquid Line", "wave=1" },
    { "Pluck", "" },
    { "Space", "mod_on=0" },
    { "Overdrive", "mod_on=1" },
    { "Filter FM", "mod_on=1" },
    { "Tracking", "mod_on=1" },
    { "Sub", "mod_on=1" },
    { "Sustain", "mod_on=1" },
    { "Utility", "mod_on=0" },
};

// clang-format off
const SoundRecipe kSounds[] = {
    // ---- Stock: the circuit as it left the factory, Mods off, different units ----------
    { "Stock", "Rubber Floor", "Rubbery,Squelchy", "cutoff=20 resonance=80 envmod=60 decay=45 accent=75",
      "The classic squelch, with room on the cutoff knob to go either way." },
    { "Stock", "Jack Pluck", "Plucky,Vintage", "cutoff=30 resonance=65 envmod=50 decay=30 accent=40 +reverb=club-room",
      "Short and bouncy with gentle accents, in a small room." },
    { "Stock", "Warehouse Squelch", "Squelchy,Dark", "cutoff=12 resonance=88 envmod=70 decay=50 accent=85",
      "Low cutoff, high Env Mod: dark between notes, wide open on every attack." },
    { "Stock", "Bubble Square", "Rubbery,Hollow", "wave=1 cutoff=25 resonance=85 envmod=55 decay=55 accent=70",
      "Square wave, high resonance, a round bubbling tone." },
    { "Stock", "Velvet Square", "Smooth,Warm", "wave=1 cutoff=18 resonance=60 envmod=40 decay=60 accent=50",
      "Soft square bass with modest resonance." },
    { "Stock", "Hollow Talker", "Talking,Hollow", "wave=1 cutoff=10 resonance=92 envmod=75 decay=70 accent=80",
      "Long decay and high resonance on the square: every note says something." },
    { "Stock", "Last Tram", "Squelchy,Dark", "cutoff=5 resonance=85 envmod=80 decay=35 accent=90",
      "Nearly closed, then snapped open by big accents." },
    { "Stock", "Copper Wire", "Bright,Plucky", "cutoff=40 resonance=70 envmod=35 decay=25 accent=60",
      "Bright and wiry, short sweeps." },
    { "Stock", "Long Vowel", "Talking,Squelchy", "cutoff=15 resonance=90 envmod=65 decay=85 accent=70",
      "Decay all the way open: tied and slid notes keep sweeping." },
    { "Stock", "Snap Back", "Plucky,Squelchy", "cutoff=25 resonance=75 envmod=60 decay=10 accent=80",
      "Shortest decay, so plain notes click and accents bark." },
    { "Stock", "Midnight Ink", "Dark,Smooth", "wave=1 cutoff=8 resonance=78 envmod=50 decay=40 accent=65",
      "Dark square for late, quiet lines." },
    { "Stock", "Old Tape Rub", "Vintage,Noisy", "cutoff=22 resonance=72 envmod=52 decay=48 accent=45 +calibration=noisy-veteran +eq=dark-tape",
      "A tired unit, dulled as if bounced to cassette." },
    { "Stock", "Chalk Line", "Bright,Clean", "cutoff=52 resonance=38 envmod=30 decay=32 accent=50",
      "Clean, bright and moderate. Sits on top of a busy kick." },
    { "Stock", "Puddle Jump", "Rubbery,Squelchy", "wave=1 cutoff=30 resonance=88 envmod=60 decay=30 accent=85",
      "Short square blips that bounce off the accents." },
    { "Stock", "Wah Floor", "Talking,Squelchy", "cutoff=0 resonance=95 envmod=85 decay=60 accent=85",
      "Cutoff at zero, the envelope does all the work. Turn the cutoff up slowly." },
    { "Stock", "Low Trim Groove", "Dark,Warm", "cutoff=48 resonance=82 envmod=58 decay=45 accent=72 +calibration=low-trim-unit",
      "A unit with its cutoff trim set low and the knob raised to make up for it: the sweep opens like a stock unit, the floor stays darker." },
    { "Stock", "Bright Trim Snap", "Bright,Plucky", "cutoff=15 resonance=70 envmod=55 decay=25 accent=75 +calibration=bright-trim-unit",
      "A unit trimmed bright: snappy, forward and short." },
    { "Stock", "Hot Accent Bounce", "Squelchy,Screaming", "cutoff=10 resonance=85 envmod=50 decay=40 accent=90 +calibration=hot-accent-unit",
      "A unit with hot accent trims. Accented steps jump right out." },
    { "Stock", "Soft Accent Glide", "Smooth,Liquid", "wave=1 cutoff=22 resonance=70 envmod=45 decay=65 accent=60 slide_time=65 +calibration=soft-accent-unit",
      "Gentle accents and slower slides for melodic square lines." },
    { "Stock", "Long Tail Stack", "Talking,Screaming", "cutoff=8 resonance=90 envmod=45 decay=40 accent=85 +calibration=long-tail-unit",
      "Accent runs climb and hang: the capacitor holds on." },
    { "Stock", "Wide Square Bass", "Hollow,Warm", "wave=1 cutoff=12 resonance=55 envmod=40 decay=50 accent=55 +calibration=wide-square-unit",
      "Wide pulse square: hollow, woody low end." },
    { "Stock", "Narrow Square Reed", "Bright,Hollow", "wave=1 cutoff=35 resonance=75 envmod=45 decay=40 accent=65 +calibration=narrow-square-unit",
      "Narrow pulse square: a reedy, nasal tone." },
    { "Stock", "Attic Dust", "Vintage,Warm", "cutoff=20 resonance=78 envmod=55 decay=55 accent=60 +calibration=dusty-unit",
      "An older unit with a little hiss and drift." },
    { "Stock", "Tired Cap Swell", "Talking,Smooth", "cutoff=10 resonance=92 envmod=50 decay=50 accent=80 +calibration=tired-capacitors",
      "Aged accent capacitor: slow, round swells that stack." },
    { "Stock", "Stretched Octaves", "Vintage,Squelchy", "cutoff=25 resonance=80 envmod=55 decay=40 accent=70 +calibration=stretched-octave-unit",
      "Octave jumps land a hair sharp, the way old units do." },
    { "Stock", "Late Gate Shuffle", "Vintage,Plucky", "cutoff=28 resonance=74 envmod=48 decay=42 accent=55 +calibration=late-gate-unit",
      "Slow gate and jittery timing give a loose, human push." },
    { "Stock", "Ringing Loop", "Whistling,Bright", "cutoff=30 resonance=96 envmod=40 decay=55 accent=70 +calibration=high-loop-unit",
      "Loop gain trimmed up: the filter rings between notes." },
    { "Stock", "Tame Ladder", "Smooth,Squelchy", "cutoff=25 resonance=100 envmod=60 decay=50 accent=75 +calibration=tame-loop-unit",
      "Full resonance that stays round, on a unit trimmed tame." },
    { "Stock", "Wide Sweep Squelch", "Squelchy,Rubbery", "cutoff=5 resonance=85 envmod=80 decay=45 accent=80 +calibration=wide-sweep-unit",
      "Filter CV trimmed wide: the envelope sweeps further." },
    { "Stock", "Fast Sweep Plip", "Plucky,Bright", "cutoff=15 resonance=80 envmod=50 decay=20 accent=85 +calibration=fast-sweep-unit",
      "A small accent capacitor: accents pop and vanish." },
    { "Stock", "Slow Sweep Climb", "Screaming,Talking", "cutoff=6 resonance=95 envmod=45 decay=45 accent=95 +calibration=slow-sweep-unit",
      "A large accent capacitor: runs of accents climb higher and higher." },
    { "Stock", "Thin Coupling Pluck", "Plucky,Bright", "cutoff=40 resonance=60 envmod=55 decay=15 accent=60 +calibration=thin-coupling-unit",
      "Tight low end and short notes. Leaves room for the kick." },
    { "Stock", "Sub Unit Rumble", "Deep,Warm", "wave=1 cutoff=20 resonance=50 envmod=35 decay=55 accent=50 +calibration=sub-heavy-unit",
      "A unit trimmed for low end, square wave, low resonance." },

    // ---- Squelch: the classic squelch with light distortion and a little space ---------
    { "Squelch", "First Squelch", "Squelchy,Rubbery", "cutoff=18 resonance=84 envmod=64 decay=46 accent=82 voicing=Glass_Low_Lift dist_drive=55 +delay=dotted-eighth delay_mix=16 +reverb=tight-room",
      "The first-load sound: saw, a squelchy line, diodes with a low lift, a light dotted echo. Reach for the cutoff." },
    { "Squelch", "Sour Drizzle", "Squelchy,Bright", "cutoff=10 resonance=88 envmod=72 decay=48 accent=85 voicing=Glass dist_drive=40 +delay=dotted-eighth",
      "Light diode drive and a dotted delay on a wide-open squelch." },
    { "Squelch", "Gum Sole", "Rubbery,Warm", "wave=1 cutoff=14 resonance=86 envmod=65 decay=52 accent=80 voicing=Amber dist_drive=35",
      "Square squelch with a warm soft overdrive." },
    { "Squelch", "Wet Floor", "Squelchy,Spacey", "cutoff=8 resonance=90 envmod=78 decay=40 accent=88 voicing=Glass_Low_Lift dist_drive=45 +delay=dotted-eighth +reverb=tight-room",
      "Slippery squelch with weight underneath, delay and a tight room." },
    { "Squelch", "Jelly Spine", "Rubbery,Talking", "wave=1 cutoff=5 resonance=93 envmod=70 decay=60 accent=82 voicing=Desk_Red dist_drive=40",
      "Wobbly square talk through a hot console input." },
    { "Squelch", "Elastic", "Rubbery,Squelchy", "cutoff=18 resonance=84 envmod=66 decay=38 accent=78 voicing=Amber_Tilt dist_drive=35",
      "Tilted overdrive brings the squelch forward." },
    { "Squelch", "Swamp Gas", "Squelchy,Dark", "cutoff=3 resonance=91 envmod=85 decay=55 accent=86 voicing=Glass_Tight dist_drive=50 +delay=filtered-haze",
      "Deep wet squelch with a cloud of filtered repeats." },
    { "Squelch", "Juice Box", "Rubbery,Warm", "wave=1 cutoff=20 resonance=82 envmod=60 decay=45 accent=75 voicing=Desk_Warm dist_drive=50 +chorus=gentle-width",
      "Sweet square squelch, lightly saturated and widened." },
    { "Squelch", "Pocket Squelch", "Squelchy,Plucky", "cutoff=15 resonance=87 envmod=62 decay=30 accent=80 voicing=Glass dist_drive=50",
      "Short, tight and squelchy." },
    { "Squelch", "Rubber Duck", "Rubbery,Hollow", "wave=1 cutoff=10 resonance=90 envmod=68 decay=35 accent=90 voicing=Mid_Fist dist_drive=30",
      "A squeaky square with a fist of midrange." },
    { "Squelch", "Sticky Keys", "Squelchy,Gritty", "cutoff=12 resonance=86 envmod=74 decay=58 accent=84 voicing=Lopsided dist_drive=40",
      "Lopsided diode drive gives the squelch a gritty edge." },
    { "Squelch", "Neon Puddle", "Squelchy,Bright", "cutoff=6 resonance=89 envmod=76 decay=44 accent=86 voicing=Iron_Sub dist_drive=45 +reverb=small-plate",
      "Glossy squelch with a weighty bottom and a short plate." },
    { "Squelch", "Pressure Valve", "Screaming,Squelchy", "cutoff=20 resonance=85 envmod=60 decay=50 accent=95 voicing=Glass dist_drive=35 +calibration=hot-accent-unit",
      "Hot accent trims: the accents let off steam." },
    { "Squelch", "Bent Spoon", "Hollow,Squelchy", "wave=1 cutoff=16 resonance=88 envmod=70 decay=46 accent=82 voicing=Honk dist_drive=35",
      "Square through a mid-hump clipper: nasal and bent." },
    { "Squelch", "Slime Trail", "Talking,Squelchy", "cutoff=0 resonance=94 envmod=90 decay=65 accent=80 slide_time=70 voicing=Desk_Warm dist_drive=60",
      "Everything from the envelope, long slides, console warmth." },

    // ---- Drive: distorted leads ---------------------------------------------------------
    { "Drive", "Pedal Snarl", "Gritty,Aggressive", "cutoff=15 resonance=85 envmod=65 decay=45 accent=85 voicing=Grind dist_drive=60 +delay=dotted-eighth",
      "The bass line through a pedal on the floor, with a dotted echo." },
    { "Drive", "Rust Belt", "Gritty,Aggressive", "cutoff=10 resonance=80 envmod=55 decay=40 accent=80 voicing=Grind_Tight dist_drive=70 dist_dynamics=25 dist_dyn_post=1",
      "Tight hard clip, compressed after: every note equally loud and rough." },
    { "Drive", "Hollow Scream", "Screaming,Hollow", "cutoff=12 resonance=90 envmod=70 decay=50 accent=90 voicing=Hollow dist_drive=70",
      "The scooped stomp box folds the resonance into a throat." },
    { "Drive", "Brick Dust", "Gritty,Dark", "cutoff=5 resonance=75 envmod=40 decay=35 accent=75 voicing=Grind_Deep dist_drive=80",
      "Low envelope, all drive: the tone comes from the clipper, not the sweep." },
    { "Drive", "Torn Speaker", "Gritty,Noisy", "wave=1 cutoff=20 resonance=82 envmod=60 decay=40 accent=80 voicing=Rip_Seam dist_drive=65",
      "Gated fuzz on a square: notes tear and sputter." },
    { "Drive", "Furnace", "Aggressive,Warm", "cutoff=8 resonance=88 envmod=72 decay=55 accent=88 voicing=Thick_Fur dist_drive=60",
      "Big biased fuzz with a clean low band underneath." },
    { "Drive", "Chrome Teeth", "Bright,Aggressive", "cutoff=25 resonance=86 envmod=60 decay=30 accent=85 voicing=Razor_Scoop dist_drive=65",
      "Bright, scooped and sharp." },
    { "Drive", "Hot Wire", "Gritty,Dark", "cutoff=18 resonance=84 envmod=66 decay=42 accent=82 voicing=Amber_Shade dist_drive=75",
      "Soft overdrive into a dark colour filter. Smoky lead." },
    { "Drive", "Grit Engine", "Gritty,Squelchy", "wave=1 cutoff=12 resonance=86 envmod=64 decay=48 accent=84 voicing=Grit_Ramp dist_drive=70",
      "Square squelch with an overdrive that gets dirtier as it gets louder." },
    { "Drive", "Steel Toe", "Aggressive,Dark", "cutoff=4 resonance=70 envmod=35 decay=30 accent=70 voicing=Hollow_Anchor dist_drive=85",
      "Closed filter, heavy clipping, solid low end. For hard lines." },
    { "Drive", "Folded Light", "Bright,Gritty", "cutoff=20 resonance=88 envmod=60 decay=45 accent=85 voicing=Paper_Fold dist_drive=55",
      "Wavefolding turns the resonant peak into a glassy chord." },
    { "Drive", "Paper Cutter", "Bright,Aggressive", "wave=1 cutoff=15 resonance=85 envmod=65 decay=35 accent=85 voicing=Paper_Cut dist_drive=60",
      "Thin, sharp folding on the square." },
    { "Drive", "Scrapheap", "Aggressive,Chaotic", "cutoff=10 resonance=90 envmod=70 decay=50 accent=90 voicing=Scrapyard dist_drive=60 +limiter=safety-clip",
      "Everything turned up. The limiter keeps it in the room." },
    { "Drive", "Molten", "Aggressive,Talking", "cutoff=6 resonance=92 envmod=75 decay=55 accent=88 voicing=Meltdown dist_drive=50",
      "Folded and biased: the squelch melts into new shapes." },
    { "Drive", "Sputter Box", "Gritty,Percussive", "cutoff=22 resonance=80 envmod=55 decay=30 accent=80 voicing=Sputter dist_drive=70",
      "Gated fuzz cuts the tail of each note short." },
    { "Drive", "Nasal Drive", "Hollow,Gritty", "wave=1 cutoff=25 resonance=86 envmod=58 decay=40 accent=82 voicing=Nasal dist_drive=65",
      "Square through a nasal mid-hump clipper." },
    { "Drive", "Lean Machine", "Gritty,Squelchy", "cutoff=14 resonance=87 envmod=68 decay=46 accent=86 voicing=Leaning dist_drive=65",
      "Asymmetric drive leans the squelch over." },
    { "Drive", "Split Crunch", "Aggressive,Deep", "cutoff=12 resonance=86 envmod=66 decay=44 accent=86 +distortion=split-band-crunch",
      "Hard clip above 160 Hz, a clean sub below." },

    // ---- Deep: basses for the bottom of the mix ----------------------------------------
    { "Deep Bass", "Floor Tom Bass", "Deep,Warm", "wave=1 cutoff=5 resonance=40 envmod=30 decay=45 accent=50 +eq=sub-weight",
      "Round square bass with weight at the fundamental." },
    { "Deep Bass", "Basement", "Deep,Dark", "cutoff=0 resonance=55 envmod=35 decay=55 accent=55",
      "Saw with the filter all the way down. Felt more than heard." },
    { "Deep Bass", "Velvet Rope", "Deep,Smooth", "wave=1 cutoff=10 resonance=45 envmod=25 decay=70 accent=40 +eq=sub-weight",
      "Smooth long square bass." },
    { "Deep Bass", "Low Tide", "Deep,Squelchy", "cutoff=10 resonance=70 envmod=50 decay=70 accent=60 +calibration=sub-heavy-unit",
      "A sub-heavy unit on the saw, long notes with enough squelch to talk." },
    { "Deep Bass", "Iron Lung", "Deep,Gritty", "cutoff=8 resonance=50 envmod=40 decay=40 accent=60 voicing=Iron_Sub dist_drive=40",
      "Console grain on a low saw." },
    { "Deep Bass", "Cellar Door", "Deep,Hollow", "wave=1 cutoff=0 resonance=62 envmod=72 decay=72 accent=55",
      "Square at the bottom of the knob with a hollow bloom." },
    { "Deep Bass", "Thick Carpet", "Deep,Warm", "cutoff=6 resonance=35 envmod=30 decay=60 accent=45 voicing=Desk_Warm dist_drive=35",
      "Low resonance, warm saturation. Fills the floor." },
    { "Deep Bass", "Mud Pump", "Deep,Rubbery", "wave=1 cutoff=4 resonance=60 envmod=55 decay=35 accent=70",
      "Short square pumps in the low mids." },
    { "Deep Bass", "Rumble Strip", "Deep,Dark", "cutoff=0 resonance=88 envmod=62 decay=30 accent=72 +calibration=low-trim-unit",
      "A low-trimmed unit with the cutoff closed and the resonance up: a short growl on every note." },
    { "Deep Bass", "Coal Seam", "Deep,Gritty", "wave=1 cutoff=3 resonance=55 envmod=38 decay=48 accent=55 voicing=Glass_Low_Lift dist_drive=35",
      "Square with a low lift and a little diode grit." },
    { "Deep Bass", "Pillow Bass", "Deep,Smooth", "wave=1 cutoff=12 resonance=30 envmod=20 decay=75 accent=35 +calibration=soft-accent-unit",
      "Soft, long and polite." },
    { "Deep Bass", "Dark Matter", "Deep,Squelchy", "cutoff=0 resonance=82 envmod=60 decay=40 accent=70 +calibration=sub-heavy-unit",
      "Sub-heavy trims under a closed squelch." },
    { "Deep Bass", "Weighted", "Deep,Gritty", "wave=1 cutoff=7 resonance=50 envmod=35 decay=55 accent=60 voicing=Grind_Deep dist_drive=35",
      "Hard clip on the top, the low band kept clean." },
    { "Deep Bass", "Abyss", "Deep,Spacey", "wave=1 cutoff=0 resonance=60 envmod=30 decay=80 accent=50 +reverb=dark-hall",
      "A low square in a dark hall." },

    // ---- Scream: accents at full, resonance near the top ------------------------------
    { "Scream", "Siren Song", "Screaming,Talking", "cutoff=15 resonance=95 envmod=60 decay=50 accent=100 +calibration=hot-accent-unit accvol_couple=0 acc_vol=55",
      "Hot accent unit, filter accent at full with the volume accent held back: put four accents in a row and listen to it climb." },
    { "Scream", "Kettle", "Screaming,Whistling", "cutoff=20 resonance=98 envmod=55 decay=40 accent=100 voicing=Glass dist_drive=45",
      "Near self-oscillation with diode drive to fatten the whistle." },
    { "Scream", "Banshee", "Screaming,Aggressive", "cutoff=10 resonance=96 envmod=70 decay=55 accent=100 voicing=Grind dist_drive=55 +limiter=soft-catch",
      "Full accent into a hard clipper. The scream that cuts through anything." },
    { "Scream", "Tea Time Scream", "Screaming,Hollow", "wave=1 cutoff=18 resonance=94 envmod=60 decay=45 accent=100 +calibration=long-tail-unit",
      "Square scream on a unit with big accent trims." },
    { "Scream", "Rising Panic", "Screaming,Talking", "cutoff=8 resonance=97 envmod=50 decay=40 accent=100 voicing=Amber dist_drive=50 +calibration=slow-sweep-unit",
      "Slow sweep capacitor: each accent in a run goes higher." },
    { "Scream", "Wail", "Screaming,Smooth", "cutoff=12 resonance=93 envmod=65 decay=60 accent=95 acc_decay=25",
      "Longer accent decay turns the scream into a wail." },
    { "Scream", "Feedback Loop", "Screaming,Whistling", "cutoff=25 resonance=100 envmod=45 decay=50 accent=100 +calibration=high-loop-unit +limiter=safety-clip",
      "Loop gain trimmed up, resonance full. Whistles on its own." },
    { "Scream", "Hot Kettle", "Screaming,Hollow", "wave=1 cutoff=14 resonance=96 envmod=58 decay=48 accent=100 voicing=Hollow dist_drive=55",
      "Square scream through the scooped clipper." },
    { "Scream", "Alarm Bell", "Screaming,Noisy", "cutoff=5 resonance=95 envmod=75 decay=45 accent=100 voicing=Rip_Seam dist_drive=55",
      "Gated fuzz on full accents. Alarmed." },
    { "Scream", "Pressure Cooker", "Screaming,Gritty", "cutoff=10 resonance=94 envmod=62 decay=50 accent=100 voicing=Desk_Red dist_drive=55 +calibration=hot-accent-unit",
      "Console overdrive on a hot accent unit." },
    { "Scream", "Howl", "Screaming,Dark", "wave=1 cutoff=6 resonance=98 envmod=66 decay=56 accent=98 voicing=Amber_Shade dist_drive=50",
      "Dark resonant colour filter after the overdrive. A low howl." },
    { "Scream", "Shriek", "Screaming,Bright", "cutoff=18 resonance=96 envmod=60 decay=42 accent=100 sweep_couple=0 sweep_amt=0 accvol_couple=0 acc_vol=45",
      "Sweep amount at the spiky end: accents are sharp clicks of light." },
    { "Scream", "Peak Hour", "Screaming,Aggressive", "cutoff=12 resonance=92 envmod=68 decay=50 accent=100 voicing=Grind_Tight dist_drive=60 +delay=dotted-eighth",
      "The big-room scream, with a dotted delay." },
    { "Scream", "Scream Therapy", "Screaming,Warm", "cutoff=9 resonance=97 envmod=55 decay=45 accent=100 voicing=Thick_Fur dist_drive=45 +calibration=long-tail-unit",
      "Furry fuzz and long accent tails." },

    // ---- Whistle: the resonant peak as the voice ---------------------------------------
    { "Whistle", "Steam Pipe", "Whistling,Bright", "mod_on=0 cutoff=40 resonance=100 envmod=25 decay=60 accent=60 +calibration=high-loop-unit",
      "Resonance at full on a hot loop: a pure whistle on every note." },
    { "Whistle", "Bird Call", "Whistling,Spacey", "mod_on=0 cutoff=55 resonance=98 envmod=30 decay=40 accent=70 +calibration=high-loop-unit +delay=ping-wide",
      "High, chirping whistles bouncing left and right." },
    { "Whistle", "Glass Harp", "Whistling,Smooth", "mod_on=0 wave=1 cutoff=50 resonance=100 envmod=20 decay=70 accent=50 +calibration=high-loop-unit +reverb=bright-chamber",
      "A glassy ringing square in a bright chamber." },
    { "Whistle", "Thin Whistle", "Whistling,Bright", "mod_on=0 cutoff=60 resonance=97 envmod=35 decay=30 accent=60",
      "Stock unit, resonance near the top, cutoff high." },
    { "Whistle", "Ghost Tone", "Whistling,Spacey", "mod_on=1 mod_hires=1 cutoff=45 resonance=85 envmod=25 decay=60 accent=60 +reverb=dark-hall",
      "High resonance mod: the filter sings at the cutoff." },
    { "Whistle", "Singing Wire", "Whistling,Talking", "mod_on=1 mod_hires=1 mod_tracking=70 cutoff=35 resonance=90 envmod=15 decay=70 accent=50",
      "The filter follows the keys, so the whistle plays the melody." },
    { "Whistle", "Ether Line", "Whistling,Spacey", "mod_on=1 mod_hires=1 mod_tracking=100 mod_overdrive=5 cutoff=30 resonance=92 envmod=10 decay=80 accent=40 +delay=quarter-space",
      "Full tracking and high resonance: a melody played by the filter itself." },
    { "Whistle", "Tin Flute", "Whistling,Hollow", "mod_on=0 wave=1 cutoff=65 resonance=96 envmod=30 decay=35 accent=65 +calibration=high-loop-unit",
      "Thin, breathy square whistle." },
    { "Whistle", "Night Owl", "Whistling,Dark", "mod_on=1 mod_hires=1 cutoff=25 resonance=88 envmod=40 decay=55 accent=70 +delay=dub-echo",
      "Low whistle with long resonant echoes." },
    { "Whistle", "Whistle Stop", "Whistling,Squelchy", "mod_on=0 cutoff=48 resonance=99 envmod=45 decay=25 accent=80 voicing=Glass dist_drive=30 +calibration=high-loop-unit",
      "Short whistling plucks, lightly driven." },

    // ---- Liquid: square leads with long slides -----------------------------------------
    { "Liquid Line", "Liquid Lead", "Liquid,Smooth", "cutoff=30 resonance=55 envmod=35 decay=70 accent=50 slide_time=80 +chorus=liquid-motion +delay=dotted-eighth +reverb=bright-chamber",
      "Square, long slides, mid cutoff, low resonance, chorus and space." },
    { "Liquid Line", "Mercury", "Liquid,Bright", "cutoff=25 resonance=65 envmod=40 decay=75 accent=55 slide_time=85 +chorus=slow-swirl",
      "Slow swirling chorus on long glides." },
    { "Liquid Line", "Honey Drip", "Liquid,Warm", "cutoff=20 resonance=60 envmod=30 decay=80 accent=45 slide_time=75 mod_on=1 mod_amp_sustain=35 +chorus=gentle-width +reverb=small-plate",
      "Amp sustain holds the notes, so ties really hold." },
    { "Liquid Line", "Silk Thread", "Liquid,Smooth", "cutoff=35 resonance=50 envmod=25 decay=65 accent=40 slide_time=90 +chorus=deep-throb",
      "Fine and smooth with deep chorus movement." },
    { "Liquid Line", "Water Glass", "Liquid,Bright", "cutoff=40 resonance=70 envmod=30 decay=60 accent=50 slide_time=70 +chorus=fast-shimmer +reverb=bright-chamber",
      "A clear, ringing square with a quick shimmer." },
    { "Liquid Line", "Molasses", "Liquid,Dark", "cutoff=15 resonance=58 envmod=35 decay=85 accent=50 slide_time=95 mod_on=1 mod_amp_sustain=50 mod_soft_attack=6 +chorus=liquid-motion",
      "Thick, slow and dark, with soft attacks." },
    { "Liquid Line", "Lazy River", "Liquid,Smooth", "cutoff=28 resonance=62 envmod=42 decay=72 accent=55 slide_time=88 +delay=dotted-wide-dub",
      "Long glides drifting through dotted echoes." },
    { "Liquid Line", "Saw Silk", "Liquid,Warm", "wave=0 cutoff=25 resonance=55 envmod=30 decay=70 accent=45 slide_time=80 +chorus=wide-fatness",
      "The liquid recipe on a saw, doubled and wide." },
    { "Liquid Line", "Oil Slick", "Liquid,Squelchy", "cutoff=10 resonance=75 envmod=50 decay=78 accent=60 slide_time=85 voicing=Amber dist_drive=25 +chorus=gentle-width",
      "A darker, squelchier liquid with a touch of overdrive." },
    { "Liquid Line", "Moon Pool", "Liquid,Spacey", "cutoff=32 resonance=60 envmod=28 decay=82 accent=40 slide_time=92 mod_on=1 mod_amp_sustain=40 +chorus=slow-swirl +reverb=dark-hall",
      "Long held glides in a dark hall." },
    { "Liquid Line", "Wax Bloom", "Liquid,Warm", "cutoff=22 resonance=68 envmod=45 decay=76 accent=58 slide_time=78 +chorus=deep-throb +delay=quarter-space",
      "Slow blobs of tone rising and falling." },
    { "Liquid Line", "Glide Path", "Liquid,Smooth", "cutoff=26 resonance=64 envmod=38 decay=66 accent=52 slide_time=100 +chorus=liquid-motion",
      "Slide time at full: every slide takes its time." },

    // ---- Pluck: short, percussive, clicky ----------------------------------------------
    { "Pluck", "Click Track", "Percussive,Bright", "mod_on=0 cutoff=70 resonance=20 envmod=50 decay=0 accent=60",
      "Short decay, high cutoff, low resonance: ticks rather than notes." },
    { "Pluck", "Woodblock", "Percussive,Hollow", "mod_on=0 wave=1 cutoff=55 resonance=40 envmod=60 decay=5 accent=70",
      "Hollow square knocks." },
    { "Pluck", "Rubber Band", "Plucky,Rubbery", "mod_on=0 cutoff=35 resonance=70 envmod=65 decay=8 accent=75",
      "Twangy, elastic plucks." },
    { "Pluck", "Staccato Saw", "Plucky,Clean", "mod_on=0 cutoff=45 resonance=55 envmod=50 decay=12 accent=60 +reverb=short-ambience",
      "Clean short saw notes with a hint of space." },
    { "Pluck", "Pip Squeak", "Plucky,Squelchy", "mod_on=0 wave=1 cutoff=58 resonance=90 envmod=35 decay=2 accent=70",
      "Tiny high square pips, the resonance ringing on each." },
    { "Pluck", "Tick Tock", "Percussive,Clean", "mod_on=1 mod_amp_decay=60 cutoff=60 resonance=30 envmod=40 decay=5 accent=60",
      "Short amp decay mod makes every note a click." },
    { "Pluck", "Knuckle", "Percussive,Gritty", "mod_on=0 cutoff=30 resonance=60 envmod=70 decay=4 accent=85 voicing=Desk_Red dist_drive=45",
      "Hard short hits with console grit." },
    { "Pluck", "Marimba Bass", "Plucky,Warm", "mod_on=0 wave=1 cutoff=50 resonance=35 envmod=45 decay=10 accent=50 +reverb=small-plate",
      "Woody square plucks on a short plate." },
    { "Pluck", "Dry Snap", "Percussive,Bright", "mod_on=0 cutoff=55 resonance=45 envmod=55 decay=2 accent=70 +calibration=fast-sweep-unit",
      "The shortest snap, with fast accent sweeps." },
    { "Pluck", "Short Fuse", "Plucky,Aggressive", "mod_on=0 cutoff=25 resonance=80 envmod=70 decay=6 accent=88 voicing=Grind dist_drive=45",
      "Short squelchy bursts through a pedal." },

    // ---- Space: echoes and big rooms ----------------------------------------------------
    { "Space", "Echo Chamber", "Spacey,Squelchy", "wave=1 cutoff=12 resonance=85 envmod=55 decay=70 accent=70 +delay=dub-echo +reverb=dark-hall",
      "Long resonant echoes and a dark hall." },
    { "Space", "Orbit", "Spacey,Squelchy", "cutoff=10 resonance=88 envmod=60 decay=65 accent=75 +delay=filtered-haze +reverb=deep-space",
      "Squelch in orbit, trailing filtered repeats." },
    { "Space", "Fog Machine", "Spacey,Dark", "wave=1 cutoff=5 resonance=90 envmod=50 decay=80 accent=65 +delay=long-dark-tail +reverb=endless-void",
      "Long dark tails that fill the room with fog." },
    { "Space", "Cave Drip", "Spacey,Plucky", "cutoff=30 resonance=92 envmod=45 decay=20 accent=80 +delay=dotted-wide-dub +reverb=stone-chamber",
      "Short resonant drips echoing in stone." },
    { "Space", "Satellite", "Spacey,Smooth", "wave=1 cutoff=20 resonance=86 envmod=50 decay=55 accent=70 +delay=triplet-bounce +reverb=dark-hall +chorus=slow-swirl",
      "Triplet echoes, slow chorus, a dark hall." },
    { "Space", "Night Shift", "Spacey,Squelchy", "cutoff=8 resonance=89 envmod=62 decay=60 accent=78 voicing=Glass dist_drive=30 +delay=dub-echo +reverb=deep-space",
      "Driven squelch sent far away." },
    { "Space", "Dub Plate", "Spacey,Vintage", "wave=1 cutoff=14 resonance=80 envmod=55 decay=58 accent=72 +delay=dotted-wide-dub +reverb=spring-box +eq=dark-tape",
      "Dotted echoes, a springy tank and a dull top end." },
    { "Space", "Hollow Moon", "Spacey,Hollow", "wave=1 cutoff=4 resonance=93 envmod=48 decay=75 accent=68 +delay=long-dark-tail +reverb=deep-space",
      "Hollow square bubbles drifting in space." },
    { "Space", "Tunnel Vision", "Spacey,Hollow", "cutoff=18 resonance=87 envmod=58 decay=48 accent=76 +delay=filtered-haze +reverb=stone-chamber +eq=telephone",
      "Narrow band, filtered echoes: heard from down a tunnel." },
    { "Space", "Drift Ice", "Spacey,Smooth", "wave=1 cutoff=25 resonance=82 envmod=40 decay=72 accent=60 +chorus=wet-wash +reverb=endless-void",
      "Washed in chorus and an endless tail." },

    // ---- Overdrive (Mods): drive into the filter, which quenches the resonance ---------
    { "Overdrive", "Fat Filter", "Warm,Squelchy", "mod_overdrive=35 cutoff=15 resonance=70 envmod=55 decay=45 accent=75",
      "Overdrive into the filter fattens the tone and rounds the peak." },
    { "Overdrive", "Quenched Peak", "Gritty,Warm", "mod_overdrive=55 cutoff=20 resonance=90 envmod=60 decay=50 accent=80",
      "High resonance, but the drive into the ladder holds it back. Compare with a pedal after the filter." },
    { "Overdrive", "Ladder Burn", "Gritty,Aggressive", "mod_overdrive=70 mod_muffler=1 cutoff=10 resonance=80 envmod=65 decay=40 accent=85",
      "Heavy overdrive into the filter, softened by the muffler." },
    { "Overdrive", "Warm Front", "Warm,Smooth", "wave=1 mod_overdrive=25 cutoff=18 resonance=75 envmod=50 decay=55 accent=70",
      "A little overdrive on the square for warmth." },
    { "Overdrive", "Overcooked", "Aggressive,Gritty", "mod_overdrive=85 mod_muffler=2 cutoff=8 resonance=85 envmod=70 decay=45 accent=85 +limiter=safety-clip",
      "Overdrive near the top and the hard muffler. Loud and thick." },
    { "Overdrive", "Thick Squelch", "Squelchy,Warm", "mod_overdrive=45 cutoff=6 resonance=88 envmod=75 decay=50 accent=82",
      "A fat, saturated squelch from inside the filter." },
    { "Overdrive", "Pre And Post", "Gritty,Squelchy", "mod_overdrive=40 cutoff=14 resonance=86 envmod=62 decay=48 accent=80 voicing=Grind dist_drive=45",
      "Overdrive before the filter and a pedal after it, both at once." },
    { "Overdrive", "Muffled Fist", "Gritty,Dark", "wave=1 mod_overdrive=60 mod_muffler=2 cutoff=12 resonance=78 envmod=58 decay=42 accent=80",
      "Square overdriven into the filter with the hard muffler." },

    // ---- Filter FM (Mods): the output modulating the cutoff ----------------------------
    { "Filter FM", "Edge Rider", "Screaming,Gritty", "mod_overdrive=50 mod_filter_fm=60 mod_muffler=1 cutoff=15 resonance=88 envmod=60 decay=45 accent=90",
      "Overdrive, filter FM and the soft muffler: an FM scream with an edge." },
    { "Filter FM", "Period Doubler", "Chaotic,Screaming", "mod_overdrive=60 mod_filter_fm=90 mod_muffler=1 cutoff=20 resonance=95 envmod=55 decay=50 accent=95 +limiter=safety-clip",
      "On the edge of chaos: the tone flips between states from bar to bar." },
    { "Filter FM", "Metal Tongue", "Gritty,Bright", "mod_filter_fm=40 cutoff=25 resonance=85 envmod=55 decay=40 accent=80 accvol_couple=0 acc_vol=55",
      "Moderate filter FM gives a metallic edge to the squelch." },
    { "Filter FM", "Buzz Saw", "Aggressive,Gritty", "mod_filter_fm=70 mod_overdrive=30 cutoff=10 resonance=80 envmod=65 decay=35 accent=85 voicing=Glass dist_drive=35",
      "Buzzing FM sidebands, lightly driven." },
    { "Filter FM", "Bell Bass", "Bright,Plucky", "wave=1 mod_filter_fm=50 cutoff=35 resonance=70 envmod=40 decay=30 accent=70 +reverb=small-plate",
      "FM on a square gives bell-like plucks." },
    { "Filter FM", "Chaos Garden", "Chaotic,Aggressive", "mod_overdrive=65 mod_filter_fm=100 mod_muffler=2 cutoff=18 resonance=97 envmod=60 decay=55 accent=100 +limiter=hard-wall",
      "Everything at full. Unstable by design, held down by the limiter." },
    { "Filter FM", "Grit Shimmer", "Gritty,Spacey", "wave=1 mod_filter_fm=30 mod_overdrive=40 cutoff=20 resonance=86 envmod=58 decay=48 accent=82 +delay=sixteenth-ripple",
      "Gentle FM and tiny repeats for a shimmering grit." },
    { "Filter FM", "Snarl FM", "Screaming,Aggressive", "mod_filter_fm=55 mod_overdrive=45 mod_muffler=1 cutoff=12 resonance=90 envmod=66 decay=44 accent=92 voicing=Hollow dist_drive=45",
      "An FM scream pushed into the scooped clipper." },

    // ---- Tracking (Mods): the cutoff follows the keys ----------------------------------
    { "Tracking", "Pitch Gate", "Percussive,Dark", "mod_tracking=100 cutoff=0 resonance=70 envmod=30 decay=40 accent=70",
      "Full tracking with the cutoff closed: low notes nearly vanish, so pitch makes the rhythm." },
    { "Tracking", "Keyed Filter", "Squelchy,Bright", "mod_tracking=60 cutoff=15 resonance=80 envmod=45 decay=50 accent=75",
      "High notes brighter, low notes darker, like a keyboard synth." },
    { "Tracking", "Ladder Melody", "Whistling,Talking", "mod_tracking=100 mod_hires=1 cutoff=20 resonance=95 envmod=20 decay=60 accent=60",
      "The resonance tracks the notes: a whistler for melodies." },
    { "Tracking", "Follow Me", "Squelchy,Smooth", "wave=1 mod_tracking=45 cutoff=20 resonance=75 envmod=50 decay=55 accent=70",
      "Moderate tracking on the square keeps octave jumps even." },
    { "Tracking", "Vanishing Point", "Percussive,Squelchy", "mod_tracking=100 mod_ext_range=1 cutoff=0 resonance=80 envmod=35 decay=35 accent=80 accvol_couple=0 acc_vol=20",
      "Extended range lets low notes fall away completely." },
    { "Tracking", "Even Keel", "Squelchy,Clean", "mod_tracking=80 cutoff=25 resonance=70 envmod=45 decay=45 accent=65",
      "Strong tracking for a level brightness across the range." },

    // ---- Sub (Mods): a sub-oscillator under the line -----------------------------------
    { "Sub", "Sub Floor", "Deep,Warm", "wave=1 mod_sub_level=50 mod_sub_oct=0 cutoff=10 resonance=60 envmod=45 decay=50 accent=60",
      "A square sub an octave down under a low square line." },
    { "Sub", "Two Storey", "Deep,Squelchy", "mod_sub_level=40 cutoff=15 resonance=80 envmod=60 decay=45 accent=75",
      "Squelch on top, sub underneath." },
    { "Sub", "Deep Foundation", "Deep,Dark", "wave=1 mod_sub_level=60 mod_sub_oct=1 cutoff=5 resonance=50 envmod=35 decay=55 accent=55",
      "Sub two octaves down. Check it on big speakers." },
    { "Sub", "Sub Squelch", "Deep,Squelchy", "mod_sub_level=35 cutoff=8 resonance=88 envmod=70 decay=48 accent=82 voicing=Glass_Low_Lift dist_drive=35",
      "Driven squelch on a sub." },
    { "Sub", "Bunker Bass", "Deep,Warm", "wave=1 mod_overdrive=30 mod_muffler=2 mod_sub_level=45 cal_c21=60 cutoff=8 resonance=35 envmod=40 decay=50 accent=60",
      "Overdrive, the hard muffler, a sub and a big coupling capacitor: the sub-heavy modded bass." },
    { "Sub", "Lower Hold", "Deep,Squelchy", "mod_sub_level=55 mod_sub_oct=1 cutoff=12 resonance=75 envmod=55 decay=40 accent=70",
      "A two-octave sub under a mid squelch." },
    { "Sub", "Grounded", "Deep,Gritty", "wave=1 mod_sub_level=30 cutoff=18 resonance=82 envmod=50 decay=52 accent=72 voicing=Iron_Sub dist_drive=40",
      "Square and sub through a console stage." },
    { "Sub", "Underpass", "Deep,Plucky", "mod_sub_level=50 cutoff=6 resonance=85 envmod=65 decay=45 accent=78 +delay=eighth-tight",
      "Sub-weighted squelch with a tight eighth echo." },

    // ---- Sustain (Mods): amp envelope, attack, accent sweep and range mods -------------
    { "Sustain", "Held Breath", "Droning,Talking", "wave=1 mod_amp_sustain=60 cutoff=15 resonance=85 envmod=45 decay=90 accent=60",
      "Amp sustain keeps ties alive, the filter keeps talking." },
    { "Sustain", "Endless Note", "Droning,Spacey", "mod_amp_sustain=100 cutoff=8 resonance=90 envmod=30 decay=100 accent=50 +reverb=deep-space",
      "Full sustain: a drone for long ties and slow cutoff rides." },
    { "Sustain", "Accent Only", "Percussive,Screaming", "mod_soft_attack=30 mod_amp_decay=7 cutoff=20 resonance=85 envmod=55 decay=30 accent=100 voicing=Glass dist_drive=30",
      "Soft attack at full and the shortest amp decay: only accented notes come through." },
    { "Sustain", "Short Amp", "Percussive,Squelchy", "mod_amp_decay=80 cutoff=25 resonance=80 envmod=60 decay=40 accent=80 accvol_couple=0 acc_vol=20",
      "A short amp decay chops every note." },
    { "Sustain", "Soft Landing", "Smooth,Warm", "wave=1 mod_soft_attack=12 cutoff=20 resonance=75 envmod=40 decay=60 accent=70",
      "Soft attacks on plain notes, accents still punch." },
    { "Sustain", "Long Decay Wah", "Talking,Squelchy", "mod_amp_sustain=30 mod_ext_range=1 decay=100 cutoff=5 resonance=92 envmod=70 accent=75",
      "Extended Env Mod and a long decay: one long wah per note." },
    { "Sustain", "Slow Bloom", "Smooth,Droning", "wave=1 mod_soft_attack=25 mod_amp_sustain=50 env_attack=60 cutoff=12 resonance=80 envmod=55 decay=85 accent=50 +chorus=slow-swirl",
      "Slow filter attack and soft amp attack: notes bloom." },
    { "Sustain", "Ghost Notes", "Percussive,Spacey", "mod_soft_attack=20 mod_amp_decay=25 cutoff=30 resonance=88 envmod=60 decay=30 accent=95 +delay=dotted-eighth voicing=Glass dist_drive=30",
      "Plain notes are whispers, accents speak through light diode drive, the delay fills the gaps." },
    { "Sustain", "Organ Pipe", "Droning,Warm", "wave=1 mod_amp_sustain=80 cutoff=30 resonance=40 envmod=10 decay=100 accent=30 +chorus=fat-double +reverb=stone-chamber",
      "Steady held square tones, doubled, in a stone room." },
    { "Sustain", "Fast Sweep Snap", "Plucky,Squelchy", "mod_sweep_speed=1 cutoff=15 resonance=88 envmod=55 decay=40 accent=95 accvol_couple=0 acc_vol=45",
      "Fast accent sweep mod: accents snap open and shut." },
    { "Sustain", "Slow Sweep Wail", "Screaming,Talking", "mod_sweep_speed=2 cutoff=10 resonance=94 envmod=50 decay=50 accent=100",
      "Slow accent sweep mod: runs of accents stack into a wail." },
    { "Sustain", "Extended Squelch", "Squelchy,Bright", "mod_ext_range=1 cutoff=10 resonance=88 envmod=60 decay=45 accent=80",
      "Extended cutoff and Env Mod ranges: further down, further up." },

    // ---- Utility -----------------------------------------------------------------------
    { "Utility", "Init Line", "Clean", "",
      "Every knob at its default on the reference unit, Mods off, no effects. Start here." },
    { "Utility", "Clean Sub", "Deep,Clean", "wave=1 cutoff=0 resonance=20 envmod=10 decay=60 accent=40",
      "Square sub-bass, cutoff nearly closed, low resonance." },
    { "Utility", "Bare Saw", "Bright,Clean", "cutoff=100 resonance=0 envmod=0 decay=100 accent=0",
      "Filter open, no envelope: the oscillator as it is." },
    { "Utility", "Bare Square", "Bright,Clean", "wave=1 cutoff=100 resonance=0 envmod=0 decay=100 accent=0",
      "The square from the shaper, filter open." },
    { "Utility", "Accent Lab", "Screaming,Clean", "cutoff=10 resonance=100 envmod=0 decay=50 accent=100",
      "Env Mod at zero isolates the accent sweep. Play runs of accents to hear the capacitor charge." },
};
// clang-format on

std::map<std::string, SoundRef>& soundRegistry()
{
    static std::map<std::string, SoundRef> r;
    return r;
}

const Family& family (std::string_view name)
{
    for (const Family& f : kFamilies)
        if (name == f.name)
            return f;
    fail ("unknown sound family '" + std::string (name) + "'");
}

std::vector<std::string> splitTags (std::string_view s)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s)
    {
        if (c == ',') { out.push_back (cur); cur.clear(); }
        else cur += c;
    }
    if (! cur.empty()) out.push_back (cur);
    return out;
}
}

const SoundRef& findSound (std::string_view name)
{
    const auto& r = soundRegistry();
    const auto it = r.find (std::string (name));
    if (it == r.end()) fail ("unknown sound '" + std::string (name) + "'");
    return it->second;
}

void addSounds (Library& lib)
{
    for (const SoundRecipe& s : kSounds)
    {
        const std::string context = std::string ("synth/") + s.name;
        Patch patch = baseSound();
        applySpec (patch, family (s.family).base, context);
        applySpec (patch, s.spec, context);

        SoundRef ref;
        ref.name = s.name;
        ref.family = s.family;
        ref.patch = patch;
        ref.character = splitTags (s.character);
        if (! soundRegistry().emplace (s.name, ref).second) fail ("duplicate sound " + context);

        std::vector<std::string> tags { s.family, waveformTag (patch.values), distortionTag (patch.values) };
        for (const auto& c : ref.character) tags.push_back (c);
        if (param (patch.values, acid::kModOn) >= 0.5f) tags.push_back ("Mods");

        const std::string slug = slugify (s.name);
        Entry e;
        e.levelKey = "synth/" + slug;
        e.levelParam = acid::kMasterVolume;
        applyLevelTrim (patch.values, lib.levelTrims().trim (e.levelKey));
        e.data = makeParamPreset (acid::PresetType::Synth, patch, s.name, joinTags (tags), s.description);
        e.path = "synth/" + slug + ".mw12f";
        e.group = s.family;
        lib.add (std::move (e));
    }
}
}
