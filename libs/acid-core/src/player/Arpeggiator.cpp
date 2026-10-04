#include "acid/seq/Arpeggiator.h"
#include <bit>

namespace acid::seq
{
bool Arpeggiator::noteOn (int note) noexcept
{
    if (note < 0 || note > 127)
        return false;

    const bool wasEmpty = ! anyHeld();
    keys[note / 64] |= std::uint64_t { 1 } << (note % 64);
    return wasEmpty;
}

bool Arpeggiator::noteOff (int note) noexcept
{
    if (note < 0 || note > 127)
        return false;

    const std::uint64_t bit = std::uint64_t { 1 } << (note % 64);

    if ((keys[note / 64] & bit) == 0)
        return false;

    keys[note / 64] &= ~bit;
    return ! anyHeld();
}

int Arpeggiator::heldCount() const noexcept
{
    return std::popcount (keys[0]) + std::popcount (keys[1]);
}

int Arpeggiator::noteForIndex (int arpIndex) const noexcept
{
    if (! anyHeld())
        return -1;

    const int count = heldCount() < kMaxArpIndex ? heldCount() : kMaxArpIndex;
    int wanted = (arpIndex < 1 || arpIndex > count) ? 1 : arpIndex;

    for (int word = 0; word < 2; ++word)
    {
        std::uint64_t bits = keys[word];

        while (bits != 0)
        {
            const int bit = std::countr_zero (bits);

            if (--wanted == 0)
                return word * 64 + bit;

            bits &= bits - 1;
        }
    }

    return -1;
}

int Arpeggiator::stepNote (const Step& step, int transpose) const noexcept
{
    const int base = noteForIndex (step.arpIndex);

    if (base < 0)
        return -1;

    const int note = base + 12 * step.octave + transpose;
    return note < 0 ? 0 : (note > 127 ? 127 : note);
}
}
