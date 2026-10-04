// The arpeggiator: indexing of held keys, the missing-index fallback, the cursor, grid
// alignment, and the step attributes it shares with the sequencer.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

namespace
{
/** An arpeggiator pattern of Note steps with the given 1-based indices. */
void setArp (Pattern& p, std::initializer_list<int> indices)
{
    int i = 0;

    for (int index : indices)
    {
        Step s;
        s.state = StepState::Note;
        s.arpIndex = static_cast<std::uint8_t> (index);
        p.steps[static_cast<std::size_t> (i++)] = s;
    }

    p.length = static_cast<std::uint8_t> (i);
}

std::vector<int> played (const Session& s)
{
    std::vector<int> r;
    for (const Ev& e : s.noteOns()) r.push_back (e.note);
    return r;
}

Session arpSession()
{
    Session s;
    s.ctl.playMode = PlayMode::Arpeggiator;
    return s;
}
}

TEST_CASE ("Arpeggiator: held keys are indexed low to high", "[arp]")
{
    Arpeggiator a;
    REQUIRE_FALSE (a.anyHeld());
    REQUIRE (a.noteForIndex (1) == -1);

    REQUIRE (a.noteOn (64));                  // the first key starts the arpeggio
    REQUIRE_FALSE (a.noteOn (48));
    REQUIRE_FALSE (a.noteOn (72));
    REQUIRE_FALSE (a.noteOn (55));
    REQUIRE_FALSE (a.noteOn (55));            // twice: still one key
    REQUIRE (a.heldCount() == 4);

    REQUIRE (a.noteForIndex (1) == 48);
    REQUIRE (a.noteForIndex (2) == 55);
    REQUIRE (a.noteForIndex (3) == 64);
    REQUIRE (a.noteForIndex (4) == 72);

    // An index with no key plays index 1.
    REQUIRE (a.noteForIndex (5) == 48);
    REQUIRE (a.noteForIndex (10) == 48);
    REQUIRE (a.noteForIndex (0) == 48);
    REQUIRE (a.noteForIndex (200) == 48);

    REQUIRE_FALSE (a.noteOff (48));
    REQUIRE (a.noteForIndex (1) == 55);
    REQUIRE_FALSE (a.noteOff (48));           // already up
    REQUIRE_FALSE (a.noteOff (64));
    REQUIRE_FALSE (a.noteOff (72));
    REQUIRE (a.noteOff (55));                 // the last key: the arpeggio stops
    REQUIRE_FALSE (a.anyHeld());

    REQUIRE_FALSE (a.noteOn (-1));
    REQUIRE_FALSE (a.noteOn (128));
    REQUIRE_FALSE (a.anyHeld());
}

TEST_CASE ("Arpeggiator: only the ten lowest keys can be addressed", "[arp]")
{
    Arpeggiator a;
    for (int n = 40; n < 52; ++n) a.noteOn (n);         // twelve keys
    REQUIRE (a.heldCount() == 12);

    for (int i = 1; i <= kMaxArpIndex; ++i)
        REQUIRE (a.noteForIndex (i) == 39 + i);

    REQUIRE (a.noteForIndex (11) == 40);

    // Notes in the upper half of the MIDI range are found too.
    Arpeggiator high;
    high.noteOn (127);
    high.noteOn (63);
    high.noteOn (64);
    REQUIRE (high.noteForIndex (1) == 63);
    REQUIRE (high.noteForIndex (2) == 64);
    REQUIRE (high.noteForIndex (3) == 127);

    Step step;
    step.arpIndex = 2;
    step.octave = -1;
    REQUIRE (high.stepNote (step, 3) == 64 - 12 + 3);
    high.reset();
    REQUIRE (high.stepNote (step, 3) == -1);
}

TEST_CASE ("Arp: a 1-2-3 pattern with two keys plays low, high, low", "[arp]")
{
    Session s = arpSession();
    setArp (s.arp(), { 1, 2, 3 });
    s.keyDown (0, 52);
    s.keyDown (0, 48);
    s.keyUp (6 * kStep, 48);
    s.keyUp (6 * kStep, 52);
    s.run (7 * kStep, 64);

    REQUIRE (played (s) == std::vector<int> { 48, 52, 48, 48, 52, 48 });
    const auto ons = s.noteOns();
    for (std::size_t k = 0; k < ons.size(); ++k) REQUIRE (ons[k].sample == static_cast<std::int64_t> (k) * kStep);
    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (midiBalance (s.midi).empty());
    REQUIRE (s.allocations == 0);
}

TEST_CASE ("Arp: the step's octave offsets the indexed key, and transposition applies", "[arp]")
{
    Session s = arpSession();
    setArp (s.arp(), { 1, 2, 1, 2 });
    s.arp().steps[1].octave = 1;
    s.arp().steps[2].octave = -1;
    s.arp().steps[3].octave = 3;
    s.keyDown (0, 50);
    s.keyDown (0, 57);
    s.run (4 * kStep, 64);
    REQUIRE (played (s) == std::vector<int> { 50, 69, 38, 93 });

    s.arp().transpose = 2;
    s.ctl.liveTranspose = -1;
    s.run (4 * kStep, 64);
    REQUIRE (played (s) == std::vector<int> { 50, 69, 38, 93, 51, 70, 39, 94 });

    s.ctl.liveTranspose = 24;                  // with a very high key the result is clamped at 127
    s.keyDown (s.now, 110);
    s.keyUp (s.now + 1, 57);
    s.run (4 * kStep, 64);
    const auto all = played (s);
    REQUIRE (all[all.size() - 1] == 127);
}

TEST_CASE ("Arp: the cursor runs on while keys change and resets when all are released", "[arp]")
{
    Session s = arpSession();
    setArp (s.arp(), { 1, 1, 1, 1 });
    for (int i = 0; i < 4; ++i) s.arp().steps[static_cast<std::size_t> (i)].octave = static_cast<std::int8_t> (i - 1);   // the step is audible as the octave

    s.keyDown (0, 60);
    s.keyDown (7000, 67);                      // chord changes while a key is always down
    s.keyUp (8000, 60);
    s.keyDown (13000, 62);
    s.keyUp (20000, 62);
    s.keyUp (26000, 67);                       // all up
    s.keyDown (40000, 65);                     // a fresh start
    s.keyUp (47000, 65);
    s.run (60000, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, 48), off (kGate),
        on (kStep, 60), off (kStep + kGate),
        on (2 * kStep, 79), off (2 * kStep + kGate),               // step 2 with the new lowest key: no restart
        on (3 * kStep, 62 + 24), off (3 * kStep + kGate),
        on (4 * kStep, 67 - 12), off (4 * kStep + kGate),          // wrapped to step 0, still running
        on (40000, 65 - 12), off (40000 + kGate),                  // after a full release: step 0 again, at once
        on (46000, 65), off (46000 + kGate) });
    REQUIRE (s.status.heldNotes == 0);
    REQUIRE_FALSE (s.status.running);
}

TEST_CASE ("Arp: accent, slide and vibrato come from the step, not from the keys", "[arp]")
{
    Session s = arpSession();
    setArp (s.arp(), { 1, 2, 1, 2 });
    s.arp().steps[0].slide = true;
    s.arp().steps[1].accent = true;
    s.arp().steps[2].vibrato = true;
    s.keyDown (0, 48, 127);                    // a hard hit is not an accent here
    s.keyDown (0, 60, 1);
    s.keyUp (4 * kStep, 48);
    s.keyUp (4 * kStep, 60);
    s.run (5 * kStep, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, 48), on (kStep, 60, true, true), off (kStep + kGate),
        on (2 * kStep, 48, false, false, true), off (2 * kStep + kGate),
        on (3 * kStep, 60), off (3 * kStep + kGate) });

    // MIDI out mirrors it: pattern velocities, the slide as an overlap, CC1 around the vibrato step.
    REQUIRE (s.midi == std::vector<MidiEv> {
        { 0, 0x90, 48, 32 }, { kStep, 0x90, 60, 96 }, { kStep + 1, 0x80, 48, 0 }, { kStep + kGate, 0x80, 60, 0 },
        { 2 * kStep, 0xB0, 1, 127 }, { 2 * kStep, 0x90, 48, 32 }, { 2 * kStep + kGate, 0x80, 48, 0 },
        { 3 * kStep, 0xB0, 1, 0 }, { 3 * kStep, 0x90, 60, 32 }, { 3 * kStep + kGate, 0x80, 60, 0 } });
}

TEST_CASE ("Arp: the first key lands on the host grid when the host is playing", "[arp]")
{
    SECTION ("a key ahead of the beat waits for it")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1, 1 });
        s.arp().steps[1].octave = 1;
        s.host.play();
        s.keyDown (2 * kStep + 4000, 60);
        s.keyUp (5 * kStep + 100, 60);
        s.run (6 * kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> {
            on (3 * kStep, 60), off (3 * kStep + kGate), on (4 * kStep, 72), off (4 * kStep + kGate), on (5 * kStep, 60), off (5 * kStep + kGate) });
    }

    SECTION ("a key just behind the beat plays at once, and the next step is on the grid")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1, 1 });
        s.arp().steps[1].octave = 1;
        s.host.play();
        s.keyDown (2 * kStep + 1000, 60);          // 21 ms late: inside the catch window
        s.keyUp (4 * kStep + 100, 60);
        s.run (6 * kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> {
            on (2 * kStep + 1000, 60), off (2 * kStep + kGate), on (3 * kStep, 72), off (3 * kStep + kGate), on (4 * kStep, 60), off (4 * kStep + kGate) });
    }

    SECTION ("exactly on the beat")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1 });
        s.host.play();
        s.keyDown (7 * kStep, 60);
        s.keyUp (7 * kStep + 500, 60);
        s.run (9 * kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (7 * kStep, 60), off (7 * kStep + kGate) });
    }

    SECTION ("the pattern's shuffle follows the host grid")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1, 1, 1 });
        s.arp().shuffle = 100;
        s.host.play();
        s.keyDown (kStep - 10, 60);                // lands on grid step 1, which is an odd one: delayed
        s.keyUp (4 * kStep, 60);
        s.run (5 * kStep, 64);
        const auto ons = s.noteOns();
        REQUIRE (ons.size() == 3);
        REQUIRE (ons[0].sample == kStep + 3000);
        REQUIRE (ons[1].sample == 2 * kStep);
        REQUIRE (ons[2].sample == 3 * kStep + 3000);
    }

    SECTION ("host stopped: the first key plays at once")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1 });
        s.keyDown (1234, 60);
        s.keyUp (1234 + 2 * kStep, 60);
        s.run (4 * kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (1234, 60), off (1234 + kGate), on (1234 + kStep, 60), off (1234 + kStep + kGate) });
    }
}

TEST_CASE ("Arp: stopping", "[arp]")
{
    SECTION ("a gate held open with no end closes when the last key goes up")
    {
        Session s = arpSession();
        s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
        setSteps (s.arp(), "C:S . . .");           // one slid note, sustained over the rests
        s.keyDown (0, 60);
        s.keyUp (2 * kStep + 500, 60);
        s.run (8 * kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, 60), off (2 * kStep + 500) });
    }

    SECTION ("all-notes-off stops at once, mid-gate")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1, 2 });
        s.keyDown (0, 60);
        s.keyDown (0, 64);
        s.midiAt (kStep + 1000, 0xB0, 123, 0);
        s.keyUp (3 * kStep, 60);                   // stale releases do nothing
        s.keyUp (3 * kStep, 64);
        s.run (5 * kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, 60), off (kGate), on (kStep, 64), off (kStep + 1000) });
        REQUIRE_FALSE (s.status.running);
    }

    SECTION ("leaving the mode stops at once")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1 });
        s.arp().steps[0].slide = true;
        s.keyDown (0, 60);
        s.run (kStep + 1000, 64);
        s.ctl.playMode = PlayMode::Sequencer;
        s.run (kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, 60), on (kStep, 60, false, true), off (kStep + 1000) });
        REQUIRE (midiBalance (s.midi).empty());
    }

    SECTION ("a host relocation cuts the note and the arpeggio carries on from the new position")
    {
        Session s = arpSession();
        setArp (s.arp(), { 1, 1, 1, 1 });
        for (int i = 0; i < 4; ++i) s.arp().steps[static_cast<std::size_t> (i)].octave = static_cast<std::int8_t> (i);
        s.host.play();
        s.keyDown (0, 40);
        s.run (kStep + 1000, 64);
        s.host.locate (0.75);                      // to grid step 3
        s.run (2 * kStep, 64);
        s.keyUp (s.now, 40);
        s.run (kStep, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, 40), off (kGate), on (kStep, 52), off (kStep + 1000),
            on (kStep + 1000, 76), off (kStep + 1000 + kGate), on (2 * kStep + 1000, 40), off (2 * kStep + 1000 + kGate) });
        REQUIRE (midiBalance (s.midi).empty());
    }
}

TEST_CASE ("Arp: pattern selection, queueing and overrides work as in the sequencer", "[arp]")
{
    Session s = arpSession();
    setArp (s.arp (0), { 1, 1 });
    setArp (s.arp (1), { 2, 2 });
    setSteps (s.seq (0), "C C");                   // the sequencer bank is not the arpeggiator's
    s.keyDown (0, 50);
    s.keyDown (0, 60);
    s.run (1000, 64);
    REQUIRE (s.status.running);
    REQUIRE (s.status.playingSlot == 0);
    REQUIRE (s.status.heldNotes == 2);
    s.ctl.slot = 1;
    s.run (1000, 64);
    REQUIRE (s.status.queuedSlot == 1);
    s.run (4 * kStep - s.now, 64);
    REQUIRE (s.status.playingSlot == 1);
    REQUIRE (played (s) == std::vector<int> { 50, 50, 60, 60 });

    s.ctl.ovrNoteClr = true;
    s.run (2 * kStep, 64);
    REQUIRE (played (s).size() == 4);
    s.ctl.ovrNoteClr = false;
    s.ctl.ovrAccentSet = true;
    s.run (2 * kStep, 64);
    REQUIRE (s.noteOns().back().accent);
    REQUIRE (s.noteOns().size() == 6);
}
