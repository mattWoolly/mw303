// The 28 playback edge cases of the sequencer research (document 06, section 9.5), one
// test case each, numbered as they are there. All run at 120 BPM and 48 kHz unless they say
// otherwise: a 1/16 step is 6000 samples (kStep) and the authentic gate closes 3065 samples
// after the onset (kGate).
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

namespace
{
/** Plays the given number of steps from the song origin, then stops the host. */
void playSteps (Session& s, int steps, int blockSize = 64)
{
    s.host.play();
    s.run (steps * kStep, blockSize);
    s.host.stop();
    s.run (64, blockSize);
}

void requireClean (const Session& s)
{
    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (midiBalance (s.midi).empty());
    REQUIRE (s.orderViolations == 0);
    REQUIRE (s.allocations == 0);
    REQUIRE (s.overflows == 0);
}
}

TEST_CASE ("Edge 01: slide into a note holds the gate and sends a legato NoteOn", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C:S D . .");
    playSteps (s, 4);

    REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (kStep, kC + 2, false, true), off (kStep + kGate) });
    requireClean (s);
}

TEST_CASE ("Edge 02: slide, ties, then a note: held through the ties, legato into the note", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C:S ~ ~ D . .");
    playSteps (s, 6);

    REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (3 * kStep, kC + 2, false, true), off (3 * kStep + kGate) });
    requireClean (s);
}

TEST_CASE ("Edge 03: slide into a rest, under each policy", "[edge]")
{
    SECTION ("Release: the slide is ignored and the gate has its normal length")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::Release;
        setSteps (s.seq(), "C:S . D .");
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kGate), on (2 * kStep, kC + 2), off (2 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("HoldThroughRest: the old pitch sounds through the rest's gate time")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest;
        setSteps (s.seq(), "C:S . D .");
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kStep + kGate), on (2 * kStep, kC + 2), off (2 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("SustainToNextNote: the gate is held across the rests and the next note is legato")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
        setSteps (s.seq(), "C:S . . D . .");
        playSteps (s, 6);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (3 * kStep, kC + 2, false, true), off (3 * kStep + kGate) });
        requireClean (s);
    }
}

TEST_CASE ("Edge 04: slide, tie, rest: held through the tie, then the rest decides", "[edge]")
{
    SECTION ("HoldThroughRest: off at the rest's gate time")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest;
        setSteps (s.seq(), "C:S ~ . D");
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (2 * kStep + kGate), on (3 * kStep, kC + 2), off (3 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("Release: off at the tie's gate time")
    {
        Session s;
        setSteps (s.seq(), "C:S ~ . D");
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kStep + kGate), on (3 * kStep, kC + 2), off (3 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("SustainToNextNote: legato into the note after the rest")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
        setSteps (s.seq(), "C:S ~ . D");
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (3 * kStep, kC + 2, false, true), off (3 * kStep + kGate) });
        requireClean (s);
    }
}

TEST_CASE ("Edge 05: a slide on the last step glides into the first note of the next pass", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C D:S");
    playSteps (s, 5);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, kC), off (kGate),
        on (kStep, kC + 2), on (2 * kStep, kC, false, true), off (2 * kStep + kGate),
        on (3 * kStep, kC + 2), on (4 * kStep, kC, false, true), off (4 * kStep + kGate) });
    requireClean (s);
}

TEST_CASE ("Edge 06: slide to the same pitch is a legato NoteOn that can change the accent", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C:S C:A . .");
    playSteps (s, 4);

    REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (kStep, kC, true, true), off (kStep + kGate) });
    requireClean (s);
}

TEST_CASE ("Edge 07: a tie after a rest, or at the start with nothing sounding, is silent", "[edge]")
{
    Session s;
    setSteps (s.seq(), "~ . ~ C .");
    playSteps (s, 5);

    REQUIRE (s.notes() == std::vector<Ev> { on (3 * kStep, kC), off (3 * kStep + kGate) });
    requireClean (s);

    // Across the loop point a tie does hold a note that is sounding: the wrap is a step
    // boundary like any other.
    Session wrap;
    setSteps (wrap.seq(), "~ . . C");
    playSteps (wrap, 6);
    REQUIRE (wrap.notes() == std::vector<Ev> { on (3 * kStep, kC), off (4 * kStep + kGate) });
}

TEST_CASE ("Edge 08: a tie chain followed by a plain note: NoteOff, then NoteOn", "[edge]")
{
    SECTION ("the chain's gate ends in the last tied step and the note is struck afresh")
    {
        Session s;
        setSteps (s.seq(), "C ~ ~ D");
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (2 * kStep + kGate), on (3 * kStep, kC + 2), off (3 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("with a full-length gate the NoteOff lands one sample before the NoteOn")
    {
        Session s;
        setSteps (s.seq(), "C ~ ~ D");
        s.seq().gateLength = 100;
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (3 * kStep - 1), on (3 * kStep, kC + 2), off (4 * kStep - 1) });
        requireClean (s);
    }

    SECTION ("when both fall on one sample, the NoteOff comes first")
    {
        // The gate is held for a tie; the tie is then edited into a note before it arrives.
        Session s;
        setSteps (s.seq(), "C ~ . .");
        s.host.play();
        s.run (kGate + 100, 64);                      // the hold has been decided
        s.seq().steps[1].state = StepState::Note;
        s.seq().steps[1].note = 4;
        s.run (4 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kStep), on (kStep, kC + 4), off (kStep + kGate) });
        requireClean (s);
    }
}

TEST_CASE ("Edge 09: a one-step pattern", "[edge]")
{
    SECTION ("with slide: a continuous drone, legato into itself")
    {
        Session s;
        setSteps (s.seq(), "C:S");
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), on (kStep, kC, false, true), on (2 * kStep, kC, false, true), on (3 * kStep, kC, false, true),
            off (4 * kStep) });      // released by the stop
        requireClean (s);
    }

    SECTION ("without slide: retriggered every step")
    {
        Session s;
        setSteps (s.seq(), "C");
        playSteps (s, 3);
        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), off (kGate), on (kStep, kC), off (kStep + kGate), on (2 * kStep, kC), off (2 * kStep + kGate) });
        requireClean (s);
    }
}

TEST_CASE ("Edge 10: a failed probability roll on the slide target is seen by the hold decision", "[edge]")
{
    const std::uint32_t seed = 4242;

    auto render = [seed] (int blockSize)
    {
        Session s;
        setSteps (s.seq(), "C:S D");
        s.seq().steps[1].probability = 50;
        s.seq().gen.seed = seed;
        playSteps (s, 64, blockSize);
        requireClean (s);
        return s.notes();
    };

    // What must happen, from the roll alone: a pass makes step 1 a legato target, a fail
    // makes it a rest, so the slid note is released at its normal gate time.
    std::vector<Ev> expected;
    int passes = 0;

    for (std::int64_t pair = 0; pair < 32; ++pair)
    {
        const std::int64_t t = pair * 2 * kStep;
        expected.push_back (on (t, kC));

        if (stepRollPasses (seed, pair * 2 + 1, 50))
        {
            expected.push_back (on (t + kStep, kC + 2, false, true));
            expected.push_back (off (t + kStep + kGate));
            ++passes;
        }
        else
        {
            expected.push_back (off (t + kGate));
        }
    }

    REQUIRE (passes > 6);
    REQUIRE (passes < 26);

    const auto realtime = render (64);
    REQUIRE (realtime == expected);
    REQUIRE (render (4096) == expected);     // an offline render with big blocks is identical
    REQUIRE (render (1) == expected);
}

TEST_CASE ("Edge 11: a ratcheted step that is slid into: first hit legato, later hits retrigger", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C:S D . .");
    s.seq().steps[1].ratchet = 3;
    playSteps (s, 4);

    // Three hits 2000 samples apart, each with a third of the gate (1021.6 samples).
    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, kC),
        on (kStep, kC + 2, false, true), off (kStep + 1022),
        on (kStep + 2000, kC + 2), off (kStep + 3022),
        on (kStep + 4000, kC + 2), off (kStep + 5022) });
    requireClean (s);
}

TEST_CASE ("Edge 12: a queued pattern takes over at the wrap and a pending slide carries into it", "[edge]")
{
    Session s;
    setSteps (s.seq (0), "C D E F:S");
    setSteps (s.seq (1), "G A");

    s.host.play();
    s.run (1000, 64);
    REQUIRE (s.status.playingSlot == 0);
    REQUIRE (s.status.queuedSlot == -1);

    s.ctl.slot = 1;
    s.run (1000, 64);
    REQUIRE (s.status.playingSlot == 0);          // still the old pattern...
    REQUIRE (s.status.queuedSlot == 1);           // ...with the new one waiting
    REQUIRE (s.status.currentStep == 0);

    s.run (4 * kStep + 100 - s.now, 64);
    REQUIRE (s.status.playingSlot == 1);
    REQUIRE (s.status.queuedSlot == -1);
    REQUIRE (s.status.currentStep == 0);

    s.run (7 * kStep - s.now, 64);
    s.host.stop();
    s.run (64, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, kC), off (kGate), on (kStep, kC + 2), off (kStep + kGate), on (2 * kStep, kC + 4), off (2 * kStep + kGate),
        on (3 * kStep, kC + 5),
        on (4 * kStep, kC + 7, false, true), off (4 * kStep + kGate),          // the slide lands in the new pattern
        on (5 * kStep, kC + 9), off (5 * kStep + kGate), on (6 * kStep, kC + 7), off (6 * kStep + kGate) });
    requireClean (s);
}

TEST_CASE ("Edge 13: the length is shortened while playing", "[edge]")
{
    SECTION ("running free: a position past the new end wraps to the first step")
    {
        Session s;
        s.cfg.hostLink = false;
        setSteps (s.seq (0), "C C# D D# E F F# G");
        setSteps (s.seq (1), "A");
        s.host.play();
        s.run (5 * kStep + 1000, 64);              // step 5 is sounding
        s.seq().length = 4;
        s.ctl.slot = 1;                            // and a pattern is queued
        s.run (64, 64);
        REQUIRE (s.status.queuedSlot == 1);
        s.ctl.slot = 0;                            // changed our mind: the queue empties
        s.run (10 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        std::vector<int> played;
        for (const Ev& e : s.noteOns()) played.push_back (e.note - kC);
        REQUIRE (played == std::vector<int> { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3 });
        requireClean (s);
    }

    SECTION ("locked to the host: the step is the song position modulo the new length")
    {
        Session s;
        setSteps (s.seq(), "C C# D D# E F F# G");
        s.host.play();
        s.run (5 * kStep + 1000, 64);
        s.seq().length = 4;
        s.run (10 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        std::vector<int> played;
        for (const Ev& e : s.noteOns()) played.push_back (e.note - kC);
        REQUIRE (played == std::vector<int> { 0, 1, 2, 3, 4, 5, 2, 3, 0, 1 });
        requireClean (s);
    }

    SECTION ("a queued pattern still takes over at the end of the shortened pass")
    {
        Session s;
        s.cfg.hostLink = false;
        setSteps (s.seq (0), "C C# D D# E F F# G");
        setSteps (s.seq (1), "A A");
        s.host.play();
        s.run (5 * kStep + 1000, 64);
        s.seq (0).length = 4;
        s.ctl.slot = 1;
        s.run (8 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        std::vector<int> played;
        for (const Ev& e : s.noteOns()) played.push_back (e.note - kC);
        REQUIRE (played == std::vector<int> { 0, 1, 2, 3, 4, 5, 9, 9 });
        requireClean (s);
    }
}

TEST_CASE ("Edge 14: edits while a step sounds", "[edge]")
{
    SECTION ("an edit to the sounding step is heard on the next pass, never mid-note")
    {
        Session s;
        setSteps (s.seq(), "C . . .");
        s.host.play();
        s.run (1000, 64);
        s.seq().steps[0].note = 7;
        s.seq().steps[0].accent = true;
        s.run (5 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kGate), on (4 * kStep, kC + 7, true), off (4 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("an edit to the next step before the gate-off instant changes the hold decision")
    {
        Session s;
        setSteps (s.seq(), "C:S D . .");
        s.host.play();
        s.run (1000, 64);
        s.seq().steps[1].state = StepState::Rest;
        s.run (4 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kGate) });
        requireClean (s);
    }

    SECTION ("and the other way: a rest edited into a note is slid into")
    {
        Session s;
        setSteps (s.seq(), "C:S . . .");
        s.host.play();
        s.run (1000, 64);
        s.seq().steps[1].state = StepState::Note;
        s.seq().steps[1].note = 3;
        s.run (4 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (kStep, kC + 3, false, true), off (kStep + kGate) });
        requireClean (s);
    }
}

TEST_CASE ("Edge 15: live transpose applies to the next NoteOn and clamps to the MIDI range", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C:S D:S E:S F");
    s.host.play();
    s.run (1000, 64);
    s.ctl.liveTranspose = 5;                       // mid-note: nothing is retuned
    s.run (kStep + 1000 - s.now, 64);
    s.ctl.liveTranspose = -7;                      // mid-glide: the glide's target is not changed
    s.run (4 * kStep - s.now, 64);
    s.host.stop();
    s.run (64, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, kC), on (kStep, kC + 2 + 5, false, true), on (2 * kStep, kC + 4 - 7, false, true),
        on (3 * kStep, kC + 5 - 7, false, true), off (3 * kStep + kGate) });
    requireClean (s);

    // Pattern transpose and live transpose add, and the result is clamped, not wrapped.
    Session high;
    setSteps (high.seq(), "B+++");                 // 36 + 36 + 11 = 83
    high.seq().transpose = 24;
    high.ctl.liveTranspose = 24;                   // 131
    playSteps (high, 1);
    REQUIRE (high.noteOns().front().note == 127);

    Session low;
    setSteps (low.seq(), "C-");                    // 24
    low.seq().transpose = -12;
    low.ctl.liveTranspose = -24;                   // -12
    playSteps (low, 1);
    REQUIRE (low.noteOns().front().note == 0);
}

TEST_CASE ("Edge 16: transport stop releases the note at the top of the block and clears the slide", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C:S D:S E:S F:S");
    s.host.play();
    s.run (kStep + 1024, 64);
    REQUIRE (s.status.gateOpen);
    REQUIRE (s.status.running);
    s.host.stop();
    s.run (64, 64);
    REQUIRE_FALSE (s.status.gateOpen);
    REQUIRE_FALSE (s.status.running);
    REQUIRE (s.status.currentStep == -1);

    REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (kStep, kC + 2, false, true), off (kStep + 1024) });
    REQUIRE (s.midiNotes().back() == MidiEv { kStep + 1024, 0x80, kC + 2, 0 });

    // Starting again does not slide in from the note that was cut.
    const std::int64_t restart = s.now;
    s.host.locate (0.0);
    s.host.play();
    s.run (100, 64);
    REQUIRE (s.notes().back() == on (restart, kC));
    s.host.stop();
    s.run (64, 64);
    requireClean (s);
}

TEST_CASE ("Edge 17: a loop wrap releases the note, drops the slide and fires the step at the loop start", "[edge]")
{
    for (bool hostSplits : { true, false })
    {
        Session s;
        setSteps (s.seq(), "C D E F G A B:S C+:S");          // the last step before the wrap carries a slide
        s.seq().gateLength = 100;
        s.host.looping = true;
        s.host.loopStart = 1.0;                              // step 4
        s.host.loopEnd = 2.0;                                // end of step 7
        s.host.splitsAtLoop = hostSplits;
        s.host.play();
        s.run (8 * kStep + 2 * kStep, 512);                  // once through, then two steps of the loop
        s.host.stop();
        s.run (64, 64);

        const std::int64_t wrap = 8 * kStep;
        std::vector<Ev> expected;
        for (int k = 0; k < 6; ++k)
        {
            expected.push_back (on (k * kStep, kC + std::vector<int> { 0, 2, 4, 5, 7, 9 }[static_cast<std::size_t> (k)]));
            expected.push_back (off ((k + 1) * kStep - 1));
        }
        expected.push_back (on (6 * kStep, kC + 11));
        expected.push_back (on (7 * kStep, kC + 12, false, true));
        expected.push_back (off (wrap));                     // released at the wrap, although it was sliding
        expected.push_back (on (wrap, kC + 7));              // step 4 at the first sample after the wrap, not legato
        expected.push_back (off (wrap + kStep - 1));
        expected.push_back (on (wrap + kStep, kC + 9));
        expected.push_back (off (wrap + 2 * kStep - 1));

        REQUIRE (s.notes() == expected);
        requireClean (s);
    }
}

TEST_CASE ("Edge 18: position jitter from the host neither doubles nor drops a step", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C C# D D# E F F# G G# A A# B C+ C#+ D+ D#+");
    acid::Rng rng (99);
    s.host.play();

    while (s.now < 64 * kStep)
    {
        // Up to 2 ms either way at this tempo: below the relocation tolerance.
        s.host.jitter = s.now == 0 ? 0.0 : (rng.nextDouble() - 0.5) * 0.008;
        s.runBlock (1024);
    }

    s.host.jitter = 0.0;
    s.host.stop();
    s.run (64, 64);

    const auto ons = s.noteOns();
    REQUIRE (ons.size() >= 64);
    REQUIRE (ons.size() <= 65);      // the 65th step sits on the last sample boundary; jitter may pull it in

    for (std::size_t k = 0; k < 64; ++k)
    {
        REQUIRE (ons[k].note == kC + static_cast<int> (k % 16));                       // in order, each once
        REQUIRE (std::abs (ons[k].sample - static_cast<std::int64_t> (k) * kStep) <= 100);
    }

    requireClean (s);
}

TEST_CASE ("Edge 19: negative song positions use floor and a positive modulo", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C D E");
    s.seq().shuffle = 100;
    s.host.locate (-0.75);                         // three steps before the origin
    s.host.play();
    s.run (5 * kStep, 64);
    s.host.stop();
    s.run (64, 64);

    // Grid step -3 is step 0 of the pattern (-3 mod 3) and odd, so it is delayed by half a step.
    const auto ons = s.noteOns();
    REQUIRE (ons.size() == 5);
    REQUIRE (ons[0] == on (3000, kC));
    REQUIRE (ons[1] == on (kStep, kC + 2));
    REQUIRE (ons[2] == on (2 * kStep + 3000, kC + 4));
    REQUIRE (ons[3] == on (3 * kStep, kC));
    REQUIRE (ons[4] == on (4 * kStep + 3000, kC + 2));
    requireClean (s);
}

TEST_CASE ("Edge 20: a tempo change between blocks needs no state change", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C:S D:S E F");
    s.host.play();
    s.run (kStep + 1024, 64);                      // inside the slide chain
    s.host.setTempo (150.0);                       // a step is now 4800 samples
    s.run (48000, 64);
    s.host.stop();
    s.run (64, 64);

    // 1024 samples of step 1 are done at 120 BPM; the remaining 4976/6000 of it take 3980.8 samples.
    const std::int64_t step2 = kStep + 1024 + 3981;
    const auto notes = s.notes();
    REQUIRE (notes.size() >= 7);
    REQUIRE (notes[0] == on (0, kC));
    REQUIRE (notes[1] == on (kStep, kC + 2, false, true));
    REQUIRE (notes[2] == on (step2, kC + 4, false, true));               // the chain is not broken
    REQUIRE (notes[3] == off (step2 + 2400 + 65));                       // half a step at the new tempo plus 1.35 ms
    REQUIRE (notes[4] == on (step2 + 4800, kC + 5));
    requireClean (s);
}

TEST_CASE ("Edge 21: shuffle parity comes from the host grid, so an odd length does not flip it", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C D E");
    s.seq().shuffle = 100;                         // odd grid steps are half a step late
    playSteps (s, 9);

    const auto ons = s.noteOns();
    REQUIRE (ons.size() == 9);

    for (std::size_t g = 0; g < 9; ++g)
    {
        const std::int64_t expected = static_cast<std::int64_t> (g) * kStep + (g % 2 == 1 ? 3000 : 0);
        REQUIRE (ons[g].sample == expected);
        REQUIRE (ons[g].note == kC + 2 * static_cast<int> (g % 3));
    }
    requireClean (s);

    // Triplet and dotted divisions ignore the shuffle amount.
    for (DivMod mod : { DivMod::Triplet, DivMod::Dotted })
    {
        Session t;
        setSteps (t.seq(), "C D E");
        t.seq().shuffle = 100;
        t.seq().divMod = mod;
        t.host.play();
        t.run (48000, 64);
        t.host.stop();
        t.run (64, 64);

        const std::int64_t step = mod == DivMod::Triplet ? 4000 : 9000;
        const auto tOns = t.noteOns();
        REQUIRE (tOns.size() == static_cast<std::size_t> (48000 / step + (48000 % step != 0 ? 1 : 0)));
        for (std::size_t g = 0; g < tOns.size(); ++g)
            REQUIRE (tOns[g].sample == static_cast<std::int64_t> (g) * step);
    }
}

TEST_CASE ("Edge 22: a gate as long as the step, with no slide, closes one sample before the next onset", "[edge]")
{
    Session s;
    setSteps (s.seq(), "C D E");
    s.seq().gateLength = 100;
    s.seq().steps[1].gate = 127;                   // the per-step gate, also full length
    playSteps (s, 3);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (0, kC), off (kStep - 1), on (kStep, kC + 2), off (2 * kStep - 1), on (2 * kStep, kC + 4), off (3 * kStep - 1) });
    requireClean (s);

    // The shortened step of a shuffled pair is clamped the same way.
    Session sw;
    setSteps (sw.seq(), "C D");
    sw.seq().shuffle = 100;
    playSteps (sw, 2);
    REQUIRE (sw.notes() == std::vector<Ev> {
        on (0, kC), off (kGate), on (kStep + 3000, kC + 2), off (2 * kStep - 1) });   // 3000 samples of room, 3065 of gate
}

TEST_CASE ("Edge 23: ordering at one sample, and the MIDI shape of a slide", "[edge]")
{
    SECTION ("voice: NoteOff precedes NoteOn at the same offset")
    {
        // A long note is still sounding when a pattern with a finer grid takes over at once.
        Session s;
        s.cfg.launchQuant = LaunchQuant::Immediate;
        setSteps (s.seq (0), "C");
        s.seq (0).div = StepDiv::d1_4;
        s.seq (0).gateLength = 100;
        setSteps (s.seq (1), "D E");
        s.host.play();
        s.run (9000, 64);
        s.ctl.slot = 1;
        s.run (3 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), off (2 * kStep), on (2 * kStep, kC + 2), off (2 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("MIDI out: the target's NoteOn, then the source's NoteOff one sample later")
    {
        Session s;
        setSteps (s.seq(), "C:S D . .");
        playSteps (s, 4);

        REQUIRE (s.midiNotes() == std::vector<MidiEv> {
            { 0, 0x90, kC, 32 }, { kStep, 0x90, kC + 2, 32 }, { kStep + 1, 0x80, kC, 0 }, { kStep + kGate, 0x80, kC + 2, 0 } });
        requireClean (s);
    }

    SECTION ("the overlap survives a block boundary between the two")
    {
        Session s;
        setSteps (s.seq(), "C:S D . .");
        s.host.play();
        s.run (kStep + 1, kStep + 1);              // the NoteOn is the last sample of this block
        s.run (3 * kStep - 1, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.midiNotes() == std::vector<MidiEv> {
            { 0, 0x90, kC, 32 }, { kStep, 0x90, kC + 2, 32 }, { kStep + 1, 0x80, kC, 0 }, { kStep + kGate, 0x80, kC + 2, 0 } });
        requireClean (s);
    }
}

TEST_CASE ("Edge 24: host-note triggering", "[edge]")
{
    SECTION ("a trigger note starts its pattern, its release stops it, the transport start does nothing")
    {
        Session s;
        s.cfg.triggerMode = TriggerMode::HostNote;
        setSteps (s.seq (0), "C D E F");
        setSteps (s.seq (1), "G A B C+");
        s.host.play();
        s.keyDown (10000, 24);
        s.keyUp (23000, 24);
        s.keyDown (40000, 25);
        s.keyUp (47000, 25);
        s.run (9000, 64);
        REQUIRE (s.notes().empty());               // the host is playing, but no trigger is held
        REQUIRE_FALSE (s.status.running);
        s.run (3000, 64);
        REQUIRE (s.status.running);
        REQUIRE (s.status.playingSlot == 0);
        REQUIRE (s.status.heldNotes == 1);
        s.run (48000, 64);
        REQUIRE_FALSE (s.status.running);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (10000, kC), off (10000 + kGate), on (16000, kC + 2), off (16000 + kGate), on (22000, kC + 4), off (23000),
            on (40000, kC + 7), off (40000 + kGate), on (46000, kC + 9), off (47000) });
        requireClean (s);
    }

    SECTION ("a second trigger while one is held is a pattern change that keeps the phase")
    {
        Session s;
        s.cfg.triggerMode = TriggerMode::HostNote;
        s.cfg.restartOnTrigger = false;
        s.cfg.launchQuant = LaunchQuant::Immediate;
        setSteps (s.seq (0), "C D E F");
        setSteps (s.seq (1), "G A B C+");
        s.host.play();
        s.keyDown (0, 24);
        s.keyDown (7000, 25);
        s.keyUp (20000, 25);                       // back to the note that is still down
        s.keyUp (26000, 24);
        s.run (30000, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), off (kGate), on (kStep, kC + 2), off (kStep + kGate),
            on (2 * kStep, kC + 11), off (2 * kStep + kGate), on (3 * kStep, kC + 12), off (3 * kStep + kGate),
            on (4 * kStep, kC), off (26000) });
        requireClean (s);
    }

    SECTION ("with the default quantisation the second pattern waits for the end of the first")
    {
        Session s;
        s.cfg.triggerMode = TriggerMode::HostNote;
        s.cfg.restartOnTrigger = false;
        setSteps (s.seq (0), "C D E F");
        setSteps (s.seq (1), "G A B C+");
        s.host.play();
        s.keyDown (0, 24);
        s.keyDown (7000, 25);
        s.keyUp (36000, 24);
        s.keyUp (36000, 25);
        s.run (8000, 64);
        REQUIRE (s.status.playingSlot == 0);
        REQUIRE (s.status.queuedSlot == 1);
        s.run (30000, 64);

        std::vector<int> played;
        for (const Ev& e : s.noteOns()) played.push_back (e.note - kC);
        REQUIRE (played == std::vector<int> { 0, 2, 4, 5, 7, 9 });
        requireClean (s);
    }

    SECTION ("notes outside the 48-note zone do nothing, and the zone follows the base note")
    {
        Session s;
        s.cfg.triggerMode = TriggerMode::HostNote;
        s.cfg.hostNoteBase = 36;
        setSteps (s.seq (47), "E");
        s.keyDown (0, 35);
        s.keyDown (100, 84);
        s.keyUp (200, 35);
        s.keyUp (300, 84);
        s.keyDown (1000, 83);                      // base + 47: bank D, slot 12
        s.keyUp (2000, 83);
        s.run (4000, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (1000, kC + 4), off (2000) });
        REQUIRE (s.status.playingSlot == 47);
        requireClean (s);
    }
}

TEST_CASE ("Edge 25: trigger notes restart the pattern or keep its phase", "[edge]")
{
    // The contract has no mode that transposes the pattern by the trigger key (TriggerMode is
    // Live or HostNote). What it does have: every trigger note restarts from step 1 when
    // restartOnTrigger is on, the phase is kept when it is off, and live transpose moves the
    // pattern by a key's worth of semitones.
    SECTION ("restartOnTrigger: each trigger note starts at step 1 on its own sample")
    {
        Session s;
        s.cfg.triggerMode = TriggerMode::HostNote;
        s.cfg.restartOnTrigger = true;
        setSteps (s.seq (0), "C D E F");
        s.host.locate (0.1);                       // the host is off the grid: the trigger sets the grid
        s.host.play();
        s.keyDown (500, 24);
        s.keyDown (8500, 24);                      // re-struck (no release in between): restart
        s.keyUp (16000, 24);
        s.run (20000, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (500, kC), off (500 + kGate), on (6500, kC + 2), off (8500),
            on (8500, kC), off (8500 + kGate), on (14500, kC + 2), off (16000) });
        requireClean (s);
    }

    SECTION ("restartOnTrigger off: a trigger joins the pattern where the song position says")
    {
        Session s;
        s.cfg.triggerMode = TriggerMode::HostNote;
        s.cfg.restartOnTrigger = false;
        setSteps (s.seq (0), "C D E F");
        s.host.play();
        s.keyDown (8500, 24);
        s.keyUp (20000, 24);
        s.run (24000, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (2 * kStep, kC + 4), off (2 * kStep + kGate), on (3 * kStep, kC + 5), off (20000) });
        requireClean (s);
    }

    SECTION ("live transpose plays the pattern relative to a key")
    {
        Session s;
        setSteps (s.seq (0), "C E G");
        s.ctl.liveTranspose = 7;
        s.host.play();
        s.run (3 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        std::vector<int> played;
        for (const Ev& e : s.noteOns()) played.push_back (e.note);
        REQUIRE (played == std::vector<int> { kC + 7, kC + 11, kC + 14 });
    }
}

TEST_CASE ("Edge 26: External mode mutes the sequencer, Sequencer mode ignores live notes", "[edge]")
{
    SECTION ("External: only the played notes reach the voice while the transport runs")
    {
        Session s;
        s.ctl.playMode = PlayMode::External;
        setSteps (s.seq(), "C D E F");
        s.host.play();
        s.keyDown (5000, 60, 80);
        s.keyUp (9000, 60);
        s.run (4 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (5000, 60, true, false, true), off (9000) });
        REQUIRE_FALSE (s.status.running);
        REQUIRE (s.status.playingSlot == -1);
        requireClean (s);
    }

    SECTION ("Sequencer: played notes are not merged into the pattern")
    {
        Session s;
        setSteps (s.seq(), "C . . .");
        s.host.play();
        s.keyDown (8000, 60, 100);
        s.keyUp (12000, 60);
        s.run (4 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kGate) });
        requireClean (s);
    }
}

TEST_CASE ("Edge 27: releasing the last arpeggiator key mid-gate", "[edge]")
{
    Session s;
    s.ctl.playMode = PlayMode::Arpeggiator;
    setSteps (s.arp(), "C C+ C++");                // index 1, climbing an octave per step
    s.keyDown (1000, 60);
    s.keyUp (2000, 60);                            // mid-gate
    s.keyDown (20000, 60);
    s.keyUp (27000, 60);
    s.run (3000, 64);
    REQUIRE_FALSE (s.status.running);              // the cursor is reset...
    REQUIRE (s.status.gateOpen);                   // ...and the note is still sounding
    s.run (40000, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        on (1000, 60), off (1000 + kGate),                    // ends at its own gate time, not at the key-up
        on (20000, 60), off (20000 + kGate),                  // the next press starts from step 1 again
        on (26000, 72), off (26000 + kGate) });
    requireClean (s);
}

TEST_CASE ("Edge 28: a host that plays without a position is counted in samples at its tempo", "[edge]")
{
    SECTION ("tempo from the host")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.host.sendPpq = false;
        s.host.bpm = 150.0;                        // a step is 4800 samples
        s.host.play();
        s.run (4 * 4800, 64);
        s.host.stop();
        s.run (64, 64);

        const auto ons = s.noteOns();
        REQUIRE (ons.size() == 4);
        for (std::size_t k = 0; k < 4; ++k)
        {
            REQUIRE (ons[k].note == kC + std::vector<int> { 0, 2, 4, 5 }[k]);
            REQUIRE (ons[k].sample == static_cast<std::int64_t> (k) * 4800);
        }
        requireClean (s);
    }

    SECTION ("the count runs from when the plugin started, also while the host is stopped")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.host.sendPpq = false;
        s.run (kStep + 1000, 64);                  // stopped, but the internal counter runs
        REQUIRE (s.notes().empty());
        s.host.play();
        s.run (2 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        // 1000 samples into step 1 at the start: steps 2 and 3 follow on the counted grid.
        REQUIRE (s.notes() == std::vector<Ev> {
            on (2 * kStep, kC + 4), off (2 * kStep + kGate), on (3 * kStep, kC + 5), off (3 * kStep + 1000) });
        requireClean (s);
    }

    SECTION ("no tempo either: 120 BPM, or the internal tempo control")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.host.sendPpq = false;
        s.host.sendBpm = false;
        s.ctl.internalBpm = 100.0;                 // a step is 7200 samples
        s.host.play();
        s.run (4 * 7200, 64);
        s.host.stop();
        s.run (64, 64);

        const auto ons = s.noteOns();
        REQUIRE (ons.size() == 4);
        for (std::size_t k = 0; k < 4; ++k)
            REQUIRE (ons[k].sample == static_cast<std::int64_t> (k) * 7200);
        requireClean (s);
    }
}
