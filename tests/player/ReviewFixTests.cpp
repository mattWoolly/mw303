// Regression tests for the defects an independent review found in the player. Each section
// pins the corrected behaviour. 120 BPM and 48 kHz unless stated: one quarter note is 24000
// samples, a 1/16 step 6000.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace ptest;

namespace
{
constexpr std::int64_t kQuarter = 24000;
constexpr int kA = kC + 9;    // the note the queued pattern plays first in these tests

void requireClean (const Session& s)
{
    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (midiBalance (s.midi).empty());
    REQUIRE (s.orderViolations == 0);
    REQUIRE (s.allocations == 0);
    REQUIRE (s.overflows == 0);
}

std::int64_t firstOnOf (const Session& s, int note)
{
    for (const Ev& e : s.noteOns())
        if (e.note == note) return e.sample;

    return -1;
}

/** The most NoteOns that share one sample: more than one is a burst of late steps. */
int mostNoteOnsOnOneSample (const Session& s)
{
    int most = 0, run = 0;
    std::int64_t at = -1;

    for (const Ev& e : s.noteOns())
    {
        run = e.sample == at ? run + 1 : 1;
        at = e.sample;
        most = std::max (most, run);
    }

    return most;
}

//==============================================================================
struct NextBarCase
{
    const char* name;
    int num, den;
    StepDiv div;
    int shuffleOld, shuffleNew;
    double barQ, stepQ;
};

const NextBarCase kNextBarCases[] = {
    { "3/4, quarter steps, shuffle 50",         3, 4, StepDiv::d1_4, 50, 50,  3.0, 1.0 },
    { "3/4, quarter steps, only the new swung", 3, 4, StepDiv::d1_4, 0,  100, 3.0, 1.0 },
    { "3/4, quarter steps, only the old swung", 3, 4, StepDiv::d1_4, 70, 0,   3.0, 1.0 },
    { "7/8, eighth steps, shuffle 100",         7, 8, StepDiv::d1_8, 100, 100, 3.5, 0.5 },
    { "7/8, eighth steps, shuffle 30 into 80",  7, 8, StepDiv::d1_8, 30, 80,  3.5, 0.5 },
    { "4/4, whole-note steps, shuffle 50",      4, 4, StepDiv::d1_1, 50, 50,  4.0, 4.0 },
    { "4/4, sixteenth steps, shuffle 60",       4, 4, StepDiv::d1_16, 60, 60, 4.0, 0.25 },
};

/** Plays slot 0, queues slot 1 at queueAt samples under NextBar, and plays on. Every change
    falls on a block boundary in every blocking, as it does in a host. */
void renderNextBar (Session& s, const NextBarCase& c, std::int64_t queueAt, std::int64_t total, int blockSize, bool hostLink)
{
    s.host.timeSigNum = c.num;
    s.host.timeSigDen = c.den;
    s.cfg.launchQuant = LaunchQuant::NextBar;
    s.cfg.hostLink = hostLink;
    setSteps (s.seq (0), "C D:S E F:A G D:S E C+");
    setSteps (s.seq (1), "A B:S A+ B");
    s.seq (0).div = s.seq (1).div = c.div;
    s.seq (0).shuffle = static_cast<std::uint8_t> (c.shuffleOld);
    s.seq (1).shuffle = static_cast<std::uint8_t> (c.shuffleNew);

    s.host.play();
    s.run (queueAt, blockSize);
    s.ctl.slot = 1;
    s.run (total - queueAt, blockSize);
    s.host.stop();
    s.run (64, 64);
}
}

//==============================================================================
// Finding 1: NextBar launch.
//==============================================================================
TEST_CASE ("Review fix: a NextBar launch lands on the first bar line after the queue instant, in any blocking", "[reviewfix][nextbar]")
{
    for (const NextBarCase& c : kNextBarCases)
    {
        const auto barSamples = static_cast<std::int64_t> (std::llround (c.barQ * static_cast<double> (kQuarter)));
        const double swingNewQ = 0.5 * (c.shuffleNew / 100.0) * c.stepQ;
        const double swingOldQ = 0.5 * (c.shuffleOld / 100.0) * c.stepQ;
        const auto swingOld = static_cast<std::int64_t> (std::llround (swingOldQ * static_cast<double> (kQuarter)));

        // Queue instants around the first bar line: well before it, one sample before it, on
        // it, between it and a delayed first step, and well after it.
        const std::int64_t queueTimes[] = { barSamples * 2 / 5, barSamples - 1, barSamples,
                                            barSamples + std::max<std::int64_t> (1, swingOld / 2),
                                            barSamples + barSamples / 3 };

        for (const std::int64_t queueAt : queueTimes)
        {
            for (const bool hostLink : { true, false })
            {
                // The first bar line that has not passed when the pattern is queued.
                const std::int64_t bars = (queueAt + barSamples - 1) / barSamples;
                const double lineQ = static_cast<double> (bars) * c.barQ;
                const auto gridIndex = static_cast<std::int64_t> (std::llround (lineQ / c.stepQ));
                const double onsetQ = lineQ + ((gridIndex % 2 != 0) ? swingNewQ : 0.0);
                const std::int64_t expected = sampleOf (onsetQ, 120.0L, 48000.0L);
                const std::int64_t total = (bars + 2) * barSamples;

                Session reference;
                renderNextBar (reference, c, queueAt, total, 64, hostLink);

                INFO (c.name << ", queued at sample " << queueAt << ", hostLink " << hostLink);
                REQUIRE (firstOnOf (reference, kA) == expected);
                REQUIRE (mostNoteOnsOnOneSample (reference) == 1);
                requireClean (reference);

                for (const int blockSize : { 1, 7, 1000, 4096, 40000 })
                {
                    Session other;
                    renderNextBar (other, c, queueAt, total, blockSize, hostLink);

                    INFO ("block size " << blockSize);
                    REQUIRE (other.voice == reference.voice);
                    REQUIRE (other.midi == reference.midi);
                    REQUIRE (other.allocations == 0);
                }
            }
        }
    }
}

TEST_CASE ("Review fix: queued between a bar line and its delayed step, the launch waits for the next bar", "[reviewfix][nextbar]")
{
    // 3/4, quarter steps, 50 % shuffle: bar line 3.0 is an odd grid step, played at 3.25.
    // Queued at 3.1, the old pattern still plays that step, and the new one starts on bar
    // line 6.0, an even grid step, so on the line.
    Session s;
    renderNextBar (s, kNextBarCases[0], kQuarter * 31 / 10, 9 * kQuarter, 64, true);

    const auto ons = s.noteOns();
    REQUIRE (ons.size() >= 7);
    REQUIRE (ons[3].sample == 3 * kQuarter + kQuarter / 4);
    REQUIRE (ons[3].note == kC + 5);                       // the old pattern's fourth step
    REQUIRE (firstOnOf (s, kA) == 6 * kQuarter);
    requireClean (s);
}

TEST_CASE ("Review fix: the gate-off look-ahead and the NextBar boundary agree on a shuffled launch", "[reviewfix][nextbar]")
{
    // The old pattern's third step (2.0) carries a slide. Its gate-off decision falls after
    // the pattern is queued, so it looks ahead into the queued pattern, whose first step is
    // the delayed one at 3.25.
    auto render = [] (const char* newPattern, int blockSize)
    {
        Session s;
        s.host.timeSigNum = 3;
        s.cfg.launchQuant = LaunchQuant::NextBar;
        setSteps (s.seq (0), "C C C:S C C C");
        setSteps (s.seq (1), newPattern);

        for (int i : { 0, 1 })
        {
            s.seq (i).div = StepDiv::d1_4;
            s.seq (i).shuffle = 50;
        }

        s.host.play();
        s.run (2 * kQuarter + 100, blockSize);
        s.ctl.slot = 1;
        s.run (6 * kQuarter - s.now, blockSize);
        s.host.stop();
        s.run (64, 64);
        return s.notes();
    };

    const std::int64_t launch = 3 * kQuarter + kQuarter / 4;      // 78000
    const std::int64_t gateOff = 2 * kQuarter + kQuarter / 2 + 65;   // half a quarter step plus 1.35 ms

    for (const int blockSize : { 1, 64, 1000, 40000 })
    {
        INFO ("block size " << blockSize);

        // Into a note: the slide holds the gate and the new pattern's first note is legato.
        const auto intoNote = render ("A A A", blockSize);
        REQUIRE (intoNote.size() > 4);
        REQUIRE (intoNote[4] == on (2 * kQuarter, kC));
        REQUIRE (intoNote[5] == on (launch, kA, false, true));
        REQUIRE (voiceBalance (intoNote).empty());

        // Into a rest (Release policy): the slide is dropped at the normal gate time.
        const auto intoRest = render (". A A", blockSize);
        REQUIRE (intoRest.size() > 5);
        REQUIRE (intoRest[5] == off (gateOff));
        REQUIRE (voiceBalance (intoRest).empty());
    }
}

TEST_CASE ("Review fix: the NextBar line follows what it was worked out from", "[reviewfix][nextbar]")
{
    auto longPattern = [] (Session& s)
    {
        for (auto& st : s.seq (0).steps)
        {
            st.state = StepState::Note;
            st.note = 0;
        }

        s.seq (0).length = 64;
        setSteps (s.seq (1), "A A A A");
        s.cfg.launchQuant = LaunchQuant::NextBar;
    };

    SECTION ("switching the quantisation to NextBar long after queueing does not launch in the past")
    {
        Session s;
        longPattern (s);
        s.cfg.launchQuant = LaunchQuant::EndOfPattern;
        s.host.play();
        s.run (10000, 64);
        s.ctl.slot = 1;                                // queued for the end of the four-bar pattern
        s.run (220800 - s.now, 64);                    // 2.3 bars in
        s.cfg.launchQuant = LaunchQuant::NextBar;
        s.run (4 * 96000 - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (firstOnOf (s, kA) == 3 * 96000);
        REQUIRE (mostNoteOnsOnOneSample (s) == 1);
        requireClean (s);
    }

    SECTION ("a line from an earlier spell of NextBar is not reused")
    {
        Session s;
        longPattern (s);
        s.host.play();
        s.run (9600, 64);
        s.ctl.slot = 1;                                // NextBar: the line is bar 1
        s.run (9600, 64);
        s.cfg.launchQuant = LaunchQuant::EndOfPattern; // changed our mind: wait for the pattern end (bar 4)
        s.run (220800 - s.now, 64);                    // 2.3 bars in, bar 1 long gone
        s.cfg.launchQuant = LaunchQuant::NextBar;      // and back: the next bar line from here is bar 3
        s.run (4 * 96000 - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (firstOnOf (s, kA) == 3 * 96000);
        REQUIRE (mostNoteOnsOnOneSample (s) == 1);
        requireClean (s);
    }

    SECTION ("a pattern queued after a launch waits for its own bar line")
    {
        Session s;
        longPattern (s);
        s.host.play();
        s.run (48000, 64);
        s.ctl.slot = 1;                                // launches on bar 1
        s.run (124800 - s.now, 64);                    // 1.3 bars in
        REQUIRE (s.status.playingSlot == 1);
        s.ctl.slot = 0;                                // back to the first pattern: on bar 2
        s.run (3 * 96000 - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        std::vector<std::int64_t> a, c;
        for (const Ev& e : s.noteOns()) (e.note == kA ? a : c).push_back (e.sample);

        REQUIRE (a.size() == 16);                      // exactly bar 1, in 1/16 steps
        REQUIRE (a.front() == 96000);
        REQUIRE (a.back() == 2 * 96000 - kStep);
        REQUIRE (c.size() == 32);                      // bar 0 and bar 2
        REQUIRE (c[16] == 2 * 96000);
        REQUIRE (mostNoteOnsOnOneSample (s) == 1);
        requireClean (s);
    }

    SECTION ("a time signature change moves the line to the new bar grid")
    {
        Session s;
        longPattern (s);
        s.host.play();
        s.run (kQuarter / 2, 64);
        s.ctl.slot = 1;                                // 4/4: the line is 4.0
        s.run (kQuarter / 2, 64);
        s.host.timeSigNum = 3;                         // 3/4 from 1.0 on: the line is 3.0
        s.host.changed = true;
        s.run (5 * kQuarter - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (firstOnOf (s, kA) == 3 * kQuarter);
        REQUIRE (mostNoteOnsOnOneSample (s) == 1);
        requireClean (s);
    }

    SECTION ("a host-locked relocation moves the line with the song")
    {
        for (const double target : { 9.0, 0.0 })
        {
            Session s;
            longPattern (s);
            s.host.locate (5.0);
            s.host.play();
            s.run (kQuarter / 2, 64);
            s.ctl.slot = 1;                            // at 5.5: the line is 8.0
            s.run (kQuarter / 2, 64);
            s.host.locate (target);                    // at 6.0 the song jumps
            s.run (4 * kQuarter, 64);
            s.host.stop();
            s.run (64, 64);

            // From 9.0 the next bar line is 12.0, three quarters after the jump. A jump onto
            // bar line 0.0 launches there and then.
            INFO ("relocated to " << target);
            REQUIRE (firstOnOf (s, kA) == kQuarter + (target > 0.0 ? 3 * kQuarter : 0));
            REQUIRE (mostNoteOnsOnOneSample (s) == 1);
            requireClean (s);
        }
    }

    SECTION ("queueing another pattern keeps the same bar line while it has not passed")
    {
        Session s;
        longPattern (s);
        setSteps (s.seq (2), "A A");
        s.host.play();
        s.run (kQuarter, 64);
        s.ctl.slot = 1;
        s.run (kQuarter, 64);
        s.ctl.slot = 2;
        s.run (6 * kQuarter - s.now, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (firstOnOf (s, kA) == 4 * kQuarter);
        REQUIRE (s.status.playingSlot == 2);
        requireClean (s);
    }
}

//==============================================================================
// Finding 2: absurd host positions.
//==============================================================================
TEST_CASE ("Review fix: the step search is bounded and saturates outside its range", "[reviewfix][hugeppq]")
{
    const StepClock plain { 0.25, 0.0, 0.0, 0 };
    const StepClock swung { 0.0625, 0.03125, 3.0, 7 };
    const double inf = std::numeric_limits<double>::infinity();

    for (const StepClock& c : { plain, swung })
    {
        // Out of range: saturated, never 0 (which made the caller walk towards q for ever).
        for (const double q : { 5.0e18, 1.0e30, 1.0e300, inf })
        {
            REQUIRE (c.firstStepAtOrAfter (q) == kMaxStepIndex);
            REQUIRE (c.firstStepAtOrAfter (-q) == -kMaxStepIndex);
            REQUIRE (c.onsetQ (c.firstStepAtOrAfter (-q)) >= -q);
        }

        REQUIRE (c.firstStepAtOrAfter (std::numeric_limits<double>::quiet_NaN()) == 0);

        // In range, however far out, the answer is exact: the step at or after q, and the
        // one before it starts before q.
        for (const double q : { -1.0e12, -1.0e9, -12345.678, 0.0, 0.3, 98765.4321, 1.0e9, 1.0e12 })
        {
            const std::int64_t g = c.firstStepAtOrAfter (q);
            INFO ("q " << q << " g " << g);
            REQUIRE (c.onsetQ (g) >= q);
            REQUIRE (c.onsetQ (g - 1) < q);
            REQUIRE (c.stepAt (q) <= g);
        }
    }

    // The grid index of an anchor follows the same limit.
    REQUIRE (gridIndexFor (1.0e9, 0.25) == 4000000000LL);
    REQUIRE (gridIndexFor (5.0e18, 0.25) == 0);
}

TEST_CASE ("Review fix: the tracker treats an absurd host position as no position", "[reviewfix][hugeppq]")
{
    const double inf = std::numeric_limits<double>::infinity();
    const double qps = 120.0 / (60.0 * 48000.0);

    for (const double bad : { 1.0e15, 3.9e18, 5.0e18, -5.0e18, 1.0e300, -1.0e300, inf, -inf, std::numeric_limits<double>::quiet_NaN(),
                              PlayheadTracker::kMaxAbsPpq * 1.0000001 })
    {
        PlayheadTracker tracker;
        tracker.prepare (48000.0);

        TransportInfo t;
        t.valid = true;
        t.playing = true;
        t.ppqValid = true;
        t.bpmValid = true;
        t.bpm = 120.0;
        t.ppq = 8.0;
        tracker.process (t, 480, 120.0);

        // The garbage arrives while playing: the clock carries on from where it was, with
        // no jump.
        for (int block = 1; block < 50; ++block)
        {
            t.ppq = bad;
            const ClockBlock& b = tracker.process (t, 480, 120.0);

            INFO ("bad position " << bad << ", block " << block);
            REQUIRE (b.numSegments == 1);
            REQUIRE (b.segments[0].startPpq == 8.0 + static_cast<double> (block * 480) * qps);
            REQUIRE_FALSE (b.segments[0].discontinuity);
            REQUIRE_FALSE (b.relocated);
            REQUIRE (b.hostRolling);
            REQUIRE (PlayheadTracker::isUsablePpq (tracker.position()));
        }

        // Garbage loop points are not trusted either.
        t.ppq = 8.0 + 50.0 * 480.0 * qps;
        t.looping = true;
        t.loopStartPpq = bad;
        t.loopEndPpq = t.ppq + 100.0 * qps;
        const ClockBlock& b = tracker.process (t, 480, 120.0);
        REQUIRE (b.numSegments == 1);
        REQUIRE (PlayheadTracker::isUsablePpq (tracker.position()));
    }

    // The edge of the range itself is a position like any other.
    REQUIRE (PlayheadTracker::isUsablePpq (PlayheadTracker::kMaxAbsPpq));
    REQUIRE (PlayheadTracker::isUsablePpq (-PlayheadTracker::kMaxAbsPpq));
}

TEST_CASE ("Review fix: the tracker's own position never leaves the range", "[reviewfix][hugeppq]")
{
    PlayheadTracker tracker;
    tracker.prepare (48000.0);

    TransportInfo t;
    t.valid = true;
    t.playing = true;
    t.ppqValid = true;
    t.bpmValid = true;
    t.bpm = 120.0;
    t.ppq = PlayheadTracker::kMaxAbsPpq - 1.0;      // legal, one quarter from the end
    tracker.process (t, 48000, 120.0);              // two quarters: the block runs past the end

    TransportInfo none;                             // no news: extrapolated
    const ClockBlock& b = tracker.process (none, 48000, 120.0);
    REQUIRE (b.numSegments == 1);
    REQUIRE (b.segments[0].startPpq == 0.0);
    REQUIRE (b.segments[0].discontinuity);
    REQUIRE (b.relocated);
    REQUIRE (tracker.position() == 48000.0 * (120.0 / (60.0 * 48000.0)));
}

TEST_CASE ("Review fix: process() returns, and plays normally, whatever position the host reports", "[reviewfix][hugeppq]")
{
    const double inf = std::numeric_limits<double>::infinity();

    Session normal;
    setSteps (normal.seq(), "C D:S E F:A");
    normal.host.play();
    normal.run (16 * kStep, 512);
    normal.host.stop();
    normal.run (64, 64);

    for (const bool hostLink : { true, false })
    {
        for (const double bad : { 1.0e15, 1.0e17, 3.9e18, 5.0e18, -5.0e18, 1.0e300, inf })
        {
            // The host sits at a garbage position from the first block on. The player counts
            // samples instead, from 0, so the output is the ordinary one.
            Session s;
            s.cfg.hostLink = hostLink;
            setSteps (s.seq(), "C D:S E F:A");
            s.host.locate (bad);
            s.host.play();
            s.run (16 * kStep, 512);
            s.host.stop();
            s.run (64, 64);

            INFO ("host position " << bad << ", hostLink " << hostLink);
            REQUIRE (s.voice == normal.voice);
            REQUIRE (s.midi == normal.midi);
            requireClean (s);
        }
    }

    SECTION ("in the arpeggiator and with garbage arriving mid-song")
    {
        Session s;
        s.ctl.playMode = PlayMode::Arpeggiator;
        setSteps (s.arp(), "C C:S C C");
        s.host.play();
        s.keyDown (100, 60);
        s.run (4 * kStep, 64);
        s.host.locate (1.0e300);                        // from here on the host talks nonsense
        s.run (8 * kStep, 64);
        s.keyUp (s.now, 60);
        s.run (2 * kStep, 64);

        REQUIRE (s.noteOns().size() >= 11);
        REQUIRE (s.noteOns().size() <= 12);
        REQUIRE (mostNoteOnsOnOneSample (s) == 1);
        requireClean (s);
    }

    SECTION ("a song position at the very end of the range wraps and playback carries on")
    {
        for (const bool hostLink : { true, false })
        {
            Session s;
            s.cfg.hostLink = hostLink;
            setSteps (s.seq(), "C D:S E F:A");
            s.host.locate (PlayheadTracker::kMaxAbsPpq - 2.0);
            s.host.play();
            s.run (32 * kStep, 512);                    // two quarters in, the host leaves the range
            s.host.stop();
            s.run (64, 64);

            // One note per step throughout, give or take the step lost at the wrap.
            INFO ("hostLink " << hostLink);
            REQUIRE (s.noteOns().size() >= 30);
            REQUIRE (s.noteOns().size() <= 33);
            REQUIRE (mostNoteOnsOnOneSample (s) == 1);
            requireClean (s);
        }
    }
}

TEST_CASE ("Review fix: the step loop puts a bounded number of events on a sample", "[reviewfix][hugeppq]")
{
    // An absurdly low sample rate makes steps shorter than a sample: here four steps of four
    // ratchet hits each want to share every sample. The step loop takes a bounded handful
    // per sample, skips what it cannot play, returns, and leaves nothing on. This is the
    // last-resort bound on the loop: it holds whatever the state.
    Session s (100.0, 64);
    setSteps (s.seq(), "C D E F");
    s.seq().div = StepDiv::d1_64;
    s.seq().divMod = DivMod::Triplet;
    for (auto& st : s.seq().steps) st.ratchet = 4;
    s.host.bpm = 1000.0;
    s.host.play();
    s.run (400, 8);
    s.host.stop();
    s.run (8, 8);

    int most = 0, run = 0;
    std::int64_t at = -1;

    for (const Ev& e : s.notes())
    {
        run = e.sample == at ? run + 1 : 1;
        at = e.sample;
        most = std::max (most, run);
    }

    INFO ("most note events on one sample: " << most);
    REQUIRE (most <= 2 * 8);                       // eight engine events, each at most a NoteOff and a NoteOn
    REQUIRE (s.noteOns().size() > 400);            // and it does keep playing
    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (midiBalance (s.midi).empty());
    REQUIRE (s.orderViolations == 0);
    REQUIRE (s.allocations == 0);
    REQUIRE (s.overflows == 0);
}

//==============================================================================
// Finding 3: Note Clr is a hard mute.
//==============================================================================
TEST_CASE ("Review fix: Note Clr mutes under every slide-into-rest policy", "[reviewfix][noteclr]")
{
    const SlideIntoRest policies[] = { SlideIntoRest::Release, SlideIntoRest::HoldThroughRest, SlideIntoRest::SustainToNextNote };

    for (const SlideIntoRest policy : policies)
    {
        INFO ("policy " << static_cast<int> (policy));

        SECTION ("pressed while a slid note sounds: it ends at its own gate time " + std::to_string (static_cast<int> (policy)))
        {
            Session s;
            s.cfg.slideIntoRest = policy;
            setSteps (s.seq(), "C:S D E F");
            s.host.play();
            s.run (1000, 64);
            s.ctl.ovrNoteClr = true;
            s.run (16 * kStep, 64);                    // four bars of mute

            REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kGate) });
            REQUIRE_FALSE (s.status.gateOpen);
            REQUIRE (s.status.running);

            // Released: the pattern is heard again from the next step.
            s.ctl.ovrNoteClr = false;
            s.run (4 * kStep - 1000, 64);
            s.host.stop();
            s.run (64, 64);
            REQUIRE (s.noteOns().size() == 4);         // steps 17, 18 and 19; step 16 fell inside the mute
            REQUIRE (s.noteOns()[1].sample == 17 * kStep);
            requireClean (s);
        }

        SECTION ("pressed after the slide has held the gate: it closes at the next boundary " + std::to_string (static_cast<int> (policy)))
        {
            Session s;
            s.cfg.slideIntoRest = policy;
            setSteps (s.seq(), "C:S D E F");
            s.host.play();
            s.run (kGate + 500, 64);                   // past the gate time: the slide is holding
            REQUIRE (s.status.gateOpen);
            s.ctl.ovrNoteClr = true;
            s.run (8 * kStep, 64);

            REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kStep) });
            s.ctl.ovrNoteClr = false;
            s.host.stop();
            s.run (64, 64);
            requireClean (s);
        }

        SECTION ("pressed while a tie chain sounds " + std::to_string (static_cast<int> (policy)))
        {
            Session s;
            s.cfg.slideIntoRest = policy;
            setSteps (s.seq(), "C ~ ~ ~ D");
            s.host.play();
            s.run (kStep + 1000, 64);                  // inside the first tie
            s.ctl.ovrNoteClr = true;
            s.run (8 * kStep, 64);

            REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kStep + kGate) });
            s.ctl.ovrNoteClr = false;
            s.host.stop();
            s.run (64, 64);
            requireClean (s);
        }
    }

    SECTION ("a gate sustained across rests closes at the first muted boundary")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
        setSteps (s.seq(), "C:S . . . . . . D");
        s.host.play();
        s.run (2 * kStep + 3000, 64);
        REQUIRE (s.status.gateOpen);                   // droning across the rests, as the policy says
        s.ctl.ovrNoteClr = true;
        s.run (16 * kStep, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (3 * kStep) });
        s.ctl.ovrNoteClr = false;
        s.host.stop();
        s.run (64, 64);
        requireClean (s);
    }

    SECTION ("ratchet hits that are left do not sound")
    {
        Session s;
        setSteps (s.seq(), "C D");
        s.seq().steps[0].ratchet = 4;                  // hits at 0, 1500, 3000, 4500
        s.host.play();
        s.run (2000, 64);
        s.ctl.ovrNoteClr = true;
        s.run (8 * kStep, 64);

        const auto notes = s.notes();
        REQUIRE (notes.size() == 4);
        REQUIRE (notes[2] == on (1500, kC));
        REQUIRE (notes[3].type == VoiceEvent::Type::NoteOff);
        REQUIRE (notes[3].sample < 3000);
        s.ctl.ovrNoteClr = false;
        s.host.stop();
        s.run (64, 64);
        requireClean (s);
    }

    SECTION ("in the arpeggiator")
    {
        Session s;
        s.ctl.playMode = PlayMode::Arpeggiator;
        s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
        setSteps (s.arp(), "C:S C:S");
        s.keyDown (0, 60);
        s.run (1000, 64);
        s.ctl.ovrNoteClr = true;
        s.run (8 * kStep, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, 60), off (kGate) });
        s.ctl.ovrNoteClr = false;
        s.keyUp (s.now, 60);
        s.run (kStep, 64);
        requireClean (s);
    }

    SECTION ("the mute is block-size invariant")
    {
        auto render = [] (int blockSize)
        {
            Session s;
            s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
            setSteps (s.seq(), "C:S D:S . E:A ~ F:S");
            s.seq().steps[3].ratchet = 3;
            s.host.play();
            s.run (7000, blockSize);
            s.ctl.ovrNoteClr = true;
            s.run (9000, blockSize);
            s.ctl.ovrNoteClr = false;
            s.run (11000, blockSize);
            s.ctl.ovrNoteClr = true;
            s.run (2500, blockSize);
            s.ctl.ovrNoteClr = false;
            s.run (30000, blockSize);
            s.host.stop();
            s.run (64, 64);
            REQUIRE (voiceBalance (s.notes()).empty());
            REQUIRE (midiBalance (s.midi).empty());
            return s.voice;
        };

        const auto reference = render (64);
        for (const int blockSize : { 1, 500, 4096 }) REQUIRE (render (blockSize) == reference);
    }
}

//==============================================================================
// Finding 4: the fixed output and oversized blocks.
//==============================================================================
TEST_CASE ("Review fix: the densest pattern never overflows a block of the documented size", "[reviewfix][overflow]")
{
    for (const double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const int block = Player::maxBlockWithoutOverflow (sampleRate);
        REQUIRE (block == static_cast<int> (sampleRate / 8.0));

        // Plain notes make the most voice events (every hit is a NoteOff and a NoteOn);
        // slides, accent changes and vibrato make the most MIDI out.
        struct Dense { StepDiv div; DivMod mod; int shuffle; const char* steps; int atLeast; };

        for (const Dense d : { Dense { StepDiv::d1_64, DivMod::Triplet, 0, "C D E F G A B C+", 400 },
                               Dense { StepDiv::d1_64, DivMod::Triplet, 0, "C:AV D:S D:A E:V F:AS F G:V A", 350 },
                               Dense { StepDiv::d1_64, DivMod::Full, 100, "C D E F G A B C+", 250 },
                               Dense { StepDiv::d1_64, DivMod::Full, 100, "C:AV D:S D:A E:V F:AS F G:V A", 250 } })
        {
            Session s (sampleRate, 512);
            setSteps (s.seq(), d.steps);
            s.seq().div = d.div;
            s.seq().divMod = d.mod;
            s.seq().shuffle = static_cast<std::uint8_t> (d.shuffle);
            for (auto& st : s.seq().steps) st.ratchet = 4;
            s.host.bpm = 1000.0;                       // the tracker's ceiling
            s.host.play();

            int mostVoice = 0, mostMidi = 0;

            for (int i = 0; i < 40; ++i)
            {
                s.runBlock (block);
                mostVoice = std::max (mostVoice, s.out->numVoiceEvents);
                mostMidi = std::max (mostMidi, s.out->numMidiOut);
            }

            s.host.stop();
            s.run (64, 64);

            INFO ("rate " << sampleRate << ": most voice events " << mostVoice << ", most MIDI " << mostMidi);
            REQUIRE (mostVoice >= d.atLeast);          // it really is dense
            REQUIRE (mostVoice <= PlayerOutput::kMaxVoiceEvents - 8);
            REQUIRE (mostMidi <= PlayerOutput::kMaxMidiOut - 16);
            requireClean (s);
        }
    }

    REQUIRE (Player::maxBlockWithoutOverflow (0.0) == 1);
}

TEST_CASE ("Review fix: an oversized block overflows safely, and cut to the documented size it loses nothing", "[reviewfix][overflow]")
{
    // The review's case: 1/64 steps, four ratchets, 300 BPM, 65536 samples at 44.1 kHz, which
    // is about 119 steps and 950 voice events.
    auto make = [] (Session& s)
    {
        setSteps (s.seq(), "C C C C C C C C");
        s.seq().div = StepDiv::d1_64;
        for (auto& st : s.seq().steps) st.ratchet = 4;
        s.host.bpm = 300.0;
        s.host.play();
    };

    Session whole (44100.0, 512), cut (44100.0, 512), small (44100.0, 512);
    make (whole);
    make (cut);
    make (small);

    whole.runBlock (65536);
    cut.run (65536, Player::maxBlockWithoutOverflow (44100.0));
    small.run (65536, 64);

    // In one block: flagged, truncated, and nothing left on.
    REQUIRE (whole.overflows == 1);
    REQUIRE (whole.voice.size() < small.voice.size());
    whole.host.stop();
    whole.run (64, 64);
    REQUIRE (voiceBalance (whole.notes()).empty());
    REQUIRE (midiBalance (whole.midi).empty());

    // Cut up by the caller: complete.
    REQUIRE (cut.overflows == 0);
    REQUIRE (cut.voice == small.voice);
    REQUIRE (cut.midi == small.midi);
    REQUIRE (cut.noteOns().size() > 450);
}

//==============================================================================
// Finding 5: a tie after a rest that a slide held open.
//==============================================================================
TEST_CASE ("Review fix: a tie after a rest is silent, also when HoldThroughRest held the rest open", "[reviewfix][tie]")
{
    SECTION ("HoldThroughRest: the old pitch ends at the rest's gate time, not the tie's")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest;
        setSteps (s.seq(), "C:S . ~ ~ D");
        s.host.play();
        s.run (5 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kStep + kGate), on (4 * kStep, kC + 2), off (4 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("HoldThroughRest: a tie before the rest still holds (edge case 4 is unchanged)")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest;
        setSteps (s.seq(), "C:S ~ . ~ D");
        s.host.play();
        s.run (5 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (2 * kStep + kGate), on (4 * kStep, kC + 2), off (4 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("HoldThroughRest: the flag does not leak into the next note's ties")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::HoldThroughRest;
        setSteps (s.seq(), "C:S . D ~ ~ .");
        s.host.play();
        s.run (6 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kStep + kGate), on (2 * kStep, kC + 2), off (4 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("SustainToNextNote still holds across rests and ties until the next note")
    {
        Session s;
        s.cfg.slideIntoRest = SlideIntoRest::SustainToNextNote;
        setSteps (s.seq(), "C:S . ~ . D");
        s.host.play();
        s.run (5 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), on (4 * kStep, kC + 2, false, true), off (4 * kStep + kGate) });
        requireClean (s);
    }

    SECTION ("Release: the slide never reaches the rest, and the tie is silent")
    {
        Session s;
        setSteps (s.seq(), "C:S . ~ . D");
        s.host.play();
        s.run (5 * kStep, 64);
        s.host.stop();
        s.run (64, 64);

        REQUIRE (s.notes() == std::vector<Ev> { on (0, kC), off (kGate), on (4 * kStep, kC + 2), off (4 * kStep + kGate) });
        requireClean (s);
    }
}

//==============================================================================
// Finding 6: arpeggiator notes below the MIDI range.
//==============================================================================
TEST_CASE ("Review fix: an arpeggiator step pushed out of the MIDI range is clamped, not silenced", "[reviewfix][arp]")
{
    SECTION ("the note rule")
    {
        Arpeggiator arp;
        Step step;
        step.arpIndex = 1;
        step.octave = -1;

        REQUIRE (arp.stepNote (step, 0) == -1);       // no key: the only negative result
        REQUIRE (arp.stepNote (step, -100) == -1);

        arp.noteOn (5);
        REQUIRE (arp.stepNote (step, 0) == 0);        // 5 - 12 is below the range
        REQUIRE (arp.stepNote (step, -24) == 0);
        REQUIRE (arp.stepNote (step, 12) == 5);

        step.octave = 3;
        arp.reset();
        arp.noteOn (120);
        REQUIRE (arp.stepNote (step, 0) == 127);
        REQUIRE (arp.stepNote (step, 127) == 127);
    }

    SECTION ("through the player: a low key with a downward octave and transpose still sounds")
    {
        Session s;
        s.ctl.playMode = PlayMode::Arpeggiator;
        s.ctl.liveTranspose = -24;
        setSteps (s.arp(), "C C C C");
        s.arp().steps[1].octave = -1;
        s.arp().steps[2].octave = 1;
        s.arp().steps[3].octave = 3;
        for (auto& st : s.arp().steps) st.arpIndex = 1;
        s.keyDown (0, 5);
        s.keyUp (4 * kStep, 5);
        s.run (5 * kStep, 64);

        // Key 5, transposed by -24: every step would be below 0 except the last (5 + 36 - 24).
        std::vector<int> notes;
        for (const Ev& e : s.noteOns()) notes.push_back (e.note);
        REQUIRE (notes == std::vector<int> { 0, 0, 0, 17 });

        const auto ons = s.noteOns();
        for (std::size_t k = 0; k < ons.size(); ++k) REQUIRE (ons[k].sample == static_cast<std::int64_t> (k) * kStep);
        requireClean (s);
    }
}
