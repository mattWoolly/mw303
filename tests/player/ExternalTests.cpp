// External play mode (the mono note handler) and MIDI out.
#include "Harness.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ptest;
using Catch::Approx;
using Kind = ExternalNotes::Action::Kind;

namespace
{
Session extSession()
{
    Session s;
    s.ctl.playMode = PlayMode::External;
    return s;
}

/** External-mode NoteOns always carry the vibrato flag: the mod wheel sets the depth. */
Ev xon (std::int64_t sample, int note, bool accent = false, bool legato = false)
{
    return on (sample, note, accent, legato, true);
}

std::vector<Ev> ofType (const Session& s, VoiceEvent::Type type)
{
    std::vector<Ev> r;
    for (const Ev& e : s.voice)
        if (e.type == type) r.push_back (e);
    return r;
}
}

TEST_CASE ("ExternalNotes: last-note priority, legato and slide-back", "[external]")
{
    ExternalNotes x;

    auto a = x.noteOn (60, 100, NotePriority::Last);
    REQUIRE (a.kind == Kind::NoteOn);
    REQUIRE (a.note == 60);
    REQUIRE (a.velocity == 100);
    REQUIRE_FALSE (a.legato);
    REQUIRE (x.gateOpen());

    a = x.noteOn (67, 30, NotePriority::Last);          // overlap: a slide
    REQUIRE (a.kind == Kind::NoteOn);
    REQUIRE (a.note == 67);
    REQUIRE (a.legato);

    a = x.noteOn (55, 90, NotePriority::Last);
    REQUIRE (a.note == 55);
    REQUIRE (a.legato);
    REQUIRE (x.heldCount() == 3);

    REQUIRE (x.noteOff (60, NotePriority::Last).kind == Kind::None);   // not the sounding key

    a = x.noteOff (55, NotePriority::Last);             // back to the most recent key still down
    REQUIRE (a.kind == Kind::NoteOn);
    REQUIRE (a.note == 67);
    REQUIRE (a.velocity == 30);
    REQUIRE (a.legato);

    a = x.noteOff (67, NotePriority::Last);
    REQUIRE (a.kind == Kind::NoteOff);
    REQUIRE_FALSE (x.gateOpen());

    REQUIRE (x.noteOff (67, NotePriority::Last).kind == Kind::None);   // already up
    REQUIRE (x.noteOn (200, 100, NotePriority::Last).kind == Kind::None);
}

TEST_CASE ("ExternalNotes: low and high priority", "[external]")
{
    ExternalNotes low;
    REQUIRE (low.noteOn (60, 80, NotePriority::Low).note == 60);
    REQUIRE (low.noteOn (64, 80, NotePriority::Low).kind == Kind::None);     // higher: only remembered
    auto a = low.noteOn (55, 80, NotePriority::Low);
    REQUIRE (a.kind == Kind::NoteOn);
    REQUIRE (a.note == 55);
    REQUIRE (a.legato);
    a = low.noteOff (55, NotePriority::Low);
    REQUIRE (a.note == 60);                                                   // the lowest still down
    REQUIRE (a.legato);
    a = low.noteOff (60, NotePriority::Low);
    REQUIRE (a.note == 64);
    REQUIRE (low.noteOff (64, NotePriority::Low).kind == Kind::NoteOff);

    ExternalNotes high;
    REQUIRE (high.noteOn (60, 80, NotePriority::High).note == 60);
    REQUIRE (high.noteOn (50, 80, NotePriority::High).kind == Kind::None);
    a = high.noteOn (72, 80, NotePriority::High);
    REQUIRE (a.note == 72);
    REQUIRE (a.legato);
    REQUIRE (high.noteOff (60, NotePriority::High).kind == Kind::None);
    a = high.noteOff (72, NotePriority::High);
    REQUIRE (a.kind == Kind::NoteOn);
    REQUIRE (a.note == 50);
    REQUIRE (high.noteOff (50, NotePriority::High).kind == Kind::NoteOff);
}

TEST_CASE ("ExternalNotes: the sustain pedal holds the gate", "[external]")
{
    ExternalNotes x;
    x.noteOn (60, 80, NotePriority::Last);
    REQUIRE (x.sustain (true).kind == Kind::None);
    REQUIRE (x.noteOff (60, NotePriority::Last).kind == Kind::None);     // held by the pedal
    REQUIRE (x.gateOpen());
    REQUIRE (x.heldCount() == 0);

    auto a = x.noteOn (64, 120, NotePriority::Last);                     // a key under the pedal slides
    REQUIRE (a.kind == Kind::NoteOn);
    REQUIRE (a.legato);
    REQUIRE (x.noteOff (64, NotePriority::Last).kind == Kind::None);

    a = x.noteOn (64, 20, NotePriority::Last);                           // the same pitch again: legato, new velocity
    REQUIRE (a.kind == Kind::NoteOn);
    REQUIRE (a.note == 64);
    REQUIRE (a.velocity == 20);
    REQUIRE (a.legato);

    REQUIRE (x.sustain (false).kind == Kind::None);                      // the key is still down
    REQUIRE (x.sustain (true).kind == Kind::None);
    REQUIRE (x.noteOff (64, NotePriority::Last).kind == Kind::None);
    REQUIRE (x.sustain (false).kind == Kind::NoteOff);                   // pedal up with no key: the gate closes
    REQUIRE_FALSE (x.gateOpen());

    x.noteOn (60, 80, NotePriority::Last);
    x.sustain (true);
    REQUIRE (x.allNotesOff().kind == Kind::NoteOff);                     // closes whatever the pedal says
    REQUIRE_FALSE (x.gateOpen());
    REQUIRE (x.allNotesOff().kind == Kind::None);
}

TEST_CASE ("External: overlapping keys slide, releasing slides back, the last release closes the gate", "[external]")
{
    Session s = extSession();
    s.keyDown (100, 60, 100);
    s.keyDown (2000, 64, 50);
    s.keyUp (3000, 64);
    s.keyUp (4000, 60);
    s.keyDown (5000, 62, 65);
    s.keyUp (5000, 62);                        // a zero-length note keeps its order
    s.run (6000, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        xon (100, 60, true), xon (2000, 64, false, true), xon (3000, 60, true, true), off (4000),
        xon (5000, 62, true), off (5000) });
    REQUIRE (voiceBalance (s.notes()).empty());
    REQUIRE (s.midi.empty());                  // no thru, and nothing to mirror
    REQUIRE (s.allocations == 0);
    REQUIRE (s.orderViolations == 0);
}

TEST_CASE ("External: the accent threshold", "[external]")
{
    Session s = extSession();
    s.keyDown (0, 60, 64);
    s.keyUp (100, 60);
    s.keyDown (200, 60, 65);
    s.keyUp (300, 60);
    s.midiAt (400, 0x90, 60, 127);
    s.midiAt (500, 0x90, 60, 0);               // NoteOn with velocity 0 is a NoteOff
    s.run (1000, 64);
    REQUIRE (s.notes() == std::vector<Ev> { xon (0, 60, false), off (100), xon (200, 60, true), off (300), xon (400, 60, true), off (500) });

    Session t = extSession();
    t.cfg.accentThreshold = 100;
    t.keyDown (0, 60, 100);
    t.keyUp (100, 60);
    t.keyDown (200, 60, 101);
    t.keyUp (300, 60);
    t.run (1000, 64);
    REQUIRE (t.notes() == std::vector<Ev> { xon (0, 60, false), off (100), xon (200, 60, true), off (300) });
}

TEST_CASE ("External: priority modes through the player", "[external]")
{
    for (NotePriority priority : { NotePriority::Last, NotePriority::Low, NotePriority::High })
    {
        Session s = extSession();
        s.cfg.notePriority = priority;
        s.keyDown (0, 60);
        s.keyDown (100, 67);
        s.keyDown (200, 55);
        s.keyUp (300, 55);
        s.keyUp (400, 67);
        s.keyUp (500, 60);
        s.run (1000, 64);

        std::vector<Ev> expected;

        switch (priority)
        {
            case NotePriority::Last:
                expected = { xon (0, 60, true), xon (100, 67, true, true), xon (200, 55, true, true), xon (300, 67, true, true),
                             xon (400, 60, true, true), off (500) };
                break;
            case NotePriority::Low:
                expected = { xon (0, 60, true), xon (200, 55, true, true), xon (300, 60, true, true), off (500) };
                break;
            case NotePriority::High:
                expected = { xon (0, 60, true), xon (100, 67, true, true), xon (400, 60, true, true), off (500) };
                break;
        }

        REQUIRE (s.notes() == expected);
    }
}

TEST_CASE ("External: the sustain pedal", "[external]")
{
    Session s = extSession();
    s.keyDown (0, 60, 100);
    s.midiAt (100, 0xB0, 64, 127);
    s.keyUp (200, 60);                         // held
    s.keyDown (300, 60, 20);                   // the same pitch under the pedal: legato, accent off
    s.keyUp (400, 60);
    s.keyDown (500, 72, 20);                   // another pitch under the pedal: a slide
    s.keyUp (600, 72);
    s.midiAt (700, 0xB0, 64, 0);               // pedal up: the gate closes
    s.keyDown (800, 48, 20);
    s.midiAt (900, 0xB0, 64, 63);              // below 64 is up: nothing changes
    s.keyUp (1000, 48);
    s.run (2000, 64);

    REQUIRE (s.notes() == std::vector<Ev> {
        xon (0, 60, true), xon (300, 60, false, true), xon (500, 72, false, true), off (700), xon (800, 48), off (1000) });
}

TEST_CASE ("External: pitch bend is scaled by the bend range", "[external]")
{
    Session s = extSession();
    s.midiAt (0, 0xE0, 0x7F, 0x7F);            // full up
    s.midiAt (100, 0xE0, 0, 0);                // full down
    s.midiAt (200, 0xE0, 0, 64);               // centre
    s.midiAt (300, 0xE0, 0, 96);               // half up
    s.run (400, 64);

    auto bends = ofType (s, VoiceEvent::Type::PitchBend);
    REQUIRE (bends.size() == 4);
    REQUIRE (bends[0].sample == 0);
    REQUIRE (bends[0].value == Approx (2.0f));
    REQUIRE (bends[1].sample == 100);
    REQUIRE (bends[1].value == Approx (-2.0f));
    REQUIRE (bends[2].value == 0.0f);
    REQUIRE (bends[3].value == Approx (2.0 * 4096.0 / 8191.0));

    // A new range re-scales the bend that is being held.
    s.cfg.bendRange = 12;
    s.run (64, 64);
    bends = ofType (s, VoiceEvent::Type::PitchBend);
    REQUIRE (bends.size() == 5);
    REQUIRE (bends[4].sample == 400);
    REQUIRE (bends[4].value == Approx (12.0 * 4096.0 / 8191.0));

    s.cfg.bendRange = 0;
    s.midiAt (s.now + 10, 0xE0, 0x7F, 0x7F);
    s.run (64, 64);
    REQUIRE (ofType (s, VoiceEvent::Type::PitchBend).back().value == 0.0f);

    // Pitch bend also reaches the voice while a pattern plays.
    Session q;
    q.midiAt (50, 0xE0, 0x7F, 0x7F);
    q.run (64, 64);
    REQUIRE (ofType (q, VoiceEvent::Type::PitchBend).size() == 1);
}

TEST_CASE ("External: the mod wheel sets the vibrato depth", "[external]")
{
    Session s = extSession();
    s.midiAt (10, 0xB0, 1, 127);
    s.midiAt (20, 0xB0, 1, 0);
    s.midiAt (30, 0xB0, 1, 64);
    s.run (64, 64);

    const auto depth = ofType (s, VoiceEvent::Type::VibratoDepth);
    REQUIRE (depth.size() == 4);
    REQUIRE (depth[0].sample == 0);            // entering the mode: the wheel is at rest
    REQUIRE (depth[0].value == 0.0f);
    REQUIRE (depth[1].value == 1.0f);
    REQUIRE (depth[2].value == 0.0f);
    REQUIRE (depth[3].value == Approx (64.0f / 127.0f));
    REQUIRE (depth[3].sample == 30);
}

TEST_CASE ("External: all-notes-off, all-sound-off, reset controllers and system reset", "[external]")
{
    for (int message : { 123, 120, 0xFF })
    {
        Session s = extSession();
        s.keyDown (0, 60);
        s.keyDown (10, 64);
        s.midiAt (20, 0xB0, 64, 127);

        if (message == 0xFF) s.midiAt (500, 0xFF, 0, 0);
        else                 s.midiAt (500, 0xB0, message, 0);

        s.keyUp (600, 60);                     // stale
        s.keyUp (610, 64);
        s.keyDown (700, 50);                   // a fresh note is not legato
        s.midiAt (750, 0xB0, 64, 0);
        s.keyUp (800, 50);
        s.run (1000, 64);

        REQUIRE (s.notes() == std::vector<Ev> { xon (0, 60, true), xon (10, 64, true, true), off (500), xon (700, 50, true), off (800) });
    }

    Session s = extSession();
    s.midiAt (0, 0xB0, 1, 100);
    s.midiAt (0, 0xE0, 0, 0);
    s.keyDown (10, 60);
    s.midiAt (20, 0xB0, 64, 127);
    s.keyUp (30, 60);
    s.midiAt (100, 0xB0, 121, 0);              // reset all controllers: wheel, bend and pedal return to rest
    s.run (200, 64);

    REQUIRE (s.notes() == std::vector<Ev> { xon (10, 60, true), off (100) });
    REQUIRE (ofType (s, VoiceEvent::Type::VibratoDepth).back() == Ev { 100, VoiceEvent::Type::VibratoDepth, 0, false, false, false, 0.0f });
    REQUIRE (ofType (s, VoiceEvent::Type::PitchBend).back() == Ev { 100, VoiceEvent::Type::PitchBend, 0, false, false, false, 0.0f });
}

TEST_CASE ("External: any MIDI channel plays", "[external]")
{
    Session s = extSession();
    s.midiAt (0, 0x95, 60, 100);
    s.midiAt (100, 0x8A, 60, 0);
    s.run (200, 64);
    REQUIRE (s.notes() == std::vector<Ev> { xon (0, 60, true), off (100) });
}

TEST_CASE ("External: MIDI thru", "[external][midi]")
{
    SECTION ("off: nothing is sent")
    {
        Session s = extSession();
        s.keyDown (0, 60);
        s.keyUp (100, 60);
        s.run (200, 64);
        REQUIRE (s.midi.empty());
    }

    SECTION ("on: messages pass unchanged, on their own samples")
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        s.midiAt (5, 0x93, 60, 101);
        s.midiAt (40, 0xB3, 74, 12);
        s.midiAt (41, 0xE3, 1, 2);
        s.midiAt (99, 0x83, 60, 7);
        s.run (200, 64);
        REQUIRE (s.midi == std::vector<MidiEv> { { 5, 0x93, 60, 101 }, { 40, 0xB3, 74, 12 }, { 41, 0xE3, 1, 2 }, { 99, 0x83, 60, 7 } });
    }

    SECTION ("thru works even when sequencer MIDI out is disabled, and never in other modes")
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        s.cfg.midiOut = false;
        s.keyDown (0, 60);
        s.keyUp (10, 60);
        s.run (64, 64);
        REQUIRE (s.midi.size() == 2);

        Session q;
        q.cfg.midiThru = true;
        q.keyDown (0, 60);
        q.keyUp (10, 60);
        q.run (64, 64);
        REQUIRE (q.midi.empty());
    }

    SECTION ("a key held through a mode change is ended, and its late release is not passed")
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        s.keyDown (0, 60);
        s.keyDown (10, 72);
        s.keyUp (5000, 60);
        s.run (1024, 64);
        s.ctl.playMode = PlayMode::Sequencer;
        s.run (8000, 64);

        REQUIRE (s.midi == std::vector<MidiEv> { { 0, 0x90, 60, 100 }, { 10, 0x90, 72, 100 }, { 1024, 0x80, 60, 0 }, { 1024, 0x80, 72, 0 } });
        REQUIRE (midiBalance (s.midi).empty());
    }

    SECTION ("a doubled NoteOn is ended first, so on and off always pair up")
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        s.keyDown (0, 60);
        s.keyDown (10, 60);
        s.keyUp (20, 60);
        s.keyUp (30, 60);
        s.run (64, 64);
        REQUIRE (s.midi == std::vector<MidiEv> { { 0, 0x90, 60, 100 }, { 10, 0x80, 60, 0 }, { 10, 0x90, 60, 100 }, { 20, 0x80, 60, 0 } });
    }

    SECTION ("turning thru off ends the notes it passed")
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        s.keyDown (0, 60);
        s.run (64, 64);
        s.cfg.midiThru = false;
        s.run (64, 64);
        REQUIRE (s.midi == std::vector<MidiEv> { { 0, 0x90, 60, 100 }, { 64, 0x80, 60, 0 } });
    }
}

TEST_CASE ("MIDI out: velocities for accented and plain notes", "[midi]")
{
    Session s;
    setSteps (s.seq(), "C:A D");
    s.host.play();
    s.run (2 * kStep, 64);
    REQUIRE (s.midi == std::vector<MidiEv> {
        { 0, 0x90, kC, 96 }, { kGate, 0x80, kC, 0 }, { kStep, 0x90, kC + 2, 32 }, { kStep + kGate, 0x80, kC + 2, 0 } });

    s.cfg.outAccentVelocity = 127;
    s.cfg.outNormalVelocity = 90;
    s.run (2 * kStep, 64);
    REQUIRE (s.midi[4] == MidiEv { 2 * kStep, 0x90, kC, 127 });
    REQUIRE (s.midi[6] == MidiEv { 3 * kStep, 0x90, kC + 2, 90 });
    REQUIRE (s.midi.size() == 8);
}

TEST_CASE ("MIDI out: a slide chain is a chain of overlaps", "[midi]")
{
    Session s;
    setSteps (s.seq(), "C:S D:S E:S F . . . .");
    s.host.play();
    s.run (8 * kStep, 64);

    REQUIRE (s.midi == std::vector<MidiEv> {
        { 0, 0x90, kC, 32 },
        { kStep, 0x90, kC + 2, 32 }, { kStep + 1, 0x80, kC, 0 },
        { 2 * kStep, 0x90, kC + 4, 32 }, { 2 * kStep + 1, 0x80, kC + 2, 0 },
        { 3 * kStep, 0x90, kC + 5, 32 }, { 3 * kStep + 1, 0x80, kC + 4, 0 },
        { 3 * kStep + kGate, 0x80, kC + 5, 0 } });
    REQUIRE (midiBalance (s.midi).empty());
}

TEST_CASE ("MIDI out: CC1 brackets vibrato steps", "[midi]")
{
    Session s;
    setSteps (s.seq(), "C:V D:V E . F:V . . .");
    s.host.play();
    s.run (5 * kStep, 64);
    s.host.stop();
    s.run (64, 64);

    REQUIRE (s.midi == std::vector<MidiEv> {
        { 0, 0xB0, 1, 127 }, { 0, 0x90, kC, 32 }, { kGate, 0x80, kC, 0 },
        { kStep, 0x90, kC + 2, 32 }, { kStep + kGate, 0x80, kC + 2, 0 },
        { 2 * kStep, 0xB0, 1, 0 }, { 2 * kStep, 0x90, kC + 4, 32 }, { 2 * kStep + kGate, 0x80, kC + 4, 0 },
        { 4 * kStep, 0xB0, 1, 127 }, { 4 * kStep, 0x90, kC + 5, 32 }, { 4 * kStep + kGate, 0x80, kC + 5, 0 },
        { 5 * kStep, 0xB0, 1, 0 } });          // the wheel is returned to rest at the stop
}

TEST_CASE ("MIDI out: a slide to the same pitch", "[midi]")
{
    SECTION ("same accent: one long note")
    {
        Session s;
        setSteps (s.seq(), "C:S C . .");
        s.host.play();
        s.run (4 * kStep, 64);
        REQUIRE (s.midi == std::vector<MidiEv> { { 0, 0x90, kC, 32 }, { kStep + kGate, 0x80, kC, 0 } });
    }

    SECTION ("changed accent: a re-strike under the sustain pedal")
    {
        Session s;
        setSteps (s.seq(), "C:S C:AS C:S D . . . .");
        s.host.play();
        s.run (8 * kStep, 64);
        REQUIRE (s.midi == std::vector<MidiEv> {
            { 0, 0x90, kC, 32 },
            { kStep, 0xB0, 64, 127 }, { kStep, 0x80, kC, 0 }, { kStep, 0x90, kC, 96 },
            { 2 * kStep, 0x80, kC, 0 }, { 2 * kStep, 0x90, kC, 32 },
            { 3 * kStep, 0x90, kC + 2, 32 }, { 3 * kStep + 1, 0x80, kC, 0 },
            { 3 * kStep + kGate, 0x80, kC + 2, 0 }, { 3 * kStep + kGate, 0xB0, 64, 0 } });
        REQUIRE (midiBalance (s.midi).empty());
    }
}

TEST_CASE ("MIDI out: the enable switch", "[midi]")
{
    SECTION ("disabled: the voice plays, MIDI out is silent")
    {
        Session s;
        s.cfg.midiOut = false;
        setSteps (s.seq(), "C:SV D:A");
        s.host.play();
        s.run (4 * kStep, 64);
        REQUIRE (s.noteOns().size() == 4);
        REQUIRE (s.midi.empty());
    }

    SECTION ("disabled mid-note: the note is ended at the top of the block")
    {
        Session s;
        setSteps (s.seq(), "C:SV D:S");
        s.host.play();
        s.run (kStep + 1000, 64);
        s.cfg.midiOut = false;
        s.run (4 * kStep, 64);
        REQUIRE (s.midi == std::vector<MidiEv> {
            { 0, 0xB0, 1, 127 }, { 0, 0x90, kC, 32 }, { kStep, 0xB0, 1, 0 }, { kStep, 0x90, kC + 2, 32 }, { kStep + 1, 0x80, kC, 0 },
            { kStep + 1000, 0x80, kC + 2, 0 } });
    }

    SECTION ("enabled mid-note: it joins in at the next NoteOn")
    {
        Session s;
        s.cfg.midiOut = false;
        setSteps (s.seq(), "C:S D:S E F");
        s.host.play();
        s.run (kStep + 1000, 64);
        s.cfg.midiOut = true;
        s.run (4 * kStep - s.now, 64);
        REQUIRE (s.midi == std::vector<MidiEv> {
            { 2 * kStep, 0x90, kC + 4, 32 }, { 2 * kStep + kGate, 0x80, kC + 4, 0 },
            { 3 * kStep, 0x90, kC + 5, 32 }, { 3 * kStep + kGate, 0x80, kC + 5, 0 } });
    }
}

TEST_CASE ("External: MIDI thru never strands a note under a controller flood", "[external][midi][thru]")
{
    // QA finding: 20 note-ons, 1100 pitch bends, then the 20 note-offs in one 1024-sample
    // block. The bends filled the player's output, four note-offs did not fit, and the held
    // bits were cleared anyway: four notes stayed on downstream for good. Each note passed
    // through now keeps one output slot free for its note-off, so the flood is what gets cut.
    for (const int block : { 1024, 4096 })
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        for (int i = 0; i < 20; ++i) s.midiAt (0, 0x91, 40 + i, 100);
        for (int i = 0; i < 1100; ++i) s.midiAt (1, 0xE1, i & 0x7F, 64);
        for (int i = 0; i < 20; ++i) s.midiAt (2, 0x81, 40 + i, 0);
        s.run (block, block);
        s.run (48000, 512);
        INFO ("block " << block);
        CHECK (midiBalance (s.midi).empty());
        CHECK (s.overflows > 0);       // the flood was cut, and said so
        int bends = 0;
        for (const MidiEv& e : s.midi) if ((e.status & 0xF0) == 0xE0) ++bends;
        CHECK (bends > 900);           // only what the reserve needs was given up
    }

    // The same with the flood before the note-ons: a note-on that would leave no room for its
    // note-off is not passed (and its release is then not passed either).
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        for (int i = 0; i < 1100; ++i) s.midiAt (0, 0xB1, 1, i & 0x7F);
        for (int i = 0; i < 40; ++i) s.midiAt (1, 0x91, 40 + i, 100);
        for (int i = 0; i < 40; ++i) s.midiAt (2, 0x81, 40 + i, 0);
        s.run (1024, 1024);
        s.run (4800, 512);
        CHECK (midiBalance (s.midi).empty());
    }

    // Many notes held at once across blocks, each block full of pressure messages.
    {
        Session s = extSession();
        s.cfg.midiThru = true;
        for (int i = 0; i < 100; ++i) s.midiAt (0, 0x90 | (i % 16), 20 + i, 100);
        for (int b = 0; b < 4; ++b)
            for (int i = 0; i < 1500; ++i) s.midiAt (1024 * b + 1, 0xD0, i & 0x7F, 0);
        for (int i = 0; i < 100; ++i) s.midiAt (4096, 0x80 | (i % 16), 20 + i, 0);
        s.run (8192, 1024);
        CHECK (midiBalance (s.midi).empty());
    }
}
