#include "acid/seq/ExternalNotes.h"

namespace acid::seq
{
void ExternalNotes::reset() noexcept
{
    held.fill (false);
    velocities.fill (0);
    stamps.fill (0);
    pressCounter = 0;
    numHeld = 0;
    sounding = -1;
    pedal = false;
}

int ExternalNotes::pick (NotePriority priority) const noexcept
{
    int best = -1;

    for (int n = 0; n < 128; ++n)
    {
        const auto i = static_cast<std::size_t> (n);

        if (! held[i])
            continue;

        if (best < 0)
        {
            best = n;
            continue;
        }

        switch (priority)
        {
            case NotePriority::Low:  break;                                  // the first one found is the lowest
            case NotePriority::High: best = n; break;
            case NotePriority::Last: if (stamps[i] > stamps[static_cast<std::size_t> (best)]) best = n; break;
        }
    }

    return best;
}

ExternalNotes::Action ExternalNotes::noteOn (int note, int velocity, NotePriority priority) noexcept
{
    if (note < 0 || note > 127)
        return {};

    const auto i = static_cast<std::size_t> (note);

    if (! held[i])
    {
        held[i] = true;
        ++numHeld;
    }

    velocities[i] = static_cast<std::uint8_t> (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
    stamps[i] = ++pressCounter;

    const int winner = pick (priority);

    // A key that loses the priority contest is only remembered. Striking the sounding pitch
    // again (possible under the pedal) is a legato NoteOn that can change the accent.
    if (winner != note && winner == sounding)
        return {};

    Action a;
    a.kind = Action::Kind::NoteOn;
    a.note = static_cast<std::uint8_t> (winner);
    a.velocity = velocities[static_cast<std::size_t> (winner)];
    a.legato = sounding >= 0;
    sounding = winner;
    return a;
}

ExternalNotes::Action ExternalNotes::noteOff (int note, NotePriority priority) noexcept
{
    if (note < 0 || note > 127 || ! held[static_cast<std::size_t> (note)])
        return {};

    held[static_cast<std::size_t> (note)] = false;
    --numHeld;

    if (note != sounding)
        return {};

    Action a;

    if (numHeld > 0)
    {
        // Slide back to a key that is still down.
        const int winner = pick (priority);
        a.kind = Action::Kind::NoteOn;
        a.note = static_cast<std::uint8_t> (winner);
        a.velocity = velocities[static_cast<std::size_t> (winner)];
        a.legato = true;
        sounding = winner;
        return a;
    }

    if (pedal)
        return {};    // the pedal holds the gate

    a.kind = Action::Kind::NoteOff;
    a.note = static_cast<std::uint8_t> (sounding);
    sounding = -1;
    return a;
}

ExternalNotes::Action ExternalNotes::sustain (bool down) noexcept
{
    pedal = down;

    if (down || sounding < 0 || held[static_cast<std::size_t> (sounding)])
        return {};

    Action a;
    a.kind = Action::Kind::NoteOff;
    a.note = static_cast<std::uint8_t> (sounding);
    sounding = -1;
    return a;
}

ExternalNotes::Action ExternalNotes::allNotesOff() noexcept
{
    Action a;

    if (sounding >= 0)
    {
        a.kind = Action::Kind::NoteOff;
        a.note = static_cast<std::uint8_t> (sounding);
    }

    held.fill (false);
    numHeld = 0;
    sounding = -1;
    return a;
}
}
