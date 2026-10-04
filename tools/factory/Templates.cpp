// Generator templates: one file per template of the built-in table (acid::seq::Scales), and
// templates of our own for the acid idioms, which also set the density knobs.
#include "Common.h"

#include "acid/seq/Scales.h"

#include <string_view>
#include <utility>
#include <vector>

namespace factory
{
namespace
{
const char* categoryTag (acid::seq::ScaleCategory c)
{
    switch (c)
    {
        case acid::seq::ScaleCategory::Scale: return "Scale";
        case acid::seq::ScaleCategory::Pentatonic: return "Pentatonic";
        case acid::seq::ScaleCategory::Chord: return "Chord";
        case acid::seq::ScaleCategory::Interval: return "Interval";
        case acid::seq::ScaleCategory::Acid: return "Acid";
        case acid::seq::ScaleCategory::Other: return "Other";
    }
    return "Other";
}

struct AcidTemplate
{
    const char* name;
    const char* style;          // Style tag
    std::uint16_t noteMask;
    std::uint8_t octaveMask;
    bool slide, accent, vibrato;
    std::uint8_t note, slideAmt, accentAmt, vibratoAmt;
    const char* description;
};

using acid::seq::noteMaskOf;
using acid::seq::octaveMaskOf;

// clang-format off
const AcidTemplate kAcidTemplates[] = {
    { "Root Stomp",        "Rolling",   noteMaskOf ({ 0 }),                 octaveMaskOf ({ 0, 1 }),     true,  true,  false, 90, 20, 25, 0,
      "The root in two octaves, nearly every step, a few accents and slides. Ride the knobs." },
    { "Anthem Pair",       "Anthem",    noteMaskOf ({ 0, 3 }),              octaveMaskOf ({ 0 }),        true,  true,  false, 100, 40, 95, 0,
      "Two pitch classes, every step, accent on almost all of them, slides in clusters." },
    { "Anthem Fifth",      "Anthem",    noteMaskOf ({ 0, 7 }),              octaveMaskOf ({ 0, 1 }),     true,  true,  false, 100, 35, 90, 0,
      "Root and fifth, saturated accents. The C13 charge does the rest." },
    { "Chicago Jack",      "Chicago",   noteMaskOf ({ 0, 3, 5, 6, 7, 10 }), octaveMaskOf ({ 0, 1 }),     true,  true,  false, 85, 15, 15, 0,
      "Minor pentatonic with the flat fifth, dense notes, sparse accents and slides." },
    { "Minimal Space",     "Minimal",   noteMaskOf ({ 0, 7 }),              octaveMaskOf ({ -1, 0 }),    true,  true,  false, 35, 15, 15, 0,
      "Few low notes and lots of room for the echoes." },
    { "Hard Stab",         "Hard",      noteMaskOf ({ 0, 3 }),              octaveMaskOf ({ 0, 1 }),     true,  true,  false, 75, 25, 25, 0,
      "Root and minor third, octave stabs, rests between them." },
    { "Trance Offbeat",    "Trance",    noteMaskOf ({ 0, 3, 7, 10 }),       octaveMaskOf ({ 0, 1 }),     true,  true,  false, 75, 20, 30, 0,
      "Minor seventh chord tones for rolling off-beat lines." },
    { "Dub Weight",        "Dub",       noteMaskOf ({ 0, 5, 7, 10 }),       octaveMaskOf ({ -1, 0 }),    true,  true,  false, 45, 30, 15, 0,
      "Low, sparse, slidey. For slow tempos and long echoes." },
    { "Breaks Octaves",    "Breaks",    noteMaskOf ({ 0, 3, 5, 7, 10 }),    octaveMaskOf ({ 0, 1 }),     true,  true,  false, 100, 10, 100, 0,
      "Every step, every accent, notes leaping between two octaves." },
    { "Modal Walk",        "Melodic",   noteMaskOf ({ 0, 2, 3, 5, 7, 9, 10 }), octaveMaskOf ({ 0, 1 }), true,  true,  false, 80, 25, 15, 0,
      "Dorian, for longer melodic lines. Use with a 32-step pattern." },
    { "Phrygian Creep",    "Melodic",   noteMaskOf ({ 0, 1, 3, 5, 7 }),     octaveMaskOf ({ 0 }),        true,  true,  false, 80, 30, 20, 0,
      "The dark flat second, one octave, slides between neighbours." },
    { "Vibrato Pair",      "Experimental", noteMaskOf ({ 0, 3 }),           octaveMaskOf ({ 0, 1 }),     true,  true,  true,  70, 20, 25, 30,
      "Two notes with vibrato steps scattered through them." },
    { "Rolling Octaves",   "Rolling",   noteMaskOf ({ 0, 7 }),              octaveMaskOf ({ 0, 1 }),     true,  true,  false, 100, 15, 30, 0,
      "Root and fifth in two octaves, every step. A rolling bed." },
    { "Sparse Squelch",    "Chicago",   noteMaskOf ({ 0, 3, 7 }),           octaveMaskOf ({ 0, 1 }),     true,  true,  false, 85, 30, 12, 0,
      "Minor triad, dense notes, accents kept rare so each one counts." },
};
// clang-format on

Entry templateEntry (const acid::seq::GenParams& g, const std::string& name, std::vector<std::string> tags, const std::string& description)
{
    Entry e;
    e.data = acid::makeTemplatePreset (g);
    e.data.name = name;
    e.data.author = kAuthor;
    e.data.tags = joinTags (tags);
    e.data.description = description;
    e.path = "template/" + slugify (name) + ".mw12f";
    e.group = tags.front();
    return e;
}

std::string intervalsText (std::uint16_t mask)
{
    std::string s;
    for (int i = 0; i < 12; ++i)
        if (mask & (1u << i)) { if (! s.empty()) s += ' '; s += std::to_string (i); }
    return s;
}

/** Density knobs for built-in acid templates whose note and octave masks repeat another
    template's: the file sets the amounts, so the two do not load the same thing. */
struct DensityOverride
{
    const char* name;
    std::uint8_t note, slideAmt, accentAmt;
    const char* description;
};

const DensityOverride kOverrides[] = {
    { "Root Fifth Bounce", 75, 35, 40,
      "Acid template: semitones 0 7 above the root, in two octaves, with gaps, slides and accents set for a bouncing line. "
      "The Perfect Fifth template has the same notes at neutral density." },
};

bool sameTemplate (const acid::seq::GenParams& a, const acid::seq::GenParams& b)
{
    return a.noteMask == b.noteMask && a.root == b.root && a.octaveMask == b.octaveMask && a.useSlide == b.useSlide
           && a.useAccent == b.useAccent && a.useVibrato == b.useVibrato && a.amountNote == b.amountNote
           && a.amountSlide == b.amountSlide && a.amountAccent == b.amountAccent && a.amountVibrato == b.amountVibrato;
}
}

void addTemplates (Library& lib)
{
    // Every template file loads something no other one does.
    std::vector<std::pair<std::string, acid::seq::GenParams>> made;
    const auto addUnique = [&] (const acid::seq::GenParams& g, const std::string& name, std::vector<std::string> tags,
                                const std::string& description) {
        for (const auto& [other, h] : made)
            if (sameTemplate (g, h)) fail ("template/" + name + " sets exactly what template/" + other + " sets");
        made.emplace_back (name, g);
        lib.add (templateEntry (g, name, std::move (tags), description));
    };

    for (const acid::seq::ScaleTemplate& t : acid::seq::scaleTemplates())
    {
        acid::seq::GenParams g;
        acid::seq::applyScaleTemplate (t, g);
        std::string description = std::string (categoryTag (t.category)) + " template: semitones " + intervalsText (t.noteMask)
                                  + " above the root. Set the root on the generator.";
        for (const DensityOverride& o : kOverrides)
            if (std::string_view (t.name) == o.name)
            {
                g.amountNote = o.note;
                g.amountSlide = o.slideAmt;
                g.amountAccent = o.accentAmt;
                description = o.description;
            }
        addUnique (g, t.name, { categoryTag (t.category) }, description);
    }
    for (const AcidTemplate& a : kAcidTemplates)
    {
        acid::seq::GenParams g;
        g.noteMask = a.noteMask;
        g.octaveMask = a.octaveMask;
        g.useSlide = a.slide;
        g.useAccent = a.accent;
        g.useVibrato = a.vibrato;
        g.amountNote = a.note;
        g.amountSlide = a.slideAmt;
        g.amountAccent = a.accentAmt;
        g.amountVibrato = a.vibratoAmt;
        addUnique (g, a.name, { "Acid", a.style }, a.description);
    }
}
}
