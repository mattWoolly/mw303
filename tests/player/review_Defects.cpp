// Independent review: defects found in the player, each encoded as a test that failed
// against the implementation as reviewed. 120 BPM, 48 kHz unless stated: 1 quarter = 24000
// samples.
//
// After the fixes: the reproductions are kept as they were written and now pass. Three
// tests asked for something the design cannot give and were amended, each with a note at the
// test saying what changed and why. The wider regression tests for the same findings are in
// ReviewFixTests.cpp.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

namespace
{
std::vector<Ev> renderNextBar (int blockSize)
{
    // 3/4 time, quarter-note steps, 50 % shuffle: the bar line at 3.0 is grid step 3, which is
    // odd, so the new pattern's first step is delayed to 3.25 (sample 78000).
    Session s;
    s.host.timeSigNum = 3;
    s.host.timeSigDen = 4;
    s.cfg.launchQuant = LaunchQuant::NextBar;
    setSteps (s.seq (0), "C C C C C C C C");
    setSteps (s.seq (1), "A A A A A A A A");
    for (int i : { 0, 1 })
    {
        s.seq (i).div = StepDiv::d1_4;
        s.seq (i).shuffle = 50;
    }

    s.host.play();
    s.run (60000, 1000);              // 2.5 quarters in
    s.ctl.slot = 1;                   // queue the switch for the next bar line (3.0)
    s.run (240000 - 60000, blockSize);
    s.host.stop();
    s.run (64, 64);
    return s.noteOns();
}
}

TEST_CASE ("Review defect: NextBar launch depends on the block size when the first new step is shuffled", "[review][defect]")
{
    const std::vector<Ev> big = renderNextBar (40000);
    const std::vector<Ev> small = renderNextBar (64);

    // Where does the first A (note 45) play?
    auto firstA = [] (const std::vector<Ev>& v) -> std::int64_t
    {
        for (const Ev& e : v)
            if (e.note == 45) return e.sample;
        return -1;
    };

    INFO ("first A with 40000-sample blocks: " << firstA (big) << ", with 64-sample blocks: " << firstA (small));
    CHECK (firstA (big) == firstA (small));
    CHECK (big == small);
}

TEST_CASE ("Review defect: Note Clr does not mute a note held by SustainToNextNote", "[review][defect]")
{
    // "Note Clr mutes" (spec 6.4). Hold Note Clr while a slid note sounds: every following
    // step is a rest, and the SustainToNextNote policy then holds the gate for as long as the
    // mute is held, so the muted sequencer drones.
    Session s;
    s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
    setSteps (s.seq(), "C:S D E F");
    s.host.play();
    s.run (1000, 64);                 // C sounds, slide pending
    s.ctl.ovrNoteClr = true;          // mute
    s.run (16 * kStep, 64);           // four bars of mute

    INFO ("gate still open after four muted bars: " << s.status.gateOpen);
    CHECK_FALSE (s.status.gateOpen);

    s.ctl.ovrNoteClr = false;
    s.host.stop();
    s.run (64, 64);
}

TEST_CASE ("Review probe: a tie after a rest that a slide held open", "[review][defect]")
{
    // Was a hidden probe. Decided as the review proposed (the tie is silent) and now part of
    // the suite.
    // Document 06 section 9.5 item 7: a tie after a rest is silent. Under HoldThroughRest the
    // rest keeps the old pitch for the rest's gate time; does the tie then extend it further?
    Session s;
    s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest;
    setSteps (s.seq(), "C:S . ~ .");
    s.host.play();
    s.run (4 * kStep, 64);
    s.host.stop();
    s.run (64, 64);

    const auto n = s.notes();
    REQUIRE (n.size() >= 2);
    INFO ("NoteOff at " << n[1].sample << "; the rest's gate ends at " << kStep + kGate << ", the tie's at " << 2 * kStep + kGate);
    CHECK (n[1].sample == kStep + kGate);
}

TEST_CASE ("Review defect: a huge finite host position makes the step search return step 0", "[review][defect]")
{
    // Root cause of an unbounded loop in Player::advance: for |q / stepQ| >= 4e18 the search
    // gave up and returned 0, whose onset is ~q in the past. The player then fired steps
    // 0, 1, 2, ... one by one trying to catch up with q, and process() never returned
    // (measured: a host ppq of 3.9e18 or more hung process() indefinitely).
    //
    // Amended. The review's check was onsetQ (g) >= q / 2 for q = 5e18 with 1/16 steps. No
    // 64-bit step index can satisfy that: it needs g >= 1e19, and the largest int64 is
    // 9.2e18. What the search does now is saturate at kMaxStepIndex (it never returns 0 for a
    // far position), and the playhead tracker keeps such positions away from it altogether.
    const StepClock c { 0.25, 0.0, 0.0, 0 };
    const double q = 5.0e18;
    const std::int64_t g = c.firstStepAtOrAfter (q);
    INFO ("firstStepAtOrAfter (5e18) = " << g << ", onset " << c.onsetQ (g));
    CHECK (g == kMaxStepIndex);
    CHECK (c.firstStepAtOrAfter (-q) == -kMaxStepIndex);

    // At the edge of what the tracker lets through, the search is exact.
    const double edge = PlayheadTracker::kMaxAbsPpq;
    const std::int64_t e = c.firstStepAtOrAfter (edge);
    CHECK (c.onsetQ (e) >= edge);
    CHECK (c.onsetQ (e - 1) < edge);
}

TEST_CASE ("Review defect (hung): Player::process with a host ppq of 5e18 returns", "[review][defect]")
{
    // Was hidden because it never returned. It returns now, and the pattern plays as if the
    // host had given no position: one note per step, counted in samples.
    Session s;
    setSteps (s.seq(), "C C C C");
    s.host.locate (5.0e18);
    s.host.play();
    s.run (512, 512);
    CHECK (s.noteOns().size() == 1);
    s.run (4 * kStep - 512, 512);
    CHECK (s.noteOns().size() == 4);
    s.host.stop();
    s.run (64, 64);
    CHECK (voiceBalance (s.notes()).empty());
}

TEST_CASE ("Review probe: an oversized block of dense ratchets overflows the fixed output", "[review][defect]")
{
    // Player.h used to say "Blocks larger than the prepared size are fine." 1/64 steps with 4
    // ratchets at 300 BPM in one 65536-sample block at 44.1 kHz is about 119 steps, 950 voice
    // events, and the fixed output holds 512.
    //
    // Amended. The output arrays are a contract type of fixed size, so one process() call
    // cannot carry 950 events whatever the player does. The header now states the limit and
    // gives the block length that can never overflow (Player::maxBlockWithoutOverflow). The
    // probe checked that the oversized block does not overflow. It now checks what is
    // promised: the oversized block is flagged, and the same span in blocks of the
    // documented size is complete.
    auto make = [] (Session& s)
    {
        setSteps (s.seq(), "C C C C C C C C");
        s.seq().div = StepDiv::d1_64;
        for (auto& st : s.seq().steps) st.ratchet = 4;
        s.host.bpm = 300.0;
        s.host.play();
    };

    Session whole (44100.0, 512);
    make (whole);
    whole.runBlock (65536);
    INFO ("voice events " << whole.voice.size() << ", overflowed blocks " << whole.overflows);
    CHECK (whole.overflows == 1);

    Session cut (44100.0, 512);
    make (cut);
    cut.run (65536, Player::maxBlockWithoutOverflow (44100.0));
    INFO ("cut into blocks of " << Player::maxBlockWithoutOverflow (44100.0) << ": voice events " << cut.voice.size());
    CHECK (cut.overflows == 0);
    CHECK (cut.noteOns().size() >= 4 * 118);
}
