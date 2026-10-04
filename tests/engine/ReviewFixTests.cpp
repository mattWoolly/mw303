// Pins for the review fixes in the engine, beyond the reviewer's own cases (review_*.cpp):
// the chunk position handoff under random block sizes, the documented MIDI-out capacity,
// the balanced overflow policy, the pending MIDI of zero-length blocks, and the parameter
// sanitising.
#include "EngineTestSupport.h"
#include "acid/seq/Player.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <map>

using namespace etest;

namespace
{
struct Out
{
    std::vector<float> l, r;
    std::vector<TimedMidi> midi;
    bool overflowed = false;
    std::size_t allocations = 0;
};

using TransportFn = std::function<seq::TransportInfo (std::int64_t pos)>;

Out runEngine (Engine& e, const ParamValues& p, std::int64_t total, const std::function<int()>& nextBlock,
               const TransportFn& tf, int midiCapacity = 8192, bool guard = false)
{
    Out o;
    o.l.assign (static_cast<std::size_t> (total), 0.0f);
    o.r.assign (static_cast<std::size_t> (total), 0.0f);
    std::vector<seq::MidiMsg> cap (static_cast<std::size_t> (std::max (1, midiCapacity)));
    for (std::int64_t pos = 0; pos < total;)
    {
        const int n = static_cast<int> (std::min<std::int64_t> (std::max (1, nextBlock()), total - pos));
        MidiOutBuffer mo { cap.data(), midiCapacity, 0 };
        {
            std::unique_ptr<test::ScopedAllocGuard> g;
            if (guard) g = std::make_unique<test::ScopedAllocGuard>();
            e.process (p, tf (pos), {}, mo, o.l.data() + pos, o.r.data() + pos, n);
            if (g) o.allocations += g->count();
        }
        o.overflowed = o.overflowed || mo.overflowed;
        for (int i = 0; i < mo.count; ++i)
            o.midi.push_back ({ pos + mo.messages[i].offset, mo.messages[i] });
        pos += n;
    }
    return o;
}

TransportFn rolling_bpm (double bpm, double sr)
{
    return [=] (std::int64_t pos) {
        seq::TransportInfo t;
        t.valid = t.playing = t.ppqValid = t.bpmValid = true;
        t.bpm = bpm;
        t.ppq = static_cast<double> (pos) * bpm / (60.0 * sr);
        return t;
    };
}

TransportFn pastLoop (double bpm, double sr, double startPpq)
{
    return [=] (std::int64_t pos) {
        seq::TransportInfo t;
        t.valid = t.playing = t.ppqValid = t.bpmValid = true;
        t.bpm = bpm;
        t.ppq = startPpq + static_cast<double> (pos) * bpm / (60.0 * sr);
        t.looping = true;
        t.loopStartPpq = 0.25;
        t.loopEndPpq = 3.0;
        return t;
    };
}

/** True when every note-on is matched by one note-off and no note is opened twice. */
bool balanced (const std::vector<TimedMidi>& midi, int& notesOn)
{
    std::map<int, int> open;
    notesOn = 0;
    for (const auto& m : midi)
    {
        const int status = m.msg.data[0] & 0xF0;
        if (status == 0x90 && m.msg.data[2] > 0)
        {
            ++notesOn;
            if (++open[m.msg.data[1]] > 1) return false;
        }
        else if (status == 0x80 || status == 0x90)
        {
            if (--open[m.msg.data[1]] < 0) return false;
        }
    }
    return true;
}
}

TEST_CASE ("review fix: split blocks past the loop end match unsplit ones for random block sizes", "[engine][loop][review-fix]")
{
    // The reviewer's case with 512 against 16384; here against a stream of random sizes from
    // 1 to 3000 at two sample rates, with the host loop flags still on far past the loop.
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("C:A . E:S G . C+:A . B:S A . G:V . E ~ . C:A");
    ParamValues p = plainParams();

    for (const double sr : { 44100.0, 96000.0 })
    {
        const auto render = [&] (const std::function<int()>& blocks) {
            Engine e;
            e.publishSettings ({});
            e.publishBank (*bank);
            e.prepare (sr, 512);
            return runEngine (e, p, static_cast<std::int64_t> (3 * sr), blocks, pastLoop (131.0, sr, 40.5));
        };
        const Out ref = render ([] { return 512; });
        const Out rnd = render (randomBlocks (11, 1, 3000));
        INFO ("sample rate " << sr);
        REQUIRE (ref.midi.size() > 8);
        CHECK (rnd.midi.size() == ref.midi.size());
        CHECK (hashOf (rnd.l, rnd.r) == hashOf (ref.l, ref.r));
    }
}

TEST_CASE ("review fix: a loop that wraps inside a split block still wraps once, where the tracker puts it", "[engine][loop][review-fix]")
{
    // The host does not cut its blocks at the loop point. Blocks of 512 and of 20000 must
    // agree: the wrap happens inside one of the 20000-sample blocks, in a later chunk.
    Session a, b;
    for (Session* s : { &a, &b })
    {
        s->setPattern (0, "C . . . E . . . G . . . C+ . . .");
        s->host.looping = true;
        s->host.loopStart = 0.0;
        s->host.loopEnd = 2.0;
        s->host.splitsAtLoop = false;
        s->host.bpm = 120.0;
        s->prepare (kFs, 512);
        s->host.play();
    }
    const Rendered ra = a.render (static_cast<std::int64_t> (4 * kFs), 512);
    const Rendered rb = b.render (static_cast<std::int64_t> (4 * kFs), 20000);
    REQUIRE (ra.midiOut.size() >= 8);
    CHECK (ra.midiOut.size() == rb.midiOut.size());
    CHECK (hashOf (ra) == hashOf (rb));
}

TEST_CASE ("review fix: midiOutCapacityFor sizes a buffer the densest pattern never overflows", "[engine][midiout][review-fix]")
{
    const double sr = 48000.0;
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("acidsteps v1 len=16 div=1/64 mod=tri shuffle=0 gate=auto dir=fwd transpose=0 name=\"x\"\n"
                                    "C:R4S D:R4A E:R4 F:R4S G:R4 A:R4AS B:R4 C+:R4 C:R4S D:R4A E:R4 F:R4S G:R4 A:R4AS B:R4 C+:R4");
    ParamValues p = plainParams();
    p[kSeqRun] = 1.0f;

    Engine e;
    e.publishSettings ({});
    e.publishBank (*bank);
    e.prepare (sr, 65536);
    const int capacity = e.midiOutCapacityFor (65536);
    CHECK (capacity == 11 * seq::PlayerOutput::kMaxMidiOut);     // 65536 / 6000 chunks, rounded up
    CHECK (e.midiOutCapacityFor (6000) == seq::PlayerOutput::kMaxMidiOut);
    CHECK (e.midiOutCapacityFor (6001) == 2 * seq::PlayerOutput::kMaxMidiOut);
    CHECK (e.midiOutCapacityFor (0) == seq::PlayerOutput::kMaxMidiOut);

    const Out full = runEngine (e, p, static_cast<std::int64_t> (3 * sr), [] { return 65536; }, rolling_bpm (1000.0, sr), capacity);
    CHECK_FALSE (full.overflowed);
    int notesOn = 0;
    CHECK (balanced (full.midi, notesOn));
    CHECK (notesOn == 9600 / 2);    // 3 s at 1000 bpm, 16 steps of 1/64 triplets with four ratchets: 4800 note-ons
    CHECK (full.midi.size() == 9600);

    // The same with 512-sample blocks is the same stream.
    Engine f;
    f.publishSettings ({});
    f.publishBank (*bank);
    f.prepare (sr, 512);
    const Out small = runEngine (f, p, static_cast<std::int64_t> (3 * sr), [] { return 512; }, rolling_bpm (1000.0, sr), f.midiOutCapacityFor (512));
    CHECK_FALSE (small.overflowed);
    REQUIRE (small.midi.size() == full.midi.size());
    for (std::size_t i = 0; i < small.midi.size(); ++i)
    {
        CHECK (small.midi[i].sample == full.midi[i].sample);
        CHECK (small.midi[i].msg.data[0] == full.midi[i].msg.data[0]);
        CHECK (small.midi[i].msg.data[1] == full.midi[i].msg.data[1]);
        CHECK (small.midi[i].msg.data[2] == full.midi[i].msg.data[2]);
    }
}

TEST_CASE ("review fix: a too-small MIDI-out buffer reports overflow and stays balanced at any capacity", "[engine][midiout][review-fix]")
{
    const double sr = 48000.0;
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("acidsteps v1 len=16 div=1/64 mod=tri shuffle=0 gate=auto dir=fwd transpose=0 name=\"x\"\n"
                                    "C:R4S D:R4A E:R4 F:R4S G:R4 A:R4AS B:R4 C+:R4 C:R4S D:R4A E:R4 F:R4S G:R4 A:R4AS B:R4 C+:R4");
    ParamValues p = plainParams();
    p[kSeqRun] = 1.0f;

    for (const int capacity : { 1, 2, 3, 7, 64, 1000, 4096 })
    {
        Engine e;
        e.publishSettings ({});
        e.publishBank (*bank);
        e.prepare (sr, 65536);
        const Out o = runEngine (e, p, static_cast<std::int64_t> (2 * sr), [] { return 65536; }, rolling_bpm (1000.0, sr), capacity, true);
        INFO ("capacity " << capacity);
        CHECK (o.allocations == 0);
        CHECK (o.overflowed);
        if (capacity <= 1000) CHECK (e.status().midiOutOverflowed);    // the status is per block: the short last block fits 4096
        int notesOn = 0;
        CHECK (balanced (o.midi, notesOn));
        if (capacity >= 2) CHECK (notesOn > 0);
        // Vibrato: a CC1 that went high always comes back low.
        int cc1 = 0;
        for (const auto& m : o.midi)
            if ((m.msg.data[0] & 0xF0) == 0xB0 && m.msg.data[1] == 1) cc1 = m.msg.data[2] >= 64 ? 1 : 0;
        CHECK (cc1 == 0);
    }

    // A caller that passes no buffer is not told about an overflow it opted into.
    Engine e;
    e.publishSettings ({});
    e.publishBank (*bank);
    e.prepare (sr, 512);
    std::vector<float> l (512), r (512);
    MidiOutBuffer none;
    e.process (p, rolling_bpm (1000.0, sr) (0), {}, none, l.data(), r.data(), 512);
    CHECK_FALSE (none.overflowed);
    CHECK_FALSE (e.status().midiOutOverflowed);
}

TEST_CASE ("review fix: zero-length blocks keep their MIDI, up to the pending capacity, closing messages first", "[engine][external][review-fix]")
{
    Engine e;
    e.publishSettings ({});
    e.publishBank (seq::PatternBank {});
    e.prepare (48000.0, 512);
    ParamValues p = plainParams();
    p[kPlayMode] = 0.0f;
    std::vector<seq::MidiMsg> scratch (64);
    std::vector<float> l (512), r (512);
    MidiOutBuffer mo { scratch.data(), 64, 0 };

    // A note-on in a zero-length block sounds from the start of the next block.
    const seq::MidiMsg on = noteOn (0, 48).msg, off = noteOff (0, 48).msg;
    {
        test::ScopedAllocGuard g;
        e.process (p, {}, { &on, 1 }, mo, l.data(), r.data(), 0);
        CHECK (g.count() == 0);
    }
    CHECK_FALSE (e.status().gateOpen);
    for (int i = 0; i < 4; ++i) e.process (p, {}, {}, mo, l.data(), r.data(), 512);
    CHECK (e.status().gateOpen);
    CHECK (peakOf (l) > 0.0f);

    // A flood of zero-length blocks beyond the pending capacity: the note-off at the end
    // still gets through, in place of one of the controllers.
    std::vector<seq::MidiMsg> flood;
    for (int i = 0; i < Engine::kPendingMidiCapacity + 500; ++i) flood.push_back (controller (0, 74, i & 127).msg);
    e.process (p, {}, { flood.data(), flood.size() }, mo, l.data(), r.data(), 0);
    CHECK (e.status().midiInDropped);
    e.process (p, {}, { &off, 1 }, mo, l.data(), r.data(), 0);
    for (int i = 0; i < 10; ++i) e.process (p, {}, {}, mo, l.data(), r.data(), 512);
    CHECK_FALSE (e.status().gateOpen);
    CHECK_FALSE (e.status().midiInDropped);

    // Pending messages come before the next block's own messages: a pending note-off and a
    // note-on at offset 0 of the next block leave the new note sounding.
    const seq::MidiMsg on2 = noteOn (0, 52).msg;
    e.process (p, {}, { &on, 1 }, mo, l.data(), r.data(), 512);
    REQUIRE (e.status().gateOpen);
    e.process (p, {}, { &off, 1 }, mo, l.data(), r.data(), 0);
    e.process (p, {}, { &on2, 1 }, mo, l.data(), r.data(), 512);
    CHECK (e.status().gateOpen);

    // Before prepare() nothing is kept.
    Engine cold;
    cold.process (p, {}, { &on, 1 }, mo, l.data(), r.data(), 0);
    cold.publishSettings ({});
    cold.publishBank (seq::PatternBank {});
    cold.prepare (48000.0, 512);
    for (int i = 0; i < 4; ++i) cold.process (p, {}, {}, mo, l.data(), r.data(), 512);
    CHECK_FALSE (cold.status().gateOpen);
}

TEST_CASE ("review fix: MIDI floods in split blocks keep block-size invariance up to the scratch, then drop by priority", "[engine][external][review-fix]")
{
    // 20000 controllers spread over a 6000-sample block, a note-on at 0 and a note-off at
    // 5999: prepared at 6000 (one chunk) and at 512 (chunks cut where the scratch fills)
    // give the same audio and the gate closes in both.
    const auto run = [] (int prepared, std::vector<float>& out) {
        Engine e;
        e.publishSettings ({});
        e.publishBank (seq::PatternBank {});
        e.prepare (48000.0, prepared);
        ParamValues p = plainParams();
        p[kPlayMode] = 0.0f;
        std::vector<seq::MidiMsg> in;
        in.push_back (noteOn (0, 48).msg);
        for (int i = 0; i < 20000; ++i)
        {
            seq::MidiMsg m = controller (0, 74, i & 127).msg;
            m.offset = (i * 3) / 10;    // 0..5999
            in.push_back (m);
        }
        in.push_back (noteOff (5999, 48).msg);
        std::vector<seq::MidiMsg> scratch (64);
        std::vector<float> r (6000);
        out.assign (6000, 0.0f);
        MidiOutBuffer mo { scratch.data(), 64, 0 };
        test::ScopedAllocGuard g;
        e.process (p, {}, { in.data(), in.size() }, mo, out.data(), r.data(), 6000);
        CHECK (g.count() == 0);
        CHECK_FALSE (e.status().midiInDropped);
        return e.status().gateOpen;
    };
    std::vector<float> whole, split;
    CHECK_FALSE (run (6000, whole));
    CHECK_FALSE (run (512, split));
    CHECK (hashOf (whole, whole) == hashOf (split, split));

    // More than the scratch holds on one sample: the note-off and the note-on survive, the
    // controllers give way, and the status says so.
    Engine e;
    e.publishSettings ({});
    e.publishBank (seq::PatternBank {});
    e.prepare (48000.0, 512);
    ParamValues p = plainParams();
    p[kPlayMode] = 0.0f;
    std::vector<seq::MidiMsg> in;
    in.push_back (noteOn (0, 48).msg);
    for (int i = 0; i < 9000; ++i) in.push_back (controller (0, 74, i & 127).msg);
    std::vector<seq::MidiMsg> scratch (64);
    std::vector<float> l (1024), r (1024);
    MidiOutBuffer mo { scratch.data(), 64, 0 };
    e.process (p, {}, { in.data(), in.size() }, mo, l.data(), r.data(), 1024);
    CHECK (e.status().gateOpen);
    CHECK (e.status().midiInDropped);
    in.clear();
    for (int i = 0; i < 9000; ++i) in.push_back (controller (0, 74, i & 127).msg);
    in.push_back (noteOff (0, 48).msg);    // last of 9001 messages on sample 0
    e.process (p, {}, { in.data(), in.size() }, mo, l.data(), r.data(), 1024);
    CHECK_FALSE (e.status().gateOpen);
    CHECK (e.status().midiInDropped);
}

TEST_CASE ("review fix: non-finite and out-of-range parameters are sanitised for every consumer", "[engine][params][review-fix]")
{
    // Every row at once: NaN, +inf, -inf and values far outside the range. The engine must
    // render finite audio, pick a legal slot, treat every non-finite value as the row default
    // and every out-of-range value as the clamped one (1e9 is the maximum, -1e9 the minimum).
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("C . D . E . F . G . A . B . C+ .");
    const auto render = [&] (const ParamValues& p) {
        Engine e;
        e.publishSettings ({});
        e.publishBank (*bank);
        e.prepare (48000.0, 512);
        return runEngine (e, p, 24000, [] { return 512; }, rolling_bpm (120.0, 48000.0));
    };

    ParamValues wild = plainParams();
    wild[kSeqRun] = 1.0f;
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == kSeqRun || i == kPlayMode) continue;
        switch (i % 5)
        {
            case 0: wild[static_cast<std::size_t> (i)] = std::numeric_limits<float>::quiet_NaN(); break;
            case 1: wild[static_cast<std::size_t> (i)] = std::numeric_limits<float>::infinity(); break;
            case 2: wild[static_cast<std::size_t> (i)] = -std::numeric_limits<float>::infinity(); break;
            case 3: wild[static_cast<std::size_t> (i)] = 1.0e9f; break;
            default: wild[static_cast<std::size_t> (i)] = -1.0e9f; break;
        }
    }
    // Keep the pattern audible: the selection and the mute override take their defaults.
    wild[kPatternBank] = std::numeric_limits<float>::quiet_NaN();
    wild[kPatternSlot] = std::numeric_limits<float>::infinity();
    wild[kOvrNoteClr] = -std::numeric_limits<float>::infinity();
    const Out o = render (wild);
    CHECK (allFinite (o.l));
    REQUIRE (o.midi.size() > 2);    // a slot plays: the pattern selection came out legal

    ParamValues expected = plainParams();
    expected[kSeqRun] = 1.0f;
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == kSeqRun || i == kPlayMode) continue;
        const ParamInfo& info = kParamTable[static_cast<std::size_t> (i)];
        switch (i % 5)
        {
            case 0: case 1: case 2: expected[static_cast<std::size_t> (i)] = info.def; break;
            case 3: expected[static_cast<std::size_t> (i)] = info.max; break;
            default: expected[static_cast<std::size_t> (i)] = info.min; break;
        }
    }
    expected[kPatternBank] = paramInfo (kPatternBank).def;
    expected[kPatternSlot] = paramInfo (kPatternSlot).def;
    expected[kOvrNoteClr] = paramInfo (kOvrNoteClr).def;
    const Out e = render (expected);
    CHECK (hashOf (o.l, o.r) == hashOf (e.l, e.r));
    CHECK (o.midi.size() == e.midi.size());
}

TEST_CASE ("review fix: the first block after reset() also starts at the parameters", "[engine][fx][review-fix]")
{
    // Prepare, play with the effects on, reset(), then play with fx_bypass: from the first
    // sample after the reset the output equals a fresh engine's with the same parameters.
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A");
    ParamValues loud = typicalParams();
    loud[kSeqRun] = 1.0f;
    loud[kDelayOn] = 1.0f;
    ParamValues bypassed = loud;
    bypassed[kFxBypass] = 1.0f;

    Engine e;
    e.publishSettings ({});
    e.publishBank (*bank);
    e.prepare (48000.0, 512);
    (void) runEngine (e, loud, 9600, [] { return 512; }, rolling_bpm (125.0, 48000.0));
    e.reset();
    const Out afterReset = runEngine (e, bypassed, 4800, [] { return 512; }, rolling_bpm (125.0, 48000.0));

    Engine f;
    f.publishSettings ({});
    f.publishBank (*bank);
    f.prepare (48000.0, 512);
    const Out fresh = runEngine (f, bypassed, 4800, [] { return 512; }, rolling_bpm (125.0, 48000.0));

    CHECK (peakOf (fresh.l) > 0.01f);
    CHECK (maxAbsDiff (afterReset.l, fresh.l) < 1.0e-6);
}
