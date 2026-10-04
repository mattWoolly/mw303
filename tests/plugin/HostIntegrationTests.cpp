// Host integration: what the QA pass found in real hosts, pinned at the processor.
//
//   - a song position rebuilt from frame, tempo and bar when the host gives no ppq (LV2)
//   - MIDI out delayed by the reported latency (VST3, CLAP)
//   - the quality tier read only in prepareToPlay, and a re-prepare asked for only when stopped
//   - host-set parameter values stored without float noise; getValue after a state restore
//   - note preview, preset audition cancel, the user default state, drag-out file paths
#include "PluginTestSupport.h"
#include "FactoryContent.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

using namespace ptest;

namespace
{
/** A host playhead in the shape the JUCE 8 LV2 wrapper delivers: playing, tempo, time
    signature, bar number and frame, and no song position. The host's own position runs
    alongside (exact, for the reference), with an optional linear tempo ramp in time. */
struct FramePlayHead final : juce::AudioPlayHead
{
    bool playing = true;
    bool withPpq = false;            // true: a host that does give ppq (the reference)
    double sampleRate = kFs;
    std::int64_t frame = 0;
    double ppq = 0.0;
    double bpmStart = 125.0;
    double bpmEnd = 125.0;           // reached at rampSamples, constant after
    double rampSamples = 1.0;

    double bpmAt (double sample) const
    {
        const double t = std::clamp (sample / rampSamples, 0.0, 1.0);
        return bpmStart + (bpmEnd - bpmStart) * t;
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpmAt (static_cast<double> (frame)));
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setTimeInSamples (frame);
        p.setBarCount (static_cast<std::int64_t> (std::floor (ppq / 4.0 + 1.0e-9)));
        if (withPpq)
            p.setPpqPosition (ppq);
        return p;
    }

    void advance (int n)
    {
        // Integrate the tempo exactly, sample by sample, as the host's own beat map does.
        for (int i = 0; i < n; ++i)
            ppq += bpmAt (static_cast<double> (frame + i) + 0.5) / (60.0 * sampleRate);
        frame += n;
    }
};

/** A pattern whose steps all differ, so the first note says where playback started. */
seq::Pattern ladder (Driver& d)
{
    return d.patternFromText ("C C# D D# E F F# G G# A A# B C+ C#+ D+ D#+");
}

std::vector<TimedMidi> renderWithFrameHead (FramePlayHead& head, std::int64_t total, int block)
{
    Driver d;
    d.processor.setPlayHead (&head);
    d.processor.controller().commitPattern (false, 0, ladder (d));
    d.processor.handlePendingMessages();
    d.prepare (kFs, block);
    std::vector<TimedMidi> out;
    for (std::int64_t pos = 0; pos < total; pos += block)
    {
        juce::AudioBuffer<float> buffer (2, block);
        buffer.clear();
        juce::MidiBuffer midi;
        d.processor.processBlock (buffer, midi);
        for (const auto meta : midi)
            out.push_back ({ pos + meta.samplePosition, meta.getMessage() });
        head.advance (block);
    }
    return out;
}

std::vector<TimedMidi> noteOns (const std::vector<TimedMidi>& midi)
{
    std::vector<TimedMidi> ons;
    for (const auto& m : midi)
        if (m.message.isNoteOn())
            ons.push_back (m);
    return ons;
}

std::string readText (const std::filesystem::path& p)
{
    std::ifstream f (p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}

//==============================================================================
TEST_CASE ("host: without ppq, a start in the middle of a bar plays the step the song position names", "[plugin][host][lv2]")
{
    ScratchConfigDir scratch;
    // REAPER rendering from beat 5.5 at 125 BPM: step 22 of the 16-step ladder, i.e. step 6.
    const double startQ = 5.5;
    const auto startFrame = static_cast<std::int64_t> (std::llround (startQ * 60.0 / 125.0 * kFs));
    const std::int64_t total = static_cast<std::int64_t> (2 * 4 * 60.0 / 125.0 * kFs);

    FramePlayHead reference;
    reference.withPpq = true;
    reference.frame = startFrame;
    reference.ppq = static_cast<double> (startFrame) * 125.0 / (60.0 * kFs);
    FramePlayHead lv2 = reference;
    lv2.withPpq = false;

    const auto refOns = noteOns (renderWithFrameHead (reference, total, 512));
    const auto lv2Ons = noteOns (renderWithFrameHead (lv2, total, 512));
    REQUIRE (refOns.size() > 8);
    REQUIRE (lv2Ons.size() == refOns.size());
    CHECK (refOns.front().message.getNoteNumber() == lv2Ons.front().message.getNoteNumber());
    for (std::size_t i = 0; i < refOns.size(); ++i)
    {
        INFO ("note " << i);
        CHECK (lv2Ons[i].sample == refOns[i].sample);
        CHECK (lv2Ons[i].message.getNoteNumber() == refOns[i].message.getNoteNumber());
    }
}

TEST_CASE ("host: without ppq, a tempo ramp is followed within a sample", "[plugin][host][lv2]")
{
    ScratchConfigDir scratch;
    // 125 to 170 BPM over 4 bars (linear in time), then 2 more bars at 170.
    FramePlayHead reference;
    reference.withPpq = true;
    reference.bpmEnd = 170.0;
    reference.rampSamples = 16.0 * 60.0 / 147.5 * kFs;
    FramePlayHead lv2 = reference;
    lv2.withPpq = false;
    const auto total = static_cast<std::int64_t> (reference.rampSamples + 8.0 * 60.0 / 170.0 * kFs);

    const auto refOns = noteOns (renderWithFrameHead (reference, total, 256));
    const auto lv2Ons = noteOns (renderWithFrameHead (lv2, total, 256));
    REQUIRE (refOns.size() > 60);
    REQUIRE (lv2Ons.size() == refOns.size());
    std::int64_t worst = 0;
    for (std::size_t i = 0; i < refOns.size(); ++i)
        worst = std::max (worst, std::abs (lv2Ons[i].sample - refOns[i].sample));
    INFO ("largest onset difference " << worst << " samples");
    CHECK (worst <= 1);
}

TEST_CASE ("host: without ppq, a tempo step on a block boundary is followed exactly", "[plugin][host][lv2]")
{
    ScratchConfigDir scratch;
    // 125 BPM for two bars, then 150 (REAPER reports a step at the block that starts on it).
    struct StepHead final : juce::AudioPlayHead
    {
        bool withPpq = false;
        std::int64_t frame = 0;
        double ppq = 0.0;
        double bpm() const { return ppq < 8.0 - 1.0e-9 ? 125.0 : 150.0; }
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (true);
            p.setBpm (bpm());
            p.setTimeSignature (TimeSignature { 4, 4 });
            p.setTimeInSamples (frame);
            p.setBarCount (static_cast<std::int64_t> (std::floor (ppq / 4.0 + 1.0e-9)));
            if (withPpq) p.setPpqPosition (ppq);
            return p;
        }
    };
    const auto run = [] (bool withPpq) {
        StepHead head;
        head.withPpq = withPpq;
        Driver d;
        d.processor.setPlayHead (&head);
        d.processor.controller().commitPattern (false, 0, ladder (d));
        d.processor.handlePendingMessages();
        d.prepare (kFs, 512);    // 2 bars at 125 BPM are 184320 samples: 360 blocks
        std::vector<TimedMidi> out;
        for (std::int64_t pos = 0; pos < 360000; pos += 512)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            d.processor.processBlock (buffer, midi);
            for (const auto meta : midi)
                out.push_back ({ pos + meta.samplePosition, meta.getMessage() });
            head.ppq += 512.0 * head.bpm() / (60.0 * kFs);
            head.frame += 512;
        }
        return noteOns (out);
    };
    const auto ref = run (true);
    const auto lv2 = run (false);
    REQUIRE (ref.size() > 40);
    REQUIRE (lv2.size() == ref.size());
    for (std::size_t i = 0; i < ref.size(); ++i)
    {
        INFO ("note " << i);
        CHECK (lv2[i].sample == ref[i].sample);
    }
}

//==============================================================================
TEST_CASE ("host: MIDI out is delayed by the reported latency when compensation is on", "[plugin][host][midi]")
{
    ScratchConfigDir scratch;
    const auto run = [] (bool compensate, int& latency) {
        Driver d;
        d.processor.setMidiOutLatencyCompensation (compensate);
        d.processor.controller().commitPattern (false, 0, ladder (d));
        d.processor.controller().setParam (kSeqRun, 1.0f);
        d.processor.handlePendingMessages();
        d.prepare (kFs, 256);
        latency = d.processor.getLatencySamples();
        return d.render (static_cast<std::int64_t> (kFs), 256).midiOut;
    };
    int latencyOff = 0, latencyOn = 0;
    const auto plain = run (false, latencyOff);
    const auto delayed = run (true, latencyOn);
    REQUIRE (latencyOn == latencyOff);
    REQUIRE (latencyOn > 0);
    REQUIRE (! plain.empty());

    // Every message, in order, latency samples later; whatever is still in the line at the
    // end of the render is what is missing at the end.
    std::size_t matched = 0;
    for (std::size_t i = 0; i < plain.size() && i < delayed.size(); ++i)
    {
        INFO ("message " << i);
        CHECK (delayed[i].sample == plain[i].sample + latencyOn);
        CHECK (delayed[i].message.getDescription() == plain[i].message.getDescription());
        ++matched;
    }
    CHECK (matched + 4 >= plain.size());
}

TEST_CASE ("host: a bypass flushes the delayed MIDI out with the note-offs", "[plugin][host][midi]")
{
    ScratchConfigDir scratch;
    Driver d;
    d.processor.setMidiOutLatencyCompensation (true);
    d.processor.controller().commitPattern (false, 0, d.patternFromText ("C:S D:S C:S D:S C:S D:S C:S D:S C:S D:S C:S D:S C:S D:S C:S D:S"));
    d.processor.controller().setParam (kSeqRun, 1.0f);
    d.processor.handlePendingMessages();
    d.prepare (kFs, 64);
    const auto played = d.render (4800, 64).midiOut;

    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    d.processor.processBlockBypassed (buffer, midi);
    std::vector<TimedMidi> all = played;
    for (const auto meta : midi)
        all.push_back ({ 0, meta.getMessage() });
    CHECK (countNoteOns (all) == countNoteOffs (all));
}

//==============================================================================
TEST_CASE ("host: the quality tier is read in prepareToPlay only, and a change waits for a stop", "[plugin][host][quality]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    Settings s = c.settings();
    s.realtimeQuality = Quality::Normal;
    s.offlineQuality = Quality::High;
    c.setSettings (s);
    d.processor.handlePendingMessages();

    struct Listener final : juce::AudioProcessorListener
    {
        int latencyRequests = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        {
            if (details.latencyChanged) ++latencyRequests;
        }
    } listener;
    d.processor.addListener (&listener);

    d.prepare (kFs, 512);
    const int latency = d.processor.getLatencySamples();
    REQUIRE (d.processor.preparedQualityTier() == Quality::Normal);
    const int requestsAfterPrepare = listener.latencyRequests;

    // Offline after prepare (CLAP render.set, LV2 freewheel): no re-prepare, no request.
    d.processor.setNonRealtime (true);
    d.playHead.playing = true;
    d.render (4096, 512, [&] (std::int64_t) { d.processor.handlePendingMessages(); });
    CHECK (d.processor.preparedQualityTier() == Quality::Normal);
    CHECK (d.processor.getLatencySamples() == latency);
    CHECK (listener.latencyRequests == requestsAfterPrepare);

    // A settings change while the host plays: still nothing.
    d.processor.setNonRealtime (false);
    s.realtimeQuality = Quality::High;
    c.setSettings (s);
    d.render (4096, 512, [&] (std::int64_t) { d.processor.handlePendingMessages(); });
    CHECK (d.processor.preparedQualityTier() == Quality::Normal);
    CHECK (listener.latencyRequests == requestsAfterPrepare);

    // Stopped: one request to the host, and the latency it reports has not moved.
    d.playHead.playing = false;
    d.render (2048, 512);
    d.processor.handlePendingMessages();
    d.processor.handlePendingMessages();
    CHECK (listener.latencyRequests == requestsAfterPrepare + 1);
    CHECK (d.processor.getLatencySamples() == latency);

    // The host answers with a prepare: the new tier.
    d.prepare (kFs, 512);
    CHECK (d.processor.preparedQualityTier() == Quality::High);
    d.processor.removeListener (&listener);
}

TEST_CASE ("host: a render that starts while the plugin runs does not depend on what played before it", "[plugin][host][quality]")
{
    // REAPER with an audio device: an LV2 instance idles (or plays) in real time, then the
    // freewheel port switches on and the render starts, with no prepare in between.
    ScratchConfigDir scratch;
    const auto render = [] (bool playFirst) {
        Driver d;
        auto& c = d.processor.controller();
        c.commitPattern (false, 0, d.patternFromText ("C:A C+:S D#:A . C G- C . C:S C+ . A# C C . C:A"));
        d.processor.handlePendingMessages();
        d.prepare (kFs, 512);
        if (playFirst)
        {
            d.addNoteOn (0, 40);
            c.setPlayMode (seq::PlayMode::External);
            d.render (static_cast<std::int64_t> (kFs * 0.7), 512);   // idle playing in real time
            d.midiIn.clear();
            c.setPlayMode (seq::PlayMode::Sequencer);
        }
        d.processor.setNonRealtime (true);
        d.playHead.playing = true;
        d.playHead.ppq = 0.0;
        d.playHead.bpm = 125.0;
        return d.render (static_cast<std::int64_t> (kFs * 2.0), 512);
    };
    const auto fresh = render (false);
    const auto afterIdle = render (true);
    std::size_t firstDiff = fresh.left.size();
    for (std::size_t i = 0; i < fresh.left.size(); ++i)
        if (fresh.left[i] != afterIdle.left[i]) { firstDiff = i; break; }
    INFO ("first differing sample " << firstDiff);
    CHECK (firstDiff == fresh.left.size());
}

TEST_CASE ("host: under CLAP the latency known at construction is what the first prepare reports at common rates", "[plugin][host][clap]")
{
    ScratchConfigDir scratch;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        mw12FProcessor p;
        p.setHostFormat (mw12FProcessor::HostFormat::Clap);
        const int before = p.getLatencySamples();
        p.setPlayConfigDetails (0, 2, rate, 512);
        p.prepareToPlay (rate, 512);
        INFO ("rate " << rate);
        CHECK (before == p.getLatencySamples());
        p.releaseResources();
    }
}

//==============================================================================
TEST_CASE ("host: a host-set value is stored without float noise", "[plugin][host][state]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& param = d.processor.controller().parameter (kCutoff);
    param.setValueNotifyingHost (0.6f);
    CHECK (d.processor.controller().getParam (kCutoff) == 60.0f);
    CHECK (d.stateText().find ("id=\"cutoff\" v=\"60\"") != std::string::npos);
    CHECK (param.getValue() == 0.6f);            // the host still reads its own number back
}

TEST_CASE ("host: getValue agrees with a VST3 host's own value through pokes, notifications and restores", "[plugin][host][state]")
{
    // The JUCE VST3 wrapper keeps the host's normalised value per parameter: a host poke sets
    // it (and calls setValue only when getValue differs), a notification from the plugin sets
    // it to the notified value (not during setState), and after a state restore
    // setComponentState copies getValue into it. pluginval's restoration test reads it,
    // pokes, restores and compares. Seeds 0xf and 0x11 ran a notified write between two
    // rounds and failed on Waveform: getValue still reported an old poke.
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    auto& p = c.parameter (kWave);

    struct HostSim final : juce::AudioProcessorListener
    {
        juce::AudioProcessorParameter& param;
        float cache;
        bool inSetState = false;
        explicit HostSim (juce::AudioProcessorParameter& pp) : param (pp), cache (pp.getValue()) {}
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float v) override
        {
            if (index == param.getParameterIndex() && ! inSetState) cache = v;
        }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void poke (float v)
        {
            cache = v;
            if (! juce::approximatelyEqual (param.getValue(), v)) param.setValue (v);
        }
        void restore (Driver& drv, const std::string& state)
        {
            inSetState = true;
            drv.setStateText (state);
            inSetState = false;
            cache = param.getValue();
        }
    } host (p);
    d.processor.addListener (&host);

    const auto round = [&] (float pokeValue, const std::string& state) {
        const float original = host.cache;
        host.poke (pokeValue);
        host.restore (d, state);
        return std::abs (host.cache - original);
    };

    host.poke (0.2f);                           // an earlier test leaves a raw number behind
    const std::string state = d.stateText();
    CHECK (round (0.1455f, state) < 0.1f);      // restoration, repeat 1
    c.setParam (kWave, 0.0f);                   // a write the host is told about
    CHECK (host.cache == p.getValue());
    CHECK (round (0.1455f, state) < 0.1f);      // restoration, repeat 2
    host.poke (0.4f);
    CHECK (round (0.9f, d.stateText()) < 0.1f); // the original is a raw number this time
    d.processor.removeListener (&host);
}

//==============================================================================
TEST_CASE ("controller: a preview note sounds while the sequencer is stopped, and never stays on", "[plugin][preview]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setPlayMode (seq::PlayMode::Sequencer);
    c.setParam (kSeqRun, 0.0f);
    d.processor.handlePendingMessages();
    d.prepare (kFs, 480);

    CHECK (peakOf (d.render (4800, 480).left) < 1.0e-4f);
    c.previewNote (45, false, true);
    CHECK (peakOf (d.render (9600, 480).left) > 1.0e-3f);
    c.stopPreview();
    d.render (static_cast<std::int64_t> (kFs * 1.5), 480);
    CHECK (peakOf (d.render (4800, 480).left) < 1.0e-4f);

    // Sequencer running (an empty pattern, so it is silent itself): no preview.
    c.setParam (kSeqRun, 1.0f);
    d.render (4800, 480);
    c.previewNote (45, true, true);
    CHECK (peakOf (d.render (9600, 480).left) < 1.0e-4f);
    c.previewNote (45, true, false);
    CHECK (c.playMode() == seq::PlayMode::Sequencer);
}

TEST_CASE ("controller: an editor that closes with a preview down leaves nothing sounding", "[plugin][preview]")
{
    ScratchConfigDir scratch;
    Driver d;
    d.processor.controller().setParam (kSeqRun, 0.0f);
    d.processor.handlePendingMessages();
    d.prepare (kFs, 480);
    d.processor.controller().previewNote (40, false, true);
    d.processor.controller().previewNote (47, true, true);    // the first one ends
    d.render (4800, 480);
    d.processor.controller().stopPreview();                   // what the editor's destructor calls
    d.render (static_cast<std::int64_t> (kFs * 1.5), 480);
    CHECK (peakOf (d.render (4800, 480).left) < 1.0e-4f);
}

//==============================================================================
TEST_CASE ("controller: cancelling an audition reverts only what the audition changed", "[plugin][presets][audition]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();

    // A synth preset with its own cutoff and resonance.
    c.setParam (kCutoff, 80.0f);
    c.setParam (kResonance, 70.0f);
    const PresetEntry* saved = c.savePreset (PresetType::Synth, "Bright", "", "", "", true);
    REQUIRE (saved != nullptr);
    const std::string id = saved->id;

    c.setParam (kCutoff, 20.0f);
    c.setParam (kResonance, 10.0f);
    c.setParam (kEnvMod, 33.0f);
    d.processor.handlePendingMessages();

    c.beginAudition();
    const PresetEntry* entry = c.presets().find (id);
    REQUIRE (entry != nullptr);
    REQUIRE (c.loadPreset (*entry));
    REQUIRE (c.getParam (kCutoff) == 80.0f);

    // Meanwhile the host automates resonance (which the audition also set) and env mod.
    c.parameter (kResonance).setValueNotifyingHost (c.parameter (kResonance).convertTo0to1 (55.0f));
    c.parameter (kEnvMod).setValueNotifyingHost (c.parameter (kEnvMod).convertTo0to1 (44.0f));

    c.endAudition (false);
    CHECK (c.getParam (kCutoff) == 20.0f);       // the audition's change: reverted
    CHECK (c.getParam (kResonance) == 55.0f);    // the host's automation since: kept
    CHECK (c.getParam (kEnvMod) == 44.0f);
}

TEST_CASE ("controller: a session the host loads during an audition is not undone by cancel", "[plugin][presets][audition]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.setParam (kCutoff, 80.0f);
    const PresetEntry* saved = c.savePreset (PresetType::Synth, "Bright", "", "", "", true);
    REQUIRE (saved != nullptr);
    const std::string id = saved->id;
    c.setParam (kCutoff, 20.0f);

    c.beginAudition();
    REQUIRE (c.loadPreset (*c.presets().find (id)));
    c.setParam (kCutoff, 66.0f);
    const std::string hostSession = d.stateText();
    d.setStateText (hostSession);                // the host restores a session
    c.endAudition (false);
    CHECK (c.getParam (kCutoff) == 66.0f);
}

//==============================================================================
TEST_CASE ("controller: a saved default state is what a new instance opens with, until reset", "[plugin][default]")
{
    ScratchConfigDir scratch;
    float factoryCutoff = 0.0f;
    {
        mw12FProcessor p;
        factoryCutoff = p.controller().getParam (kCutoff);
        REQUIRE_FALSE (p.controller().hasUserDefaultState());
        p.controller().setParam (kCutoff, 12.5f);
        p.controller().setSceneName ("My Start");
        REQUIRE (p.controller().saveDefaultState());
        CHECK (p.controller().hasUserDefaultState());
        CHECK (readText (p.controller().userDefaultStatePath()).find ("<mw12F") != std::string::npos);
    }
    {
        mw12FProcessor p;
        CHECK (p.controller().getParam (kCutoff) == 12.5f);
        CHECK (p.controller().sceneName() == "My Start");
        // The host's default for the parameter is what a new instance holds.
        CHECK (p.controller().parameter (kCutoff).getDefaultValue() == p.controller().parameter (kCutoff).convertTo0to1 (12.5f));
        p.controller().setParam (kCutoff, 90.0f);
        p.controller().initScene();
        CHECK (p.controller().getParam (kCutoff) == 12.5f);
        REQUIRE (p.controller().resetDefaultState());
        CHECK_FALSE (p.controller().hasUserDefaultState());
    }
    {
        mw12FProcessor p;
        CHECK (p.controller().getParam (kCutoff) == factoryCutoff);
    }
}

//==============================================================================
TEST_CASE ("controller: two instances never share a drag-out file", "[plugin][midi]")
{
    ScratchConfigDir scratch;
    auto a = std::make_unique<Driver>();
    auto b = std::make_unique<Driver>();
    a->processor.controller().commitPattern (false, 0, a->patternFromText ("C D E F"));
    b->processor.controller().commitPattern (false, 0, b->patternFromText ("G A B C+"));
    const auto fa = a->processor.controller().writeDragMidiFile (false, 0);
    const auto fb = b->processor.controller().writeDragMidiFile (false, 0);
    REQUIRE_FALSE (fa.empty());
    REQUIRE_FALSE (fb.empty());
    CHECK (fa != fb);
    CHECK (fa.filename() == fb.filename());      // both keep the pattern's name
    a.reset();
    CHECK_FALSE (std::filesystem::exists (fa));
    CHECK (std::filesystem::exists (fb));
}

//==============================================================================
TEST_CASE ("controller: every hand edit path keeps its edit through a density move", "[plugin][generator]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const int slot = 3;
    c.generate (false, slot, 0xBEEFu);
    const auto drawn = c.pattern (false, slot);

    int note = -1;
    for (int i = 0; i < drawn.length && note < 0; ++i)
        if (drawn.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note)
            note = i;
    REQUIRE (note >= 0);
    const auto k = static_cast<std::size_t> (note);

    // A pitch change, an accent toggle and a text paste of the whole line.
    auto edited = drawn;
    edited.steps[k].note = static_cast<std::uint8_t> ((edited.steps[k].note + 5) % 12);
    edited.steps[k].accent = ! edited.steps[k].accent;
    c.commitPattern (false, slot, edited);
    const auto afterEdit = c.pattern (false, slot);

    // Every density knob moves.
    auto gp = c.genParams (false, slot);
    gp.amountNote = static_cast<std::uint8_t> (gp.amountNote + 3);
    gp.amountAccent = static_cast<std::uint8_t> (gp.amountAccent + 3);
    gp.amountSlide = static_cast<std::uint8_t> (gp.amountSlide + 3);
    c.setGenParams (false, slot, gp);
    const auto moved = c.pattern (false, slot);
    CHECK (moved.steps[k].note == afterEdit.steps[k].note);
    CHECK (moved.steps[k].accent == afterEdit.steps[k].accent);
    CHECK (moved.steps[k].state == afterEdit.steps[k].state);

    // A commit that brings new amounts with an edit: the edit stays, the amounts apply.
    auto both = c.pattern (false, slot);
    both.steps[k].slide = ! both.steps[k].slide;
    both.gen.amountNote = static_cast<std::uint8_t> (both.gen.amountNote - 6);
    c.commitPattern (false, slot, both);
    CHECK (c.pattern (false, slot).steps[k].slide == both.steps[k].slide);
    CHECK (c.pattern (false, slot).gen.amountNote == both.gen.amountNote);
}


TEST_CASE ("host: the CLAP latency padding delays the audio by exactly the padding", "[plugin][host][clap]")
{
    ScratchConfigDir scratch;
    const auto run = [] (bool clap, int& latency) {
        Driver d;
        if (clap) d.processor.setHostFormat (mw12FProcessor::HostFormat::Clap);
        d.processor.setMidiOutLatencyCompensation (false);
        d.processor.controller().commitPattern (false, 0, ladder (d));
        d.processor.controller().setParam (kSeqRun, 1.0f);
        d.processor.handlePendingMessages();
        d.prepare (kFs, 256);
        latency = d.processor.getLatencySamples();
        return d.render (24000, 256);
    };
    int plainLatency = 0, clapLatency = 0;
    const auto plain = run (false, plainLatency);
    const auto clap = run (true, clapLatency);
    const int pad = clapLatency - plainLatency;
    REQUIRE (pad >= 0);
    for (std::size_t i = 0; i + static_cast<std::size_t> (pad) < plain.left.size(); ++i)
    {
        if (clap.left[i + static_cast<std::size_t> (pad)] != plain.left[i] || clap.right[i + static_cast<std::size_t> (pad)] != plain.right[i])
        {
            FAIL ("sample " << i << " differs after a delay of " << pad);
            break;
        }
    }
}
