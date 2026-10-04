#pragma once
// The arpeggiator's view of the keyboard: the set of held keys, sorted low to high, and
// the rule that maps an arpeggiator step onto one of them. The stepping itself (clock,
// gates, slides, ratchets, queued pattern changes) is the Player's step engine, shared
// with the sequencer; this class only answers "which note does this step play now".
#include "acid/seq/Pattern.h"
#include <cstdint>

namespace acid::seq
{
class Arpeggiator
{
public:
    /** Forgets every held key. */
    void reset() noexcept { keys[0] = keys[1] = 0; }

    /** Returns true when this is the first key down: the arpeggio starts, cursor at step 1. */
    bool noteOn (int note) noexcept;

    /** Returns true when the last key went up: the arpeggio stops and its cursor resets. */
    bool noteOff (int note) noexcept;

    bool anyHeld() const noexcept { return (keys[0] | keys[1]) != 0; }
    int heldCount() const noexcept;

    /** The held key with 1-based index arpIndex, counting from the lowest. Only the lowest
        kMaxArpIndex keys can be addressed. An index beyond the number of held keys plays
        index 1. Returns -1 when no key is held. */
    int noteForIndex (int arpIndex) const noexcept;

    /** MIDI note an arpeggiator step plays: the indexed key, moved by the step's octave and
        by transpose semitones, then clamped to 0..127 as a sequencer step is. A negative
        result (-1) means one thing only: no key is held. */
    int stepNote (const Step& step, int transpose) const noexcept;

private:
    std::uint64_t keys[2] = { 0, 0 };   // bit n of keys[n / 64]: MIDI note n is down
};
}
