#pragma once
// The monophonic note handler of External play mode: turns key presses, releases and the
// sustain pedal into "start this note", "slide to this note" and "close the gate".
//
//   - A key pressed while the gate is open is a legato NoteOn (a slide).
//   - Releasing the sounding key while other keys are down slides back to the one the
//     priority rule picks.
//   - The sustain pedal holds the gate after the last key is released; a key pressed while
//     the pedal holds a note is legato, the same pitch included.
#include "acid/seq/PlayerTypes.h"
#include <array>
#include <cstdint>

namespace acid::seq
{
class ExternalNotes
{
public:
    /** What the voice must do in response to one input event. */
    struct Action
    {
        enum class Kind : std::uint8_t { None, NoteOn, NoteOff };

        Kind kind = Kind::None;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;    // NoteOn: the velocity that key was struck with
        bool legato = false;          // NoteOn: the gate was already open
    };

    /** Everything released, pedal up. Emits nothing: the caller closes the gate. */
    void reset() noexcept;

    Action noteOn (int note, int velocity, NotePriority priority) noexcept;
    Action noteOff (int note, NotePriority priority) noexcept;
    Action sustain (bool down) noexcept;

    /** Releases every key and closes the gate, pedal or not. */
    Action allNotesOff() noexcept;

    bool gateOpen() const noexcept { return sounding >= 0; }
    int soundingNote() const noexcept { return sounding; }
    int heldCount() const noexcept { return numHeld; }
    bool pedalDown() const noexcept { return pedal; }

private:
    int pick (NotePriority priority) const noexcept;

    std::array<bool, 128> held {};
    std::array<std::uint8_t, 128> velocities {};
    std::array<std::uint32_t, 128> stamps {};     // press order, for Last priority
    std::uint32_t pressCounter = 0;
    int numHeld = 0;
    int sounding = -1;
    bool pedal = false;
};
}
