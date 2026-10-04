// Scenes: a sound, its effects, and bank A filled with a main line (A1) and the variations
// the line's style calls for, all in the scene's key. The arpeggiator bank gets four arp
// patterns so ARP mode has something to play. Plus the first-load default scene, whose
// bank A is a tour of the library.
#include "Patterns.h"

#include "acid/seq/PatternOps.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace factory
{
using acid::seq::Pattern;

namespace
{
struct SceneRecipe
{
    const char* line;          // the main line, and the scene's name
    const char* sound;         // a synth preset by name
    int key;                   // pitch class of the root, 0 = C
    int bpm;                   // the tempo the scene is written for (saved in the description)
    const char* spec;          // changes to the sound for this scene
};

// clang-format off
const SceneRecipe kScenes[] = {
    // ---- Chicago jack -----------------------------------------------------------------
    { "Jack Street",       "Jack Pluck",           0, 122, "voicing=Desk_Warm dist_drive=30" },
    { "Box Jam",           "Old Tape Rub",         5, 120, "+reverb=club-room" },
    { "Back Room Jack",    "Attic Dust",           2, 124, "+reverb=tight-room" },
    { "Pump Room",         "Low Trim Groove",      7, 121, "voicing=Desk_Warm dist_drive=40 +reverb=club-room" },
    { "Station Wagon",     "Init Line",            9, 122, "cutoff=30 resonance=80 envmod=55 decay=50 +reverb=club-room" },
    { "Hot Plate",         "Late Gate Shuffle",    4, 123, "voicing=Iron_Sub dist_drive=30" },
    { "Corner Store",      "Chalk Line",           8, 120, "+reverb=club-room +eq=dark-tape" },
    { "Late Bus",          "Copper Wire",          3, 122, "voicing=Desk_Warm dist_drive=35 +reverb=tight-room" },
    { "Loop Jam",          "Rubber Floor",         5, 124, "+reverb=club-room" },
    { "Warehouse Party",   "Stretched Octaves",   10, 125, "voicing=Desk_Red dist_drive=30 +reverb=club-room" },
    { "Seven Note Jack",   "Snap Back",            1, 121, "+reverb=tight-room" },
    { "Skate Rink",        "Thin Coupling Pluck",  6, 126, "voicing=Desk_Warm dist_drive=45" },

    // ---- Anthem ------------------------------------------------------------------------
    { "Bright Signal",     "Peak Hour",            9, 130, "" },
    { "All Hands",         "Banshee",              7, 132, "+delay=dotted-eighth" },
    { "Crowd Lift",        "Siren Song",           5, 128, "voicing=Grind_Deep dist_drive=50 +delay=dotted-eighth" },
    { "Beacon Call",       "Kettle",               4, 134, "+delay=dotted-eighth" },
    { "Peak Lights",       "Hollow Scream",       10, 130, "+delay=dotted-eighth" },
    { "Whistle Post",      "Tea Time Scream",      2, 129, "voicing=Grind_Deep dist_drive=45 +delay=dotted-eighth" },
    { "Strobe Hymn",       "Pressure Cooker",      0, 131, "+delay=dotted-eighth" },
    { "Arms Aloft",        "Hot Kettle",           6, 133, "+delay=dotted-eighth +limiter=soft-catch" },
    { "Long Night",        "Rising Panic",         8, 128, "+delay=dotted-eighth" },
    { "Sky Writer",        "Howl",                11, 132, "+delay=dotted-wide-dub" },
    { "Foghorn Choir",     "Scream Therapy",       1, 135, "+delay=dotted-eighth" },
    { "Night Hymn",        "Feedback Loop",        3, 127, "voicing=Grind_Deep dist_drive=40 +delay=dotted-eighth" },

    // ---- Rolling stacked lines ------------------------------------------------------------
    { "Conveyor",          "Pedal Snarl",          9, 132, "" },
    { "Wheel Spin",        "Two Storey",           7, 134, "+delay=eighth-tight" },
    { "Stack Pile",        "Grit Engine",          5, 130, "+delay=dotted-eighth" },
    { "Rolling Stock",     "Pocket Squelch",       4, 133, "+delay=eighth-tight" },
    { "Crank Shaft",       "Rust Belt",            2, 135, "" },
    { "Turbine",           "Sticky Keys",          0, 131, "+delay=sixteenth-ripple" },
    { "Treadmill",         "Underpass",            6, 132, "" },
    { "Spinning Plate",    "Elastic",             10, 130, "+delay=dotted-eighth" },
    { "Gear Tooth",        "Chrome Teeth",         8, 136, "" },
    { "Fly Wheel",         "Lean Machine",         3, 134, "+delay=eighth-tight" },
    { "Steady Roll",       "Keyed Filter",        11, 132, "voicing=Glass dist_drive=40 +delay=dotted-eighth" },
    { "Lathe",             "Fat Filter",           1, 133, "voicing=Grind_Tight dist_drive=45" },

    // ---- Melodic and modal --------------------------------------------------------------
    { "Glass River",       "Liquid Lead",          2, 112, "mod_on=1 mod_amp_sustain=30" },
    { "Paper Lantern",     "Mercury",              9,  96, "mod_on=1 mod_amp_sustain=35 +reverb=bright-chamber" },
    { "Slow Coast",        "Molasses",             4,  84, "+delay=dotted-wide-dub +reverb=dark-hall" },
    { "Morning Fold",      "Water Glass",          7, 118, "mod_on=1 mod_amp_sustain=25 +delay=dotted-eighth" },
    { "Rain On Tin",       "Honey Drip",           5, 100, "+delay=quarter-space" },
    { "Lantern Walk",      "Lazy River",           0,  92, "mod_on=1 mod_amp_sustain=30 +reverb=small-plate" },
    { "Small Hours",       "Moon Pool",           10,  78, "+delay=long-dark-tail" },
    { "Kite String",       "Silk Thread",          5, 104, "mod_on=1 mod_amp_sustain=30 +delay=dotted-eighth +reverb=bright-chamber" },
    { "Tide Chart",        "Glide Path",           8, 108, "mod_on=1 mod_amp_sustain=35 +reverb=small-plate" },
    { "Inkwell",           "Soft Accent Glide",    1,  88, "mod_on=1 mod_amp_sustain=40 +chorus=liquid-motion +delay=dotted-wide-dub +reverb=dark-hall" },
    { "Brass Kettle",      "Oil Slick",            3, 122, "mod_on=1 mod_amp_sustain=25 +delay=dotted-eighth" },
    { "Hill Road",         "Wax Bloom",           11, 126, "mod_on=1 mod_amp_sustain=30 +reverb=bright-chamber" },

    // ---- Minimal -------------------------------------------------------------------------
    { "Empty Room",        "Hollow Moon",          5, 124, "" },
    { "Pale Hours",        "Fog Machine",          9, 122, "" },
    { "Dust Motes",        "Orbit",                4, 126, "" },
    { "Low Ceiling",       "Echo Chamber",         7, 120, "" },
    { "Thin Line",         "Night Owl",            2, 124, "+reverb=deep-space" },
    { "Wall Clock",        "Cave Drip",            0, 125, "accvol_couple=0 acc_vol=50" },
    { "Grey Light",        "Tunnel Vision",        8, 123, "" },
    { "Echo Floor",        "Midnight Ink",         6, 127, "+delay=dub-echo +reverb=deep-space" },
    { "Faint Pulse",       "Satellite",           10, 124, "" },
    { "Hush",              "Hollow Talker",        3, 122, "+delay=long-dark-tail +reverb=dark-hall" },
    { "Neon Drip",         "Ghost Tone",          11, 128, "+delay=filtered-haze" },
    { "Quiet Machine",     "Ringing Loop",         1, 126, "accvol_couple=0 acc_vol=50 +delay=filtered-haze +reverb=dark-hall" },

    // ---- Hard and distorted --------------------------------------------------------------
    { "Iron Gate",         "Brick Dust",           9, 140, "envmod=30 +delay=eighth-tight +limiter=glue" },
    { "Hammer Line",       "Steel Toe",            7, 145, "envmod=28 +limiter=punch-keeper" },
    { "Rivet Gun",         "Torn Speaker",         5, 138, "cutoff=12 envmod=35 +delay=eighth-tight" },
    { "Triplet Furnace",   "Furnace",              4, 142, "envmod=38 +limiter=glue" },
    { "Pressure Plate",    "Split Crunch",         2, 144, "envmod=32 +delay=eighth-tight" },
    { "Smelter",           "Molten",               0, 148, "envmod=40 +limiter=punch-keeper" },
    { "Triplet Crusher",   "Scrapheap",           10, 140, "envmod=36" },
    { "Concrete",          "Hot Wire",             8, 136, "cutoff=12 envmod=30 +delay=short-slap" },
    { "Anvil Chorus",      "Ladder Burn",          6, 142, "envmod=34 voicing=Grind_Tight dist_drive=50" },
    { "Steel Mill",        "Overcooked",          11, 150, "envmod=38" },
    { "Grind House",       "Sputter Box",          3, 138, "cutoff=14 envmod=30 +limiter=glue" },
    { "Shrapnel",          "Buzz Saw",             1, 146, "envmod=35 +delay=eighth-tight" },

    // ---- Trance-leaning ---------------------------------------------------------------------
    { "Gatekeeper",        "Sour Drizzle",         9, 138, "+reverb=small-plate" },
    { "Offbeat Rush",      "Wet Floor",            5, 140, "" },
    { "Hyperlane",         "Swamp Gas",            7, 138, "" },
    { "Neon Run",          "Neon Puddle",          4, 137, "+delay=dotted-eighth" },
    { "Star Chart",        "Bird Call",            2, 140, "+reverb=dark-hall" },
    { "Uplifter",          "Juice Box",            0, 136, "+delay=triplet-bounce" },
    { "Night Drive",       "Night Shift",         10, 139, "" },
    { "Velocity",          "Pressure Valve",       8, 142, "+delay=dotted-eighth" },
    { "Laser Line",        "Edge Rider",           6, 138, "+delay=ping-wide" },
    { "Pulse Width",       "Gum Sole",            11, 140, "+delay=dotted-eighth +reverb=small-plate" },

    // ---- Slow and dubby -------------------------------------------------------------------
    { "Low Tide Dub",      "Dub Plate",            7,  90, "" },
    { "Heavy Air",         "Abyss",                5,  84, "+delay=dub-echo" },
    { "Rocking Chair",     "Long Vowel",           9,  96, "accvol_couple=0 acc_vol=55 +delay=dub-echo +reverb=spring-box" },
    { "Smoke Ring",        "Drift Ice",            4,  78, "+delay=dotted-wide-dub delay_spread=40 cho_spatial=20" },
    { "Slow Burn Dub",     "Held Breath",          2,  88, "+delay=dub-echo +reverb=dark-hall" },
    { "Under Water",       "Basement",             0,  92, "+delay=dotted-wide-dub +reverb=dark-hall" },
    { "Fog Bank",          "Endless Note",        10,  75, "+delay=long-dark-tail" },
    { "Heavy Lids",        "Velvet Rope",          8, 100, "+delay=long-dark-tail +reverb=stone-chamber" },
    { "Drip Feed",         "Slow Bloom",           6,  86, "+delay=dub-echo" },
    { "Long Shadow",       "Organ Pipe",           3,  80, "+delay=long-dark-tail" },

    // ---- Breaks and big beat -------------------------------------------------------------
    { "Crate Stack",       "Alarm Bell",           9, 126, "+eq=mid-scoop" },
    { "Breakbeat Jumper",  "Folded Light",         5, 124, "+eq=mid-scoop" },
    { "Big Shoes",         "Paper Cutter",         7, 128, "+eq=smile" },
    { "Block Party",       "Nasal Drive",          4, 122, "+eq=mid-scoop" },
    { "Turntable",         "Snarl FM",             2, 130, "+eq=mid-scoop" },
    { "Skip Step",         "Grit Shimmer",         0, 120, "voicing=Rip_Seam dist_drive=50 +eq=mid-scoop" },
    { "Funk Engine",       "Knuckle",             10, 118, "+eq=smile +delay=eighth-tight" },
    { "Speaker Stack",     "Coal Seam",            8, 128, "voicing=Thick_Fur dist_drive=55 +eq=mid-scoop" },
    { "Scratch Pad",       "Short Fuse",           6, 132, "+eq=mid-scoop" },
    { "Bottle Kick",       "Shriek",              11, 126, "voicing=Hollow dist_drive=55 +eq=smile" },

    // ---- Experimental: the Mods layer ----------------------------------------------------
    { "Feedback Garden",   "Period Doubler",       0, 132, "" },
    { "Bird Song",         "Ether Line",           9, 110, "+reverb=dark-hall" },
    { "Pitch Morse",       "Pitch Gate",           4, 128, "accvol_couple=0 acc_vol=50 +delay=dotted-eighth" },
    { "Accent Rain",       "Accent Only",          5, 126, "+delay=dotted-eighth +reverb=small-plate" },
    { "Drone Field",       "Long Decay Wah",       2,  70, "+delay=long-dark-tail +reverb=deep-space" },
    { "Edge Of Chaos",     "Chaos Garden",         7, 134, "" },
    { "Tuned Ladder",      "Ladder Melody",        3, 104, "+delay=dotted-eighth +reverb=bright-chamber" },
    { "Sub Quake",         "Deep Foundation",      6, 120, "" },
    { "Vibrato Trill",     "Singing Wire",        10, 116, "vib_speed=70 vib_depth=40 +delay=quarter-space" },
    { "Ratchet Machine",   "Ghost Notes",          8, 124, "" },

    // ---- Polymeter ---------------------------------------------------------------------------
    { "Five Against Four", "Bubble Square",        9, 124, "+delay=dotted-eighth" },
    { "Seven Steps",       "Puddle Jump",          5, 126, "+delay=triplet-bounce" },
    { "Nine Lives",        "Rubber Duck",          4, 122, "" },
    { "Eleven Hours",      "Woodblock",            7, 120, "+delay=dotted-eighth" },
    { "Thirteen Doors",    "Jelly Spine",          2, 124, "" },
    { "Fifteen Floors",    "Bent Spoon",           0, 128, "+delay=eighth-tight" },
    { "Drift Seven",       "Click Track",         10, 126, "+delay=sixteenth-ripple" },
    { "Shifting Nine",     "Bell Bass",            6, 125, "" },

    // ---- Teaching -----------------------------------------------------------------------------
    { "Accent Stack",      "Accent Lab",           0, 125, "" },
    { "Octave Bend",       "Wah Floor",            9, 125, "" },
    { "Tie Slide Plain",   "Warehouse Squelch",    5, 120, "accvol_couple=0 acc_vol=55" },
    { "Pre Post Drive",    "Pre And Post",         7, 128, "" },
    { "Decay Contrast",    "Tame Ladder",          2, 118, "decay=100" },
    { "Slide Into Accent", "Last Tram",            4, 125, "decay=80 accent=80" },
};
// clang-format on

const char* const kKeyNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

/** The variation rules a style's bank is made from, in order of preference. */
std::vector<Variation> styleVariations (std::string_view style)
{
    using V = Variation;
    if (style == "Chicago") return { V::Push, V::AccentShift, V::Breakdown, V::Answer, V::Mutation, V::Fill };
    if (style == "Anthem") return { V::Glide, V::UpTop, V::HalfAccent, V::Answer, V::Breakdown, V::Mutation };
    if (style == "Rolling") return { V::Push, V::AccentShift, V::UpTop, V::Fill, V::Mutation, V::Triplet };
    if (style == "Melodic") return { V::AccentShift, V::Glide, V::Breakdown, V::Mutation, V::HalfTime, V::Push };
    if (style == "Minimal") return { V::Push, V::AccentShift, V::Mutation, V::Stretch, V::Answer };
    if (style == "Hard") return { V::UpTop, V::AccentShift, V::Fill, V::Triplet, V::Push, V::Mutation };
    if (style == "Trance") return { V::UpTop, V::AccentShift, V::Answer, V::Breakdown, V::Fill };
    if (style == "Dub") return { V::HalfTime, V::Push, V::Breakdown, V::Mutation, V::Answer };
    if (style == "Breaks") return { V::Push, V::HalfAccent, V::Answer, V::Fill, V::Mutation };
    // Experimental lines are written for one Mods behaviour each (tracking, accent-only
    // percussion, vibrato, ratchets, drones); a generated relative keeps none of it and played
    // 3 to 7 dB away from the main line, so Mutation is only the last resort here.
    if (style == "Experimental") return { V::AccentShift, V::Push, V::Fill, V::Glide, V::Breakdown, V::Answer, V::HalfTime, V::Mutation };
    if (style == "Polymeter") return { V::Mutation, V::Push, V::Stretch, V::AccentShift, V::Fill };
    if (style == "Teaching") return { V::Push, V::AccentShift, V::Breakdown, V::Glide, V::HalfTime };
    return { V::Push, V::AccentShift, V::UpTop, V::Breakdown, V::Answer, V::Fill, V::Glide, V::Mutation };
}

/** The style's own rules first, then general ones as fallbacks for lines a rule cannot use. */
std::vector<Variation> variationsFor (std::string_view style)
{
    std::vector<Variation> v = styleVariations (style);
    for (const Variation f : { Variation::Push, Variation::AccentShift, Variation::Glide, Variation::Mutation, Variation::Fill,
                               Variation::Breakdown, Variation::Answer })
        if (std::find (v.begin(), v.end(), f) == v.end()) v.push_back (f);
    return v;
}

const char* variationPhrase (Variation v)
{
    switch (v)
    {
        case Variation::Push: return "the line moved along the grid";
        case Variation::AccentShift: return "the accents moved one note later";
        case Variation::UpTop: return "octave jumps under slides";
        case Variation::Breakdown: return "a breakdown on the beats";
        case Variation::Answer: return "a two-bar call and answer";
        case Variation::Fill: return "a fill in the last beat";
        case Variation::Glide: return "a cluster of slides";
        case Variation::HalfTime: return "half time";
        case Variation::Triplet: return "a twelve-step triplet version";
        case Variation::Mutation: return "a generated relative in the same notes";
        case Variation::HalfAccent: return "accents on the even steps only, for the build";
        case Variation::Stretch: return "a different loop length that drifts";
    }
    return "";
}

std::uint32_t hashName (std::string_view s)
{
    std::uint32_t h = 2166136261u;
    for (const char c : s) { h ^= static_cast<std::uint8_t> (c); h *= 16777619u; }
    return h;
}

int nearestShift (int key)
{
    return key <= 6 ? key : key - 12;
}

struct Bank
{
    std::vector<Pattern> patterns;
    std::vector<std::string> roles;      // "main line", phrases
    std::vector<Variation> kinds;        // per pattern after the first
};

/** The main line and its variations, named, in C. */
Bank makeBank (const Line& line, int maxSlots)
{
    const StyleRules& rules = styleRules (line.style);
    Bank b;
    Pattern main = parseSteps (line.steps, std::string ("line/") + line.name);
    if (const auto why = curateStyle (main, rules, false); ! why.empty())
        fail (std::string ("line '") + line.name + "' breaks the " + line.style + " rules: " + why);
    setPatternName (main, line.name);
    b.patterns.push_back (main);
    b.roles.emplace_back ("main line");

    const std::uint32_t seed = hashName (line.name);
    for (const Variation v : variationsFor (line.style))
    {
        if (static_cast<int> (b.patterns.size()) >= maxSlots) break;
        Pattern out;
        if (! makeVariation (main, v, rules, seed, out)) continue;
        // A variation has to differ from everything already in the bank by more than a
        // rotation or a transposition; Push is the exception, as moving the line against the
        // kick is the whole of what it does.
        bool duplicate = false;
        for (const Pattern& p : b.patterns)
            if (acid::seq::samePattern (p, out) || (v != Variation::Push && sameLineUpTo (p, out, false))) duplicate = true;
        if (duplicate) continue;
        setPatternName (out, std::string (line.name) + " " + variationName (v));
        b.patterns.push_back (out);
        b.roles.emplace_back (variationPhrase (v));
        b.kinds.push_back (v);
    }
    if (maxSlots >= 4 && b.patterns.size() < 4)
        fail (std::string ("line '") + line.name + "' made only " + std::to_string (b.patterns.size()) + " bank patterns");
    return b;
}

std::string describeScene (const Line& line, int key, int bpm, const Bank& bank)
{
    std::string d = std::string (line.style == std::string_view ("Showcase") ? "The first-load scene" : line.style)
                    + (line.style == std::string_view ("Showcase") ? "" : " scene") + " in " + kKeyNames[key]
                    + ", written for about " + std::to_string (bpm) + " BPM. Bank A: ";
    for (std::size_t i = 0; i < bank.roles.size(); ++i)
    {
        if (i > 0) d += ", ";
        d += "A" + std::to_string (i + 1) + " " + bank.roles[i];
    }
    d += ".";
    return d;
}

std::vector<std::string> sceneTags (std::string_view style, const Patch& patch, const SoundRef& sound)
{
    std::vector<std::string> tags { "Seq", std::string (style), waveformTag (patch.values), distortionTag (patch.values) };
    for (std::size_t i = 0; i < sound.character.size() && i < 2; ++i) tags.push_back (sound.character[i]);
    if (param (patch.values, acid::kModOn) >= 0.5f) tags.emplace_back ("Mods");
    return tags;
}

acid::Settings sceneSettings()
{
    return acid::Settings {};
}

/** Settings a scene carries: how the sequencer plays it, the effects order, and the tempo it
    was written for as the internal clock's tempo (used by the standalone app and by hosts that
    send no tempo; with a host tempo the scene follows the host). Personal preferences
    (quality tiers, MIDI routing, host following) are left to the user. */
constexpr std::uint32_t kSceneSettingsMask = acid::kSetTriggerMode | acid::kSetLaunchQuant | acid::kSetSlideIntoRest
                                             | acid::kSetNotePriority | acid::kSetFxOrder | acid::kSetInternalBpm;

void addArpBank (acid::seq::PatternBank& bank, int sceneIndex)
{
    const auto arps = arpLines();
    for (int j = 0; j < 4; ++j)
    {
        const auto i = static_cast<std::size_t> ((sceneIndex * 5 + j * 13) % static_cast<int> (arps.size()));
        bank.arp[static_cast<std::size_t> (j)] = arpPattern (arps[i]);
    }
}

Entry makeSceneEntry (Library& lib, const std::string& name, const Line& line, const SoundRef& sound, std::string_view spec,
                      int key, int bpm, Bank& bank, int sceneIndex, const std::string& path, const std::string& levelKey)
{
    std::vector<Pattern*> ptrs;
    for (Pattern& p : bank.patterns) ptrs.push_back (&p);
    transposeTogether (ptrs, nearestShift (key));

    Patch patch = sound.patch;
    applySpec (patch, spec, "scene/" + name);
    setParam (patch.values, acid::kPlayMode, 1.0f);
    setParam (patch.values, acid::kPatternBank, 0.0f);
    setParam (patch.values, acid::kPatternSlot, 1.0f);
    setParam (patch.values, acid::kLiveTranspose, 0.0f);
    applyLevelTrim (patch.values, lib.levelTrims().trim (levelKey));

    auto pb = std::make_unique<acid::seq::PatternBank>();
    for (std::size_t i = 0; i < bank.patterns.size(); ++i) pb->seq[i] = bank.patterns[i];
    addArpBank (*pb, sceneIndex);

    acid::Settings settings = sceneSettings();
    settings.fxOrder = patch.fxOrder;
    settings.internalBpm = static_cast<float> (bpm);

    Entry e;
    e.data = acid::makePreset (acid::PresetType::Scene, patch.values, settings, pb.get());
    e.data.settingsMask = kSceneSettingsMask;
    e.data.name = name;
    e.data.author = kAuthor;
    e.data.tags = joinTags (sceneTags (line.style, patch, sound));
    e.data.description = describeScene (line, key, bpm, bank);
    e.path = path;
    e.group = line.style;
    e.bpm = bpm;
    e.levelKey = levelKey;
    e.levelParam = acid::kMasterVolume;
    return e;
}

void poolBank (const Bank& bank, std::string_view style, int bpm, int key, int sceneIndex, std::string_view sceneName)
{
    const std::string keyText = kKeyNames[key];
    for (const PooledPattern& q : patternPool())
        if (sameLineUpTo (q.pattern, bank.patterns[0], true))
            fail (std::string ("the main line of ") + std::string (sceneName) + " is the line " + q.pattern.name + " moved or transposed");
    PooledPattern main;
    main.pattern = bank.patterns[0];
    main.style = std::string (style);
    main.role = "Main";
    main.bpm = bpm;
    main.description = std::string (style) + " line in " + keyText + ", about " + std::to_string (bpm)
                       + " BPM. The main line of the scene " + std::string (sceneName) + ".";
    poolPattern (main);

    // One variation per scene goes to the library, rotating through the kinds the bank made,
    // but only one that is a new line: not the main line (or anything else already in the
    // library) moved along the grid, transposed or played at another step rate. Push and
    // Half Time stay scene slots.
    const std::size_t nv = bank.kinds.size();
    for (std::size_t k = 0; k < nv; ++k)
    {
        const std::size_t pick = 1 + (static_cast<std::size_t> (sceneIndex) + k) % nv;
        bool known = false;
        for (const PooledPattern& q : patternPool())
            if (sameLineUpTo (q.pattern, bank.patterns[pick], true)) { known = true; break; }
        if (known) continue;
        PooledPattern var;
        var.pattern = bank.patterns[pick];
        var.style = std::string (style);
        var.role = "Variation";
        var.bpm = bpm;
        var.description = std::string (style) + " line in " + keyText + ", about " + std::to_string (bpm) + " BPM: "
                          + bank.roles[pick] + ", from the scene " + std::string (sceneName) + ".";
        poolPattern (var);
        return;
    }
    fail (std::string ("scene ") + std::string (sceneName) + " has no variation that is a new line for the pattern library");
}
}

void addScenes (Library& lib)
{
    // Every hand-written line against its style's rules, all failures at once.
    std::string problems;
    for (const Line& l : handLines())
    {
        const Pattern p = parseSteps (l.steps, std::string ("line/") + l.name);
        if (const auto why = curateStyle (p, styleRules (l.style), false); ! why.empty())
            problems += std::string ("\n  ") + l.name + " (" + l.style + "): " + why;
        // Lengths: whole bars for the four-on-the-floor styles (8 and 16 steps, 12 for
        // triplets), two or four bars for melodic lines; the space and drift styles are free.
        const std::string_view style = l.style;
        const bool free = style == "Minimal" || style == "Polymeter" || style == "Experimental";
        const bool ok = free || (style == "Melodic" ? (p.length == 32 || p.length == 64) : (p.length == 8 || p.length == 12 || p.length == 16));
        if (! ok) problems += std::string ("\n  ") + l.name + " (" + l.style + "): " + std::to_string (p.length) + " steps";
    }
    // Lines that are too alike: within a style, no two share more than kMaxSkeletonShare of
    // their rhythmic skeleton (state, accent and slide, step by step); across the library, no
    // two play the same thing on more than kMaxLineShare of their steps at any rotation and
    // transposition.
    {
        constexpr double kMaxSkeletonShare = 0.8;
        constexpr double kMaxLineShare = 0.8;
        const auto lines = handLines();
        std::vector<Pattern> parsed;
        for (const Line& l : lines) parsed.push_back (parseSteps (l.steps, std::string ("line/") + l.name));
        for (std::size_t i = 0; i < lines.size(); ++i)
            for (std::size_t j = i + 1; j < lines.size(); ++j)
            {
                const auto pct = [] (double x) { return std::to_string (static_cast<int> (std::lround (x * 100.0))) + " %"; };
                if (std::string_view (lines[i].style) == lines[j].style)
                    if (const double k = skeletonSimilarity (parsed[i], parsed[j]); k > kMaxSkeletonShare + 1.0e-9)
                        problems += std::string ("\n  ") + lines[i].name + " and " + lines[j].name + " (" + lines[i].style
                                    + "): the same rhythmic skeleton on " + pct (k) + " of their steps";
                if (const double k = lineSimilarity (parsed[i], parsed[j]); k > kMaxLineShare + 1.0e-9)
                    problems += std::string ("\n  ") + lines[i].name + " and " + lines[j].name + ": the same line on " + pct (k)
                                + " of their steps, moved or transposed";
            }
    }
    if (! problems.empty()) fail ("lines that break their style's rules:" + problems);

    // The first-load scene: the opening line and its variations, then a tour of the styles,
    // every pattern in the scene's key.
    {
        const Line& line = findLine ("Opening Line");
        Bank bank = makeBank (line, 7);
        for (const char* tour : { "Bright Signal", "Jack Street", "Triplet Furnace", "Thirteen Doors", "Glass River" })
        {
            Bank other = makeBank (findLine (tour), 1);
            bank.patterns.push_back (other.patterns[0]);
            bank.roles.push_back (std::string ("the ") + styleRules (findLine (tour).style).style + " line " + tour);
        }
        const int key = 9;
        const SoundRef& sound = findSound ("First Squelch");
        Entry e = makeSceneEntry (lib, "First Squelch", line, sound, "", key, 125, bank, 0, "default.mw12f", "default");
        e.data.tags = joinTags ({ "Seq", "Showcase", "Saw", distortionTag (e.data.params), "Squelchy", "Rubbery" });
        Entry copy = e;
        copy.path = "scene/first-squelch.mw12f";
        copy.levelKey = "default";
        // The first-load file is a session state, not a preset: the same content as the scene
        // (a state restores everything and gives what it does not carry its default, which is
        // what a fresh instance wants), but a preset browser lists scenes, not states, so the
        // scene shows once, as scene/first-squelch.mw12f.
        e.data.type = acid::PresetType::State;
        lib.add (std::move (e));
        lib.add (std::move (copy));
        poolBank (bank, "Showcase", 125, key, 0, "First Squelch");
    }

    int index = 1;
    for (const SceneRecipe& r : kScenes)
    {
        const Line& line = findLine (r.line);
        Bank bank = makeBank (line, 6);
        const SoundRef& sound = findSound (r.sound);
        const std::string slug = slugify (r.line);
        Entry e = makeSceneEntry (lib, r.line, line, sound, r.spec, r.key, r.bpm, bank, index,
                                  "scene/" + slug + ".mw12f", "scene/" + slug);
        lib.add (std::move (e));
        poolBank (bank, line.style, r.bpm, r.key, index, r.line);
        ++index;
    }
}
}
