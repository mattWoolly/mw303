// Sequencer playback features: directions, ratchets, probability, overrides, queued
// switching at each quantisation, the Live trigger mode, status for the UI, overflow, reset
// and mode changes.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>

using namespace ptest;

namespace
{
void playSteps (Session& s, int steps, int blockSize = 64)
{
    s.host.play();
    s.run (steps * kStep, blockSize);
    s.host.stop();
    s.run (64, blockSize);
}

std::vector<int> playedNotes (const Session& s)
{
    std::vector<int> r;
    for (const Ev& e : s.noteOns()) r.push_back (e.note - kC);
    return r;
}

void requireClean (const Session& s)
{
    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (midiBalance (s.midi).empty());
    REQUIRE (s.orderViolations == 0);
    REQUIRE (s.allocations == 0);
}
}

TEST_CASE ("Sequencer: the four directions", "[sequencer]")
{
    SECTION ("forward")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        playSteps (s, 9);
        REQUIRE (playedNotes (s) == std::vector<int> { 0, 2, 4, 5, 0, 2, 4, 5, 0 });
    }

    SECTION ("reverse")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.seq().direction = Direction::Reverse;
        playSteps (s, 9);
        REQUIRE (playedNotes (s) == std::vector<int> { 5, 4, 2, 0, 5, 4, 2, 0, 5 });
    }

    SECTION ("ping-pong does not repeat the ends")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.seq().direction = Direction::PingPong;
        playSteps (s, 13);
        REQUIRE (playedNotes (s) == std::vector<int> { 0, 2, 4, 5, 4, 2, 0, 2, 4, 5, 4, 2, 0 });
    }

    SECTION ("random is a function of the pattern seed and the step index")
    {
        const int notes[] = { 0, 2, 4, 5 };
        std::vector<int> expected;
        for (std::int64_t g = 0; g < 40; ++g)
            expected.push_back (notes[stepPosition (Direction::Random, 4, 0, 321, g)]);

        Session s;
        setSteps (s.seq(), "C D E F");
        s.seq().direction = Direction::Random;
        s.seq().gen.seed = 321;
        playSteps (s, 40);
        REQUIRE (playedNotes (s) == expected);

        // Starting in the middle of the song plays the same steps at the same places.
        Session mid;
        setSteps (mid.seq(), "C D E F");
        mid.seq().direction = Direction::Random;
        mid.seq().gen.seed = 321;
        mid.host.locate (5.0);                     // step 20
        playSteps (mid, 20);
        REQUIRE (playedNotes (mid) == std::vector<int> (expected.begin() + 20, expected.end()));

        // Another seed, another order.
        Session other;
        setSteps (other.seq(), "C D E F");
        other.seq().direction = Direction::Random;
        other.seq().gen.seed = 322;
        playSteps (other, 40);
        REQUIRE (playedNotes (other) != expected);
    }

    SECTION ("a relocation lands on the step the song position dictates, in every direction")
    {
        for (Direction d : { Direction::Forward, Direction::Reverse, Direction::PingPong })
        {
            Session full, part;
            for (Session* s : { &full, &part })
            {
                setSteps (s->seq(), "C D E F G");
                s->seq().direction = d;
            }

            playSteps (full, 30);
            part.host.locate (11 * 0.25);
            playSteps (part, 19);

            const auto all = playedNotes (full);
            REQUIRE (playedNotes (part) == std::vector<int> (all.begin() + 11, all.end()));
        }
    }
}

TEST_CASE ("Sequencer: ratchets", "[sequencer]")
{
    SECTION ("four evenly spaced hits, each with a quarter of the gate")
    {
        Session s;
        setSteps (s.seq(), "C .");
        s.seq().gateLength = 50;
        s.seq().steps[0].ratchet = 4;
        s.seq().steps[0].accent = true;
        playSteps (s, 2);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC, true), off (750), on (1500, kC, true), off (2250), on (3000, kC, true), off (3750), on (4500, kC, true), off (5250) });
        requireClean (s);
    }

    SECTION ("a full gate still leaves a sample between hits")
    {
        Session s;
        setSteps (s.seq(), "C .");
        s.seq().gateLength = 100;
        s.seq().steps[0].ratchet = 2;
        playSteps (s, 2);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (2999), on (3000, kC), off (5999) });
    }

    SECTION ("a slide leaves from the last hit")
    {
        Session s;
        setSteps (s.seq(), "C:S D . .");
        s.seq().steps[0].ratchet = 2;
        playSteps (s, 4);
        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), off (1533), on (3000, kC), on (kStep, kC + 2, false, true), off (kStep + kGate) });
        requireClean (s);
    }

    SECTION ("hits divide the actual step, shuffle included")
    {
        Session s;
        setSteps (s.seq(), "C D");
        s.seq().shuffle = 100;                     // the first step lasts 9000 samples, the second 3000
        s.seq().gateLength = 10;
        s.seq().steps[0].ratchet = 3;
        s.seq().steps[1].ratchet = 3;
        playSteps (s, 2);

        std::vector<std::int64_t> onsets;
        for (const Ev& e : s.noteOns()) onsets.push_back (e.sample);
        REQUIRE (onsets == std::vector<std::int64_t> { 0, 3000, 6000, 9000, 10000, 11000 });
        requireClean (s);
    }

    SECTION ("out-of-range values are clamped")
    {
        Session s;
        setSteps (s.seq(), "C D");
        s.seq().steps[0].ratchet = 0;
        s.seq().steps[1].ratchet = 200;
        playSteps (s, 2);
        REQUIRE (s.noteOns().size() == 1 + 4);
        requireClean (s);
    }
}

TEST_CASE ("Sequencer: probability is deterministic", "[sequencer]")
{
    const std::uint32_t seed = 20260102;

    auto make = [seed] (Session& s, int probability)
    {
        setSteps (s.seq(), "C D E F G A B C+");
        s.seq().gen.seed = seed;
        for (int i = 0; i < 8; ++i) s.seq().steps[static_cast<std::size_t> (i)].probability = static_cast<std::uint8_t> (probability);
    };

    SECTION ("0 % never plays and 100 % always plays")
    {
        Session never, always;
        make (never, 0);
        make (always, 100);
        playSteps (never, 64);
        playSteps (always, 64);
        REQUIRE (never.noteOns().empty());
        REQUIRE (always.noteOns().size() == 64);
    }

    SECTION ("the steps that play are exactly the ones the hash says")
    {
        Session s;
        make (s, 35);
        playSteps (s, 256);

        std::vector<std::int64_t> expected;
        for (std::int64_t g = 0; g < 256; ++g)
            if (stepRollPasses (seed, g, 35)) expected.push_back (g * kStep);

        std::vector<std::int64_t> actual;
        for (const Ev& e : s.noteOns()) actual.push_back (e.sample);

        REQUIRE (actual == expected);
        REQUIRE (actual.size() > 60);
        REQUIRE (actual.size() < 120);
    }

    SECTION ("two runs, and runs with different block sizes, are identical")
    {
        Session a, b, c;
        make (a, 50);
        make (b, 50);
        make (c, 50);
        playSteps (a, 128, 64);
        playSteps (b, 128, 64);
        playSteps (c, 128, 4096);
        REQUIRE (a.voice == b.voice);
        REQUIRE (a.noteOns() == c.noteOns());
    }

    SECTION ("a render that starts in the middle of the song matches the full render")
    {
        Session full, part;
        make (full, 50);
        make (part, 50);
        playSteps (full, 96);
        part.host.locate (10.0);                   // step 40
        playSteps (part, 56);

        std::vector<Ev> tail;
        for (Ev e : full.noteOns())
            if (e.sample >= 40 * kStep) { e.sample -= 40 * kStep; tail.push_back (e); }

        REQUIRE (part.noteOns() == tail);
    }

    SECTION ("a different seed gives a different result")
    {
        Session a, b;
        make (a, 50);
        make (b, 50);
        b.seq().gen.seed = seed + 1;
        playSteps (a, 64);
        playSteps (b, 64);
        REQUIRE (a.noteOns() != b.noteOns());
    }
}

TEST_CASE ("Sequencer: live overrides", "[sequencer]")
{
    auto make = [] (Session& s)
    {
        setSteps (s.seq(), "C:A . D:S E:V");
        s.seq().steps[1].note = 7;                 // the rest keeps a pitch: G
    };

    SECTION ("Note Set makes every step sound, with the pitch the rest stored")
    {
        Session s;
        make (s);
        s.ctl.ovrNoteSet = true;
        playSteps (s, 4);
        REQUIRE (playedNotes (s) == std::vector<int> { 0, 7, 2, 4 });
        requireClean (s);
    }

    SECTION ("Note Set also overrides a failing probability")
    {
        Session s;
        make (s);
        for (auto& st : s.seq().steps) st.probability = 0;
        s.ctl.ovrNoteSet = true;
        playSteps (s, 4);
        REQUIRE (s.noteOns().size() == 4);
    }

    SECTION ("Note Clr mutes while the pattern keeps running")
    {
        Session s;
        make (s);
        s.ctl.ovrNoteClr = true;
        s.host.play();
        s.run (2 * kStep + 100, 64);
        REQUIRE (s.notes().empty());
        REQUIRE (s.status.running);
        REQUIRE (s.status.currentStep == 2);
        s.ctl.ovrNoteClr = false;
        s.run (2 * kStep - 100, 64);
        s.host.stop();
        s.run (64, 64);
        REQUIRE (playedNotes (s) == std::vector<int> { 4 });
        requireClean (s);
    }

    SECTION ("Note Clr wins over Note Set")
    {
        Session s;
        make (s);
        s.ctl.ovrNoteSet = true;
        s.ctl.ovrNoteClr = true;
        playSteps (s, 4);
        REQUIRE (s.notes().empty());
    }

    SECTION ("Slide Set slides every step, Slide Clr none")
    {
        Session set, clr;
        make (set);
        make (clr);
        set.ctl.ovrSlideSet = true;
        clr.ctl.ovrSlideClr = true;
        set.ctl.ovrNoteSet = true;
        clr.ctl.ovrNoteSet = true;
        playSteps (set, 4);
        playSteps (clr, 4);

        const auto a = set.noteOns();
        REQUIRE (a.size() == 4);
        REQUIRE_FALSE (a[0].legato);
        REQUIRE (a[1].legato);
        REQUIRE (a[2].legato);
        REQUIRE (a[3].legato);

        for (const Ev& e : clr.noteOns()) REQUIRE_FALSE (e.legato);
        requireClean (set);
        requireClean (clr);

        // Both held: Clr wins.
        Session both;
        make (both);
        both.ctl.ovrSlideSet = true;
        both.ctl.ovrSlideClr = true;
        playSteps (both, 4);
        for (const Ev& e : both.noteOns()) REQUIRE_FALSE (e.legato);
    }

    SECTION ("Accent and Vibrato Set and Clr")
    {
        Session accSet, accClr, vibSet, vibClr;
        for (Session* s : { &accSet, &accClr, &vibSet, &vibClr }) make (*s);
        accSet.ctl.ovrAccentSet = true;
        accClr.ctl.ovrAccentClr = true;
        vibSet.ctl.ovrVibSet = true;
        vibClr.ctl.ovrVibClr = true;
        for (Session* s : { &accSet, &accClr, &vibSet, &vibClr }) playSteps (*s, 4);

        for (const Ev& e : accSet.noteOns()) REQUIRE (e.accent);
        for (const Ev& e : accClr.noteOns()) REQUIRE_FALSE (e.accent);
        for (const Ev& e : vibSet.noteOns()) REQUIRE (e.vibrato);
        for (const Ev& e : vibClr.noteOns()) REQUIRE_FALSE (e.vibrato);
    }

    SECTION ("overrides are momentary and leave the pattern untouched")
    {
        Session plain, overridden;
        make (plain);
        make (overridden);
        const Pattern before = overridden.seq();

        playSteps (plain, 8);

        overridden.ctl.ovrNoteSet = overridden.ctl.ovrSlideSet = overridden.ctl.ovrAccentSet = overridden.ctl.ovrVibSet = true;
        overridden.host.play();
        overridden.run (4 * kStep - 100, 64);
        overridden.ctl = PlayerControls {};
        overridden.run (100, 64);
        const std::size_t firstPass = overridden.voice.size();
        overridden.run (4 * kStep, 64);
        overridden.host.stop();
        overridden.run (64, 64);

        REQUIRE (std::memcmp (&before, &overridden.seq(), sizeof (Pattern)) == 0);

        // The second pass is the plain pattern again. The first step is still slid into,
        // because the last overridden step carried a slide.
        std::vector<Ev> second (overridden.voice.begin() + static_cast<std::ptrdiff_t> (firstPass), overridden.voice.end());
        std::vector<Ev> reference;
        for (const Ev& e : plain.voice)
            if (e.sample >= 4 * kStep && e.type != VoiceEvent::Type::VibratoDepth) reference.push_back (e);

        REQUIRE (second.size() == reference.size());
        REQUIRE (second[0] == on (4 * kStep, kC, true, true));
        for (std::size_t i = 1; i < second.size(); ++i) REQUIRE (second[i] == reference[i]);
        requireClean (overridden);
    }
}

TEST_CASE ("Sequencer: queued pattern switching at each quantisation", "[sequencer]")
{
    SECTION ("end of pattern, with the new grid anchored at the launch")
    {
        Session s;
        setSteps (s.seq (0), "C D");
        setSteps (s.seq (1), "E F G");
        s.seq (1).div = StepDiv::d1_8;
        s.seq (1).divMod = DivMod::Triplet;        // a step is 8000 samples
        s.host.play();
        s.run (1000, 64);
        s.ctl.slot = 1;
        s.run (2 * kStep + 4 * 8000 - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        const auto ons = s.noteOns();
        REQUIRE (ons == std::vector<Ev> { on (0, kC), on (kStep, kC + 2), on (2 * kStep, kC + 4), on (2 * kStep + 8000, kC + 5),
                                          on (2 * kStep + 16000, kC + 7), on (2 * kStep + 24000, kC + 4) });
        requireClean (s);
    }

    SECTION ("end of pattern counts a ping-pong pass as there and back")
    {
        Session s;
        setSteps (s.seq (0), "C D E");
        s.seq (0).direction = Direction::PingPong;
        setSteps (s.seq (1), "A");
        s.host.play();
        s.run (1000, 64);
        s.ctl.slot = 1;
        s.run (6 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);
        REQUIRE (playedNotes (s) == std::vector<int> { 0, 2, 4, 2, 9, 9 });
    }

    SECTION ("selecting the playing pattern again empties the queue")
    {
        Session s;
        setSteps (s.seq (0), "C D E F");
        setSteps (s.seq (1), "A");
        s.host.play();
        s.run (1000, 64);
        s.ctl.slot = 1;
        s.run (1000, 64);
        REQUIRE (s.status.queuedSlot == 1);
        s.ctl.slot = 0;
        s.run (1000, 64);
        REQUIRE (s.status.queuedSlot == -1);
        s.run (8 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);
        REQUIRE (playedNotes (s) == std::vector<int> { 0, 2, 4, 5, 0, 2, 4, 5 });
    }

    SECTION ("next bar")
    {
        for (int timeSigNum : { 4, 3 })
        {
            Session s;
            s.cfg.launchQuant = LaunchQuant::NextBar;
            s.host.timeSigNum = timeSigNum;
            setSteps (s.seq (0), "C D E F G A");
            setSteps (s.seq (1), "B B");
            s.host.play();
            s.run (5 * kStep + 10, 64);
            s.ctl.slot = 1;
            s.run (64, 64);
            REQUIRE (s.status.queuedSlot == 1);

            const int stepsPerBar = timeSigNum * 4;
            s.run ((stepsPerBar + 2) * kStep - s.now, 64);
            REQUIRE (s.status.playingSlot == 1);
            s.host.stop();
            s.run (64, 64);

            const int a[] = { 0, 2, 4, 5, 7, 9 };
            std::vector<int> expected;
            for (int g = 0; g < stepsPerBar; ++g) expected.push_back (a[g % 6]);
            expected.push_back (11);
            expected.push_back (11);
            REQUIRE (playedNotes (s) == expected);
            REQUIRE (s.noteOns()[static_cast<std::size_t> (stepsPerBar)].sample == stepsPerBar * kStep);
            requireClean (s);
        }
    }

    SECTION ("next bar, across a change of division, carrying a slide")
    {
        Session s;
        s.cfg.launchQuant = LaunchQuant::NextBar;
        setSteps (s.seq (0), "C:S");               // a drone
        setSteps (s.seq (1), "E F");
        s.seq (1).div = StepDiv::d1_8;
        s.host.play();
        s.run (1000, 64);
        s.ctl.slot = 1;
        s.run (16 * kStep + 3 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        const auto notes = s.notes();
        REQUIRE (notes.size() == 16 + 4);
        for (std::size_t g = 1; g < 16; ++g) REQUIRE (notes[g] == on (static_cast<std::int64_t> (g) * kStep, kC, false, true));
        REQUIRE (notes[16] == on (16 * kStep, kC + 4, false, true));           // slid into across the change
        REQUIRE (notes[17] == off (16 * kStep + kStep + 65));                  // half an 1/8 step plus 1.35 ms
        REQUIRE (notes[18] == on (18 * kStep, kC + 5));
        requireClean (s);
    }

    SECTION ("immediate keeps the place in the pattern")
    {
        Session s;
        s.cfg.launchQuant = LaunchQuant::Immediate;
        setSteps (s.seq (0), "C D E F");
        setSteps (s.seq (1), "G A B C+");
        s.host.play();
        s.run (kStep + 500, 64);
        s.ctl.slot = 1;
        s.run (64, 64);
        REQUIRE (s.status.playingSlot == 1);
        REQUIRE (s.status.queuedSlot == -1);
        s.run (6 * kStep - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (playedNotes (s) == std::vector<int> { 0, 2, 11, 12, 7, 9 });
        const auto notes = s.notes();
        REQUIRE (notes[3] == off (kStep + kGate));    // the note that was sounding finished under its own gate
        requireClean (s);
    }

    SECTION ("while stopped a selection applies at once")
    {
        Session s;
        setSteps (s.seq (0), "C");
        setSteps (s.seq (30), "E");
        s.ctl.bank = 2;
        s.ctl.slot = 6;
        s.run (64, 64);
        REQUIRE (s.status.playingSlot == 30);
        REQUIRE (s.status.queuedSlot == -1);
        playSteps (s, 2);
        REQUIRE (playedNotes (s) == std::vector<int> { 4, 4 });

        // Out-of-range selections are clamped.
        s.ctl.bank = 99;
        s.ctl.slot = -3;
        s.run (64, 64);
        REQUIRE (s.status.playingSlot == slotIndex (3, 0));
    }
}

TEST_CASE ("Sequencer: Live trigger mode, transport and the Run control", "[sequencer]")
{
    SECTION ("following the host: play starts, stop stops")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.run (kStep, 64);
        REQUIRE (s.notes().empty());
        REQUIRE_FALSE (s.status.running);
        playSteps (s, 2);
        REQUIRE (s.noteOns().size() == 2);
        REQUIRE_FALSE (s.status.running);
    }

    SECTION ("not following the host: only Run starts and stops, on the song grid")
    {
        Session s;
        s.cfg.followHostTransport = false;
        setSteps (s.seq(), "C D E F");
        s.host.play();
        s.run (2 * kStep + 1000, 64);
        REQUIRE (s.notes().empty());
        s.ctl.run = true;
        s.run (3 * kStep, 64);
        s.ctl.run = false;
        s.run (64, 64);
        REQUIRE_FALSE (s.status.running);
        s.host.stop();
        s.run (kStep, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (3 * kStep, kC + 5), off (3 * kStep + kGate), on (4 * kStep, kC), off (4 * kStep + kGate),
            on (5 * kStep, kC + 2), off (5 * kStep + 1000) });
        requireClean (s);
    }

    SECTION ("host stopped: Run plays at once from step 1 on the internal clock")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.host.bpm = 150.0;                        // a stopped host still reports its tempo
        s.run (5000, 64);
        s.ctl.run = true;
        s.run (3 * 4800, 64);
        REQUIRE (s.status.running);
        REQUIRE (s.status.internalClock);
        s.ctl.run = false;
        s.run (64, 64);

        REQUIRE (playedNotes (s) == std::vector<int> { 0, 2, 4 });
        const auto ons = s.noteOns();
        for (std::size_t k = 0; k < 3; ++k) REQUIRE (ons[k].sample == 5000 + static_cast<std::int64_t> (k) * 4800);
        requireClean (s);
    }

    SECTION ("standalone: no host transport at all, tempo from the controls")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.host.present = false;
        s.ctl.internalBpm = 90.0;                  // a step is 8000 samples
        s.ctl.run = true;
        s.run (4 * 8000, 64);
        s.ctl.run = false;
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), off (4065), on (8000, kC + 2), off (12065), on (16000, kC + 4), off (20065), on (24000, kC + 5), off (28065) });
        REQUIRE (s.status.bpm == 90.0);
        requireClean (s);
    }

    SECTION ("Run keeps the pattern going when the host stops, and a host start relocates it")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.ctl.run = true;
        s.host.play();
        s.run (2 * kStep + 500, 64);
        s.host.stop();                             // carries on, on the internal clock, in time
        s.run (2 * kStep, 64);
        REQUIRE (s.status.running);
        REQUIRE (s.status.internalClock);
        s.host.locate (0.0);
        s.host.play();                             // 500 samples into step 4: cut, and back to the song position
        s.run (kStep, 64);
        s.ctl.run = false;
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), off (kGate), on (kStep, kC + 2), off (kStep + kGate), on (2 * kStep, kC + 4), off (2 * kStep + kGate),
            on (3 * kStep, kC + 5), off (3 * kStep + kGate), on (4 * kStep, kC), off (4 * kStep + 500),
            on (4 * kStep + 500, kC), off (4 * kStep + 500 + kGate) });
        requireClean (s);
    }

    SECTION ("host link on: the pattern phase is the song position")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.host.locate (2.75 + 0.1);                // inside step 11 (step 3 of the pattern)
        playSteps (s, 2);
        const auto ons = s.noteOns();
        REQUIRE (ons.size() == 2);
        REQUIRE (ons[0] == on (3600, kC));         // step 12 is the first to start after the play position
        REQUIRE (ons[1] == on (9600, kC + 2));
    }

    SECTION ("host link off: free-running from the trigger point, deaf to relocations")
    {
        Session s;
        s.cfg.hostLink = false;
        setSteps (s.seq(), "C D E F");
        s.host.locate (2.75 + 0.1);
        s.host.play();
        s.run (2 * kStep + 100, 64);
        s.host.locate (57.123);                    // ignored
        s.run (2 * kStep - 100, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> {
            on (0, kC), off (kGate), on (kStep, kC + 2), off (kStep + kGate),
            on (2 * kStep, kC + 4), off (2 * kStep + kGate), on (3 * kStep, kC + 5), off (3 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("a start just after a step still plays that step")
    {
        Session s;
        setSteps (s.seq(), "C D E F");
        s.host.locate (0.5 + 0.0005);              // 12 samples past step 2
        playSteps (s, 1);
        REQUIRE (s.noteOns().front() == on (0, kC + 4));
    }
}

TEST_CASE ("Sequencer: status for the UI", "[sequencer]")
{
    Session s;
    setSteps (s.seq(), "C D E F G");
    s.run (64, 64);
    REQUIRE_FALSE (s.status.running);
    REQUIRE (s.status.currentStep == -1);
    REQUIRE (s.status.playingSlot == 0);
    REQUIRE (s.status.queuedSlot == -1);
    REQUIRE (s.status.internalClock);
    REQUIRE_FALSE (s.status.hostRolling);

    s.host.locate (0.0);
    s.host.play();

    for (int g = 0; g < 12; ++g)
    {
        s.run ((g * kStep + 100) - (s.now - 64), 64);
        REQUIRE (s.status.running);
        REQUIRE (s.status.hostRolling);
        REQUIRE_FALSE (s.status.internalClock);
        REQUIRE (s.status.currentStep == g % 5);
        REQUIRE (s.status.gateOpen);
        REQUIRE (s.status.bpm == 120.0);
        REQUIRE (std::abs (s.status.ppq - static_cast<double> (s.now - 64) / 24000.0) < 1e-9);
    }

    s.run (kGate, 64);
    REQUIRE_FALSE (s.status.gateOpen);
}

TEST_CASE ("Sequencer: output overflow drops notes but never leaves one on", "[sequencer]")
{
    Session s (48000.0, 512);
    setSteps (s.seq(), "C D:S E F G:S A B C+");
    s.seq().div = StepDiv::d1_64;
    for (auto& st : s.seq().steps) st.ratchet = 4;
    s.host.bpm = 300.0;
    s.host.play();
    s.runBlock (1 << 20);                          // 21 seconds in one block: thousands of events
    REQUIRE (s.overflows == 1);
    REQUIRE (s.out->numVoiceEvents <= PlayerOutput::kMaxVoiceEvents);
    REQUIRE (s.out->numMidiOut <= PlayerOutput::kMaxMidiOut);
    REQUIRE (s.out->numVoiceEvents > PlayerOutput::kMaxVoiceEvents - 16);

    s.run (48000, 64);                             // ordinary blocks again: playback carries on
    REQUIRE (s.overflows == 1);
    REQUIRE (s.voice.size() > 1000);
    s.host.stop();
    s.run (64, 64);

    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (midiBalance (s.midi).empty());
    REQUIRE (s.orderViolations == 0);
    REQUIRE (s.allocations == 0);
}

TEST_CASE ("Sequencer: reset is a panic, and nothing happens before prepare", "[sequencer]")
{
    SECTION ("before prepare")
    {
        Player player;
        auto bank = std::make_unique<PatternBank>();
        auto out = std::make_unique<PlayerOutput>();
        setSteps (bank->seq[0], "C D E F");
        TransportInfo t;
        t.valid = t.playing = t.ppqValid = t.bpmValid = true;
        player.process (t, {}, *bank, {}, {}, 512, *out);
        REQUIRE (out->numVoiceEvents == 0);
        REQUIRE (out->numMidiOut == 0);
        REQUIRE_FALSE (out->status.running);

        player.process (t, {}, *bank, {}, {}, 0, *out);      // an empty block is harmless too
        REQUIRE (out->numVoiceEvents == 0);
    }

    SECTION ("reset mid-note: AllOff for the voice, a note-off on MIDI out, then playback resumes")
    {
        Session s;
        setSteps (s.seq(), "C:SV D:S E:S F:S");
        s.host.play();
        s.run (kStep + 1000, 64);
        REQUIRE (s.status.gateOpen);
        const std::int64_t at = s.now;
        s.player.reset();
        s.run (64, 64);

        bool allOff = false;
        for (const Ev& e : s.voice)
            if (e.sample == at && e.type == VoiceEvent::Type::AllOff) allOff = true;
        REQUIRE (allOff);

        bool midiOff = false;
        for (const MidiEv& e : s.midi)
            if (e.sample == at && e.status == 0x80 && e.d1 == kC + 2) midiOff = true;
        REQUIRE (midiOff);

        s.run (4 * kStep, 64);
        REQUIRE (s.status.running);                // the host is still playing
        s.host.stop();
        s.run (64, 64);
        REQUIRE (voiceBalance (s.notes()).empty());
        REQUIRE (midiBalance (s.midi).empty());
        REQUIRE (s.allocations == 0);
    }

    SECTION ("prepare again mid-note is a panic too: nothing is left on downstream")
    {
        Session s;
        setSteps (s.seq(), "C:S");
        s.host.play();
        s.run (1000, 64);
        s.player.prepare (96000.0, 256);
        s.host.stop();
        s.host.sampleRate = 96000.0;
        s.run (64, 64);
        REQUIRE (s.notes().back().type == VoiceEvent::Type::AllOff);
        REQUIRE (s.notes().back().sample == 1000);
        REQUIRE (s.midiNotes().back() == MidiEv { 1000, 0x80, kC, 0 });
        REQUIRE_FALSE (s.status.gateOpen);
        REQUIRE (midiBalance (s.midi).empty());
    }

    SECTION ("the first prepare has nothing to turn off")
    {
        Session s;
        s.run (64, 64);
        REQUIRE (s.notes().empty());
        REQUIRE (s.midi.empty());
    }
}

TEST_CASE ("Sequencer: changing mode or trigger mode never leaves a note on", "[sequencer]")
{
    SECTION ("sequencer to external, mid-slide")
    {
        Session s;
        setSteps (s.seq(), "C:S D:S");
        s.host.play();
        s.run (kStep + 1000, 64);
        const std::int64_t at = s.now;
        s.ctl.playMode = PlayMode::External;
        s.run (4 * kStep, 64);
        REQUIRE (s.notes().back() == off (at));
        REQUIRE (s.midiNotes().back().sample == at);
        REQUIRE (voiceBalance (s.notes()).empty());
        REQUIRE (midiBalance (s.midi).empty());
    }

    SECTION ("external to sequencer with a key down")
    {
        Session s;
        s.ctl.playMode = PlayMode::External;
        setSteps (s.seq(), ". . . .");
        s.keyDown (100, 50);
        s.keyUp (20000, 50);                       // arrives after the mode change: ignored
        s.run (5000, 64);
        REQUIRE (s.status.gateOpen);
        s.ctl.playMode = PlayMode::Sequencer;
        s.run (30000, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (100, 50, true, false, true), off (5000) });
    }

    SECTION ("live to host note, mid-note")
    {
        Session s;
        setSteps (s.seq(), "C:S");
        s.host.play();
        s.run (1000, 64);
        s.cfg.triggerMode = TriggerMode::HostNote;
        s.run (4 * kStep, 64);
        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (1000) });
        REQUIRE_FALSE (s.status.running);
        s.cfg.triggerMode = TriggerMode::Live;     // and back: the host is playing, so it starts
        s.run (64, 64);
        REQUIRE (s.status.running);
    }

    SECTION ("the vibrato depth is announced on every mode change")
    {
        Session s;
        s.run (64, 64);
        REQUIRE (s.voice == std::vector<Ev> { { 0, VoiceEvent::Type::VibratoDepth, 0, false, false, false, 1.0f } });
        s.midiAt (100, 0xB0, 1, 127);              // the wheel is remembered but has no effect on patterns
        s.run (200, 64);
        REQUIRE (s.voice.size() == 1);
        s.ctl.playMode = PlayMode::External;
        s.run (64, 64);
        REQUIRE (s.voice.back() == Ev { 264, VoiceEvent::Type::VibratoDepth, 0, false, false, false, 1.0f });
        s.ctl.playMode = PlayMode::Arpeggiator;
        s.run (64, 64);
        REQUIRE (s.voice.back() == Ev { 328, VoiceEvent::Type::VibratoDepth, 0, false, false, false, 1.0f });
    }
}

TEST_CASE ("Sequencer: process is declared noexcept and allocates nothing", "[sequencer]")
{
    Player player;
    PlayerOutput* out = nullptr;
    const PatternBank* bank = nullptr;
    static_assert (noexcept (player.process (TransportInfo {}, std::span<const MidiMsg> {}, *bank, PlayerControls {}, PlayerConfig {}, 64, *out)));
    static_assert (noexcept (player.reset()));
    static_assert (std::is_trivially_copyable_v<PlayerConfig>);
    static_assert (std::is_trivially_copyable_v<PlayerControls>);

    // PlayerConfig mirrors the sequencer fields of Settings.
    acid::Settings settings;
    settings.triggerMode = TriggerMode::HostNote;
    settings.launchQuant = LaunchQuant::NextBar;
    settings.slideIntoRest = SlideIntoRest::HoldThroughRest;
    settings.notePriority = NotePriority::High;
    settings.followHostTransport = false;
    settings.hostLink = false;
    settings.restartOnTrigger = false;
    settings.hostNoteBase = 36;
    settings.accentThreshold = 99;
    settings.bendRange = 7;
    settings.midiOut = false;
    settings.midiThru = true;
    settings.outAccentVelocity = 120;
    settings.outNormalVelocity = 60;

    const PlayerConfig c = playerConfigFrom (settings);
    REQUIRE (c.triggerMode == TriggerMode::HostNote);
    REQUIRE (c.launchQuant == LaunchQuant::NextBar);
    REQUIRE (c.slideIntoRest == SlideIntoRest::HoldThroughRest);
    REQUIRE (c.notePriority == NotePriority::High);
    REQUIRE_FALSE (c.followHostTransport);
    REQUIRE_FALSE (c.hostLink);
    REQUIRE_FALSE (c.restartOnTrigger);
    REQUIRE (c.hostNoteBase == 36);
    REQUIRE (c.accentThreshold == 99);
    REQUIRE (c.bendRange == 7);
    REQUIRE_FALSE (c.midiOut);
    REQUIRE (c.midiThru);
    REQUIRE (c.outAccentVelocity == 120);
    REQUIRE (c.outNormalVelocity == 60);

    // The defaults are the defaults of Settings.
    const PlayerConfig d = playerConfigFrom (acid::Settings {});
    const PlayerConfig e;
    REQUIRE (std::memcmp (&d, &e, sizeof (PlayerConfig)) == 0);

    // A long, busy session through every mode allocates nothing.
    Session s;
    setSteps (s.seq(), "C:S D:A ~ . E:SV F G:S A");
    s.seq().steps[1].ratchet = 3;
    setSteps (s.arp(), "C:S C C:A .");
    s.host.play();
    s.run (48000, 64);
    s.ctl.playMode = PlayMode::Arpeggiator;
    s.keyDown (s.now + 10, 60);
    s.keyDown (s.now + 20, 64);
    s.keyUp (s.now + 30000, 60);
    s.keyUp (s.now + 30001, 64);
    s.run (48000, 64);
    s.ctl.playMode = PlayMode::External;
    s.cfg.midiThru = true;
    s.keyDown (s.now + 10, 60);
    s.keyDown (s.now + 2000, 64);
    s.keyUp (s.now + 3000, 64);
    s.keyUp (s.now + 4000, 60);
    s.run (48000, 64);
    REQUIRE (s.voice.size() > 20);
    REQUIRE (s.allocations == 0);
}
