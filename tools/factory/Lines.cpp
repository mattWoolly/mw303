// The hand-written lines, rooted on C (octave 0 is C2 to B2). Scenes move them into their
// own keys. Notation (PatternCodec.h): '.' rest, '~' tie, NOTE with one '+' or '-' per
// octave, ':' then flags A accent, S slide (into the next note), V vibrato, R<n> ratchet.
// P<n> probability (a failed roll plays as a rest). Header fields before the steps set
// length, shuffle and step division.
//
// Every line here is checked against its style's rules when the library is built
// (Patterns.cpp, styleRules), so a line that drifts out of its idiom stops the build.
#include "Patterns.h"

namespace factory
{
namespace
{
// clang-format off
const Line kLines[] = {
    // ---- The first-load line ----------------------------------------------------------
    { "Opening Line",      "Showcase", "C C C+:A C D#:S F C:A . C C+:S C:A A#- C ~ G-:S C" },

    // ---- Chicago jack: ties and long-short phrasing, 3 to 7 pitch classes, few accents -
    { "Jack Street",       "Chicago", "C ~ C+ D# ~ F C:A ~ G ~ F D#:S C ~ . A#-" },
    { "Box Jam",           "Chicago", "C C ~ D# C ~ G ~ F ~ D#:A C:S C+ ~ . C" },
    { "Back Room Jack",    "Chicago", "C C+ D# C ~ G- C+:A A#-" },
    { "Pump Room",         "Chicago", "shuffle=15 C . C D#:A ~ ~ F F# ~ D# C ~ . C:S D# ~" },
    { "Station Wagon",     "Chicago", "C ~ ~ C+ A# . G ~ ~ F G:A ~ A#:S C+ ~ G-" },
    { "Hot Plate",         "Chicago", "shuffle=20 C ~ ~ D# C ~ . G-:A A#- C ~ ~ F:S F# G ~" },
    { "Corner Store",      "Chicago", "shuffle=22 C ~ C ~ D#:A ~ C G- ~ A#- C ~ C+:S C ~ D#" },
    { "Late Bus",          "Chicago", "C ~ G- C D# ~ D C:A ~ A#- ~ G- C:S D# ~ ." },
    { "Loop Jam",          "Chicago", "C D# ~ G C+:A ~ A# G:S" },
    { "Warehouse Party",   "Chicago", "C C+ ~ C A#- ~ C D#:A ~ C F ~ D#:S C ~ ." },
    { "Seven Note Jack",   "Chicago", "C D D# ~ F . G G#:A ~ G F ~ D#:S D C ~" },
    { "Skate Rink",        "Chicago", "shuffle=25 C ~ ~ ~ D# C ~ G- A#- ~ C:A ~ ~ D#:S F ." },

    // ---- Anthem: one or two pitch classes, accent on nearly every step, clustered slides
    { "Bright Signal",     "Anthem", "C:A C:A C+:AS C:A C:A C:AS C:AS C:A D#:A C:A C:A C:AS C+:AS C:A C:A C:A" },
    { "All Hands",         "Anthem", "C:A C:A C:A C:AS C:A C:A C:A C:AS C:AS C:A C:A C:A C:A C:AS C:AS C:A" },
    { "Crowd Lift",        "Anthem", "C:A C:A G:A C:A C:AS C:A G:A C:A C:A C:A G:AS G:A C:A C:AS C:A C:A" },
    { "Beacon Call",       "Anthem", "C:A C:A C:A . C:A C:AS A#-:AS C:A C:A C:A C:A C:A C:A C:AS C+:AS C:A" },
    { "Peak Lights",       "Anthem", "C C:A C:A C C:AS C:A C C:A C:A C:AS C:AS C:A C C:A C:AS C+:A" },
    { "Whistle Post",      "Anthem", "C:A D#:A C:A C:A C:AS D#:AS C:A ~ C:A D#:A C:A C:AS C:AS D#:A C:A C:A" },
    { "Strobe Hymn",       "Anthem", "C:A C C:A C C:A C:AS F:A C:A C:A C C:A C C:A C:AS F:AS C:A" },
    { "Arms Aloft",        "Anthem", "C:A C:A C:A G-:AS C:A C:A C:A C:A C:A C:A C:A G-:AS C:AS C:AS C:A C:A" },
    { "Long Night",        "Anthem", "C:A C C:A C:A C:AS C:A C C:A C:A C C:A C:AS C:AS C:A C C:A" },
    { "Sky Writer",        "Anthem", "C:A C:A C:A ~ C+:AS C:A C:A C:AS D#:A C:A ~ C:A C+:AS C:AS C:A C:A" },
    { "Foghorn Choir",     "Anthem", "C:A C:A C:AS C:AS C:A C:A C:A C:A C:A C:A C:AS C:AS C:A C:A C:A C:AS" },
    { "Night Hymn",        "Anthem", "G-:A C C:A C:AS C:A G-:A C C:A C:AS C:AS C:A G-:A C C:A C:AS C:A" },

    // ---- Rolling stacked lines: continuous sixteenths, root and octave interlocked -----
    { "Conveyor",          "Rolling", "C C+ C C:A C+ C C:S C+ C C+:A C C D#:S C+ C C:A" },
    { "Wheel Spin",        "Rolling", "C C+ C ~ C C+:A . C C C+ C:S C D#:A C . C+" },
    { "Stack Pile",        "Rolling", "C C+:S C C:A C C+:S C D#:A C C+ C C:A C G-:S C F:A" },
    { "Rolling Stock",     "Rolling", "C:A C C+ C C:A C C+ C:S D# C:A C C+ C C:A C A#-:S" },
    { "Crank Shaft",       "Rolling", "C ~ C+ C D# C C+:A . C C C+ C:S G C C+:A ." },
    { "Turbine",           "Rolling", "C . C+ C:A C D# C+ C . C C+:A C F:S F+ C C" },
    { "Treadmill",         "Rolling", "C C:A C+ C C C:A C+ C:S C C:A C+ C C C:A D#+ C:S" },
    { "Spinning Plate",    "Rolling", "C G- C:A C+ C G- C:AS D# C G- C:A C+ C G- C A#-:S" },
    { "Gear Tooth",        "Rolling", "C C C+:S C C D#:A C C C+:S C C G-:A C C C+:S F:A" },
    { "Fly Wheel",         "Rolling", "C+ C ~ C+:A C C+ C:S D# . D#+ C C:A C+ C . C" },
    { "Steady Roll",       "Rolling", "C C+ C C+ C:A C+ C C+ C C+:S D# C+ C:A C+ C C+" },
    { "Lathe",             "Rolling", "C C C:A C C+:S C C C:A D# C C:A C C+:S C G- C:A" },

    // ---- Melodic and modal: two-bar lines in a mode, articulation changing per bar ------
    { "Glass River",       "Melodic", "C ~ D# F G:S A A# ~ G F:A D# ~ D C:S D# F  G ~ A# A:A G F:S G ~ D# D C:A ~ A#- C:S D ~" },
    { "Paper Lantern",     "Melodic", "C D# G C+:S A# G ~ G# G F:A D# ~ D:S D# F G  G# ~ G F D#:S D C ~ A#- C:A D D# ~ G:S F D#" },
    { "Slow Coast",        "Melodic", "C C# D#:S F ~ D# C# C ~ A#- G#-:S A#- C ~ C# D#  F:A G F D#:S C# ~ C A#- C:A ~ C# D#:S F G ~ F" },
    { "Morning Fold",      "Melodic", "C E G A#:S C+ A# G ~ F E:A D ~ C D:S E G  A ~ G E:A D C:S D ~ E G A:S A# C+ ~ A# G" },
    { "Rain On Tin",       "Melodic", "C ~ A#- C D#:S F ~ D# C:A ~ A#- G-:S A#- C ~ D#  F ~ G A:S A# ~ A G:A F ~ D#:S D C ~ . A#-" },
    { "Lantern Walk",      "Melodic", "C G- C D#:S D C ~ G-:A A#- C ~ D#:S F G ~ F  D# ~ D C:A A#- ~ G#-:S A#- C ~ D D#:S G F ~ D#" },
    { "Small Hours",       "Melodic", "C ~ D# G B-:S C ~ D D#:A ~ D C:S B- C ~ G#-  G- ~ B- D:S F D# ~ D C:A ~ G#- G-:S B- D ~ C" },
    { "Kite String",       "Melodic", "C D E:S G A ~ G E:A D ~ C:S D E ~ G ~  A C+:S A G E:A ~ D C ~ D:S E G A ~ G E" },
    { "Tide Chart",        "Melodic", "C ~ C+ A#:S A G ~ F D#:A ~ D D#:S F ~ G .  C ~ C+ A# A:A G:S F ~ D# D ~ C:A ~ A#- C:S D  "
                                      "F ~ G A:S A# C+ ~ A# A:A ~ G F:S G ~ D# .  D ~ D# F G:S A ~ G:A F D# ~ D:S C ~ A#- C:A" },
    { "Inkwell",           "Melodic", "C ~ ~ C# C:S A#- ~ G#- G-:A ~ G#- A#-:S C ~ D# F  C# ~ D# F:S D# C# ~ C A#-:A ~ C C#:S D# ~ C .  "
                                      "G- ~ G#- A#-:S C ~ C# C:A ~ A#- G#- ~ G-:S G#- ~ .  C ~ D# C#:S C A#- ~ G#-:A ~ G- G#-:S A#- C ~ C# ." },
    { "Brass Kettle",      "Melodic", "C ~ D# E G ~ A#:S C+ A# G:A ~ E D# C ~ G-  C ~ E G A:S A# ~ G E:A D# ~ C A#-:S C ~ ." },
    { "Hill Road",         "Melodic", "C D E F#:S G ~ A G:A F# E ~ D:S E G ~ B-  C ~ E G A:S B ~ A G:A ~ F# E:S D E ~ C  "
                                      "D ~ E F#:S G A ~ B A:A G ~ F#:S E D ~ C  B- ~ C D:S E ~ F# E:A ~ D C:S B- A-:A ~ B- C" },

    // ---- Minimal: space, low notes, odd lengths against the drums ----------------------
    { "Empty Room",        "Minimal", "C . . C:S C+ ~ . . C . . . C:A ~ . ." },
    { "Pale Hours",        "Minimal", "C . . C ~ . G- . . C:A ~ . ." },
    { "Dust Motes",        "Minimal", "C ~ . . C+:S C . . D# . ." },
    { "Low Ceiling",       "Minimal", "C . . . C ~ ~ . C:S C+ . . C:A . . ." },
    { "Thin Line",         "Minimal", "C . C . . D#:S C ~ . . C . . G-:A ." },
    { "Wall Clock",        "Minimal", "C . . C:P70 . . C:A . . C:P50 . . C+:S C ~ ." },
    { "Grey Light",        "Minimal", "C ~ ~ ~ . . C:S C ~ . . . D# ~ . ." },
    { "Echo Floor",        "Minimal", "C . . . . . C:A . C:S C+ ~ . . . . ." },
    { "Faint Pulse",       "Minimal", "C . . C:P65 . . . C:AS D# ~ . . ." },
    { "Hush",              "Minimal", "C ~ . . . C . . G-:S C ~ . . . . C:A" },
    { "Neon Drip",         "Minimal", "C . C+:P60 . . C:A . . C:S A#- ." },
    { "Quiet Machine",     "Minimal", "C . . C . C:S C+ . C:A . . C . . ." },

    // ---- Hard and distorted: root-heavy, octave stabs, rests, triplet pairs ------------
    { "Iron Gate",         "Hard", "C C C+ C . C C:A C+:S C . C C C+ C:S D# C:A" },
    { "Hammer Line",       "Hard", "C . C C+:A C C . C:S C+ C C . C:A C C+:S C" },
    { "Rivet Gun",         "Hard", "C C+ C C:A . C C+ C:S D# C . C:A C+ C C ." },
    { "Triplet Furnace",   "Hard", "div=1/8 mod=tri C C+ C:A C C:S D# C C+:A C . C C+:S" },
    { "Pressure Plate",    "Hard", "C C C:A C+ C C . C:S G- C C:A C+ C C:S C+ ." },
    { "Smelter",           "Hard", "C . C+:S C C:A C C G-:S C . C C+:A C C:S D# ." },
    { "Triplet Crusher",   "Hard", "div=1/8 mod=tri C:A C C+ C:S C+ C C:A D# C . C:S C+" },
    { "Concrete",          "Hard", "C C C . C:A C+ C C C:S C+ . C C:A C C ." },
    { "Anvil Chorus",      "Hard", "div=1/8 mod=tri C C:A C . C+:S C C C:A G- C:S C+ C" },
    { "Steel Mill",        "Hard", "C:A C C+ C C:S C+ C . C:A C C+ C C:S G C ." },
    { "Grind House",       "Hard", "C C+ . C:A C C+:S C . C C+:A . C C:S F C ." },
    { "Shrapnel",          "Hard", "C . C:A C C+:S D# C . C C:A C+ C:S C . C G-" },

    // ---- Trance-leaning: off-beat rolling bass under the kick ---------------------------
    { "Gatekeeper",        "Trance", ". C C+ C . C C+:A C . C C+:S C . D# C+:A C" },
    { "Offbeat Rush",      "Trance", ". C ~ C . C ~ C+ . C:A C G- . G- A#-:S C" },
    { "Hyperlane",         "Trance", ". C C+:A C . C:S C+ C . C C+:A C . D#:S D#+ C" },
    { "Neon Run",          "Trance", ". C:A C C+ . C C:A G- . C:A C C+ . A#- C:AS C+" },
    { "Star Chart",        "Trance", ". C C+ . C C:A C+ ~ . C C+ . C:A D#:S D#+ C" },
    { "Uplifter",          "Trance", ". C ~ C:A . D# D# D# . F ~ F:A . G:S G+ ." },
    { "Night Drive",       "Trance", ". C C:A C+ . C C:S C . G#- G#-:A G# . A#- A#-:S C+" },
    { "Velocity",          "Trance", "C:A . C C+ . C . C+:S C C . C+ C:A . D# F" },
    { "Laser Line",        "Trance", ". C:A C+ C . C C+:A ~ . C:S C+ C . D#+:A C+ ." },
    { "Pulse Width",       "Trance", ". C C . C:A C . C:S C+ C . C:A C C:S D# C" },

    // ---- Slow and dubby: long notes, slides, space for the echoes ----------------------
    { "Low Tide Dub",      "Dub", "C ~ ~ . . C:S C+ ~ . . G-:A ~ ~ . A#- ." },
    { "Heavy Air",         "Dub", "shuffle=15 C ~ . C:S D# ~ ~ . C:A ~ . . G- ~ A#-:S C" },
    { "Rocking Chair",     "Dub", "shuffle=20 C ~ C+:S C ~ . G-:A ~ C ~ . D#:S F ~ . ." },
    { "Smoke Ring",        "Dub", "C:V ~ ~ ~ G-:S C ~ . D#:A ~ ~ . C ~ . ." },
    { "Slow Burn Dub",     "Dub", "C . C ~ D#:S F ~ . C:A ~ . G- A#-:S C ~ ." },
    { "Under Water",       "Dub", "C ~ ~ C+:S C ~ ~ . . . A#-:A ~ G-:S C ~ ." },
    { "Fog Bank",          "Dub", "C ~ . . C:S D# ~ ~ . . G-:A ~ ~ . . ." },
    { "Heavy Lids",        "Dub", "div=1/8 C ~ C+:S C . G-:A ~ A#-:S C ~ D# ~ C:A . . ." },
    { "Drip Feed",         "Dub", "shuffle=25 C . . C:S C+ . . . D#:A ~ . . C . G-:S C" },
    { "Long Shadow",       "Dub", "C:V ~ ~ ~ ~ . C:S D#:V ~ ~ . . A#-:A ~ ~ ." },

    // ---- Breaks and big beat: all accents, octaves everywhere, swung -------------------
    { "Crate Stack",       "Breaks", "C:A C+:A D#+:A C:A F+:A C:A G+:A C+:A A#:A C+:A D#+:A C:A F+:A C+:A G:A C:A" },
    { "Breakbeat Jumper",  "Breaks", "shuffle=30 C:A C+:A C:A D#+ F:A F+:A C:A G+:AS A#:A A#-:A C:A C+ D#:A D#+:A G:A C+:AS" },
    { "Big Shoes",         "Breaks", "C:A C+ G:A C+:A A#:A C F+:A C+:A D#:A D#+ C:A C+:A G-:A G A#:A C+:A" },
    { "Block Party",       "Breaks", "shuffle=25 C:A C:AS C+:A C:A D#+:A D#:AS F+:A F:A G:A G+:AS F:A F+:A D#:A D#+:AS C:A C+:A" },
    { "Turntable",         "Breaks", "C:A C+ C:A C+:A D# D#+:A C:A C+ F:A F+:A C C+:A G:A G+ A#:A C+:A" },
    { "Skip Step",         "Breaks", "shuffle=40 C:A C+:A A# C+:A G:A C+:A F C+:A D#:A C+:A F C+:A G:A C+:A A# D#+:A" },
    { "Funk Engine",       "Breaks", "C:A C+:S C:A D# C+:A F C:A G+:S C:A A# C+:A G C:A F+:S C:A D#+" },
    { "Speaker Stack",     "Breaks", "shuffle=20 C:A G:A C+:AS G:A C D#+:A C+:A A#:A C:A G:A C+:AS F+:A C D#+:A C+:A G:A" },
    { "Scratch Pad",       "Breaks", "C:A C:A C+:AS C:A C+:A C:A D#+:AS D#:A C:A C+:A C:AS G+:A C:A C+:A A#:AS C+:A" },
    { "Bottle Kick",       "Breaks", "shuffle=35 C:A D#:A C+:A D#+ F:A F+:AS G:A G+:A A#:A A#+ G:A G+:A ~ F+:A D#:A D#+:A" },

    // ---- Experimental: lines written for the Mods ---------------------------------------
    { "Feedback Garden",   "Experimental", "C:A C:A C:A C:A C . C:S C+ C:A C:A C:A . C D#:S C C" },
    { "Bird Song",         "Experimental", "C D# F G:S A# G F D#:S C ~ G- A#-:S C ~ D# ." },
    { "Pitch Morse",       "Experimental", "C+ C- C+:P75 C+ C- C+ C-:P60 C+ C+ C- C+:P75 C- C+:A C- C+ C-:P50" },
    { "Accent Rain",       "Experimental", "C C:P70 C:A C C:A C:P50 C C:A C C:P70 C:A C C:A C:P60 C C:A" },
    { "Drone Field",       "Experimental", "C ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ D#:S ~ ~ ~" },
    { "Edge Of Chaos",     "Experimental", "C:A C:A C+:A C:A C:AS D#:A C:A C:A C:A C+:AS C:A C:A C:AS G:A C:A C:A" },
    { "Tuned Ladder",      "Experimental", "C D# G C+:S A# G D# F:S G ~ C+ D#+:S C+ A# G ~  F D# C:S D# F G:S A# ~ C+ A#:S G F D# ~ C ~" },
    { "Sub Quake",         "Experimental", "C ~ C- ~ C ~ . C:S C- ~ C ~ . G-:A ~ ." },
    { "Vibrato Trill",     "Experimental", "C:V ~ ~ . C+:AV ~ . C ~ D#:V ~ . G-:S C:V ~ ." },
    { "Ratchet Machine",   "Experimental", "C:R2 C C+:AR3 C . C:R2 C:A C+ C:R4 . C C:AR2 D# C . C+:R3" },

    // ---- Polymeter: short odd loops that drift against four on the floor ---------------
    { "Five Against Four", "Polymeter", "C C+:A C . C:S" },
    { "Seven Steps",       "Polymeter", "C . C+ C:A C . D#:S" },
    { "Nine Lives",        "Polymeter", "C C:A . C+ C . C:S C+ ." },
    { "Eleven Hours",      "Polymeter", "C . C C+:A . C C . C:S C+ ." },
    { "Thirteen Doors",    "Polymeter", "C C . C:A C+ . C . C C:S D# . ." },
    { "Fifteen Floors",    "Polymeter", "C . C+ C:A . C C+:S C . C:A C+ . C . D#:S" },
    { "Drift Seven",       "Polymeter", "C:A C C+:S C D# C ." },
    { "Shifting Nine",     "Polymeter", "C C+ C:AS D# C . C+ C:A ." },

    // ---- Teaching: each isolates one mechanism -----------------------------------------
    { "Accent Stack",      "Teaching", "C C C C C:A C:A C:A C:A C C C C . . . ." },
    { "Octave Bend",       "Teaching", "C:S C+ . . C:S C+ . . C+:S C . . C:S G- . ." },
    { "Tie Slide Plain",   "Teaching", "C ~ D# . C:S D# . . C D# . . C:AS D#:A . ." },
    { "Pre Post Drive",    "Teaching", "C C+:S C C:A D# C C:A G- C C+:A C C:S D# C C:A C" },
    { "Decay Contrast",    "Teaching", "C ~ ~ . C:A ~ ~ . C ~ ~ . C:A ~ ~ ." },
    { "Slide Into Accent", "Teaching", "C:S C:A . . C:S D#:A . . C . C:A . C:S G-:A . ." },
};
// clang-format on
}

std::span<const Line> handLines()
{
    return kLines;
}

const Line& findLine (std::string_view name)
{
    for (const Line& l : kLines)
        if (name == l.name)
            return l;
    fail ("unknown line '" + std::string (name) + "'");
}
}
