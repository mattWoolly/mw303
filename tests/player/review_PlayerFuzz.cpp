// Independent review harness (not part of the implementers' suite).
//
// Drives Player through long randomised sessions built from a script of segments. Every
// change (transport, tempo, loop, controls, config, pattern edits, keys, resets) happens at
// an absolute sample that is a block boundary in every blocking, so the same script played
// with blocks of 1, 64, 4096 or random sizes must give identical event lists. It also checks
// note balance for the voice and MIDI out, event ordering and offsets, and allocation.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <sstream>

using namespace ptest;

namespace
{
void rvRandomStep (Step& s, acid::Rng& rng)
{
    const double r = rng.nextDouble();
    s.state = r < 0.6 ? StepState::Note : (r < 0.75 ? StepState::Tie : StepState::Rest);
    s.note = static_cast<std::uint8_t> (rng.nextInt (12));
    s.octave = static_cast<std::int8_t> (rng.nextInt (5) - 1);
    s.arpIndex = static_cast<std::uint8_t> (1 + rng.nextInt (10));
    s.accent = rng.chance (0.3);
    s.slide = rng.chance (0.35);
    s.vibrato = rng.chance (0.15);
    s.probability = static_cast<std::uint8_t> (rng.chance (0.7) ? 100 : rng.nextInt (101));
    s.ratchet = static_cast<std::uint8_t> (rng.chance (0.8) ? 1 : 1 + rng.nextInt (4));
    s.gate = static_cast<std::uint8_t> (rng.chance (0.8) ? 0 : rng.nextInt (128));
}

void rvRandomPattern (Pattern& p, acid::Rng& rng)
{
    p.length = static_cast<std::uint8_t> (1 + rng.nextInt (rng.chance (0.3) ? 64 : 16));
    p.div = static_cast<StepDiv> (rng.nextInt (7));
    p.divMod = static_cast<DivMod> (rng.nextInt (3));
    p.shuffle = static_cast<std::uint8_t> (rng.chance (0.5) ? 0 : rng.nextInt (101));
    p.gateLength = static_cast<std::uint8_t> (rng.chance (0.4) ? 0 : 1 + rng.nextInt (100));
    p.direction = static_cast<Direction> (rng.nextInt (4));
    p.transpose = static_cast<std::int8_t> (rng.nextInt (25) - 12);
    p.gen.seed = static_cast<std::uint32_t> (rng.nextU64());

    for (Step& s : p.steps)
        rvRandomStep (s, rng);
}

/** Keeps a looping host's position inside its loop, so a wrap never depends on where a block ends. */
void keepInsideLoop (Host& h)
{
    if (! h.looping)
        return;

    const double q = h.ppq();

    if (q < h.loopStart || q >= h.loopEnd - 1.0e-6)
        h.locate (h.loopStart + 0.37 * (h.loopEnd - h.loopStart));
}

struct Result
{
    std::vector<Ev> voice;
    std::vector<MidiEv> midi;
    std::size_t allocations = 0;
    int orderViolations = 0;
    int overflows = 0;
    std::string voiceBal, midiBal;
    bool endOpen = false, endRunning = false;
    std::int64_t strayOverlaps = 0;   // voice: plain NoteOn on the sample of the previous NoteOff it does not follow
};

std::uint64_t rvTraceSeed = 0;
bool rvNoNextBar = false;
bool rvMirrorOnly = false;   // pattern modes only, MIDI out always on, thru off
std::int64_t rvTraceSample = -1;

enum class Blocking { One, B64, B4096, Random, Random2 };

Result playScript (std::uint64_t seed, int segments, Blocking blocking, bool allowLossyHosts)
{
    acid::Rng setup (seed);
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const double sr = rates[seed % 4];

    Session s (sr, 512);
    s.host.bpm = 60.0 + setup.nextInt (180);

    for (int i = 0; i < 4; ++i)
    {
        rvRandomPattern (s.seq (i), setup);
        rvRandomPattern (s.arp (i), setup);
    }

    acid::Rng blockRng (seed * 7919u + static_cast<std::uint64_t> (blocking));
    switch (blocking)
    {
        case Blocking::One:     s.blockSizer = [] { return 1; }; break;
        case Blocking::B64:     s.blockSizer = [] { return 64; }; break;
        case Blocking::B4096:   s.blockSizer = [] { return 4096; }; break;
        case Blocking::Random:  s.blockSizer = [&blockRng] { return 1 + blockRng.nextInt (blockRng.chance (0.1) ? 5000 : 300); }; break;
        case Blocking::Random2: s.blockSizer = [&blockRng] { return 1 + blockRng.nextInt (17); }; break;
    }

    const int keys[] = { 24, 25, 26, 27, 48, 52, 55, 59, 60, 64, 72 };
    s.host.play();
    s.ctl.run = setup.chance (0.5);

    for (int seg = 0; seg < segments; ++seg)
    {
        acid::Rng rng (seed * 1000003u + static_cast<std::uint64_t> (seg));

        // --- transport ---
        if (rng.chance (s.host.playing ? 0.03 : 0.3)) { if (s.host.playing) s.host.stop(); else s.host.play(); }
        if (rng.chance (0.05)) s.host.locate (rng.nextDouble() * 68.0 - 4.0);
        if (rng.chance (0.04)) s.host.setTempo (40.0 + rng.nextInt (260));
        if (rng.chance (0.03))
        {
            s.host.looping = ! s.host.looping;
            s.host.loopStart = rng.nextInt (8) * 1.0;
            s.host.loopEnd = s.host.loopStart + 0.5 + rng.nextInt (8) * 0.75;
            s.host.splitsAtLoop = rng.chance (0.5);
            s.host.changed = true;
        }
        if (allowLossyHosts)
        {
            if (rng.chance (0.02)) s.host.newsOnlyOnChange = ! s.host.newsOnlyOnChange;
            if (rng.chance (0.01)) s.host.present = ! s.host.present;
            if (rng.chance (0.01)) s.host.sendPpq = ! s.host.sendPpq;
        }
        // A host that only reports changes and loops without splitting its blocks never tells
        // the player where it wrapped: the player cannot be block-size invariant there.
        if (s.host.newsOnlyOnChange && s.host.looping)
            s.host.splitsAtLoop = true;
        keepInsideLoop (s.host);

        // --- controls and settings ---
        if (rng.chance (0.02)) s.ctl.playMode = static_cast<PlayMode> (rng.nextInt (3));
        if (rng.chance (s.cfg.triggerMode == TriggerMode::HostNote ? 0.15 : 0.02)) s.cfg.triggerMode = static_cast<TriggerMode> (rng.nextInt (2));
        if (rng.chance (0.03)) s.cfg.launchQuant = static_cast<LaunchQuant> (rng.nextInt (3));
        if (rvNoNextBar && s.cfg.launchQuant == LaunchQuant::NextBar) s.cfg.launchQuant = LaunchQuant::EndOfPattern;
        if (rng.chance (0.03)) s.cfg.slideIntoRest = static_cast<SlideIntoRest> (rng.nextInt (3));
        if (rng.chance (0.03)) s.cfg.notePriority = static_cast<NotePriority> (rng.nextInt (3));
        if (rng.chance (0.03)) s.cfg.hostLink = ! s.cfg.hostLink;
        if (rng.chance (0.03)) s.cfg.followHostTransport = ! s.cfg.followHostTransport;
        if (rng.chance (0.03)) s.cfg.restartOnTrigger = ! s.cfg.restartOnTrigger;
        if (rng.chance (0.03)) s.cfg.midiOut = ! s.cfg.midiOut;
        if (rng.chance (0.03)) s.cfg.midiThru = ! s.cfg.midiThru;
        if (rng.chance (s.ctl.run ? 0.02 : 0.1)) s.ctl.run = ! s.ctl.run;
        if (rng.chance (0.08)) s.ctl.slot = rng.nextInt (4);
        if (rng.chance (0.03)) s.ctl.liveTranspose = rng.nextInt (49) - 24;
        if (rng.chance (s.ctl.ovrNoteSet ? 0.5 : 0.03)) s.ctl.ovrNoteSet = ! s.ctl.ovrNoteSet;
        if (rng.chance (s.ctl.ovrNoteClr ? 0.5 : 0.03)) s.ctl.ovrNoteClr = ! s.ctl.ovrNoteClr;
        if (rng.chance (s.ctl.ovrSlideSet ? 0.5 : 0.03)) s.ctl.ovrSlideSet = ! s.ctl.ovrSlideSet;
        if (rng.chance (s.ctl.ovrSlideClr ? 0.5 : 0.03)) s.ctl.ovrSlideClr = ! s.ctl.ovrSlideClr;
        if (rng.chance (s.ctl.ovrAccentSet ? 0.5 : 0.03)) s.ctl.ovrAccentSet = ! s.ctl.ovrAccentSet;
        if (rng.chance (s.ctl.ovrVibSet ? 0.5 : 0.03)) s.ctl.ovrVibSet = ! s.ctl.ovrVibSet;
        if (rng.chance (0.02)) s.ctl.internalBpm = 40.0 + rng.nextInt (260);

        // --- pattern edits ---
        if (rng.chance (0.05)) rvRandomPattern (rng.chance (0.5) ? s.seq (rng.nextInt (4)) : s.arp (rng.nextInt (4)), rng);
        if (rng.chance (0.15))
        {
            Pattern& p = rng.chance (0.5) ? s.seq (rng.nextInt (4)) : s.arp (rng.nextInt (4));
            rvRandomStep (p.steps[static_cast<std::size_t> (rng.nextInt (kMaxSteps))], rng);
            if (rng.chance (0.3)) p.length = static_cast<std::uint8_t> (1 + rng.nextInt (64));
        }

        if (rvMirrorOnly)
        {
            if (s.ctl.playMode == PlayMode::External) s.ctl.playMode = PlayMode::Sequencer;
            s.cfg.midiOut = true;
            s.cfg.midiThru = false;
        }

        if (rng.chance (0.008)) s.player.reset();
        if (rng.chance (0.002)) s.player.prepare (s.sampleRate, 512);

        // --- segment length and MIDI inside it ---
        const std::int64_t n = 1 + rng.nextInt (rng.chance (0.3) ? 40000 : 6000);

        if (rng.chance (0.4))
        {
            std::int64_t at = s.now;
            const int count = 1 + rng.nextInt (5);

            for (int i = 0; i < count; ++i)
            {
                at += rng.nextInt (static_cast<int> (n / count) + 1);
                if (at >= s.now + n) at = s.now + n - 1;
                const double r = rng.nextDouble();
                const int key = keys[rng.nextInt (11)];

                if (r < 0.45)      s.midiAt (at, 0x90 | rng.nextInt (2), key, 1 + rng.nextInt (127));
                else if (r < 0.85) s.midiAt (at, 0x80 | rng.nextInt (2), key, 0);
                else if (r < 0.90) s.midiAt (at, 0xB0, 64, rng.chance (0.5) ? 127 : 0);
                else if (r < 0.93) s.midiAt (at, 0xB0, 1, rng.nextInt (128));
                else if (r < 0.96) s.midiAt (at, 0xE0, rng.nextInt (128), rng.nextInt (128));
                else if (r < 0.98) s.midiAt (at, 0xB0, 123, 0);
                else if (r < 0.99) s.midiAt (at, 0xB0, 121, 0);
                else               s.midiAt (at, 0xFF, 0, 0);
            }
        }

        if (rvTraceSeed == seed && s.now <= rvTraceSample && rvTraceSample < s.now + n + 150000)
            std::printf ("seg %d [%lld,%lld) sr %.0f mode %d trig %d play %d ppq %.6f bpm %.1f loop %d [%g,%g) split %d news %d present %d sendPpq %d run %d hostLink %d quant %d slot %d follow %d\n",
                         seg, (long long) s.now, (long long) (s.now + n), s.sampleRate, (int) s.ctl.playMode, (int) s.cfg.triggerMode,
                         (int) s.host.playing, s.host.ppq(), s.host.bpm, (int) s.host.looping, s.host.loopStart, s.host.loopEnd,
                         (int) s.host.splitsAtLoop, (int) s.host.newsOnlyOnChange, (int) s.host.present, (int) s.host.sendPpq,
                         (int) s.ctl.run, (int) s.cfg.hostLink, (int) s.cfg.launchQuant, s.ctl.slot, (int) s.cfg.followHostTransport);

        s.run (n, 4096);
    }

    // Wind down: keys up, pedal up, everything stopped, then a long tail.
    for (int key : keys)
    {
        s.midiAt (s.now, 0x80, key, 0);
        s.midiAt (s.now, 0x81, key, 0);
    }
    s.midiAt (s.now, 0xB0, 64, 0);
    s.host.present = true;
    s.host.newsOnlyOnChange = false;
    s.host.stop();
    s.ctl.run = false;
    s.cfg.followHostTransport = true;
    s.run (static_cast<std::int64_t> (s.sampleRate) * 40, 4096);

    Result r;
    r.voice = s.voice;
    r.midi = s.midi;
    r.allocations = s.allocations;
    r.orderViolations = s.orderViolations;
    r.overflows = s.overflows;
    r.voiceBal = voiceBalance (s.notes());
    r.midiBal = midiBalance (s.midi);
    r.endOpen = s.status.gateOpen;
    r.endRunning = s.status.running;
    return r;
}

std::string firstDifference (const std::vector<Ev>& a, const std::vector<Ev>& b)
{
    const std::size_t n = std::min (a.size(), b.size());

    for (std::size_t i = 0; i < n; ++i)
    {
        if (! (a[i] == b[i]))
        {
            std::ostringstream os;
            os << "event " << i << ": " << a[i] << " vs " << b[i];
            return os.str();
        }
    }

    if (a.size() != b.size())
        return "sizes " + std::to_string (a.size()) + " vs " + std::to_string (b.size());

    return {};
}

std::string firstDifference (const std::vector<MidiEv>& a, const std::vector<MidiEv>& b)
{
    const std::size_t n = std::min (a.size(), b.size());

    for (std::size_t i = 0; i < n; ++i)
    {
        if (! (a[i] == b[i]))
        {
            std::ostringstream os;
            os << "midi " << i << ": " << a[i] << " vs " << b[i];
            return os.str();
        }
    }

    if (a.size() != b.size())
        return "sizes " + std::to_string (a.size()) + " vs " + std::to_string (b.size());

    return {};
}
}

TEST_CASE ("Review: randomised sessions are block-size invariant, balanced, ordered and allocation-free", "[review][fuzz]")
{
    int sessions = 0;
    int mismatches = 0;
    std::size_t totalOns = 0;

    for (std::uint64_t seed = 1; seed <= 60; ++seed)
    {
        const bool lossy = seed % 3 == 0;
        const Result ref = playScript (seed, 1000, Blocking::B64, lossy);

        INFO ("seed " << seed);
        CHECK (ref.allocations == 0);
        CHECK (ref.orderViolations == 0);
        CHECK (ref.overflows == 0);
        CHECK (ref.voiceBal == "");
        CHECK (ref.midiBal == "");
        CHECK_FALSE (ref.endOpen);
        CHECK_FALSE (ref.endRunning);

        for (const Ev& e : ref.voice)
            if (e.type == VoiceEvent::Type::NoteOn) ++totalOns;

        for (Blocking b : { Blocking::B4096, Blocking::Random, Blocking::Random2 })
        {
            const Result other = playScript (seed, 1000, b, lossy);
            const std::string dv = firstDifference (ref.voice, other.voice);
            const std::string dm = firstDifference (ref.midi, other.midi);
            INFO ("blocking " << static_cast<int> (b) << " voice: " << dv << " midi: " << dm);
            CHECK (dv == "");
            CHECK (dm == "");
            if (! dv.empty() || ! dm.empty()) ++mismatches;
            CHECK (other.voiceBal == "");
            CHECK (other.midiBal == "");
            CHECK (other.allocations == 0);
            CHECK (other.orderViolations == 0);
        }

        ++sessions;
    }

    INFO ("sessions " << sessions << " mismatching blockings " << mismatches << " NoteOns " << totalOns);
    CHECK (totalOns > 20000);
    CHECK (mismatches == 0);
}

TEST_CASE ("Review: block size 1 matches block size 64 on randomised sessions", "[review][fuzz]")
{
    for (std::uint64_t seed = 101; seed <= 108; ++seed)
    {
        const Result a = playScript (seed, 60, Blocking::B64, false);
        const Result b = playScript (seed, 60, Blocking::One, false);
        INFO ("seed " << seed << " voice: " << firstDifference (a.voice, b.voice) << " midi: " << firstDifference (a.midi, b.midi));
        CHECK (a.voice == b.voice);
        CHECK (a.midi == b.midi);
        CHECK (b.voiceBal == "");
        CHECK (b.midiBal == "");
    }
}

TEST_CASE ("Review: trace one seed", "[.review-trace]")
{
    rvTraceSeed = std::strtoull (std::getenv ("RV_SEED"), nullptr, 10);
    rvTraceSample = std::strtoll (std::getenv ("RV_SAMPLE"), nullptr, 10);
    const Result a = playScript (rvTraceSeed, 1000, Blocking::B64, rvTraceSeed % 3 == 0);
    rvTraceSample = -1;
    const Result b = playScript (rvTraceSeed, 1000, static_cast<Blocking> (std::atoi (std::getenv ("RV_BLOCK"))), rvTraceSeed % 3 == 0);
    std::size_t i = 0;
    while (i < a.voice.size() && i < b.voice.size() && a.voice[i] == b.voice[i]) ++i;
    for (std::size_t k = (i > 8 ? i - 8 : 0); k < i + 4 && k < a.voice.size(); ++k)
    {
        std::ostringstream x, y;
        x << a.voice[k];
        if (k < b.voice.size()) y << b.voice[k];
        std::printf ("%zu  %-30s %s\n", k, x.str().c_str(), y.str().c_str());
    }
}

TEST_CASE ("Review: invariance with NextBar excluded (isolates other causes)", "[.review-nonextbar]")
{
    rvNoNextBar = true;
    int mismatches = 0;
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
    {
        const bool lossy = seed % 3 == 0;
        const Result ref = playScript (seed, 1000, Blocking::B64, lossy);
        CHECK (ref.voiceBal == "");
        CHECK (ref.midiBal == "");
        for (Blocking b : { Blocking::B4096, Blocking::Random, Blocking::Random2 })
        {
            const Result other = playScript (seed, 1000, b, lossy);
            const std::string dv = firstDifference (ref.voice, other.voice);
            const std::string dm = firstDifference (ref.midi, other.midi);
            if (! dv.empty() || ! dm.empty()) { ++mismatches; std::printf ("seed %llu blocking %d: %s | %s\n", (unsigned long long) seed, (int) b, dv.c_str(), dm.c_str()); }
        }
    }
    rvNoNextBar = false;
    CHECK (mismatches == 0);
}

namespace
{
struct Interval { int note; std::int64_t on; std::int64_t off; int vel; bool operator== (const Interval&) const = default; };

std::vector<Interval> mergeTouching (std::vector<Interval> v)
{
    std::sort (v.begin(), v.end(), [] (const Interval& a, const Interval& b) { return a.note != b.note ? a.note < b.note : a.on < b.on; });
    std::vector<Interval> r;
    for (const Interval& i : v)
    {
        // A note cannot overlap itself on MIDI: a retrigger inside a slide's 1-sample overlap ends it first.
        if (! r.empty() && r.back().note == i.note && r.back().off > i.on) r.back().off = i.on;
        if (! r.empty() && r.back().note == i.note && r.back().off == i.on) { r.back().off = std::max (r.back().off, i.off); continue; }
        r.push_back (i);
    }
    std::sort (r.begin(), r.end(), [] (const Interval& a, const Interval& b) { return a.on != b.on ? a.on < b.on : a.note < b.note; });
    return r;
}

std::vector<Interval> fromVoice (const std::vector<Ev>& ev)
{
    std::vector<Interval> r;
    int cur = -1; std::int64_t start = 0; int vel = 0;
    for (const Ev& e : ev)
    {
        if (e.type == VoiceEvent::Type::NoteOn)
        {
            const int v = e.accent ? 96 : 32;
            if (e.legato && cur >= 0)
            {
                if (e.note == cur) { if (v != vel) { r.push_back ({ cur, start, e.sample, vel }); start = e.sample; vel = v; } continue; }
                r.push_back ({ cur, start, e.sample + 1, vel });
            }
            else if (cur >= 0)
                r.push_back ({ cur, start, e.sample, vel });
            cur = e.note; start = e.sample; vel = v;
        }
        else if ((e.type == VoiceEvent::Type::NoteOff || e.type == VoiceEvent::Type::AllOff) && cur >= 0)
        {
            r.push_back ({ cur, start, e.sample, vel });
            cur = -1;
        }
    }
    return mergeTouching (r);
}

std::vector<Interval> fromMidi (const std::vector<MidiEv>& ev)
{
    std::vector<Interval> r;
    std::vector<std::pair<std::int64_t, int>> open (128, { -1, 0 });
    for (const MidiEv& e : ev)
    {
        const int t = e.status & 0xF0;
        if (t == 0x90 && e.d2 > 0) open[static_cast<std::size_t> (e.d1)] = { e.sample, e.d2 };
        else if (t == 0x80 || t == 0x90)
        {
            auto& o = open[static_cast<std::size_t> (e.d1)];
            if (o.first >= 0) r.push_back ({ e.d1, o.first, e.sample, o.second });
            o.first = -1;
        }
    }
    return mergeTouching (r);
}
}

TEST_CASE ("Review: MIDI out mirrors the voice (note intervals and velocities)", "[review][mirror]")
{
    rvMirrorOnly = true;
    int bad = 0;
    std::size_t total = 0;
    for (std::uint64_t seed = 300; seed < 360; ++seed)
    {
        const Result r = playScript (seed, 1000, Blocking::Random, seed % 3 == 0);
        const auto a = fromVoice (r.voice);
        const auto b = fromMidi (r.midi);
        total += a.size();
        std::size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        if (i != a.size() || a.size() != b.size())
        {
            ++bad;
            if (bad < 6)
            {
                std::printf ("seed %llu: interval %zu of %zu/%zu differs\n", (unsigned long long) seed, i, a.size(), b.size());
                for (std::size_t k = (i > 2 ? i - 2 : 0); k < i + 3; ++k)
                    std::printf ("   voice %s  midi %s\n",
                        k < a.size() ? (std::to_string (a[k].note) + "@" + std::to_string (a[k].on) + "-" + std::to_string (a[k].off) + " v" + std::to_string (a[k].vel)).c_str() : "-",
                        k < b.size() ? (std::to_string (b[k].note) + "@" + std::to_string (b[k].on) + "-" + std::to_string (b[k].off) + " v" + std::to_string (b[k].vel)).c_str() : "-");
            }
        }
    }
    rvMirrorOnly = false;
    INFO ("intervals " << total);
    CHECK (total > 10000);
    CHECK (bad == 0);
}

TEST_CASE ("Review: dump one mirror session window", "[.review-dump]")
{
    rvMirrorOnly = true;
    const std::uint64_t seed = std::strtoull (std::getenv ("RV_SEED"), nullptr, 10);
    const std::int64_t at = std::strtoll (std::getenv ("RV_SAMPLE"), nullptr, 10);
    rvTraceSeed = seed;
    rvTraceSample = at;
    const Result r = playScript (seed, 1000, Blocking::Random, seed % 3 == 0);
    rvMirrorOnly = false;
    for (const Ev& e : r.voice)
        if (e.sample >= at - 2000 && e.sample <= at + 2000) { std::ostringstream o; o << e; std::printf ("V %s\n", o.str().c_str()); }
    for (const MidiEv& e : r.midi)
        if (e.sample >= at - 2000 && e.sample <= at + 2000) { std::ostringstream o; o << e; std::printf ("M %s\n", o.str().c_str()); }
}
