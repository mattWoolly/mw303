#pragma once
// Shared test rig for the player tests: a small model of a host (transport, loop, tempo),
// a session that feeds the Player block by block and records every event with its absolute
// sample, and checks that are used by many tests (note balance, ordering).
#include "AllocGuard.h"
#include "acid/common/Rng.h"
#include "acid/seq/Player.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace ptest
{
using namespace acid::seq;

struct Ev
{
    std::int64_t sample = 0;
    VoiceEvent::Type type = VoiceEvent::Type::NoteOff;
    int note = 0;
    bool accent = false;
    bool legato = false;
    bool vibrato = false;
    float value = 0.0f;

    bool operator== (const Ev&) const = default;
};

struct MidiEv
{
    std::int64_t sample = 0;
    int status = 0;
    int d1 = 0;
    int d2 = 0;

    bool operator== (const MidiEv&) const = default;
};

inline std::ostream& operator<< (std::ostream& os, const Ev& e)
{
    static const char* names[] = { "On", "Off", "AllOff", "Bend", "Vib" };
    os << names[static_cast<int> (e.type)] << "@" << e.sample;
    if (e.type == VoiceEvent::Type::NoteOn)
        os << " n" << e.note << (e.accent ? " acc" : "") << (e.legato ? " leg" : "") << (e.vibrato ? " vib" : "");
    if (e.type == VoiceEvent::Type::PitchBend || e.type == VoiceEvent::Type::VibratoDepth)
        os << " " << e.value;
    return os;
}

inline std::ostream& operator<< (std::ostream& os, const MidiEv& e)
{
    return os << std::hex << e.status << std::dec << "@" << e.sample << " " << e.d1 << " " << e.d2;
}

inline Ev on (std::int64_t sample, int note, bool accent = false, bool legato = false, bool vibrato = false)
{
    return { sample, VoiceEvent::Type::NoteOn, note, accent, legato, vibrato, 0.0f };
}

inline Ev off (std::int64_t sample)
{
    return { sample, VoiceEvent::Type::NoteOff, 0, false, false, false, 0.0f };
}

/** The absolute sample the player gives to an event at musical time q (quarter notes from
    sample 0): the first sample at or after it. Computed here in long double, independently. */
inline std::int64_t sampleOf (long double q, long double bpm, long double sr)
{
    return static_cast<std::int64_t> (std::ceil (q * 60.0L * sr / bpm - 1.0e-3L));
}

/** Parses "C D#:A . ~ G+:AS": notes with + or - per octave and A/S/V flags, '.' a rest,
    '~' a tie. Sets the pattern length to the number of tokens. */
inline void setSteps (Pattern& p, const std::string& text)
{
    int count = 0;
    std::size_t i = 0;

    while (i < text.size() && count < kMaxSteps)
    {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) break;

        Step s;
        const char c = text[i];

        if (c == '.')
        {
            s.state = StepState::Rest;
            ++i;
        }
        else if (c == '~')
        {
            s.state = StepState::Tie;
            ++i;
        }
        else
        {
            static const int pcs[] = { 9, 11, 0, 2, 4, 5, 7 };    // A B C D E F G
            s.state = StepState::Note;
            int pc = pcs[c - 'A'];
            ++i;
            if (i < text.size() && text[i] == '#') { ++pc; ++i; }
            int octave = 0;
            while (i < text.size() && (text[i] == '+' || text[i] == '-')) { octave += text[i] == '+' ? 1 : -1; ++i; }
            s.note = static_cast<std::uint8_t> (pc);
            s.octave = static_cast<std::int8_t> (octave);
        }

        if (i < text.size() && text[i] == ':')
        {
            ++i;

            while (i < text.size() && text[i] != ' ')
            {
                if (text[i] == 'A') s.accent = true;
                if (text[i] == 'S') s.slide = true;
                if (text[i] == 'V') s.vibrato = true;
                ++i;
            }
        }

        p.steps[static_cast<std::size_t> (count++)] = s;
    }

    p.length = static_cast<std::uint8_t> (count);
}

/** A host transport. The song position is kept as a sample count from a base, so it is
    exact at any block boundary. */
struct Host
{
    double sampleRate = 48000.0;
    bool present = true;          // false: no transport at all (standalone)
    bool playing = false;
    bool sendPpq = true;
    bool sendBpm = true;
    bool newsOnlyOnChange = false;   // a host that reports a position only when something changed
    double bpm = 120.0;
    double ppqBase = 0.0;
    std::int64_t samplesFromBase = 0;
    int timeSigNum = 4;
    int timeSigDen = 4;

    bool looping = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    bool splitsAtLoop = true;     // the host cuts its blocks at the loop point

    bool changed = true;          // something happened since the last block (newsOnlyOnChange)
    double jitter = 0.0;          // added to the reported position only

    double qps() const { return bpm / (60.0 * sampleRate); }
    double ppq() const { return ppqBase + static_cast<double> (samplesFromBase) * qps(); }

    void locate (double q) { ppqBase = q; samplesFromBase = 0; changed = true; }
    void setTempo (double newBpm) { ppqBase = ppq(); samplesFromBase = 0; bpm = newBpm; changed = true; }
    void play() { playing = true; changed = true; }
    void stop() { playing = false; changed = true; }

    std::int64_t samplesToLoopEnd() const
    {
        return static_cast<std::int64_t> (std::ceil ((loopEnd - ppq()) / qps() - 1.0e-6));
    }

    TransportInfo info()
    {
        TransportInfo t;

        if (! present || (newsOnlyOnChange && ! changed))
            return t;

        changed = false;
        t.valid = true;
        t.playing = playing;
        t.ppqValid = sendPpq;
        t.ppq = ppq() + jitter;
        t.bpmValid = sendBpm;
        t.bpm = bpm;
        t.looping = looping;
        t.loopStartPpq = loopStart;
        t.loopEndPpq = loopEnd;
        t.timeSigNum = timeSigNum;
        t.timeSigDen = timeSigDen;
        return t;
    }

    void advance (int n)
    {
        if (! playing)
            return;

        samplesFromBase += n;

        if (looping)
        {
            while (ppq() >= loopEnd - 1.0e-9)
            {
                const double over = ppq() - loopEnd;
                ppqBase = loopStart + (over > 0.0 ? over : 0.0);
                samplesFromBase = 0;

                if (splitsAtLoop)
                    changed = true;
            }
        }
    }
};

struct Session
{
    explicit Session (double sr = 48000.0, int maxBlock = 4096)
        : sampleRate (sr), bank (std::make_unique<PatternBank>()), out (std::make_unique<PlayerOutput>())
    {
        host.sampleRate = sr;
        player.prepare (sr, maxBlock);
    }

    Pattern& seq (int slot = 0) { return bank->seq[static_cast<std::size_t> (slot)]; }
    Pattern& arp (int slot = 0) { return bank->arp[static_cast<std::size_t> (slot)]; }

    /** Queues a MIDI message for an absolute sample. Messages must be queued in time order. */
    void midiAt (std::int64_t sample, int status, int d1, int d2)
    {
        MidiMsg m;
        m.size = 3;
        m.data[0] = static_cast<std::uint8_t> (status);
        m.data[1] = static_cast<std::uint8_t> (d1);
        m.data[2] = static_cast<std::uint8_t> (d2);
        midiQueue.push_back ({ sample, m });
    }

    void keyDown (std::int64_t sample, int note, int velocity = 100) { midiAt (sample, 0x90, note, velocity); }
    void keyUp (std::int64_t sample, int note) { midiAt (sample, 0x80, note, 0); }

    void runBlock (int n)
    {
        const TransportInfo t = host.info();

        blockMidi.clear();

        while (midiCursor < midiQueue.size() && midiQueue[midiCursor].first < now + n)
        {
            MidiMsg m = midiQueue[midiCursor].second;
            m.offset = static_cast<std::int32_t> (std::max<std::int64_t> (0, midiQueue[midiCursor].first - now));
            blockMidi.push_back (m);
            ++midiCursor;
        }

        {
            acid::test::ScopedAllocGuard guard;
            player.process (t, blockMidi, *bank, ctl, cfg, n, *out);
            allocations += guard.count();
        }

        int last = 0;

        for (const VoiceEvent& e : out->voice())
        {
            if (e.offset < last || e.offset >= n) ++orderViolations;
            last = e.offset;
            voice.push_back ({ now + e.offset, e.type, e.note, e.accent, e.legato, e.vibrato, e.value });
        }

        last = 0;

        for (const MidiMsg& m : out->midi())
        {
            if (m.offset < last || m.offset >= n) ++orderViolations;
            last = m.offset;
            midi.push_back ({ now + m.offset, m.data[0], m.data[1], m.data[2] });
        }

        if (out->overflowed) ++overflows;

        status = out->status;
        host.advance (n);
        now += n;
    }

    /** Runs numSamples in blocks of blockSize. A host that splits at its loop point gets its
        blocks cut there. */
    void run (std::int64_t numSamples, int blockSize = 64)
    {
        const std::int64_t end = now + numSamples;

        while (now < end)
        {
            std::int64_t n = std::min<std::int64_t> (blockSizer ? blockSizer() : blockSize, end - now);

            if (host.playing && host.looping && host.splitsAtLoop)
            {
                const std::int64_t toLoop = host.samplesToLoopEnd();
                if (toLoop > 0) n = std::min (n, toLoop);
            }

            runBlock (static_cast<int> (n));
        }
    }

    /** Note events only (NoteOn, NoteOff, AllOff). */
    std::vector<Ev> notes() const
    {
        std::vector<Ev> r;

        for (const Ev& e : voice)
            if (e.type == VoiceEvent::Type::NoteOn || e.type == VoiceEvent::Type::NoteOff || e.type == VoiceEvent::Type::AllOff)
                r.push_back (e);

        return r;
    }

    std::vector<Ev> noteOns() const
    {
        std::vector<Ev> r;

        for (const Ev& e : voice)
            if (e.type == VoiceEvent::Type::NoteOn)
                r.push_back (e);

        return r;
    }

    std::vector<MidiEv> midiNotes() const
    {
        std::vector<MidiEv> r;

        for (const MidiEv& e : midi)
            if ((e.status & 0xE0) == 0x80)
                r.push_back (e);

        return r;
    }

    double sampleRate;
    std::unique_ptr<PatternBank> bank;
    std::unique_ptr<PlayerOutput> out;
    Player player;
    PlayerControls ctl;
    PlayerConfig cfg;
    Host host;

    std::vector<Ev> voice;
    std::vector<MidiEv> midi;
    PlayerStatus status;

    std::int64_t now = 0;
    std::size_t allocations = 0;
    int orderViolations = 0;
    int overflows = 0;

    std::function<int()> blockSizer;    // when set, run() asks it for every block size

    std::vector<std::pair<std::int64_t, MidiMsg>> midiQueue;
    std::size_t midiCursor = 0;
    std::vector<MidiMsg> blockMidi = std::vector<MidiMsg> (256);
};

/** Voice events are well formed: a plain NoteOn only with the gate closed, a legato NoteOn
    and a NoteOff only with it open. Returns an empty string, or what went wrong.
    If mustEndClosed, the gate must be closed after the last event. */
inline std::string voiceBalance (const std::vector<Ev>& events, bool mustEndClosed = true)
{
    bool open = false;

    for (const Ev& e : events)
    {
        switch (e.type)
        {
            case VoiceEvent::Type::NoteOn:
                if (e.legato && ! open) return "legato NoteOn with the gate closed at sample " + std::to_string (e.sample);
                if (! e.legato && open) return "plain NoteOn with the gate open at sample " + std::to_string (e.sample);
                open = true;
                break;

            case VoiceEvent::Type::NoteOff:
                if (! open) return "NoteOff with the gate closed at sample " + std::to_string (e.sample);
                open = false;
                break;

            case VoiceEvent::Type::AllOff:
                open = false;
                break;

            case VoiceEvent::Type::PitchBend:
            case VoiceEvent::Type::VibratoDepth:
                break;
        }
    }

    if (mustEndClosed && open)
        return "the gate is still open at the end";

    return {};
}

/** MIDI out is well formed: per channel and note, on and off alternate, starting with on,
    and everything is off at the end. */
inline std::string midiBalance (const std::vector<MidiEv>& events, bool mustEndClosed = true)
{
    std::vector<int> held (16 * 128, 0);

    for (const MidiEv& e : events)
    {
        const int type = e.status & 0xF0;
        const auto key = static_cast<std::size_t> ((e.status & 0x0F) * 128 + e.d1);

        if (type == 0x90 && e.d2 > 0)
        {
            if (held[key] != 0) return "note " + std::to_string (e.d1) + " turned on twice at sample " + std::to_string (e.sample);
            held[key] = 1;
        }
        else if (type == 0x80 || type == 0x90)
        {
            if (held[key] == 0) return "note " + std::to_string (e.d1) + " turned off while off at sample " + std::to_string (e.sample);
            held[key] = 0;
        }
    }

    if (mustEndClosed)
        for (std::size_t k = 0; k < held.size(); ++k)
            if (held[k] != 0)
                return "note " + std::to_string (k % 128) + " is still on at the end";

    return {};
}

// Round numbers used all over the tests: at 120 BPM and 48 kHz a 1/16 step is 6000 samples
// and the authentic gate (half a step plus 1.35 ms = 3064.8 samples) closes 3065 samples in.
inline constexpr std::int64_t kStep = 6000;
inline constexpr std::int64_t kGate = 3065;
inline constexpr int kC = 36;    // the pattern's base note: C at octave 0
}
