#include "Patterns.h"

#include "acid/seq/Generator.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include "acid/seq/Scales.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace factory
{
using acid::seq::Pattern;
using acid::seq::Step;
using acid::seq::StepState;

namespace
{
bool isNote (const Step& s) { return s.state == StepState::Note; }
int pitchOf (const Step& s) { return 12 * s.octave + s.note; }
Step& at (Pattern& p, int i) { return p.steps[static_cast<std::size_t> (i)]; }
const Step& at (const Pattern& p, int i) { return p.steps[static_cast<std::size_t> (i)]; }

/** Index of the step a slide on step i lands on: the next step that is not a Tie (wrapping),
    or -1 when that is a Rest. */
int slideTarget (const Pattern& p, int i)
{
    const int len = p.length;
    for (int k = 1; k <= len; ++k)
    {
        const int j = (i + k) % len;
        const Step& s = at (p, j);
        if (s.state == StepState::Tie) continue;
        return isNote (s) ? j : -1;
    }
    return -1;
}

int firstNote (const Pattern& p)
{
    for (int i = 0; i < p.length; ++i)
        if (isNote (at (p, i))) return i;
    return -1;
}

void clearFlags (Step& s)
{
    s.accent = false;
    s.slide = false;
    s.vibrato = false;
    s.ratchet = 1;
}
}

// ---- Statistics ---------------------------------------------------------------------------

LineStats statsOf (const Pattern& p)
{
    LineStats st;
    st.length = p.length;
    std::set<int> pcs;
    std::map<int, int> octaves;
    int run = 0;
    for (int pass = 0; pass < 2; ++pass)   // the second pass only extends rest runs across the loop
    {
        for (int i = 0; i < p.length; ++i)
        {
            const Step& s = at (p, i);
            const bool prevSilent = i == 0 ? true : at (p, i - 1).state == StepState::Rest;
            const bool orphan = s.state == StepState::Tie && (i == 0 || prevSilent);
            const bool silent = s.state == StepState::Rest || orphan;
            run = silent ? run + 1 : 0;
            st.longestRestRun = std::max (st.longestRestRun, std::min (run, static_cast<int> (p.length)));
            if (pass == 1) continue;

            if (orphan) st.orphanTie = true;
            if (silent) { ++st.rests; continue; }
            if (s.state == StepState::Tie) { ++st.ties; continue; }

            ++st.notes;
            pcs.insert (s.note);
            ++octaves[s.octave];
            if (s.accent) ++st.accents;
            if (s.vibrato) ++st.vibrato;
            if (s.ratchet > 1) ++st.ratchets;
            if (s.slide)
            {
                const int t = slideTarget (p, i);
                if (t < 0) st.slideIntoRest = true;
                else ++st.slides;
            }
        }
    }
    st.pitchClasses = static_cast<int> (pcs.size());

    int homeOctave = 0, best = -1;
    for (const auto& [o, n] : octaves)
        if (n > best) { best = n; homeOctave = o; }
    for (int i = 0; i < p.length; ++i)
        if (isNote (at (p, i)) && at (p, i).octave != homeOctave) ++st.octaveSteps;

    // Leaps between consecutive notes, including the seam from the last note back to the
    // first, which the loop plays every time it repeats.
    int prev = -1, first = -1, pairs = 0, steps = 0;
    const auto leapPair = [&] (int from, int to) {
        const Step& a = at (p, from);
        const Step& b = at (p, to);
        const int leap = std::abs (pitchOf (b) - pitchOf (a));
        if (leap > 7 && a.note != b.note && ! a.slide) ++st.bigLeapsUnslid;
        if (leap > 12) ++st.leapsOverOctave;
        ++pairs;
        if (leap <= 4) ++steps;
    };
    for (int i = 0; i < p.length; ++i)
    {
        if (! isNote (at (p, i))) continue;
        if (prev >= 0) leapPair (prev, i);
        else first = i;
        prev = i;
    }
    if (first >= 0 && prev != first) leapPair (prev, first);
    if (pairs > 0) st.stepwiseShare = static_cast<double> (steps) / pairs;
    return st;
}

// ---- Style rules --------------------------------------------------------------------------

namespace
{
constexpr std::uint16_t mask (std::initializer_list<int> pcs) { return acid::seq::noteMaskOf (pcs); }
constexpr std::uint8_t octs (std::initializer_list<int> o) { return acid::seq::octaveMaskOf (o); }

// style, pcs, accent share, slides/16, rests/16, ties/16, octave steps/16,
// generator: note mask, octave mask, note, slide, accent amounts, default tempo
const StyleRules kStyles[] = {
    { "Showcase",     1, 7, 0.05, 0.40, 6, 0, 4, 0, 8, 0, mask ({ 0, 3, 5, 7, 10 }), octs ({ 0, 1 }), 85, 25, 25, 125 },
    { "Chicago",      3, 7, 0.05, 0.35, 3, 0, 3, 2, 9, 0, mask ({ 0, 3, 5, 6, 7, 10 }), octs ({ 0, 1 }), 90, 15, 15, 122 },
    { "Anthem",       1, 2, 0.70, 1.00, 7, 0, 1, 0, 2, 0, mask ({ 0, 7 }), octs ({ 0 }), 100, 35, 95, 130 },
    { "Rolling",      1, 4, 0.10, 0.45, 4, 0, 2, 0, 1, 2, mask ({ 0, 3, 7 }), octs ({ 0, 1 }), 100, 20, 25, 132 },
    { "Melodic",      5, 8, 0.05, 0.35, 4, 0, 4, 0, 7, 0, mask ({ 0, 2, 3, 5, 7, 8, 10 }), octs ({ 0, 1 }), 85, 25, 18, 108 },
    { "Minimal",      1, 3, 0.00, 0.40, 2, 5, 11, 0, 5, 0, mask ({ 0, 3, 7 }), octs ({ 0, 1 }), 40, 20, 20, 124 },
    { "Hard",         1, 4, 0.10, 0.40, 4, 1, 5, 0, 2, 1, mask ({ 0, 3, 7 }), octs ({ 0, 1 }), 80, 25, 25, 142 },
    { "Trance",       1, 4, 0.10, 0.50, 3, 2, 5, 0, 2, 0, mask ({ 0, 3, 7, 10 }), octs ({ 0, 1 }), 92, 20, 30, 138 },
    { "Dub",          1, 5, 0.00, 0.40, 3, 3, 10, 1, 8, 0, mask ({ 0, 5, 7, 10 }), octs ({ -1, 0, 1 }), 45, 30, 15, 90 },
    { "Breaks",       3, 6, 0.50, 1.00, 4, 0, 1, 0, 1, 6, mask ({ 0, 3, 5, 7, 10 }), octs ({ 0, 1 }), 100, 10, 100, 126 },
    { "Experimental", 1, 8, 0.00, 1.00, 8, 0, 10, 0, 15, 0, mask ({ 0, 1, 6, 7 }), octs ({ -1, 0, 1 }), 80, 35, 45, 126 },
    { "Polymeter",    1, 3, 0.00, 0.50, 4, 0, 8, 0, 4, 0, mask ({ 0, 3, 7 }), octs ({ 0, 1 }), 70, 20, 25, 124 },
    { "Teaching",     1, 4, 0.00, 1.00, 6, 0, 10, 0, 10, 0, mask ({ 0, 3 }), octs ({ 0, 1 }), 80, 25, 25, 125 },
};

/** Second-bar transposition of the Answer variation, by style (0: an octave lift instead). */
int answerInterval (std::string_view style)
{
    if (style == "Chicago" || style == "Dub" || style == "Showcase") return 5;
    if (style == "Rolling" || style == "Hard" || style == "Experimental") return 3;
    if (style == "Melodic") return -2;
    if (style == "Minimal" || style == "Breaks") return 7;
    if (style == "Trance") return -4;
    return 0;
}
}

std::span<const StyleRules> allStyles()
{
    return kStyles;
}

const StyleRules& styleRules (std::string_view style)
{
    for (const StyleRules& r : kStyles)
        if (style == r.style) return r;
    fail ("unknown style '" + std::string (style) + "'");
}

// ---- Curation -----------------------------------------------------------------------------

std::string curateSanity (const Pattern& p)
{
    const LineStats st = statsOf (p);
    if (st.notes < 2) return "fewer than two notes";
    if (st.orphanTie) return "a tie with no note before it";
    if (st.slideIntoRest) return "a slide into a rest";
    return {};
}

std::string curateStyle (const Pattern& p, const StyleRules& r, bool generated)
{
    if (auto why = curateSanity (p); ! why.empty()) return why;
    const LineStats st = statsOf (p);
    const auto out = [] (double v, double lo, double hi) { return v < lo - 1.0e-9 || v > hi + 1.0e-9; };

    if (st.pitchClasses < r.minPcs || st.pitchClasses > r.maxPcs)
        return std::to_string (st.pitchClasses) + " pitch classes, the style wants " + std::to_string (r.minPcs) + " to " + std::to_string (r.maxPcs);
    if (out (st.accentShare(), r.minAccentShare, r.maxAccentShare)) return "accent share " + std::to_string (st.accentShare());
    if (st.per16 (st.slides) > r.maxSlides16 + 1.0e-9) return "too many slides";
    if (out (st.per16 (st.rests), r.minRests16, r.maxRests16)) return "rests per 16 " + std::to_string (st.per16 (st.rests));
    // Generated lines too: the generator writes no ties, so styles that need them get them
    // from addTies before this check.
    if (out (st.per16 (st.ties), r.minTies16, r.maxTies16)) return "ties per 16 " + std::to_string (st.per16 (st.ties));
    if (st.per16 (st.octaveSteps) < r.minOctaves16 - 1.0e-9) return "too few octave steps";

    if (generated)
    {
        // Boring: the line has to move somewhere: two different pitches, or accents and a slide.
        std::set<int> pitches;
        for (int i = 0; i < p.length; ++i)
            if (isNote (at (p, i))) pitches.insert (pitchOf (at (p, i)));
        if (pitches.size() < 2 && (st.accents < 2 || st.slides < 1)) return "boring: one pitch and no articulation";
        if (st.notes * 16 < 3 * p.length) return "boring: fewer than three notes a bar";
        if (st.longestRestRun > std::max (4, static_cast<int> (std::lround (r.maxRests16 * p.length / 16.0 * 0.6))))
            return "boring: a hole of " + std::to_string (st.longestRestRun) + " rests";

        // Chaotic: leaps past the octave, unslid wide leaps, a melodic line that does not walk,
        // and accent densities between the two idioms.
        if (st.leapsOverOctave > 0) return "chaotic: a leap of more than an octave";
        if (st.per16 (st.bigLeapsUnslid) > 3.0) return "chaotic: too many wide leaps without a slide";
        if (std::string_view (r.style) == "Melodic" && st.stepwiseShare < 0.5) return "chaotic: a melodic line with too little stepwise motion";
        if (r.maxAccentShare < 0.6 && st.accentShare() > 0.45) return "chaotic: accent share between sparse and saturated";
        if (st.per16 (st.slides) > 5.0) return "chaotic: slides everywhere";
    }
    return {};
}

// ---- Variations ---------------------------------------------------------------------------

const char* variationName (Variation v)
{
    switch (v)
    {
        case Variation::Push: return "Push";
        case Variation::AccentShift: return "Shift";
        case Variation::UpTop: return "Up Top";
        case Variation::Breakdown: return "Breakdown";
        case Variation::Answer: return "Answer";
        case Variation::Fill: return "Fill";
        case Variation::Glide: return "Glide";
        case Variation::HalfTime: return "Half Time";
        case Variation::Triplet: return "Triplet";
        case Variation::Mutation: return "Mutation";
        case Variation::HalfAccent: return "Build";
        case Variation::Stretch: return "Drift";
    }
    return "";
}

namespace
{
bool differs (const Pattern& a, const Pattern& b)
{
    return ! acid::seq::samePattern (a, b);
}

/** Number of steps (over the longer length) whose sounding content differs. */
int soundingDistance (const Pattern& a, const Pattern& b)
{
    const int n = std::max (a.length, b.length);
    int d = 0;
    for (int i = 0; i < n; ++i)
    {
        const Step& x = at (a, i % a.length);
        const Step& y = at (b, i % b.length);
        if (x.state != y.state) { ++d; continue; }
        if (x.state == StepState::Note && (pitchOf (x) != pitchOf (y) || x.accent != y.accent || x.slide != y.slide)) ++d;
    }
    return d;
}

bool pushVariation (const Pattern& main, Pattern& out)
{
    for (const int n : { 2, -2, 4, -4, 1, -1, 3, -3, 6, -6 })
    {
        if (std::abs (n) >= main.length) continue;
        Pattern p = main;
        acid::seq::rotate (p, n);
        if (isNote (at (p, 0)) && differs (p, main) && curateSanity (p).empty()) { out = p; return true; }
    }
    return false;
}

bool accentShift (const Pattern& main, Pattern& out)
{
    // On a line of one pitch, moving the accents along the notes is only a rotation.
    std::set<int> pitches;
    for (int i = 0; i < main.length; ++i)
        if (isNote (at (main, i))) pitches.insert (pitchOf (at (main, i)));
    if (pitches.size() < 2) return false;
    std::vector<int> notes;
    for (int i = 0; i < main.length; ++i)
        if (isNote (at (main, i))) notes.push_back (i);
    const auto n = static_cast<int> (notes.size());
    int count = 0;
    for (int i : notes) count += at (main, i).accent ? 1 : 0;
    if (count == 0 || count == n) return false;
    for (int k = 1; k < n; ++k)
    {
        Pattern p = main;
        for (int j = 0; j < n; ++j)
            at (p, notes[static_cast<std::size_t> (j)]).accent = at (main, notes[static_cast<std::size_t> ((j - k + n) % n)]).accent;
        if (differs (p, main)) { out = p; return true; }
    }
    return false;
}

bool upTop (const Pattern& main, std::uint32_t seed, Pattern& out)
{
    std::vector<int> cands;
    for (int i = 1; i < main.length; i += 2)
    {
        const Step& s = at (main, i);
        const Step& prev = at (main, i - 1);
        if (isNote (s) && isNote (prev) && ! prev.slide && s.octave <= 0 && pitchOf (s) + 12 != pitchOf (prev))
            cands.push_back (i);
    }
    if (cands.empty()) return false;
    Pattern p = main;
    const std::size_t first = seed % cands.size();
    const int changes = std::min<int> (2, static_cast<int> (cands.size()));
    for (int c = 0; c < changes; ++c)
    {
        const int i = cands[(first + static_cast<std::size_t> (c) * (cands.size() / 2 + 1)) % cands.size()];
        at (p, i).octave = static_cast<std::int8_t> (at (p, i).octave + 1);
        at (p, i - 1).slide = true;
    }
    if (! differs (p, main) || ! curateSanity (p).empty()) return false;
    out = p;
    return true;
}

void dropSlidesIntoRests (Pattern& p)
{
    for (int i = 0; i < p.length; ++i)
        if (at (p, i).slide && slideTarget (p, i) < 0) at (p, i).slide = false;
}

bool breakdown (const Pattern& main, Pattern& out)
{
    const LineStats st = statsOf (main);
    const bool saturated = st.accentShare() > 0.6;
    const int beat = main.divMod == acid::seq::DivMod::Triplet ? 3 : 4;
    // Keep the notes on the beat phase that has the most of them (an off-beat line keeps its
    // off-beats), plus the accents of a sparse-accent line.
    std::array<int, 4> perPhase {};
    for (int i = 0; i < main.length; ++i)
        if (isNote (at (main, i))) ++perPhase[static_cast<std::size_t> (i % beat)];
    const int phase = static_cast<int> (std::max_element (perPhase.begin(), perPhase.begin() + beat) - perPhase.begin());
    Pattern p = main;
    bool keeping = false;
    for (int i = 0; i < p.length; ++i)
    {
        Step& s = at (p, i);
        if (isNote (s))
        {
            keeping = i % beat == phase || (! saturated && s.accent);
            if (! keeping) { s.state = StepState::Rest; clearFlags (s); }
        }
        else if (s.state == StepState::Tie && ! keeping)
            s.state = StepState::Rest;
    }
    // Let each kept note ring into the gap after it.
    for (int i = 0; i + 1 < p.length; ++i)
        if (isNote (at (p, i)) && ! at (p, i).slide && at (p, i + 1).state == StepState::Rest && (i + 1) % beat != phase)
            at (p, i + 1).state = StepState::Tie;
    dropSlidesIntoRests (p);
    if (statsOf (p).notes < 3 || ! differs (p, main) || ! curateSanity (p).empty()) return false;
    out = p;
    return true;
}

void movePitch (Step& s, int semis)
{
    int pi = acid::seq::pitchIndex (s.note, s.octave) + semis;
    while (pi < 0) pi += 12;
    while (pi >= acid::seq::kNumPitches) pi -= 12;
    s.note = static_cast<std::uint8_t> (pi % 12);
    s.octave = static_cast<std::int8_t> (pi / 12 + acid::seq::kMinOctave);
}

bool answer (const Pattern& main, std::string_view style, Pattern& out)
{
    const int len = main.length;
    if (len * 2 > acid::seq::kMaxSteps || len < 8) return false;
    Pattern p = main;
    if (! acid::seq::duplicate (p)) return false;
    const int interval = answerInterval (style);
    const int from = len + len / 2;
    for (int i = from; i < 2 * len; ++i)
    {
        Step& s = at (p, i);
        if (interval != 0) movePitch (s, interval);
        else if (isNote (s) && (i % 2 == 1) && s.octave < 1) s.octave = static_cast<std::int8_t> (s.octave + 1);
    }
    // A different articulation in the second bar: accents move one note later.
    std::vector<int> notes;
    for (int i = len; i < 2 * len; ++i)
        if (isNote (at (p, i))) notes.push_back (i);
    int count = 0;
    for (int i : notes) count += at (p, i).accent ? 1 : 0;
    if (count > 0 && count < static_cast<int> (notes.size()))
    {
        const auto n = static_cast<int> (notes.size());
        std::vector<bool> acc;
        for (int i : notes) acc.push_back (at (p, i).accent);
        for (int j = 0; j < n; ++j)
            at (p, notes[static_cast<std::size_t> (j)]).accent = acc[static_cast<std::size_t> ((j - 1 + n) % n)];
    }
    if (! curateSanity (p).empty()) return false;
    out = p;
    return true;
}

bool fill (const Pattern& main, Pattern& out)
{
    const int len = main.length;
    const int rootStep = firstNote (main);
    if (len < 8 || rootStep < 0 || rootStep >= len - 4) return false;
    // A line that leaves its last beat empty, or nearly, keeps that space: four notes written
    // into it would be a build, not a fill.
    int lastBeatNotes = 0;
    for (int i = len - 4; i < len; ++i) lastBeatNotes += isNote (at (main, i)) ? 1 : 0;
    if (lastBeatNotes < 2) return false;
    const Step root = at (main, rootStep);
    // The line's most used other pitch class, for the third note of the fill.
    std::map<int, int> uses;
    for (int i = 0; i < len; ++i)
        if (isNote (at (main, i)) && at (main, i).note != root.note) ++uses[pitchOf (at (main, i))];
    int other = pitchOf (root) + 7, best = 0;
    for (const auto& [pitch, n] : uses)
        if (n > best) { best = n; other = pitch; }

    Pattern p = main;
    const auto set = [&p] (int i, int pitch, bool accent, bool slide, int ratchet) {
        Step& s = at (p, i);
        s = Step {};
        s.state = StepState::Note;
        Step tmp;
        tmp.note = 0;
        tmp.octave = 0;
        movePitch (tmp, pitch);
        s.note = tmp.note;
        s.octave = tmp.octave;
        s.accent = accent;
        s.slide = slide;
        s.ratchet = static_cast<std::uint8_t> (ratchet);
    };
    // The fill's accents follow the line's own density: both on a saturated line, the last
    // one on a sparse-accent line, none on a line without accents.
    const double share = statsOf (main).accentShare();
    const int r = pitchOf (root);
    const int up = root.octave < 2 ? r + 12 : r;
    // The ratcheted neighbour sits in the octave of the octave note before it, so the fill
    // moves less than an octave each way: the octave itself is reached under a slide, and a
    // leap past it and back without one is outside the idiom.
    const int upOctave = up >= 0 ? up / 12 : (up - 11) / 12;
    const int neighbour = 12 * upOctave + ((other % 12) + 12) % 12;
    set (len - 4, r, share >= 0.6, true, 1);
    set (len - 3, up, false, false, 1);
    set (len - 2, neighbour, false, false, 2);
    set (len - 1, up, share > 0.0, isNote (at (p, 0)), 1);
    if (! differs (p, main) || ! curateSanity (p).empty()) return false;
    out = p;
    return true;
}

bool glide (const Pattern& main, Pattern& out)
{
    const LineStats st = statsOf (main);
    if (st.slides >= 5) return false;
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < main.length;)
    {
        int j = i;
        while (j < main.length && isNote (at (main, j)) && ! at (main, j).slide) ++j;
        if (j - i > bestLen) { bestLen = j - i; bestStart = i; }
        i = j + 1;
    }
    if (bestLen < 3) return false;
    Pattern p = main;
    at (p, bestStart).slide = true;
    at (p, bestStart + 1).slide = true;
    if (! curateSanity (p).empty()) return false;
    out = p;
    return true;
}

bool halfTime (const Pattern& main, Pattern& out)
{
    if (main.div != acid::seq::StepDiv::d1_16 || main.divMod != acid::seq::DivMod::Full) return false;
    Pattern p = main;
    p.div = acid::seq::StepDiv::d1_8;
    out = p;
    return true;
}

bool triplet (const Pattern& main, Pattern& out)
{
    if (main.length != 16 || main.div != acid::seq::StepDiv::d1_16 || main.divMod != acid::seq::DivMod::Full) return false;
    Pattern p = main;
    acid::seq::setLength (p, 12);
    p.div = acid::seq::StepDiv::d1_8;
    p.divMod = acid::seq::DivMod::Triplet;
    p.shuffle = 0;
    dropSlidesIntoRests (p);
    if (! curateSanity (p).empty()) return false;
    out = p;
    return true;
}

bool halfAccent (const Pattern& main, Pattern& out)
{
    if (statsOf (main).accentShare() < 0.6) return false;
    Pattern p = main;
    for (int i = 1; i < p.length; i += 2)
        at (p, i).accent = false;
    if (! differs (p, main)) return false;
    out = p;
    return true;
}

bool stretch (const Pattern& main, Pattern& out)
{
    int newLength = 0;
    if (main.length == 16) newLength = 13;
    else if (main.length % 2 == 1 && main.length <= 13) newLength = main.length + 2;
    else if (main.length == 15) newLength = 11;
    else return false;
    Pattern p = main;
    acid::seq::setLength (p, newLength);
    dropSlidesIntoRests (p);
    if (! differs (p, main) || ! curateSanity (p).empty()) return false;
    out = p;
    return true;
}

bool mutation (const Pattern& main, const StyleRules& r, std::uint32_t seed, Pattern& out)
{
    const LineStats st = statsOf (main);
    const int rootStep = firstNote (main);
    if (rootStep < 0) return false;
    const int root = at (main, rootStep).note;

    acid::seq::GenParams g = main.gen;
    g.root = static_cast<std::uint8_t> (root);
    g.noteMask = 0;
    g.octaveMask = 0;
    for (int i = 0; i < main.length; ++i)
    {
        const Step& s = at (main, i);
        if (! isNote (s)) continue;
        g.noteMask = static_cast<std::uint16_t> (g.noteMask | (1u << ((s.note - root + 12) % 12)));
        g.octaveMask = static_cast<std::uint8_t> (g.octaveMask | (1u << (s.octave - acid::seq::kMinOctave)));
    }
    // Only the octaves the line lives in: one that a single note visits is left out.
    {
        std::array<int, 5> perOctave {};
        for (int i = 0; i < main.length; ++i)
            if (isNote (at (main, i))) ++perOctave[static_cast<std::size_t> (at (main, i).octave - acid::seq::kMinOctave)];
        std::uint8_t m = 0;
        for (int o = 0; o < 5; ++o)
            if (perOctave[static_cast<std::size_t> (o)] >= std::max (2, st.notes / 5)) m = static_cast<std::uint8_t> (m | (1u << o));
        if (m != 0) g.octaveMask = m;
    }
    g.useAccent = st.accents > 0;
    g.useSlide = st.slides > 0;
    g.useVibrato = false;
    const auto pct = [] (double x) { return static_cast<std::uint8_t> (std::clamp (std::lround (x * 100.0), 5L, 100L)); };
    // As many notes as the line plays; its ties are given back after the draw (below).
    g.amountNote = pct (static_cast<double> (st.notes) / st.length);
    g.amountAccent = pct (st.accentShare());
    g.amountSlide = pct (static_cast<double> (st.slides) / std::max (1, st.notes) * 1.2);
    g.amountVibrato = 0;

    // The style's ranges, loosened for ties and rests (the generator writes no ties, so a
    // tied line's relative has rests where the ties were).
    StyleRules loose = r;
    loose.maxRests16 = std::max (r.maxRests16, st.per16 (st.rests + st.ties) + 1.0);
    loose.minTies16 = 0;
    loose.minRests16 = 0;
    loose.minPcs = 1;
    loose.minOctaves16 = 0;
    loose.minAccentShare = std::min (r.minAccentShare, st.accentShare()) * 0.6;

    for (std::uint32_t k = 0; k < 400; ++k)
    {
        Pattern p = main;
        acid::seq::clearSteps (p);
        p.gen = g;
        // Where the main line rests on a beat, the relative rests too: the gate lane is locked
        // there, so an off-beat or sparse line keeps its shape.
        const int beat = main.divMod == acid::seq::DivMod::Triplet ? 3 : 4;
        for (int i = 0; i < main.length; i += beat)
            if (at (main, i).state == StepState::Rest) at (p, i).locks = acid::seq::kLaneGate;
        acid::seq::regenerate (p, seed + k * 7919u);
        // The generator writes no ties. A tied line's relative gets as many as the line has,
        // where the line has them when a note comes just before, then after any other note,
        // never on a beat the line rests on: on a sustaining sound the ties are much of the
        // level, and a relative without them plays several dB quieter.
        for (int pass = 0, ties = 0; pass < 2 && ties < st.ties; ++pass)
            for (int i = 1; i < p.length && ties < st.ties; ++i)
            {
                Step& s = at (p, i);
                const Step& prev = at (p, i - 1);
                const bool held = isNote (prev) || prev.state == StepState::Tie;
                const bool wanted = pass == 0 ? at (main, i).state == StepState::Tie : (s.locks & acid::seq::kLaneGate) == 0;
                if (s.state == StepState::Rest && held && ! (isNote (prev) && prev.slide) && wanted)
                {
                    s.state = StepState::Tie;
                    ++ties;
                }
            }
        acid::seq::keepEdits (p);
        if (! curateStyle (p, loose, true).empty()) continue;
        if (soundingDistance (p, main) < std::max (4, main.length / 4)) continue;
        // A relative keeps at least half the accents (and half the slides): on many sounds
        // the accents are the line, and a version without them falls silent or flat.
        const LineStats ps = statsOf (p);
        if (ps.accents * 2 < st.accents || ps.slides * 2 < st.slides) continue;
        // About as busy as the line, so the relative plays close to the line's level in the
        // same scene: within a quarter of its note count, and as many ties.
        if (std::abs (ps.notes - st.notes) * 4 > st.notes || ps.ties != st.ties) continue;
        out = p;
        return true;
    }
    return false;
}
}

bool makeVariation (const Pattern& main, Variation v, const StyleRules& r, std::uint32_t seed, Pattern& out)
{
    bool ok = false;
    switch (v)
    {
        case Variation::Push: ok = pushVariation (main, out); break;
        case Variation::AccentShift: ok = accentShift (main, out); break;
        case Variation::UpTop: ok = upTop (main, seed, out); break;
        case Variation::Breakdown: ok = breakdown (main, out); break;
        case Variation::Answer: ok = answer (main, r.style, out); break;
        case Variation::Fill: ok = fill (main, out); break;
        case Variation::Glide: ok = glide (main, out); break;
        case Variation::HalfTime: ok = halfTime (main, out); break;
        case Variation::Triplet: ok = triplet (main, out); break;
        case Variation::Mutation: ok = mutation (main, r, seed, out); break;
        case Variation::HalfAccent: ok = halfAccent (main, out); break;
        case Variation::Stretch: ok = stretch (main, out); break;
    }
    if (! ok || ! differs (out, main) || ! curateSanity (out).empty()) return false;
    // The generator's chaotic-leap rule, on every variation: a variation may not leap past the
    // octave, or leap over a fifth without a slide, more often than its main line does.
    const LineStats ms = statsOf (main), vs = statsOf (out);
    if (vs.per16 (vs.leapsOverOctave) > ms.per16 (ms.leapsOverOctave) + 1.0e-9) return false;
    if (vs.per16 (vs.bigLeapsUnslid) > std::max (3.0, ms.per16 (ms.bigLeapsUnslid)) + 1.0e-9) return false;
    return true;
}

// ---- Similarity ---------------------------------------------------------------------------

namespace
{
/** Same step content apart from pitch: state, and for a Note its flags, ratchet and probability. */
bool sameArticulation (const Step& a, const Step& b)
{
    if (a.state != b.state) return false;
    if (a.state != StepState::Note) return true;
    return a.accent == b.accent && a.slide == b.slide && a.vibrato == b.vibrato && a.ratchet == b.ratchet
           && a.probability == b.probability;
}
}

double lineSimilarity (const Pattern& a, const Pattern& b)
{
    if (a.length != b.length || a.length == 0) return 0.0;
    const int n = a.length;
    constexpr int kSpan = 12 * (acid::seq::kMaxOctave - acid::seq::kMinOctave + 1);
    int best = 0;
    for (int r = 0; r < n; ++r)
    {
        // Steps that agree whatever the transposition (rests, ties), plus, per transposition,
        // the notes that agree in pitch once moved by it.
        std::array<int, 2 * kSpan + 1> byShift {};
        int fixed = 0;
        for (int i = 0; i < n; ++i)
        {
            const Step& x = at (a, i);
            const Step& y = at (b, (i + r) % n);
            if (! sameArticulation (x, y)) continue;
            if (x.state != StepState::Note) { ++fixed; continue; }
            const int shift = pitchOf (y) - pitchOf (x);
            if (std::abs (shift) <= kSpan) ++byShift[static_cast<std::size_t> (shift + kSpan)];
        }
        best = std::max (best, fixed + *std::max_element (byShift.begin(), byShift.end()));
    }
    return static_cast<double> (best) / n;
}

bool sameLineUpTo (const Pattern& a, const Pattern& b, bool ignoreTiming)
{
    if (! ignoreTiming && (a.div != b.div || a.divMod != b.divMod || a.shuffle != b.shuffle)) return false;
    return lineSimilarity (a, b) >= 1.0;
}

double skeletonSimilarity (const Pattern& a, const Pattern& b)
{
    if (a.length != b.length || a.length == 0) return 0.0;
    int same = 0;
    for (int i = 0; i < a.length; ++i)
    {
        const Step& x = at (a, i);
        const Step& y = at (b, i);
        if (x.state == y.state && (x.state != StepState::Note || (x.accent == y.accent && x.slide == y.slide))) ++same;
    }
    return static_cast<double> (same) / a.length;
}

// ---- Transposition ------------------------------------------------------------------------

int transposeTogether (std::vector<Pattern*>& patterns, int semitones)
{
    // Range over every step that can ever sound (Notes inside the length) and every stored
    // pitch, so hidden steps and generator candidates stay in range too.
    int lo = 1000, hi = -1000;
    for (const Pattern* p : patterns)
        for (int i = 0; i < acid::seq::kMaxSteps; ++i)
        {
            const Step& s = at (*p, i);
            const int a = acid::seq::pitchIndex (s.note, s.octave);
            const int b = acid::seq::pitchIndex (s.candNote, s.candOctave);
            lo = std::min ({ lo, a, b });
            hi = std::max ({ hi, a, b });
        }
    int shift = semitones;
    while (lo + shift < 0) shift += 12;
    while (hi + shift >= acid::seq::kNumPitches) shift -= 12;
    if (lo + shift < 0) fail ("a bank spans more than the sequencer's range");
    for (Pattern* p : patterns)
        acid::seq::transpose (*p, shift, true);
    return shift;
}

// ---- Tags ---------------------------------------------------------------------------------

std::vector<std::string> patternTags (const Pattern& p, std::string_view role)
{
    const LineStats st = statsOf (p);
    std::vector<std::string> t;
    t.emplace_back (role);
    if (st.pitchClasses == 1) t.emplace_back ("One Note");
    else if (st.pitchClasses == 2) t.emplace_back ("Two Note");
    else if (st.pitchClasses >= 5) t.emplace_back ("Melodic Line");
    if (st.accentShare() >= 0.7) t.emplace_back ("All Accent");
    else if (st.accents > 0 && st.per16 (st.accents) <= 3.5) t.emplace_back ("Sparse Accent");
    if (st.slides >= 2) t.emplace_back ("Slides");
    if (st.per16 (st.octaveSteps) >= 3) t.emplace_back ("Octave Jumps");
    if (st.per16 (st.ties) >= 3) t.emplace_back ("Ties");
    if (st.per16 (st.rests) >= 5) t.emplace_back ("Rests");
    if (p.shuffle > 0) t.emplace_back ("Shuffle");
    if (p.divMod == acid::seq::DivMod::Triplet) t.emplace_back ("Triplet");
    if (p.length % 2 == 1) t.emplace_back ("Odd Length");
    if (p.length > 16) t.emplace_back ("Long");
    if (p.div == acid::seq::StepDiv::d1_8 && p.divMod == acid::seq::DivMod::Full) t.emplace_back ("Half Time");
    if (st.vibrato > 0) t.emplace_back ("Vibrato");
    if (st.ratchets > 0) t.emplace_back ("Ratchet");
    return t;
}

// ---- Pool ---------------------------------------------------------------------------------

namespace
{
std::vector<PooledPattern>& pool()
{
    static std::vector<PooledPattern> p;
    return p;
}
}

void poolPattern (PooledPattern p)
{
    pool().push_back (std::move (p));
}

const std::vector<PooledPattern>& patternPool()
{
    return pool();
}

// ---- Generated lines ----------------------------------------------------------------------

namespace
{
/** Ties for a generated line in a style whose rules ask for them (the generator writes none):
    unaccented, unslid notes that follow an unslid note become ties, repeats of the same pitch
    first, then off-beat steps, until the line has one more than the style's minimum (within
    its maximum). keepEdits keeps the generator memory in step, so the density knobs leave
    the ties alone. */
void addTies (Pattern& p, const StyleRules& r)
{
    if (r.minTies16 <= 0.0) return;
    const int need = static_cast<int> (std::ceil (r.minTies16 * p.length / 16.0 - 1.0e-9));
    const int most = static_cast<int> (std::floor (r.maxTies16 * p.length / 16.0 + 1.0e-9));
    const int aim = std::min (need + 1, most);
    std::vector<std::pair<int, int>> cands;     // (preference, step)
    for (int i = 1; i < p.length; ++i)
    {
        const Step& s = at (p, i);
        const Step& prev = at (p, i - 1);
        if (! isNote (s) || s.accent || s.slide || ! isNote (prev) || prev.slide) continue;
        const int pref = (pitchOf (s) == pitchOf (prev) ? 0 : 2) + (i % 2 == 1 ? 0 : 1);
        cands.emplace_back (pref, i);
    }
    std::sort (cands.begin(), cands.end());
    int ties = statsOf (p).ties;
    for (const auto& [pref, i] : cands)
    {
        if (ties >= aim) break;
        // A tie right after a tie made here would hold a note whose own step became a tie.
        if (at (p, i - 1).state != StepState::Note) continue;
        at (p, i).state = StepState::Tie;
        ++ties;
    }
    acid::seq::keepEdits (p);
}
}

std::vector<Pattern> generatedLines (const StyleRules& r, int count, std::uint32_t firstSeed, const Pattern& shape,
                                     std::vector<std::string>* rejectLog)
{
    std::vector<Pattern> accepted;
    const int length = shape.length;
    for (std::uint32_t k = 0; k < 5000 && static_cast<int> (accepted.size()) < count; ++k)
    {
        Pattern p = shape;
        p.gen.noteMask = r.noteMask;
        p.gen.octaveMask = r.octaveMask;
        p.gen.root = 0;
        p.gen.useAccent = true;
        p.gen.useSlide = true;
        p.gen.useVibrato = false;
        p.gen.amountNote = r.amountNote;
        p.gen.amountSlide = r.amountSlide;
        p.gen.amountAccent = r.amountAccent;
        p.gen.amountVibrato = 0;
        const std::uint32_t seed = firstSeed + k * 104729u;
        acid::seq::regenerate (p, seed);
        addTies (p, r);

        std::string why = curateStyle (p, r, true);
        if (why.empty())
            for (const Pattern& a : accepted)
                if (soundingDistance (a, p) < length / 3) { why = "too close to an accepted line"; break; }
        if (why.empty())
            for (const PooledPattern& a : pool())
                if (a.style == r.style && a.pattern.length == p.length && soundingDistance (a.pattern, p) < length / 4) { why = "too close to a scene line"; break; }
        if (! why.empty())
        {
            if (rejectLog) rejectLog->push_back (std::string (r.style) + " seed " + std::to_string (seed) + ": " + why);
            continue;
        }
        accepted.push_back (p);
    }
    if (static_cast<int> (accepted.size()) < count)
        fail (std::string ("the generator found only ") + std::to_string (accepted.size()) + " acceptable " + r.style + " lines");
    return accepted;
}

// ---- Arpeggiator patterns -----------------------------------------------------------------

namespace
{
// clang-format off
const ArpLine kArps[] = {
    { "Up Three",           "Up",            "1 2 3 1 2 3 1 2 3 1 2 3 1 2 3:A 1+:S" },
    { "Up Four",            "Up",            "1 2 3 4 1 2 3 4 1 2 3 4 1 2 3 4:A" },
    { "Up Four Accents",    "Up",            "1:A 2 3 4 1 2:A 3 4 1 2 3:A 4 1 2 3 4:A" },
    { "Up Octave",          "Octave Spread", "1 2 3 1+ 2+ 3+ 1 2 3 1+ 2+ 3+ 1 2:S 3+ 1+:A" },
    { "Down Four",          "Down",          "4 3 2 1 4 3 2 1 4 3 2 1 4 3 2:S 1-:A" },
    { "Down Octave",        "Down",          "3+ 2+ 1+ 3 2 1 3+ 2+ 1+:A 3 2 1 3:S 2 1 1-" },
    { "Up And Down",        "Up Down",       "1 2 3 4 3 2 1 2 3 4 3 2 1 2:S 3 4:A" },
    { "Pendulum",           "Up Down",       "1 3 2 4 1 3 2 4:A 1 3 2 4 1 3:S 2 4" },
    { "Pedal Low",          "Pedal",         "1 2 1 3 1 4 1 3 1:A 2 1 3 1 4 1:S 2+" },
    { "Pedal High",         "Pedal",         "4 1 4 2 4 3 4 2:A 4 1 4 2 4:S 3 4 1" },
    { "Root Pump",          "Octave Spread", "1 1+ 1 1+:A 1 1+ 1 1+:S 2 2+ 2 2+:A 3 3+ 3:S 3+" },
    { "Octave Bounce",      "Octave Spread", "1 1+:S 1 1+ 2 2+:S 2 2+:A 3 3+:S 3 3+ 1 1+:A 1 1+" },
    { "Chord Stab",         "Chord",         "1:A . 1 2 3 . 1:A . 2 3 . 1 1:A . 3:S 2" },
    { "Triplet Climb",      "Up",            "div=1/8 mod=tri 1 2 3 1 2 3:A 1+ 2 3 1:S 2+ 3" },
    { "Triplet Fall",       "Down",          "div=1/8 mod=tri 3+ 2+ 1+ 3 2 1:A 3 2 1 3:S 2 1-" },
    { "Five Step Turn",     "Up Down",       "1 2 3 2:S 1+:A" },
    { "Seven Up",           "Up",            "1 2 3 4 5 6 7:A" },
    { "Wide Spread",        "Octave Spread", "1- 1 1+ 2 2+ 3 3+:A 4 1- 1 1+ 2:S 2+ 3 3+ 4+:A" },
    { "Walk One",           "Random Walk",   "1 2 1 3 2 4 3 2:A 1 3 2 4:S 3 4 2 1" },
    { "Walk Two",           "Random Walk",   "2 1 3 2 4 2 1:A 3 4 3 1 2:S 4 1 3 2" },
    { "Sparse Pick",        "Random Walk",   "1 . 3 . 2:A . 1 4 . 3 . 1:S 2 . 4 ." },
    { "Gallop",             "Pedal",         "1 1 2 1 1 3:A 1 1 4 1 1 3:S 1 1 2 1" },
    { "Trill Top",          "Pedal",         "1 4 1 4:A 1 4 1 4 2 4 2 4:A 3 4 3:S 4" },
    { "Glide Up",           "Up",            "1:S 2:S 3 . 1:S 2:S 3+ . 1:S 2 3:A . 1 2:S 3 4" },
    { "Slide Ladder",       "Up",            "1:S 2:S 3:S 4 1 2 3 4:A 1:S 2:S 3:S 4+ 1 2 3 4:A" },
    { "Accent Run",         "Up",            "1:A 2:A 3:A 4:A 1 2 3 4 1:A 2:A 3:A 4:A 1 2 3 4" },
    { "Ten Fingers",        "Up Down",       "1 2 3 4 5 6 7 8 9 10 9 8 7 6 5 4:A" },
    { "Double Up",          "Up",            "1 1 2 2 3 3 4 4:A 1 1 2 2 3 3:S 4 4+" },
    { "Skipping Stone",     "Random Walk",   "1 3 . 2 4 . 3 1+:A . 4 2 . 1:S 3 . 2" },
    { "Ratchet Climb",      "Up",            "1:R2 2 3:R2 4 1 2:R3 3 4:A 1:R2 2 3 4 1:R4 2 3:A 4" },
    { "Vibrato Arp",        "Up",            "1:V ~ 2 3 1+:V ~ 2 3:A 1:V ~ 2 3 4:V ~ 3:S 2" },
    { "Shuffle Arp",        "Up",            "shuffle=50 1 2 3 4 1 2 3 4:A 1 2 3 4 1 2:S 3 4" },
    { "Half Time Arp",      "Up",            "div=1/8 1 2 3 1+ 2 3 1+:A 2+" },
    { "Quarter Pulse",      "Up",            "div=1/4 1 2 3:A 4" },
    { "Low Ostinato",       "Pedal",         "1- 1 2 1 1- 1 3 1:A 1- 1 2 1 1- 1:S 4 1" },
    { "Climb And Drop",     "Octave Spread", "1 2 3 4 1+ 2+ 3+ 4+:A 4 3 2 1 4- 3- 2-:S 1-" },
    { "Thirds",             "Up",            "1 3 2 4 3 5 4 6:A 5 7 6 8 7:S 9 8 10" },
    { "Outside In",         "Chord",         "1 4 2 3 1 4 2 3:A 1+ 4 2 3 1 4:S 2 3" },
    { "Broken Chord",       "Chord",         "1 3 2 . 1 3 2:A . 1+ 3 2 . 1 3:S 2+ ." },
    { "Hammer Root",        "Pedal",         "1:A 1 1 2 1:A 1 1 3 1:A 1 1 4 1:A 1:S 1+ 1" },
    { "Triplet Wave",       "Up Down",       "div=1/8 mod=tri 1 2 3 4 3 2 1:A 2 3 4:S 3 2" },
    { "Nine Step Arp",      "Up",            "1 2 3 1+ 2:A 3 1 2:S 3+" },
    { "Eleven Step Arp",    "Random Walk",   "1 3 2 4 1:A 3 2 4 1 3:S 2+" },
    { "Off Beat Arp",       "Up",            ". 1 2 3 . 1 2 3:A . 1 2 3 . 1 2:S 3+" },
    { "Stairs",             "Up Down",       "1 1 2 2 3 3 4 4 3 3 2 2 1:A 1 1:S 2" },
    { "Ping Pong Wide",     "Octave Spread", "1- 4+ 1- 4+ 2- 3+ 2- 3+:A 1- 4+ 1- 4+ 2- 3+:S 2- 3+" },
    { "Top Note Lead",      "Down",          "4 . 4 3 . 3 2:A . 2 1 . 1:S 2 3 4+ ." },
    { "Two Note Swap",      "Pedal",         "1 2 1 2:A 2 1 2 1 1 2 1 2:A 2 1 2:S 1+" },
    { "Accent Waves",       "Up",            "1 2:A 3 4 2:A 3 4 1:A 3 4 1 2:A 4 1 2 3:A" },
    { "Slow Bloom Arp",     "Up",            "div=1/8 1 ~ 2 ~ 3:S 4 ~ 1+:A" },
    { "Two Bar Climb",      "Up",            "1 2 3 4 1 2 3 4:A 1 2 3 4 1 2:S 3 4  2 3 4 1+ 2 3 4 1+:A 2 3 4 1+ 2:S 3 4 1" },
    { "Ladder Down Up",     "Up Down",       "4+ 3+ 2+ 1+ 4 3 2 1:A 1 2 3 4 1+ 2+:S 3+ 4+" },
    { "Sparse Octaves",     "Octave Spread", "1 . . 1+ . . 1:A . 2 . . 2+:S 1 . . ." },
    { "Dotted Arp",         "Up Down",       "mod=dot 1 2 3 4 3 2:A 1 2:S" },
    { "Seven Octave",       "Octave Spread", "1 2 1+ 3 1 2+:A 3:S" },
    { "Chord Pulse",        "Chord",         "1:A 2 3 . 1 2 3:A . 1 2:A 3 . 1 2 3:AS 1" },
};
// clang-format on
}

std::span<const ArpLine> arpLines()
{
    return kArps;
}

Pattern arpPattern (const ArpLine& a)
{
    Pattern p = parseSteps (a.steps, std::string ("arp/") + a.name);
    for (int i = 0; i < acid::seq::kMaxSteps; ++i)
        at (p, i).note = 0;
    setPatternName (p, a.name);
    return p;
}

// ---- The pattern and arpeggiator libraries -------------------------------------------------

namespace
{
const char* const kKeys[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

Entry patternEntry (acid::PresetType type, const Pattern& p, std::vector<std::string> tags, const std::string& description,
                    const std::string& group, double bpm)
{
    const std::string name = p.name;
    if (const auto why = curateSanity (p); ! why.empty())
        fail (std::string (acid::presetTypeName (type)) + "/" + name + ": " + why);
    Entry e;
    e.data = acid::makePatternPreset (type, p);
    e.data.name = name;
    e.data.author = kAuthor;
    e.data.tags = joinTags (tags);
    e.data.description = description;
    e.path = std::string (acid::presetTypeName (type)) + "/" + slugify (name) + ".mw12f";
    e.group = group;
    e.bpm = bpm;
    return e;
}

/** Names of a style's generated lines, one per shape (shapesFor), in order. */
const char* generatedName (std::string_view style, int index)
{
    static const std::map<std::string_view, std::vector<const char*>> names {
        { "Chicago", { "Back Porch Jack", "Short Order", "Fire Escape", "Bus Depot", "Laundromat" } },
        { "Anthem", { "Flag Wave", "Rooftop Chant", "High Beam", "Searchlight", "Floodlight" } },
        { "Rolling", { "Gravel Road", "Belt Drive", "Spin Cycle", "Cog Rail", "Mill Race" } },
        { "Melodic", { "Train Window", "Canal Path", "Paper Boat", "Willow Bend", "Long Letter" } },
        { "Minimal", { "Dim Hallway", "Still Pond", "Chalk Dust", "Blank Wall", "Low Hum" } },
        { "Hard", { "Forge Floor", "Blast Door", "Iron Shutter", "Breaker Yard", "Slag Heap" } },
        { "Trance", { "Night Ferry", "Glass Tower", "Long Runway", "Signal Lamp", "Far Shore" } },
        { "Dub", { "Damp Cellar", "Harbour Fog", "Slow Ripple", "Lantern Smoke", "Moss Wall" } },
        { "Breaks", { "Kerb Hop", "Spray Can", "Bounce Pad", "Bus Shelter", "Rope Swing" } },
        { "Experimental", { "Odd Machine", "Wire Garden", "Static Bloom", "Moth Wing", "Coil Whine" } },
        { "Polymeter", { "Five Pace", "Seven Wheel", "Nine Spokes", "Eleven Stairs", "Thirteen Bells", "Fifteen Lamps" } },
    };
    const auto it = names.find (style);
    if (it == names.end() || index < 0 || index >= static_cast<int> (it->second.size()))
        fail ("no name for generated " + std::string (style) + " line " + std::to_string (index + 1));
    return it->second[static_cast<std::size_t> (index)];
}

/** Shapes of the generated lines a style ships: length, step division and shuffle. */
std::vector<Pattern> shapesFor (std::string_view style)
{
    const auto shape = [] (int len, acid::seq::StepDiv div, acid::seq::DivMod mod, int shuffle) {
        Pattern p;
        p.length = static_cast<std::uint8_t> (len);
        p.div = div;
        p.divMod = mod;
        p.shuffle = static_cast<std::uint8_t> (shuffle);
        return p;
    };
    using D = acid::seq::StepDiv;
    using M = acid::seq::DivMod;
    if (style == "Melodic") return { shape (32, D::d1_16, M::Full, 0), shape (32, D::d1_16, M::Full, 20), shape (16, D::d1_16, M::Full, 0),
                                     shape (32, D::d1_16, M::Full, 0), shape (64, D::d1_16, M::Full, 0) };
    if (style == "Hard") return { shape (16, D::d1_16, M::Full, 0), shape (12, D::d1_8, M::Triplet, 0), shape (16, D::d1_16, M::Full, 0),
                                  shape (12, D::d1_8, M::Triplet, 0), shape (16, D::d1_16, M::Full, 10) };
    if (style == "Polymeter") return { shape (5, D::d1_16, M::Full, 0), shape (7, D::d1_16, M::Full, 0), shape (9, D::d1_16, M::Full, 0),
                                       shape (11, D::d1_16, M::Full, 0), shape (13, D::d1_16, M::Full, 0), shape (15, D::d1_16, M::Full, 0) };
    if (style == "Minimal") return { shape (16, D::d1_16, M::Full, 0), shape (13, D::d1_16, M::Full, 0), shape (16, D::d1_16, M::Full, 0),
                                     shape (11, D::d1_16, M::Full, 0), shape (15, D::d1_16, M::Full, 0) };
    if (style == "Breaks") return { shape (16, D::d1_16, M::Full, 30), shape (16, D::d1_16, M::Full, 40), shape (16, D::d1_16, M::Full, 0),
                                    shape (16, D::d1_16, M::Full, 25), shape (16, D::d1_16, M::Full, 35) };
    if (style == "Chicago") return { shape (16, D::d1_16, M::Full, 0), shape (8, D::d1_16, M::Full, 0), shape (16, D::d1_16, M::Full, 15),
                                     shape (16, D::d1_16, M::Full, 0), shape (16, D::d1_16, M::Full, 0) };
    if (style == "Dub") return { shape (16, D::d1_16, M::Full, 0), shape (16, D::d1_8, M::Full, 0), shape (16, D::d1_16, M::Full, 20),
                                 shape (16, D::d1_16, M::Full, 0), shape (16, D::d1_8, M::Full, 0) };
    if (style == "Trance")
    {
        // Off-beat bass: the beats are locked as rests, so the generator writes around the kick.
        std::vector<Pattern> v (5, shape (16, D::d1_16, M::Full, 0));
        for (Pattern& p : v)
            for (int i = 0; i < 16; i += 4) p.steps[static_cast<std::size_t> (i)].locks = acid::seq::kLaneGate;
        return v;
    }
    return { shape (16, D::d1_16, M::Full, 0), shape (16, D::d1_16, M::Full, 0), shape (16, D::d1_16, M::Full, 0),
             shape (16, D::d1_16, M::Full, 0), shape (16, D::d1_16, M::Full, 0) };
}
}

void addPatterns (Library& lib)
{
    // The scenes' lines: every main line and one variation per scene.
    std::set<std::string> names;
    for (const PooledPattern& pp : patternPool())
    {
        if (! names.insert (pp.pattern.name).second) continue;
        std::vector<std::string> tags { pp.style };
        for (auto& t : patternTags (pp.pattern, pp.role)) tags.push_back (t);
        lib.add (patternEntry (acid::PresetType::Pattern, pp.pattern, tags, pp.description, pp.style, pp.bpm));
    }

    // Generated lines, curated, in a spread of keys. They keep their generator memory, so the
    // density knobs reveal and hide notes without re-rolling.
    std::vector<std::string> rejects;
    int keyCycle = 0;
    for (const StyleRules& r : allStyles())
    {
        const std::string_view style = r.style;
        if (style == "Showcase" || style == "Teaching") continue;
        const std::vector<Pattern> shapes = shapesFor (style);
        int n = 0;
        for (const Pattern& shape : shapes)
        {
            const std::uint32_t seed = 1000u + 7919u * static_cast<std::uint32_t> (keyCycle) + 31u * static_cast<std::uint32_t> (n);
            Pattern p = generatedLines (r, 1, seed, shape, &rejects).front();
            const int key = (keyCycle * 5) % 12;
            std::vector<Pattern*> one { &p };
            transposeTogether (one, key <= 6 ? key : key - 12);
            setPatternName (p, generatedName (style, n));
            ++n;
            ++keyCycle;
            std::vector<std::string> tags { std::string (style) };
            for (auto& t : patternTags (p, "Generated")) tags.push_back (t);
            const bool tied = statsOf (p).ties > 0;
            const std::string description = std::string (style) + " line in " + kKeys[key] + " from the generator (seed "
                                            + std::to_string (p.gen.seed) + "), kept by the curation rules"
                                            + (tied ? ", with ties written in after the draw, as the style asks" : "")
                                            + ". The density knobs still work on it" + (tied ? " (the tied steps stay put)" : "") + ".";
            lib.add (patternEntry (acid::PresetType::Pattern, p, tags, description, std::string (style), r.defaultBpm));
        }
    }
    generatedRejects() = rejects;

    // Arpeggiator patterns.
    for (const ArpLine& a : arpLines())
    {
        const Pattern p = arpPattern (a);
        std::vector<std::string> tags { "Arp", a.shape };
        for (auto& t : patternTags (p, "Main"))
            if (t != "Main" && t != "One Note" && t != "Two Note" && t != "Melodic Line") tags.push_back (t);
        int top = 1;
        for (int i = 0; i < p.length; ++i)
            if (at (p, i).state == StepState::Note) top = std::max<int> (top, at (p, i).arpIndex);
        const std::string description = std::string ("Arpeggiator pattern, ") + std::to_string (p.length) + " steps, playing held notes 1 to "
                                        + std::to_string (top) + " (sorted low to high). Missing notes fall back to the lowest.";
        lib.add (patternEntry (acid::PresetType::Arp, p, tags, description, a.shape, 125.0));
    }
}

std::vector<std::string>& generatedRejects()
{
    static std::vector<std::string> r;
    return r;
}
}
