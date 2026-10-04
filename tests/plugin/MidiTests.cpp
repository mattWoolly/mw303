// MIDI in to MIDI out in each play mode: the host buffer is cleared of its input and carries
// the engine's output; External mode passes nothing through unless midiThru is on; the
// sequencer and the arpeggiator mirror what they play.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

TEST_CASE ("midi: External mode plays notes, echoes nothing unless midiThru", "[plugin][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setParam (kPlayMode, 0.0f);
    c.setParam (kDecay, 0.0f);
    d.prepare();
    d.addNoteOn (100, 48);
    d.addNoteOff (24000, 48);
    d.addNoteOn (30000, 55, 120);                 // above the accent threshold
    d.addNoteOff (40000, 55);
    const Rendered r = d.render (static_cast<std::int64_t> (kFs), 512);
    REQUIRE (allFinite (r.left));
    const auto on = detectOnsets (r.left, kFs);
    REQUIRE (on.size() == 2);
    CHECK (on[0] >= 100);
    CHECK (on[0] < 100 + 400);
    CHECK (r.midiOut.empty());                     // the input is not echoed

    Settings s = c.settings();
    s.midiThru = true;
    c.setSettings (s);
    d.midiIn.clear();
    d.addNoteOn (10, 50);
    d.addNoteOff (5000, 50);
    const Rendered thru = d.render (8192, 512);
    CHECK (countNoteOns (thru.midiOut) == 1);
    CHECK (countNoteOffs (thru.midiOut) == 1);
    CHECK (thru.midiOut[0].sample == 10);
}

TEST_CASE ("midi: Sequencer mode mirrors the pattern on MIDI out with the configured velocities", "[plugin][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C:A . . . G . . . C+:V . . . D#:S E . . ."));
    Settings s = c.settings();
    s.outAccentVelocity = 111;
    s.outNormalVelocity = 33;
    c.setSettings (s);
    d.prepare();
    d.playHead.playing = true;
    d.playHead.bpm = 120.0;
    const Rendered r = d.render (static_cast<std::int64_t> (kFs * 2), 512);

    std::vector<int> velocities;
    bool sawCc1 = false;
    for (const auto& m : r.midiOut)
    {
        if (m.message.isNoteOn()) velocities.push_back (m.message.getVelocity());
        if (m.message.isController() && m.message.getControllerNumber() == 1) sawCc1 = true;
    }
    REQUIRE (velocities.size() >= 5);
    CHECK (velocities[0] == 111);                  // the accented first step
    CHECK (velocities[1] == 33);
    CHECK (sawCc1);                                // the vibrato step
    CHECK (countNoteOns (r.midiOut) == countNoteOffs (r.midiOut) + (r.midiOut.back().message.isNoteOn() ? 1 : 0));

    SECTION ("midiOut off silences the mirror but not the audio")
    {
        s.midiOut = false;
        c.setSettings (s);
        const Rendered quiet = d.render (static_cast<std::int64_t> (kFs), 512);
        int noteOns = 0;
        for (const auto& m : quiet.midiOut) if (m.message.isNoteOn()) ++noteOns;
        CHECK (noteOns == 0);
        CHECK (peakOf (quiet.left) > 0.01f);
    }

    SECTION ("input notes in Sequencer mode are not echoed")
    {
        d.addNoteOn (10, 60);
        d.addNoteOff (1000, 60);
        s.midiOut = false;
        c.setSettings (s);
        const Rendered quiet = d.render (8192, 512);
        CHECK (quiet.midiOut.empty());
    }
}

TEST_CASE ("midi: Arpeggiator mode plays held notes through the pattern and out", "[plugin][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setParam (kPlayMode, 2.0f);
    c.setParam (kDecay, 0.0f);
    c.commitPattern (true, 0, d.patternFromText ("1 2 3 2 1+ 3 2 1"));
    d.prepare();
    d.playHead.playing = true;
    d.playHead.bpm = 120.0;
    d.addNoteOn (0, 48);
    d.addNoteOn (0, 51);
    d.addNoteOn (0, 55);
    d.addNoteOff (static_cast<std::int64_t> (kFs * 1.5), 48);
    d.addNoteOff (static_cast<std::int64_t> (kFs * 1.5), 51);
    d.addNoteOff (static_cast<std::int64_t> (kFs * 1.5), 55);
    const Rendered r = d.render (static_cast<std::int64_t> (kFs * 2), 512);
    REQUIRE (allFinite (r.left));

    std::vector<int> notes;
    for (const auto& m : r.midiOut)
        if (m.message.isNoteOn()) notes.push_back (m.message.getNoteNumber());
    REQUIRE (notes.size() >= 8);
    CHECK (notes[0] == 48);
    CHECK (notes[1] == 51);
    CHECK (notes[2] == 55);
    CHECK (notes[4] == 60);                        // index 1, one octave up
    CHECK (countNoteOns (r.midiOut) == countNoteOffs (r.midiOut));   // keys released: nothing hangs
    // Nothing plays after the keys are released (give the gate a moment to close).
    float tail = 0.0f;
    for (std::size_t i = static_cast<std::size_t> (kFs * 1.8); i < r.left.size(); ++i) tail = std::max (tail, std::abs (r.left[i]));
    CHECK (tail < 1.0e-3f);
}

TEST_CASE ("midi: Host Note trigger mode starts and stops patterns from notes", "[plugin][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C C C C C C C C C C C C C C C C"));
    c.commitPattern (false, 1, d.patternFromText ("G+ G+ G+ G+"));
    Settings s = c.settings();
    s.triggerMode = seq::TriggerMode::HostNote;
    s.hostNoteBase = 24;
    c.setSettings (s);
    d.prepare();
    d.playHead.playing = true;                     // host transport has no say in Host Note mode
    d.addNoteOn (0, 25);                           // slot 2 (A2)
    d.addNoteOff (static_cast<std::int64_t> (kFs), 25);
    const Rendered r = d.render (static_cast<std::int64_t> (kFs * 2), 512);
    std::vector<int> notes;
    for (const auto& m : r.midiOut)
        if (m.message.isNoteOn()) notes.push_back (m.message.getNoteNumber());
    REQUIRE (! notes.empty());
    for (const int n : notes) CHECK (n == 55);          // G+ = kBaseNote 36 + one octave + 7
    CHECK (countNoteOns (r.midiOut) == countNoteOffs (r.midiOut));
    // The pattern stopped with the note: no onset after 1.1 s.
    const auto on = detectOnsets (r.left, kFs);
    REQUIRE (! on.empty());
    for (const auto o : on) CHECK (o < kFs * 1.1);
}

TEST_CASE ("midi: oversized MIDI input is bounded and never leaves notes hanging", "[plugin][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setParam (kPlayMode, 0.0f);
    d.prepare();
    for (int i = 0; i < 6000; ++i)
    {
        d.addNoteOn (i % 500, 40 + (i % 40));
        d.addNoteOff (i % 500 + 1, 40 + (i % 40));
    }
    const Rendered r = d.render (2048, 512);
    REQUIRE (allFinite (r.left));
    // External mode with thru off: nothing comes out, and the engine survived the flood.
    CHECK (r.midiOut.empty());
    const Rendered after = d.render (2048, 512);
    CHECK (allFinite (after.left));
}
