#pragma once
// The generator template library: note masks with a suggested octave range and attribute
// switches, rooted on C (GenParams::root transposes them). Names are our own.
//
// Message thread or any thread: the table is constant data, every function is noexcept and
// allocation-free.
#include "acid/seq/Pattern.h"
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace acid::seq
{
enum class ScaleCategory : std::uint8_t { Scale, Pentatonic, Chord, Interval, Acid, Other };

struct ScaleTemplate
{
    const char* name;            // unique, case-insensitive
    ScaleCategory category;
    std::uint16_t noteMask;      // bit n = n semitones above the root (bit 0 is the root)
    std::uint8_t octaveMask;     // bit (octave - kMinOctave), as GenParams::octaveMask
    bool useSlide;
    bool useAccent;
    bool useVibrato;
};

/** Builds a note mask from intervals in semitones above the root (0..11). */
constexpr std::uint16_t noteMaskOf (std::initializer_list<int> semis) noexcept
{
    std::uint16_t m = 0;
    for (int s : semis) m = static_cast<std::uint16_t> (m | (1u << (((s % 12) + 12) % 12)));
    return m;
}

/** Builds an octave mask from octaves kMinOctave..kMaxOctave. */
constexpr std::uint8_t octaveMaskOf (std::initializer_list<int> octaves) noexcept
{
    std::uint8_t m = 0;
    for (int o : octaves)
        if (o >= kMinOctave && o <= kMaxOctave) m = static_cast<std::uint8_t> (m | (1u << (o - kMinOctave)));
    return m;
}

/** All templates. Index 0 is "Everything" (all notes, all octaves, all attributes); the
    order is stable, so an index may be stored, but prefer storing the name. */
std::span<const ScaleTemplate> scaleTemplates() noexcept;

/** Index of the template with this name (case-insensitive, surrounding spaces ignored), or -1. */
int findScaleTemplate (std::string_view name) noexcept;

/** Template by index, or nullptr when out of range. */
const ScaleTemplate* scaleTemplate (int index) noexcept;

/** Copies the template's masks and attribute switches into GenParams. Root, seed and the
    density amounts are left alone (the parity target does not store amounts in templates). */
void applyScaleTemplate (const ScaleTemplate&, GenParams&) noexcept;

/** Index of the first template whose note mask, octave mask and switches equal GenParams,
    or -1 (for showing the template name next to a hand-edited mask). */
int matchScaleTemplate (const GenParams&) noexcept;
}
