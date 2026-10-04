#include "acid/seq/Scales.h"
#include <array>

namespace acid::seq
{
namespace
{
using C = ScaleCategory;

constexpr std::uint8_t kOct0 = octaveMaskOf ({ 0 });
constexpr std::uint8_t kOct01 = octaveMaskOf ({ 0, 1 });
constexpr std::uint8_t kOctM101 = octaveMaskOf ({ -1, 0, 1 });
constexpr std::uint8_t kOctM10 = octaveMaskOf ({ -1, 0 });
constexpr std::uint8_t kOctWide = octaveMaskOf ({ -1, 0, 1, 2 });
constexpr std::uint8_t kOctAll = octaveMaskOf ({ -1, 0, 1, 2, 3 });

constexpr ScaleTemplate T (const char* name, C cat, std::uint16_t mask, std::uint8_t oct,
                           bool slide = true, bool accent = true, bool vibrato = false) noexcept
{
    return ScaleTemplate { name, cat, mask, oct, slide, accent, vibrato };
}

constexpr ScaleTemplate kTemplates[] {
    T ("Everything",            C::Other,      0x0FFF,                                   kOctAll, true, true, true),
    T ("Chromatic",             C::Scale,      0x0FFF,                                   kOct01),

    // Scales and modes
    T ("Major",                 C::Scale,      noteMaskOf ({ 0, 2, 4, 5, 7, 9, 11 }),    kOct01),
    T ("Natural Minor",         C::Scale,      noteMaskOf ({ 0, 2, 3, 5, 7, 8, 10 }),    kOct01),
    T ("Dorian",                C::Scale,      noteMaskOf ({ 0, 2, 3, 5, 7, 9, 10 }),    kOct01),
    T ("Phrygian",              C::Scale,      noteMaskOf ({ 0, 1, 3, 5, 7, 8, 10 }),    kOct01),
    T ("Lydian",                C::Scale,      noteMaskOf ({ 0, 2, 4, 6, 7, 9, 11 }),    kOct01),
    T ("Mixolydian",            C::Scale,      noteMaskOf ({ 0, 2, 4, 5, 7, 9, 10 }),    kOct01),
    T ("Locrian",               C::Scale,      noteMaskOf ({ 0, 1, 3, 5, 6, 8, 10 }),    kOct01),
    T ("Harmonic Minor",        C::Scale,      noteMaskOf ({ 0, 2, 3, 5, 7, 8, 11 }),    kOct01),
    T ("Melodic Minor",         C::Scale,      noteMaskOf ({ 0, 2, 3, 5, 7, 9, 11 }),    kOct01),
    T ("Phrygian Dominant",     C::Scale,      noteMaskOf ({ 0, 1, 4, 5, 7, 8, 10 }),    kOct01),
    T ("Hungarian Minor",       C::Scale,      noteMaskOf ({ 0, 2, 3, 6, 7, 8, 11 }),    kOct01),
    T ("Double Harmonic",       C::Scale,      noteMaskOf ({ 0, 1, 4, 5, 7, 8, 11 }),    kOct01),
    T ("Whole Tone",            C::Scale,      noteMaskOf ({ 0, 2, 4, 6, 8, 10 }),       kOct01),
    T ("Octatonic Half Whole",  C::Scale,      noteMaskOf ({ 0, 1, 3, 4, 6, 7, 9, 10 }), kOct01),
    T ("Octatonic Whole Half",  C::Scale,      noteMaskOf ({ 0, 2, 3, 5, 6, 8, 9, 11 }), kOct01),
    T ("Blues Six",             C::Scale,      noteMaskOf ({ 0, 3, 5, 6, 7, 10 }),       kOct01),
    T ("Blues Seven",           C::Scale,      noteMaskOf ({ 0, 2, 3, 5, 6, 7, 10 }),    kOct01),

    // Pentatonics
    T ("Major Pentatonic",      C::Pentatonic, noteMaskOf ({ 0, 2, 4, 7, 9 }),           kOct01),
    T ("Minor Pentatonic",      C::Pentatonic, noteMaskOf ({ 0, 3, 5, 7, 10 }),          kOct01),
    T ("Dorian Pentatonic",     C::Pentatonic, noteMaskOf ({ 0, 2, 3, 7, 9 }),           kOct01),
    T ("Suspended Pentatonic",  C::Pentatonic, noteMaskOf ({ 0, 2, 5, 7, 10 }),          kOct01),
    T ("Minor Six Pentatonic",  C::Pentatonic, noteMaskOf ({ 0, 3, 5, 7, 9 }),           kOct01),
    T ("Dark Pentatonic",       C::Pentatonic, noteMaskOf ({ 0, 1, 5, 7, 8 }),           kOct01),
    T ("Hollow Pentatonic",     C::Pentatonic, noteMaskOf ({ 0, 2, 3, 7, 8 }),           kOct01),

    // Chords
    T ("Major Triad",           C::Chord,      noteMaskOf ({ 0, 4, 7 }),                 kOct01),
    T ("Minor Triad",           C::Chord,      noteMaskOf ({ 0, 3, 7 }),                 kOct01),
    T ("Major Sixth",           C::Chord,      noteMaskOf ({ 0, 4, 7, 9 }),              kOct01),
    T ("Minor Sixth",           C::Chord,      noteMaskOf ({ 0, 3, 7, 9 }),              kOct01),
    T ("Dominant Seventh",      C::Chord,      noteMaskOf ({ 0, 4, 7, 10 }),             kOct01),
    T ("Major Seventh",         C::Chord,      noteMaskOf ({ 0, 4, 7, 11 }),             kOct01),
    T ("Minor Seventh",         C::Chord,      noteMaskOf ({ 0, 3, 7, 10 }),             kOct01),
    T ("Minor Major Seventh",   C::Chord,      noteMaskOf ({ 0, 3, 7, 11 }),             kOct01),
    T ("Minor Flat Six",        C::Chord,      noteMaskOf ({ 0, 3, 7, 8 }),              kOct01),
    T ("Major Flat Six",        C::Chord,      noteMaskOf ({ 0, 4, 7, 8 }),              kOct01),
    T ("Flat Five Seventh",     C::Chord,      noteMaskOf ({ 0, 4, 6, 10 }),             kOct01),
    T ("Half Diminished",       C::Chord,      noteMaskOf ({ 0, 3, 6, 10 }),             kOct01),
    T ("Diminished",            C::Chord,      noteMaskOf ({ 0, 3, 6, 9 }),              kOct01),
    T ("Augmented",             C::Chord,      noteMaskOf ({ 0, 4, 8 }),                 kOct01),
    T ("Suspended Second",      C::Chord,      noteMaskOf ({ 0, 2, 7 }),                 kOct01),
    T ("Suspended Fourth",      C::Chord,      noteMaskOf ({ 0, 5, 7 }),                 kOct01),
    T ("Minor Add Nine",        C::Chord,      noteMaskOf ({ 0, 2, 3, 7 }),              kOct01),
    T ("Minor Eleventh",        C::Chord,      noteMaskOf ({ 0, 3, 5, 7, 10 }),          kOctM101),

    // Intervals
    T ("Root Only",             C::Interval,   noteMaskOf ({ 0 }),                       kOct0),
    T ("Minor Second",          C::Interval,   noteMaskOf ({ 0, 1 }),                    kOct01),
    T ("Major Second",          C::Interval,   noteMaskOf ({ 0, 2 }),                    kOct01),
    T ("Minor Third",           C::Interval,   noteMaskOf ({ 0, 3 }),                    kOct01),
    T ("Major Third",           C::Interval,   noteMaskOf ({ 0, 4 }),                    kOct01),
    T ("Perfect Fourth",        C::Interval,   noteMaskOf ({ 0, 5 }),                    kOct01),
    T ("Tritone",               C::Interval,   noteMaskOf ({ 0, 6 }),                    kOct01),
    T ("Perfect Fifth",         C::Interval,   noteMaskOf ({ 0, 7 }),                    kOct01),
    T ("Minor Sixth Interval",  C::Interval,   noteMaskOf ({ 0, 8 }),                    kOct01),
    T ("Minor Seventh Interval", C::Interval,  noteMaskOf ({ 0, 10 }),                   kOct01),

    // Acid favourites
    T ("Octave Pump",           C::Acid,       noteMaskOf ({ 0 }),                       kOct01),
    T ("Three Octave Root",     C::Acid,       noteMaskOf ({ 0 }),                       kOctM101),
    T ("Root Fifth Bounce",     C::Acid,       noteMaskOf ({ 0, 7 }),                    kOct01),
    T ("Warehouse Minor",       C::Acid,       noteMaskOf ({ 0, 3, 7, 10 }),             kOctM101),
    T ("Squelch Phrygian",      C::Acid,       noteMaskOf ({ 0, 1, 3, 7 }),              kOct01),
    T ("Chromatic Creep",       C::Acid,       noteMaskOf ({ 0, 1, 2, 3 }),              kOct0),
    T ("Dark Second",           C::Acid,       noteMaskOf ({ 0, 1, 7 }),                 kOct01),
    T ("Blue Note Line",        C::Acid,       noteMaskOf ({ 0, 3, 6, 7, 10 }),          kOct01),
    T ("Tension Tritone",       C::Acid,       noteMaskOf ({ 0, 6, 7 }),                 kOct01),
    T ("Eastern Line",          C::Acid,       noteMaskOf ({ 0, 1, 4, 5, 7, 8 }),        kOct01),
    T ("Wide Leap",             C::Acid,       noteMaskOf ({ 0, 7 }),                    kOctWide),
    T ("Low Rumble",            C::Acid,       noteMaskOf ({ 0, 3, 5, 7 }),              kOctM10),
    T ("Accent Hammer",         C::Acid,       noteMaskOf ({ 0 }),                       kOct0, false, true, false),
    T ("Glide Line",            C::Acid,       noteMaskOf ({ 0, 3, 7, 10 }),             kOct01, true, false, false),
    T ("Vibrato Drone",         C::Acid,       noteMaskOf ({ 0, 7 }),                    kOct0, true, true, true),
    T ("Minor Third Wobble",    C::Acid,       noteMaskOf ({ 0, 3 }),                    kOctM101),
    T ("Sour Fourth",           C::Acid,       noteMaskOf ({ 0, 5, 6 }),                 kOct01),
    T ("Dissonant Cluster",     C::Acid,       noteMaskOf ({ 0, 1, 6, 11 }),             kOct01),
    T ("Dissonant Spread",      C::Acid,       noteMaskOf ({ 0, 1, 4, 6, 10 }),          kOctM101),
    T ("Melodic Minor Run",     C::Acid,       noteMaskOf ({ 0, 2, 3, 5, 7 }),           kOct01),
    T ("Melodic Major Run",     C::Acid,       noteMaskOf ({ 0, 2, 4, 7, 9 }),           kOctM101),
    T ("Plain Notes",           C::Acid,       noteMaskOf ({ 0, 3, 5, 7, 10 }),          kOct01, false, false, false),
};

char lower (char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char> (c - 'A' + 'a') : c; }

std::string_view trim (std::string_view s) noexcept
{
    while (! s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix (1);
    while (! s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix (1);
    return s;
}

bool equalNoCase (std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower (a[i]) != lower (b[i])) return false;
    return true;
}
}

std::span<const ScaleTemplate> scaleTemplates() noexcept { return kTemplates; }

int findScaleTemplate (std::string_view name) noexcept
{
    const std::string_view key = trim (name);
    for (std::size_t i = 0; i < std::size (kTemplates); ++i)
        if (equalNoCase (key, kTemplates[i].name)) return static_cast<int> (i);
    return -1;
}

const ScaleTemplate* scaleTemplate (int index) noexcept
{
    if (index < 0 || index >= static_cast<int> (std::size (kTemplates))) return nullptr;
    return &kTemplates[index];
}

void applyScaleTemplate (const ScaleTemplate& t, GenParams& g) noexcept
{
    g.noteMask = static_cast<std::uint16_t> (t.noteMask & 0x0FFF);
    g.octaveMask = static_cast<std::uint8_t> (t.octaveMask & 0x1F);
    g.useSlide = t.useSlide;
    g.useAccent = t.useAccent;
    g.useVibrato = t.useVibrato;
}

int matchScaleTemplate (const GenParams& g) noexcept
{
    for (std::size_t i = 0; i < std::size (kTemplates); ++i)
    {
        const ScaleTemplate& t = kTemplates[i];
        if (t.noteMask == (g.noteMask & 0x0FFF) && t.octaveMask == (g.octaveMask & 0x1F) && t.useSlide == g.useSlide
            && t.useAccent == g.useAccent && t.useVibrato == g.useVibrato)
            return static_cast<int> (i);
    }
    return -1;
}
}
