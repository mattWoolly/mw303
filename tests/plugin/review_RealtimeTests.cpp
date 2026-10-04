// Review: allocation in processBlock when the host's MidiBuffer has not been pre-sized.
// The Standalone's AudioProcessorPlayer passes its member `incomingMidi`, which nothing
// ensureSize()s (JUCE 8.0.15 juce_AudioProcessorPlayer.cpp); the plugin appends the
// sequencer's MIDI out to it with MidiBuffer::addEvent, which grows the Array on the audio
// thread (docs/research/07 section 4.2: "MidiBuffer::addEvent beyond capacity").
#include "AllocSpy.h"
#include "PluginTestSupport.h"
#include <catch2/catch_test_macros.hpp>

using namespace ptest;

TEST_CASE ("review realtime: sequencer MIDI out into an unsized host MidiBuffer (Standalone)", "[review][realtime]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C:A C+:S D#:A C G-:AS C:V C C:S C+ C A# C C C C:A"));
    c.setParam (kSeqRun, 1.0f);
    d.playHead.present = false;        // the Standalone has no playhead
    d.prepare (kFs, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;             // as AudioProcessorPlayer::incomingMidi: never ensureSize()d
    allocspy::violations.store (0);
    for (int i = 0; i < 2000; ++i)
    {
        midi.clear();
        buffer.clear();
        allocspy::ScopedAudioContext context;
        d.processor.processBlock (buffer, midi);
    }
    CHECK (allocspy::violations.load() == 0);
}
