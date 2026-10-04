// Reviewer tests for the engine. Each case fails against the engine as delivered and passes
// once the matching review finding is fixed.
#include "EngineTestSupport.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <map>

using namespace etest;

namespace
{
using TransportFn = std::function<seq::TransportInfo (std::int64_t pos)>;

struct Out
{
    std::vector<float> l, r;
    std::vector<TimedMidi> midi;
};

Out runEngine (Engine& e, const ParamValues& p, std::int64_t total, int block, const TransportFn& tf, int midiCapacity = 8192)
{
    Out o;
    o.l.assign (static_cast<std::size_t> (total), 0.0f);
    o.r.assign (static_cast<std::size_t> (total), 0.0f);
    std::vector<seq::MidiMsg> cap (static_cast<std::size_t> (midiCapacity));
    for (std::int64_t pos = 0; pos < total; pos += block)
    {
        const int n = static_cast<int> (std::min<std::int64_t> (block, total - pos));
        MidiOutBuffer mo { cap.data(), midiCapacity, 0 };
        e.process (p, tf (pos), {}, mo, o.l.data() + pos, o.r.data() + pos, n);
        for (int i = 0; i < mo.count; ++i)
            o.midi.push_back ({ pos + mo.messages[i].offset, mo.messages[i] });
    }
    return o;
}

std::vector<std::int64_t> noteOnSamples (const Out& o)
{
    std::vector<std::int64_t> v;
    for (const auto& m : o.midi)
        if ((m.msg.data[0] & 0xF0) == 0x90 && m.msg.data[2] > 0)
            v.push_back (m.sample);
    return v;
}

TransportFn rolling (double bpm, double sr)
{
    return [=] (std::int64_t pos) {
        seq::TransportInfo t;
        t.valid = t.playing = t.ppqValid = t.bpmValid = true;
        t.bpm = bpm;
        t.ppq = static_cast<double> (pos) * bpm / (60.0 * sr);
        return t;
    };
}
}

TEST_CASE ("review: loop flags on with the playhead past the loop end, oversized host blocks", "[review][engine][loop]")
{
    // Hosts keep reporting the loop (isLooping, loop points) while playing after the loop
    // region; they do not wrap there. The engine's advanced() wraps every later chunk of a split
    // block back into the loop, the tracker sees a relocation per chunk, and notes are cut,
    // dropped and moved. With blocks that are not split the same timeline plays correctly.
    const double sr = 48000.0, bpm = 120.0;
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("C . . . E . . . G . . . C+ . . .");
    ParamValues p = plainParams();

    const TransportFn pastLoop = [=] (std::int64_t pos) {
        seq::TransportInfo t;
        t.valid = t.playing = t.ppqValid = t.bpmValid = true;
        t.bpm = bpm;
        t.ppq = 16.0 + static_cast<double> (pos) * bpm / (60.0 * sr);
        t.looping = true;
        t.loopStartPpq = 0.25;
        t.loopEndPpq = 3.0;
        return t;
    };

    const auto render = [&] (int block) {
        Engine e;
        e.publishSettings ({});
        e.publishBank (*bank);
        e.prepare (sr, 512);
        return runEngine (e, p, static_cast<std::int64_t> (4 * sr), block, pastLoop);
    };

    const Out ref = render (512);       // never split
    const Out big = render (16384);     // split into 512-sample chunks
    REQUIRE (noteOnSamples (ref).size() == 8);
    CHECK (noteOnSamples (big) == noteOnSamples (ref));
    CHECK (hashOf (big.l, big.r) == hashOf (ref.l, ref.r));
}

TEST_CASE ("review: MIDI delivered in a zero-length block is not lost", "[review][engine][external]")
{
    // A zero-sample process() returns before the player sees its MIDI. In External mode a
    // note-off flushed that way leaves the gate open for good.
    Engine e;
    e.publishSettings ({});
    e.publishBank (seq::PatternBank {});
    e.prepare (48000.0, 512);
    ParamValues p = plainParams();
    p[kPlayMode] = 0.0f;

    std::vector<seq::MidiMsg> scratch (64);
    std::vector<float> l (512), r (512);
    MidiOutBuffer mo { scratch.data(), 64, 0 };
    const seq::MidiMsg on = noteOn (0, 48).msg, off = noteOff (0, 48).msg;

    e.process (p, {}, { &on, 1 }, mo, l.data(), r.data(), 512);
    for (int i = 0; i < 10; ++i) e.process (p, {}, {}, mo, l.data(), r.data(), 512);
    REQUIRE (e.status().gateOpen);

    e.process (p, {}, { &off, 1 }, mo, l.data(), r.data(), 0);
    for (int i = 0; i < 10; ++i) e.process (p, {}, {}, mo, l.data(), r.data(), 512);
    CHECK_FALSE (e.status().gateOpen);
}

TEST_CASE ("review: a full MIDI-out buffer never unbalances the note stream", "[review][engine][midiout]")
{
    // The densest pattern at the tempo ceiling, 65536-sample blocks, a 4096-message buffer (what
    // acid-render uses). The engine copies chunk after chunk until the buffer is full and drops
    // the rest without regard to note-offs and without telling the caller.
    const double sr = 48000.0, bpm = 1000.0;
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("acidsteps v1 len=16 div=1/64 mod=tri shuffle=0 gate=auto dir=fwd transpose=0 name=\"x\"\n"
                                    "C:R4S D:R4A E:R4 F:R4S G:R4 A:R4AS B:R4 C+:R4 C:R4S D:R4A E:R4 F:R4S G:R4 A:R4AS B:R4 C+:R4");
    ParamValues p = plainParams();
    p[kSeqRun] = 1.0f;

    Engine e;
    e.publishSettings ({});
    e.publishBank (*bank);
    e.prepare (sr, 65536);
    const Out o = runEngine (e, p, static_cast<std::int64_t> (3 * sr), 65536, rolling (bpm, sr), 4096);

    std::map<int, int> open;
    for (const auto& m : o.midi)
    {
        const int status = m.msg.data[0] & 0xF0;
        if (status == 0x90 && m.msg.data[2] > 0) ++open[m.msg.data[1]];
        else if (status == 0x80 || status == 0x90) --open[m.msg.data[1]];
        INFO ("note " << int (m.msg.data[1]) << " at " << m.sample);
        REQUIRE (open[m.msg.data[1]] >= 0);    // a note-off with no note-on: its note-on was dropped
        REQUIRE (open[m.msg.data[1]] <= 1);
    }
}

TEST_CASE ("review: the first block after prepare starts at the parameters, without fades", "[review][engine][fx]")
{
    // prepare() leaves the chain at the module defaults, so the first 10 ms crossfade from
    // 'not bypassed' (and from each block's default on/off) to what the parameters say.
    // acid-render hides this with a priming block; a plugin after load or a rate change does not.
    const double sr = 48000.0;
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A");
    const auto render = [&] (const ParamValues& p) {
        Engine e;
        e.publishSettings ({});
        e.publishBank (*bank);
        e.prepare (sr, 512);
        return runEngine (e, p, 4800, 512, rolling (125.0, sr));
    };
    ParamValues p = typicalParams();
    p[kSeqRun] = 1.0f;
    ParamValues bypassed = p;
    bypassed[kFxBypass] = 1.0f;
    ParamValues off = p;
    off[kDistOn] = 0.0f;
    off[kRevOn] = 0.0f;
    const Out a = render (bypassed), b = render (off);
    CHECK (maxAbsDiff (a.l, b.l) < 1.0e-6);
}

TEST_CASE ("review: MIDI input beyond the chunk scratch is not dropped when a block is split", "[review][engine][external]")
{
    // A block that is split goes through an 8192-message scratch per chunk; anything past it
    // is silently dropped. The same input in an unsplit block is delivered whole.
    const auto run = [] (int prepared) {
        Engine e;
        e.publishSettings ({});
        e.publishBank (seq::PatternBank {});
        e.prepare (48000.0, prepared);
        ParamValues p = plainParams();
        p[kPlayMode] = 0.0f;
        std::vector<seq::MidiMsg> in;
        in.push_back (noteOn (0, 48).msg);
        for (int i = 0; i < 9000; ++i) in.push_back (controller (0, 74, i & 127).msg);
        seq::MidiMsg off = noteOff (0, 48).msg;
        off.offset = 100;
        in.push_back (off);
        std::vector<seq::MidiMsg> scratch (64);
        std::vector<float> l (6000), r (6000);
        MidiOutBuffer mo { scratch.data(), 64, 0 };
        e.process (p, {}, { in.data(), in.size() }, mo, l.data(), r.data(), 6000);
        return e.status().gateOpen;
    };
    CHECK_FALSE (run (6000));    // not split (6000 = the 48 kHz overflow-free size): the note-off arrives
    CHECK_FALSE (run (512));     // split: the note-off is message 9002 of chunk 1
}

TEST_CASE ("review: non-finite parameter values are sanitised before use", "[review][engine][params]")
{
    // controlsFrom() feeds paramAsInt() with whatever the host sent. NaN or +-inf makes the
    // float-to-int conversion and the '- 1' on pattern_slot undefined behaviour (UBSan:
    // Engine.cpp:85 signed integer overflow). This case only fails in a UBSan build.
    Engine e;
    auto bank = std::make_unique<seq::PatternBank>();
    bank->seq[0] = patternFromText ("C . D . E . F . G . A . B . C+ .");
    e.publishSettings ({});
    e.publishBank (*bank);
    e.prepare (48000.0, 512);
    ParamValues p = plainParams();
    p[kSeqRun] = 1.0f;
    p[kPatternSlot] = -std::numeric_limits<float>::infinity();
    p[kPatternBank] = std::numeric_limits<float>::quiet_NaN();
    p[kLiveTranspose] = std::numeric_limits<float>::infinity();
    const Out o = runEngine (e, p, 48000, 512, rolling (120.0, 48000.0));
    CHECK (allFinite (o.l));
    const EngineStatus st = e.status();
    CHECK (st.playingSlot >= 0);
    CHECK (st.playingSlot < seq::kNumSlots);
}
