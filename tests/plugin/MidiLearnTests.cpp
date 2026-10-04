// MIDI learn: right-click Learn binds the next controller, the mapping drives the parameter
// from the audio thread without allocation, the host is told, the map lives in the state and
// in a file, and Clear removes it.
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

TEST_CASE ("midi learn: learn, apply, echo to the host, clear, persist", "[plugin][midilearn]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    d.prepare();

    struct HostSpy final : juce::AudioProcessorListener
    {
        int changes = 0;
        int lastIndex = -1;
        float lastValue = 0.0f;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value) override { ++changes; lastIndex = index; lastValue = value; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    } spy;
    d.processor.addListener (&spy);

    REQUIRE (! c.midiLearning());
    c.startMidiLearn (kCutoff);
    CHECK (c.midiLearning());
    CHECK (c.midiLearnTarget() == kCutoff);

    // The next controller moved binds; the message thread picks the result up.
    d.addController (10, 74, 64);
    d.oneBlock (512);
    CHECK (! c.midiLearning());
    CHECK (c.midiControllerFor (kCutoff) == -1);          // not yet: the message thread has not run
    d.processor.handlePendingMessages();
    CHECK (c.midiControllerFor (kCutoff) == 74);
    CHECK (c.paramForMidiController (74) == kCutoff);

    // The mapping drives the parameter on the audio thread, in natural units.
    const float before = c.getParam (kCutoff);
    d.addController (0, 74, 127);
    d.oneBlock (512);
    CHECK (c.getParam (kCutoff) == 100.0f);
    CHECK (before != 100.0f);
    const int changesBefore = spy.changes;
    d.processor.handlePendingMessages();                   // the echo tells the host
    CHECK (spy.changes > changesBefore);
    CHECK (spy.lastIndex == c.parameter (kCutoff).getParameterIndex());
    CHECK (spy.lastValue == 1.0f);
    CHECK (c.getParam (kCutoff) == 100.0f);               // and does not move it

    d.addController (0, 74, 0);
    d.oneBlock (512);
    CHECK (c.getParam (kCutoff) == 0.0f);

    // A discrete parameter snaps.
    c.setMidiMapping (20, kWave);
    d.addController (0, 20, 100);
    d.oneBlock (512);
    CHECK (c.getParam (kWave) == 1.0f);
    d.addController (0, 20, 3);
    d.oneBlock (512);
    CHECK (c.getParam (kWave) == 0.0f);

    // One controller per parameter: binding cutoff elsewhere frees 74.
    c.setMidiMapping (75, kCutoff);
    CHECK (c.paramForMidiController (74) == -1);
    CHECK (c.midiControllerFor (kCutoff) == 75);

    // The map is in the state, and comes back from it.
    const std::string state = d.stateText();
    CHECK (state.find ("<cc number=\"20\" param=\"wave\"/>") != std::string::npos);
    CHECK (state.find ("<cc number=\"75\" param=\"cutoff\"/>") != std::string::npos);
    Driver e;
    e.setStateText (state);
    CHECK (e.processor.controller().midiControllerFor (kCutoff) == 75);
    e.prepare();
    e.addController (0, 75, 127);
    e.oneBlock (512);
    CHECK (e.processor.controller().getParam (kCutoff) == 100.0f);

    // And in a file.
    std::string error;
    const auto file = c.defaultMidiMapPath();
    REQUIRE (c.saveMidiMapFile (file, &error));
    c.clearAllMidiMappings();
    CHECK (c.midiControllerFor (kCutoff) == -1);
    CHECK (c.midiControllerFor (kWave) == -1);
    CHECK (d.stateText().find ("<midi_map>") == std::string::npos);
    REQUIRE (c.loadMidiMapFile (file, &error));
    CHECK (c.midiControllerFor (kCutoff) == 75);
    CHECK (c.midiControllerFor (kWave) == 20);

    // Clear one.
    c.clearMidiMapping (kWave);
    CHECK (c.midiControllerFor (kWave) == -1);
    CHECK (c.midiControllerFor (kCutoff) == 75);
    d.addController (0, 20, 100);
    d.oneBlock (512);
    CHECK (c.getParam (kWave) == 0.0f);

    // Cancel learn.
    c.startMidiLearn (kDecay);
    c.cancelMidiLearn();
    d.addController (0, 30, 100);
    d.oneBlock (512);
    d.processor.handlePendingMessages();
    CHECK (c.midiControllerFor (kDecay) == -1);

    d.processor.removeListener (&spy);
}
