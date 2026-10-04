#pragma once
// Pattern authoring: the hand-written lines, the style rules they are checked against, the
// variation rules that turn a main line into a bank, the generator with its curation rules,
// and the pool of patterns the scenes made (which the pattern library draws on).
//
// Pattern grammar (docs/research/05 Part B.1). A slide sits on the note it leaves and holds
// the gate into the next Note. A tie holds the previous note with no new envelope, so tied
// notes darken. Accents are either sparse (0 to 3 of 16, off the kick) or saturated (all or
// nearly all steps), and the style rules below hold each style to its side of that split.
#include "Common.h"

#include "acid/seq/Pattern.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace factory
{
/** A hand-written line in the compact step notation (PatternCodec.h), rooted on C. */
struct Line
{
    const char* name;           // the pattern's name (31 bytes at most)
    const char* style;          // style key, as in the Style tag group
    const char* steps;          // header fields (len=, shuffle=, div=, mod=) may lead the steps
};

std::span<const Line> handLines();     // Lines.cpp
const Line& findLine (std::string_view name);

/** Counts that the style rules and the tags are computed from (steps [0, length)). */
struct LineStats
{
    int length = 16;
    int notes = 0;              // Note steps
    int ties = 0;
    int rests = 0;              // Rest steps and orphan ties
    int accents = 0;            // accented Note steps
    int slides = 0;             // slides that land on a Note (or a Tie after one)
    int octaveSteps = 0;        // Note steps away from the line's most common octave
    int pitchClasses = 0;
    int longestRestRun = 0;
    int bigLeapsUnslid = 0;     // leaps over 7 semitones (octaves of the same pitch class excepted) without a slide
    int leapsOverOctave = 0;    // neighbouring notes more than 12 semitones apart
    double stepwiseShare = 1.0; // share of neighbouring note pairs at most 4 semitones apart
    int vibrato = 0;
    int ratchets = 0;
    bool orphanTie = false;     // a Tie at step 0 or after a Rest
    bool slideIntoRest = false; // a slide whose next step is a Rest

    double per16 (int count) const noexcept { return 16.0 * count / length; }
    double accentShare() const noexcept { return notes > 0 ? static_cast<double> (accents) / notes : 0.0; }
};

LineStats statsOf (const acid::seq::Pattern&);

/** Ranges a style's main lines and generated lines must fall in. Hand-made variations
    (breakdowns, fills) only have to pass the sanity rules (see curateSanity). */
struct StyleRules
{
    const char* style;
    int minPcs, maxPcs;
    double minAccentShare, maxAccentShare;
    double maxSlides16;
    double minRests16, maxRests16;
    double minTies16, maxTies16;
    double minOctaves16;
    // Generator settings for this style's generated lines
    std::uint16_t noteMask;
    std::uint8_t octaveMask;
    std::uint8_t amountNote, amountSlide, amountAccent;
    int defaultBpm;
};

const StyleRules& styleRules (std::string_view style);
std::span<const StyleRules> allStyles();

/** Sanity rules every shipped pattern passes: at least two notes, no orphan tie (a tie on
    step 1 or after a rest, which plays as a rest), no slide into a rest. Returns an empty
    string when it passes. */
std::string curateSanity (const acid::seq::Pattern&);

/** The style rules (and, for generated lines, the boring and chaotic rules). Returns an
    empty string when the pattern passes, otherwise the first reason it fails. */
std::string curateStyle (const acid::seq::Pattern&, const StyleRules&, bool generated);

/** Share of steps (0..1) on which two patterns of the same length play the same thing (the
    state, and for a note its pitch, accent, slide, vibrato, ratchet and probability), at the
    best rotation of b and the best transposition. 0 when the lengths differ. */
double lineSimilarity (const acid::seq::Pattern& a, const acid::seq::Pattern& b);

/** The same line up to rotation and transposition (lineSimilarity 1), with the same step
    division, modifier and shuffle unless ignoreTiming. */
bool sameLineUpTo (const acid::seq::Pattern& a, const acid::seq::Pattern& b, bool ignoreTiming);

/** Share of steps (0..1) on which the rhythmic skeletons agree, step by step with no rotation:
    the state, and for a note whether it is accented and whether it slides. 0 when the lengths
    differ. */
double skeletonSimilarity (const acid::seq::Pattern& a, const acid::seq::Pattern& b);

/** Ways to make a related pattern from a main line. */
enum class Variation
{
    Push, AccentShift, UpTop, Breakdown, Answer, Fill, Glide, HalfTime, Triplet, Mutation, HalfAccent, Stretch
};

const char* variationName (Variation);

/** Makes a variation of main. Returns false when the rule does not apply to this line, gives
    a pattern equal to main, or leaps past the octave or over a fifth without a slide more
    often than main does (the caller then tries the next rule). seed feeds Mutation. */
bool makeVariation (const acid::seq::Pattern& main, Variation, const StyleRules&, std::uint32_t seed,
                    acid::seq::Pattern& out);

/** Transposes a set of patterns together by semitones, moving the whole set up or down an
    octave when a note would leave the sequencer's range (so nothing wraps). Returns the shift
    actually applied. */
int transposeTogether (std::vector<acid::seq::Pattern*>& patterns, int semitones);

/** Tags for a pattern from its content. */
std::vector<std::string> patternTags (const acid::seq::Pattern&, std::string_view role);

/** A pattern made for a scene bank, kept for the pattern library. */
struct PooledPattern
{
    acid::seq::Pattern pattern;
    std::string style;
    std::string role;           // "Main", "Variation" or "Generated"
    double bpm = 125.0;
    std::string description;
};

void poolPattern (PooledPattern);
const std::vector<PooledPattern>& patternPool();

/** Draws generated lines for a style with the Generator and the curation rules until count
    are accepted. shape gives length, step division and shuffle. The patterns keep their
    generator memory, so the density knobs work on them. */
std::vector<acid::seq::Pattern> generatedLines (const StyleRules&, int count, std::uint32_t firstSeed,
                                                const acid::seq::Pattern& shape, std::vector<std::string>* rejectLog);

/** The generator draws the curation rules turned down while addPatterns ran (seed: reason). */
std::vector<std::string>& generatedRejects();

/** The arpeggiator pattern library (Patterns.cpp). */
struct ArpLine
{
    const char* name;
    const char* shape;          // Arp Shape tag
    const char* steps;          // index tokens 1..10 with octave marks and flags
};
std::span<const ArpLine> arpLines();
acid::seq::Pattern arpPattern (const ArpLine&);
}
