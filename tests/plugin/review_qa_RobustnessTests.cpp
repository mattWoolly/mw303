// QA robustness review (independent): deterministic reproductions of defects the concurrent
// host simulation (review_qa_HostSim.cpp) found. Each case states the contract it checks; a
// failing case is a finding.
#include "PluginTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace ptest;

namespace
{
/** Note-on minus note-off per channel and key over a stream of host MIDI output. All Notes
    Off / All Sound Off close their channel (a receiver treats them that way). */
struct Balance
{
    std::array<std::array<int, 128>, 16> on {};

    void feed (const std::vector<TimedMidi>& midi)
    {
        for (const auto& t : midi)
        {
            const auto& m = t.message;
            if (m.isNoteOn()) ++on[static_cast<std::size_t> (m.getChannel() - 1)][static_cast<std::size_t> (m.getNoteNumber())];
            else if (m.isNoteOff())
            {
                int& b = on[static_cast<std::size_t> (m.getChannel() - 1)][static_cast<std::size_t> (m.getNoteNumber())];
                if (b > 0) --b;
            }
            else if (m.isController() && (m.getControllerNumber() == 123 || m.getControllerNumber() == 120))
                on[static_cast<std::size_t> (m.getChannel() - 1)].fill (0);
        }
    }

    int sounding() const
    {
        int s = 0;
        for (const auto& ch : on) for (int b : ch) s += b;
        return s;
    }
};
}

// Contract: Engine.h, MidiOutBuffer: "When it is too small the stream stays balanced ... a
// note-off whose note-on was dropped is dropped with it". MIDI thru in External mode must never
// leave a note on at the output when every key the host sent was released.
//
// Repro: External mode, MIDI thru on. One 1024-sample block carries 20 note-ons, then a burst of
// 1100 pitch-bend messages (a controller flood), then the 20 matching note-offs. The player's
// per-chunk output holds 1024 messages with 16 reserved for closing ones; Player::thru clears
// its "passed through and still on" bit even when sendMidi refused the note-off, so the refused
// note-offs are lost for good, while the engine already delivered their note-ons to the host.
TEST_CASE ("review qa: MIDI thru never strands a note-on under a controller flood", "[review][qa][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setPlayMode (seq::PlayMode::External);
    Settings s = c.settings();
    s.midiThru = true;
    s.midiOut = true;
    c.setSettings (s);
    d.processor.handlePendingMessages();
    d.prepare (kFs, 1024);

    for (int k = 0; k < 20; ++k)
        d.midiIn.push_back ({ 0, juce::MidiMessage::noteOn (2, 40 + k, static_cast<juce::uint8> (100)) });
    for (int k = 0; k < 1100; ++k)
        d.midiIn.push_back ({ 1, juce::MidiMessage::pitchWheel (2, (k * 37) % 16384) });
    for (int k = 0; k < 20; ++k)
        d.midiIn.push_back ({ 2, juce::MidiMessage::noteOff (2, 40 + k) });

    Balance b;
    b.feed (d.render (1024, 1024).midiOut);
    d.midiIn.clear();
    b.feed (d.render (static_cast<std::int64_t> (kFs), 512).midiOut);   // a second of nothing

    INFO ("notes left on at the output after every key was released: " << b.sounding());
    CHECK (b.sounding() == 0);

    // Turning thru off flushes what the player still believes is on; the stranded notes are
    // no longer in that list, so they stay on.
    s.midiThru = false;
    c.setSettings (s);
    d.processor.handlePendingMessages();
    b.feed (d.render (static_cast<std::int64_t> (kFs), 512).midiOut);
    INFO ("after thru off: " << b.sounding());
    CHECK (b.sounding() == 0);
}

// Same contract, through the plugin's own input cap: stageMidiIn keeps the first 4096 events of
// a host block and drops the rest in buffer order, closing messages included. A key released in
// the dropped tail stays on at the output (thru) until something else closes it.
TEST_CASE ("review qa: a host block over the MIDI input cap does not strand thru notes", "[review][qa][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setPlayMode (seq::PlayMode::External);
    Settings s = c.settings();
    s.midiThru = true;
    c.setSettings (s);
    d.processor.handlePendingMessages();
    d.prepare (kFs, 4096);

    Balance b;
    d.addNoteOn (0, 50);
    b.feed (d.render (4096, 4096).midiOut);
    d.midiIn.clear();
    for (int k = 0; k < 4200; ++k)                               // a flood ahead of the release
        d.midiIn.push_back ({ 1, juce::MidiMessage::channelPressureChange (1, k % 128) });
    d.midiIn.push_back ({ 2, juce::MidiMessage::noteOff (1, 50) });
    b.feed (d.render (4096, 4096).midiOut);
    d.midiIn.clear();
    b.feed (d.render (static_cast<std::int64_t> (kFs), 512).midiOut);
    INFO ("notes left on: " << b.sounding());
    CHECK (b.sounding() == 0);
}
