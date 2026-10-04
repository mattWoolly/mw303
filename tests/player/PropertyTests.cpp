// Note balance as a property: over long randomised sessions of mode changes, transport
// jumps, pattern edits, setting changes, resets and arbitrary MIDI input, every NoteOn is
// matched by a NoteOff, for the voice events and for MIDI out separately, event offsets
// stay ordered and inside the block, and nothing allocates.
#include "Harness.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

namespace
{
void randomStep (Step& s, acid::Rng& rng)
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

void randomPattern (Pattern& p, acid::Rng& rng)
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
        randomStep (s, rng);
}

struct Outcome
{
    std::size_t noteOns = 0;
    std::size_t legato = 0;
    std::size_t midiNoteOns = 0;
};

Outcome randomSession (std::uint64_t seed, int blocks)
{
    acid::Rng rng (seed);
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };

    Session s (rates[seed % 4], 512);
    s.host.bpm = 60.0 + rng.nextInt (180);

    for (int i = 0; i < 4; ++i)
    {
        randomPattern (s.seq (i), rng);
        randomPattern (s.arp (i), rng);
    }

    const int keys[] = { 24, 25, 26, 27, 48, 52, 55, 59, 60, 64, 72 };    // trigger notes and playing notes
    const std::int64_t maxBlock = 3000;
    std::size_t seen = 0;
    bool open = false;

    for (int block = 0; block < blocks; ++block)
    {
        // --- transport ---
        if (rng.chance (0.02)) { if (s.host.playing) s.host.stop(); else s.host.play(); }
        if (rng.chance (0.015)) s.host.locate (rng.nextDouble() * 68.0 - 4.0);
        if (rng.chance (0.01)) s.host.setTempo (40.0 + rng.nextInt (260));
        if (rng.chance (0.01))
        {
            s.host.looping = ! s.host.looping;
            s.host.loopStart = rng.nextInt (8) * 1.0;
            s.host.loopEnd = s.host.loopStart + 0.5 + rng.nextInt (8) * 0.75;
            s.host.splitsAtLoop = rng.chance (0.5);
            s.host.changed = true;
        }
        if (rng.chance (0.004)) s.host.newsOnlyOnChange = ! s.host.newsOnlyOnChange;
        if (rng.chance (0.003)) s.host.present = ! s.host.present;
        if (rng.chance (0.003)) s.host.sendPpq = ! s.host.sendPpq;

        // --- controls and settings ---
        if (rng.chance (0.012)) s.ctl.playMode = static_cast<PlayMode> (rng.nextInt (3));
        if (rng.chance (0.01)) s.cfg.triggerMode = static_cast<TriggerMode> (rng.nextInt (2));
        if (rng.chance (0.01)) s.cfg.launchQuant = static_cast<LaunchQuant> (rng.nextInt (3));
        if (rng.chance (0.01)) s.cfg.slideIntoRest = static_cast<SlideIntoRest> (rng.nextInt (3));
        if (rng.chance (0.01)) s.cfg.notePriority = static_cast<NotePriority> (rng.nextInt (3));
        if (rng.chance (0.01)) s.cfg.hostLink = ! s.cfg.hostLink;
        if (rng.chance (0.01)) s.cfg.followHostTransport = ! s.cfg.followHostTransport;
        if (rng.chance (0.01)) s.cfg.restartOnTrigger = ! s.cfg.restartOnTrigger;
        if (rng.chance (0.01)) s.cfg.midiOut = ! s.cfg.midiOut;
        if (rng.chance (0.01)) s.cfg.midiThru = ! s.cfg.midiThru;
        if (rng.chance (0.02)) s.ctl.run = ! s.ctl.run;
        if (rng.chance (0.03)) s.ctl.slot = rng.nextInt (4);
        if (rng.chance (0.01)) s.ctl.liveTranspose = rng.nextInt (49) - 24;
        if (rng.chance (0.01)) s.ctl.ovrNoteSet = ! s.ctl.ovrNoteSet;
        if (rng.chance (0.01)) s.ctl.ovrNoteClr = ! s.ctl.ovrNoteClr;
        if (rng.chance (0.01)) s.ctl.ovrSlideSet = ! s.ctl.ovrSlideSet;
        if (rng.chance (0.01)) s.ctl.ovrSlideClr = ! s.ctl.ovrSlideClr;
        if (rng.chance (0.01)) s.ctl.ovrAccentSet = ! s.ctl.ovrAccentSet;
        if (rng.chance (0.01)) s.ctl.ovrVibSet = ! s.ctl.ovrVibSet;

        // --- pattern edits ---
        if (rng.chance (0.02)) randomPattern (rng.chance (0.5) ? s.seq (rng.nextInt (4)) : s.arp (rng.nextInt (4)), rng);
        if (rng.chance (0.05))
        {
            Pattern& p = rng.chance (0.5) ? s.seq (rng.nextInt (4)) : s.arp (rng.nextInt (4));
            randomStep (p.steps[static_cast<std::size_t> (rng.nextInt (kMaxSteps))], rng);
            if (rng.chance (0.3)) p.length = static_cast<std::uint8_t> (1 + rng.nextInt (64));
        }

        // --- panic ---
        if (rng.chance (0.003)) s.player.reset();
        if (rng.chance (0.0005)) s.player.prepare (s.sampleRate, 512);

        // --- MIDI in: keys, pedal, controllers, in time order inside the block ---
        const std::int64_t n = 1 + rng.nextInt (rng.chance (0.1) ? static_cast<int> (maxBlock) : 400);

        if (rng.chance (0.3))
        {
            std::int64_t at = s.now;
            const int count = 1 + rng.nextInt (4);

            for (int i = 0; i < count; ++i)
            {
                at += rng.nextInt (static_cast<int> (n / count) + 1);
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

        s.run (n, static_cast<int> (maxBlock));

        // The gate the UI is told about is the gate the events describe.
        for (; seen < s.voice.size(); ++seen)
        {
            if (s.voice[seen].type == VoiceEvent::Type::NoteOn) open = true;
            if (s.voice[seen].type == VoiceEvent::Type::NoteOff || s.voice[seen].type == VoiceEvent::Type::AllOff) open = false;
        }

        INFO ("seed " << seed << " block " << block);
        REQUIRE (open == s.status.gateOpen);
        REQUIRE (s.orderViolations == 0);
        REQUIRE (s.allocations == 0);
        REQUIRE (s.overflows == 0);
    }

    // Wind down: everything released, everything stopped. The longest thing that may still
    // be sounding is the last note of an arpeggio, which ends at its own gate time.
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

    INFO ("seed " << seed);
    REQUIRE (voiceBalance (s.notes()) == "");
    REQUIRE (midiBalance (s.midi) == "");
    REQUIRE_FALSE (s.status.gateOpen);
    REQUIRE_FALSE (s.status.running);
    REQUIRE (s.orderViolations == 0);
    REQUIRE (s.allocations == 0);

    Outcome o;
    for (const Ev& e : s.voice)
        if (e.type == VoiceEvent::Type::NoteOn) { ++o.noteOns; if (e.legato) ++o.legato; }
    for (const MidiEv& e : s.midi)
        if ((e.status & 0xF0) == 0x90) ++o.midiNoteOns;
    return o;
}
}

TEST_CASE ("Property: every NoteOn is matched by a NoteOff, for the voice and for MIDI out", "[property]")
{
    Outcome total;

    for (std::uint64_t seed = 1; seed <= 150; ++seed)
    {
        const Outcome o = randomSession (seed, 4000);
        total.noteOns += o.noteOns;
        total.legato += o.legato;
        total.midiNoteOns += o.midiNoteOns;
    }

    // The sessions really did play: this is not a vacuous pass.
    REQUIRE (total.noteOns > 50000);
    REQUIRE (total.legato > 5000);
    REQUIRE (total.midiNoteOns > 20000);
}

TEST_CASE ("Property: the same random session twice gives the same events", "[property]")
{
    // Determinism: nothing in the player depends on anything but its inputs.
    auto render = [] (std::uint64_t seed)
    {
        acid::Rng rng (seed);
        Session s;
        randomPattern (s.seq (0), rng);
        randomPattern (s.seq (1), rng);
        s.seq (0).div = s.seq (1).div = StepDiv::d1_16;
        s.host.play();

        for (int block = 0; block < 3000; ++block)
        {
            if (rng.chance (0.02)) s.ctl.slot = rng.nextInt (2);
            if (rng.chance (0.01)) s.host.locate (rng.nextDouble() * 32.0);
            if (rng.chance (0.05)) randomStep (s.seq (rng.nextInt (2)).steps[static_cast<std::size_t> (rng.nextInt (16))], rng);
            s.run (1 + rng.nextInt (500), 500);
        }

        s.host.stop();
        s.run (64, 64);
        return std::make_pair (s.voice, s.midi);
    };

    for (std::uint64_t seed : { 5ull, 6ull, 7ull })
    {
        const auto a = render (seed);
        const auto b = render (seed);
        REQUIRE (a.first.size() > 100);
        REQUIRE (a == b);
    }
}
