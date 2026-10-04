// Independent review tests for the plugin workstream. Each case states the expectation the
// contract (docs/SPEC.md sections 1, 5, 6.6, 6.7 and docs/research/07) sets; a failing case is
// a finding, with the reproduction in the case itself.
#include "PluginTestSupport.h"
#include "acid/fx/DistortionVoicings.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>

#include <atomic>
#include <thread>

using namespace ptest;

namespace
{
const char* kBusyLine = "C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A";

void setMono (Driver& d)
{
    d.numChannels = 1;
    auto layout = d.processor.getBusesLayout();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    REQUIRE (d.processor.setBusesLayout (layout));
}

Rendered renderInternalClock (int channels, int hostBlock, int preparedHint)
{
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText (kBusyLine));
    c.setParam (kChoOn, 1.0f);           // a stereo effect, so the two channels differ
    c.setParam (kDelayOn, 1.0f);
    c.setParam (kSeqRun, 1.0f);
    d.playHead.present = false;
    if (channels == 1) setMono (d);
    d.processor.setPlayConfigDetails (0, channels, kFs, preparedHint);
    d.processor.prepareToPlay (kFs, preparedHint);
    d.blockSize = std::max (hostBlock, preparedHint);
    return d.render (static_cast<std::int64_t> (kFs), hostBlock);
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        m = std::max (m, static_cast<double> (std::abs (a[i] - b[i])));
    return m;
}
}

TEST_CASE ("review: stereo output does not depend on oversized host blocks (control)", "[review][process]")
{
    ScratchConfigDir scratch;
    const Rendered small = renderInternalClock (2, 512, 512);
    const Rendered big = renderInternalClock (2, 4096, 512);
    REQUIRE (peakOf (small.left) > 0.01f);
    CHECK (maxAbsDiff (small.left, big.left) < 1e-5);
    CHECK (maxAbsDiff (small.right, big.right) < 1e-5);
}

TEST_CASE ("review: mono output with an oversized host block matches small blocks", "[review][process]")
{
    // processBlock renders each chunk's right channel into the same scratchRight and mixes
    // only after the chunk loop, so every chunk but the last is mixed with the last chunk's
    // right channel.
    ScratchConfigDir scratch;
    const Rendered small = renderInternalClock (1, 512, 512);
    const Rendered big = renderInternalClock (1, 4096, 512);
    REQUIRE (peakOf (small.left) > 0.01f);
    INFO ("max |mono(4096-sample blocks) - mono(512-sample blocks)| = " << maxAbsDiff (small.left, big.left));
    CHECK (maxAbsDiff (small.left, big.left) < 1e-5);
}

TEST_CASE ("review: every distortion model stays selected after a second message tick", "[review][distortion]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    const auto voicings = fx::distortionVoicings();
    int unstable = 0;
    for (std::size_t i = 1; i < voicings.size(); ++i)
    {
        c.setParam (kDistModel, static_cast<float> (i));
        d.processor.handlePendingMessages();
        d.processor.handlePendingMessages();
        d.processor.handlePendingMessages();
        if (c.getParam (kDistModel) != static_cast<float> (i))
        {
            UNSCOPED_INFO ("model " << i << " (" << voicings[i].name << ") fell back to " << c.getParam (kDistModel));
            ++unstable;
        }
    }
    CHECK (unstable == 0);
}

TEST_CASE ("review: a session state loaded from a background thread keeps its dist_model", "[review][distortion][threads]")
{
    // setStateInformation from a non-message thread (pluginval "Background thread state",
    // REAPER background save/load) while the 30 Hz timer runs followDistortionModel.
    // applyPresetData updates the tracker under the mutex, then sets the parameters one by one
    // without it; a tick in between sees dist_model already at the new value but the voicing
    // rows still at the old values, calls that a hand edit, and writes dist_model = 0.
    ScratchConfigDir scratch;
    const auto voicings = fx::distortionVoicings();
    REQUIRE (voicings.size() > 10);

    auto stateWithModel = [&] (int model) {
        Driver s;
        s.processor.controller().setParam (kDistModel, static_cast<float> (model));
        s.processor.handlePendingMessages();
        REQUIRE (s.processor.controller().getParam (kDistModel) == static_cast<float> (model));
        return s.stateText();
    };
    const std::string stateA = stateWithModel (3);
    const std::string stateB = stateWithModel (9);

    // The message thread ticks at 30 Hz, so one tick lands at a random moment of a background
    // load. Modelled here: the loader runs setStateInformation, the "timer" ticks once after a
    // random delay inside the load, then again after it. A host parameter listener on
    // dist_model that takes a moment when called from the loading thread (host callbacks are
    // not free) widens the window between dist_model and the voicing rows; the race exists
    // without it (observed with a tight tick loop too).
    struct SlowListener final : juce::AudioProcessorParameter::Listener
    {
        std::thread::id quickThread = std::this_thread::get_id();
        void parameterValueChanged (int, float) override
        {
            if (std::this_thread::get_id() != quickThread)
                std::this_thread::sleep_for (std::chrono::microseconds (300));
        }
        void parameterGestureChanged (int, bool) override {}
    };
    const bool widen = GENERATE (false, true);

    Rng rng (5);
    int lost = 0;
    const int trials = 200;
    for (int t = 0; t < trials; ++t)
    {
        Driver d;
        SlowListener slow;
        if (widen) d.processor.controller().parameter (kDistModel).addListener (&slow);
        d.setStateText (stateA);
        d.processor.handlePendingMessages();
        const auto delay = std::chrono::microseconds (rng.nextInt (widen ? 700 : 200));
        std::thread loader ([&] { d.setStateText (stateB); });   // not the message thread
        const auto until = std::chrono::steady_clock::now() + delay;
        while (std::chrono::steady_clock::now() < until) {}
        d.processor.handlePendingMessages();                       // one 30 Hz tick
        loader.join();
        d.processor.handlePendingMessages();
        d.processor.handlePendingMessages();
        if (d.processor.controller().getParam (kDistModel) != 9.0f)
            ++lost;
        if (widen) d.processor.controller().parameter (kDistModel).removeListener (&slow);
    }
    INFO ("widened " << widen << ": background state loads whose dist_model was rewritten to Custom: " << lost << " of " << trials);
    CHECK (lost == 0);
}

// Updated with the QA pass's host-integration fix: the processor never re-prepares the engine
// mid-stream (re-preparing from the message timer while a host rendered dropped notes and
// moved the latency in CLAP and LV2 renders, docs/host-matrix.md). The tier is read in
// prepareToPlay only; a settings change asks the host for a new prepare while the transport
// is stopped (the Standalone re-prepares itself then), and an offline flag that arrives after
// prepare takes effect at the next prepare. These cases now pin that contract.
TEST_CASE ("review: a realtime quality change from the settings menu reaches the engine", "[review][quality]")
{
    struct Requests final : juce::AudioProcessorListener
    {
        int latency = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& c) override { latency += c.latencyChanged ? 1 : 0; }
    };

    SECTION ("a plugin asks its host for a new prepare, which applies it")
    {
        ScratchConfigDir scratch;
        Driver d;
        Requests requests;
        d.processor.addListener (&requests);
        d.prepare (kFs, 512);
        d.playHead.present = false;
        d.render (4096, 512);
        const int asked = requests.latency;

        Settings s = d.processor.controller().settings();
        s.realtimeQuality = Quality::Draft;
        d.processor.controller().setSettings (s);
        for (int i = 0; i < 10; ++i)
        {
            d.processor.handlePendingMessages();
            d.render (4096, 512);
        }
        CHECK (requests.latency == asked + 1);
        d.prepare (kFs, 512);                   // the host's answer
        CHECK (d.processor.controller().engineStatus().oversampling == voiceOversamplingFor (Quality::Draft, kFs));
        d.processor.removeListener (&requests);
    }

    SECTION ("the Standalone re-prepares itself while stopped")
    {
        ScratchConfigDir scratch;
        Driver d;
        d.processor.setHostFormat (mw12FProcessor::HostFormat::Standalone);
        d.prepare (kFs, 512);
        d.playHead.present = false;
        d.render (4096, 512);
        Settings s = d.processor.controller().settings();
        s.realtimeQuality = Quality::Draft;
        d.processor.controller().setSettings (s);
        for (int i = 0; i < 10; ++i)
        {
            d.processor.handlePendingMessages();
            d.render (4096, 512);
        }
        CHECK (d.processor.controller().engineStatus().oversampling == voiceOversamplingFor (Quality::Draft, kFs));
    }
}

TEST_CASE ("review: LV2 freewheel (setNonRealtime from processBlock's thread) selects the offline tier", "[review][quality]")
{
    // The LV2 and VST3 wrappers call setNonRealtime on the audio thread every block, with no
    // prepareToPlay around it (CLAP's render extension does the same from the main thread).
    // The flag is read at the next prepare: a render already running keeps its tier.
    ScratchConfigDir scratch;
    Driver d;
    Settings s = d.processor.controller().settings();
    s.realtimeQuality = Quality::Draft;
    s.offlineQuality = Quality::Ultra;
    d.processor.controller().setSettings (s);
    d.prepare (kFs, 512);
    d.playHead.present = false;
    d.render (4096, 512);
    REQUIRE (d.processor.controller().engineStatus().oversampling == voiceOversamplingFor (Quality::Draft, kFs));

    d.processor.setNonRealtime (true);
    for (int i = 0; i < 10; ++i)
    {
        d.processor.handlePendingMessages();
        d.render (4096, 512);
    }
    CHECK (d.processor.controller().engineStatus().oversampling == voiceOversamplingFor (Quality::Draft, kFs));
    d.prepare (kFs, 512);                       // a host that prepares while freewheeling
    CHECK (d.processor.controller().engineStatus().oversampling == voiceOversamplingFor (Quality::Ultra, kFs));
}

TEST_CASE ("review: the prepare/process handshake is a store-buffer litmus that x86 allows", "[review][threads][litmus]")
{
    // The protocol of mw12FProcessor::prepareToPlay and processBlock:
    //   prepare: prepared.store (false); while (inProcess.load() != 0) ...
    //   process: if (! prepared.load()) return; inProcess.fetch_add (1);
    //            if (! prepared.load()) return;   // then touches the engine
    // With release/acquire a store is not ordered before a later load (Dekker needs seq_cst),
    // so both sides can proceed at once: prepare sees inProcess == 0 while process sees
    // prepared == true. The processor used release/acquire (the finding) and now uses seq_cst
    // on these four operations. Both versions are counted below on the real CPU: the weak one
    // is reported (it is what x86 allows, not a guarantee), the seq_cst one must never happen.
    auto run = [] (std::memory_order storeOrder, std::memory_order loadOrder, int iterations) {
        std::atomic<bool> prepared { true };
        std::atomic<int> inProcess { 0 };
        std::atomic<int> go { 0 };
        std::atomic<int> arrived { 0 };
        int both = 0;
        std::atomic<int> sideA { -1 }, sideB { -1 };

        std::thread b ([&] {
            for (int i = 1; i <= iterations; ++i)
            {
                while (go.load (std::memory_order_acquire) != i) {}
                int r = 0;
                if (prepared.load (loadOrder))
                {
                    inProcess.fetch_add (1, storeOrder == std::memory_order_seq_cst ? std::memory_order_seq_cst : std::memory_order_acq_rel);
                    r = prepared.load (loadOrder) ? 1 : 0;
                }
                sideB.store (r, std::memory_order_relaxed);
                arrived.fetch_add (1, std::memory_order_acq_rel);
            }
        });
        for (int i = 1; i <= iterations; ++i)
        {
            prepared.store (true, std::memory_order_relaxed);
            inProcess.store (0, std::memory_order_relaxed);
            arrived.store (0, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_seq_cst);
            go.store (i, std::memory_order_release);
            prepared.store (false, storeOrder);
            const int a = inProcess.load (loadOrder) == 0 ? 1 : 0;
            while (arrived.load (std::memory_order_acquire) != 1) {}
            if (a == 1 && sideB.load (std::memory_order_relaxed) == 1)
                ++both;
        }
        b.join();
        return both;
    };

    const int iterations = 2000000;
    const int weak = run (std::memory_order_release, std::memory_order_acquire, iterations);
    const int strong = run (std::memory_order_seq_cst, std::memory_order_seq_cst, iterations);
    INFO ("both sides proceeded: release/acquire " << weak << " of " << iterations << ", seq_cst " << strong);
    WARN ("release/acquire handshake: both sides proceeded " << weak << " of " << iterations << " times (seq_cst: " << strong << ")");
    CHECK (strong == 0);
}

TEST_CASE ("review: the processor's handshake really keeps prepare and process apart", "[review][threads]")
{
    // The processor itself, hammered: prepareToPlay reallocates the scratch and the engine
    // while blocks run on another thread. Under the fixed handshake no block ever touches a
    // buffer prepare is replacing, so every block is finite and the run ends cleanly (ASan or
    // TSan builds would report a use-after-free; here it is a smoke test of the real protocol).
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText (kBusyLine));
    c.setParam (kSeqRun, 1.0f);
    d.playHead.present = false;
    d.prepare (kFs, 256);
    std::atomic<bool> stop { false };
    std::atomic<int> prepares { 0 };
    std::thread preparer ([&] {
        int k = 0;
        while (! stop.load())
        {
            d.processor.prepareToPlay (k % 2 == 0 ? 44100.0 : 48000.0, 64 + 64 * (k % 40));
            ++k;
            prepares.fetch_add (1);
        }
    });
    juce::AudioBuffer<float> buffer (2, 4096);
    juce::MidiBuffer midi;
    midi.ensureSize (8192);
    bool finite = true;
    for (int i = 0; i < 20000; ++i)
    {
        const int n = (i % 7 == 0) ? 4096 : 256;
        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), 2, n);
        midi.clear();
        block.clear();
        d.processor.processBlock (block, midi);
        for (int s = 0; s < n; s += 64)
            finite = finite && std::isfinite (block.getSample (0, s));
    }
    stop.store (true);
    preparer.join();
    CHECK (finite);
    CHECK (prepares.load() > 3);
}

TEST_CASE ("review: full state round trip restores every parameter, setting, pattern and the map", "[review][state]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    Rng rng (77);

    // Host-style normalised pokes and editor-style natural values, mixed.
    for (int i = 0; i < kNumParams; ++i)
    {
        auto& p = c.parameter (static_cast<ParamId> (i));
        if (rng.chance (0.5))
            p.setValueNotifyingHost (static_cast<float> (rng.nextDouble()));
        else
        {
            const ParamInfo& info = paramInfo (static_cast<ParamId> (i));
            c.setParam (static_cast<ParamId> (i), info.min + static_cast<float> (rng.nextDouble()) * (info.max - info.min));
        }
    }
    d.processor.handlePendingMessages();
    for (int slot = 0; slot < seq::kNumSlots; ++slot)
    {
        c.generate (false, slot, rng.nextU64());
        c.generate (true, slot, rng.nextU64());
    }
    Settings s = c.settings();
    s.triggerMode = seq::TriggerMode::HostNote;
    s.launchQuant = seq::LaunchQuant::Immediate;
    s.slideIntoRest = seq::SlideIntoRest::SustainToNextNote;
    s.notePriority = seq::NotePriority::Low;
    s.followHostTransport = false;
    s.hostLink = false;
    s.restartOnTrigger = false;
    s.internalBpm = 97.25f;
    s.hostNoteBase = 36;
    s.accentThreshold = 100;
    s.bendRange = 7;
    s.midiOut = false;
    s.midiThru = true;
    s.outAccentVelocity = 127;
    s.outNormalVelocity = 50;
    s.realtimeQuality = Quality::High;
    s.offlineQuality = Quality::Ultra;
    s.fxOrder = { 2, 0, 5, 1, 4, 3 };
    c.setSettings (s);
    c.setMidiMapping (10, kCutoff);
    c.setMidiMapping (11, kDecay);
    c.setUiValue ("edit_view", "roll");
    c.setSceneName ("Review Scene");

    const std::string text = d.stateText();
    Driver e;
    e.setStateText (text);
    auto& f = e.processor.controller();
    int paramMismatches = 0;
    for (int i = 0; i < kNumParams; ++i)
        if (f.getParam (static_cast<ParamId> (i)) != c.getParam (static_cast<ParamId> (i)))
        {
            UNSCOPED_INFO ("param " << paramInfo (static_cast<ParamId> (i)).id << " " << c.getParam (static_cast<ParamId> (i))
                           << " -> " << f.getParam (static_cast<ParamId> (i)));
            ++paramMismatches;
        }
    CHECK (paramMismatches == 0);
    int patternMismatches = 0;
    for (int slot = 0; slot < seq::kNumSlots; ++slot)
    {
        if (! seq::samePattern (f.pattern (false, slot), c.pattern (false, slot))) ++patternMismatches;
        if (! seq::samePattern (f.pattern (true, slot), c.pattern (true, slot))) ++patternMismatches;
    }
    CHECK (patternMismatches == 0);
    const Settings t = f.settings();
    CHECK (t.triggerMode == s.triggerMode);
    CHECK (t.launchQuant == s.launchQuant);
    CHECK (t.slideIntoRest == s.slideIntoRest);
    CHECK (t.notePriority == s.notePriority);
    CHECK (t.followHostTransport == s.followHostTransport);
    CHECK (t.hostLink == s.hostLink);
    CHECK (t.restartOnTrigger == s.restartOnTrigger);
    CHECK (t.internalBpm == s.internalBpm);
    CHECK (t.hostNoteBase == s.hostNoteBase);
    CHECK (t.accentThreshold == s.accentThreshold);
    CHECK (t.bendRange == s.bendRange);
    CHECK (t.midiOut == s.midiOut);
    CHECK (t.midiThru == s.midiThru);
    CHECK (t.outAccentVelocity == s.outAccentVelocity);
    CHECK (t.outNormalVelocity == s.outNormalVelocity);
    CHECK (t.realtimeQuality == s.realtimeQuality);
    CHECK (t.offlineQuality == s.offlineQuality);
    CHECK (t.fxOrder == s.fxOrder);
    CHECK (f.midiControllerFor (kCutoff) == 10);
    CHECK (f.midiControllerFor (kDecay) == 11);
    CHECK (f.uiValue ("edit_view") == "roll");
    CHECK (f.sceneName() == "Review Scene");
    CHECK (e.stateText() == text);

    // Not checked: legalValue (convertFrom0to1 (getValue())) differs from natural() in the last
    // bits for seven rows after a load (for example 200.000015 for 200). Harmless: setValue
    // keeps the exact natural value when the host hands that same normalised number back.
}

TEST_CASE ("review: a CC on a channel-mode number can be bound and then moves the parameter", "[review][midilearn]")
{
    // Learn ignores CC 120..127 (channel mode messages), but setMidiMapping and a loaded
    // midi_map accept them, after which every All Notes Off (CC 123) a host sends on stop
    // also sets the bound parameter to its minimum.
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    d.prepare();
    c.setParam (kCutoff, 50.0f);
    c.setMidiMapping (123, kCutoff);
    d.midiIn.push_back ({ 0, juce::MidiMessage::allNotesOff (1) });
    d.oneBlock (256);
    CHECK (c.paramForMidiController (123) == -1);
    CHECK (c.getParam (kCutoff) == 50.0f);

    // Nor through a map file or a session state that names one.
    xml::Element map;
    map.name = "midi_map";
    for (const int cc : { 74, 120, 123, 127 })
    {
        xml::Element row;
        row.name = "cc";
        row.setAttribute ("number", std::to_string (cc));
        row.setAttribute ("param", "cutoff");
        map.children.push_back (row);
    }
    MidiMap loaded;
    loaded.fromXml (map);
    CHECK (loaded.param[74] == static_cast<std::int16_t> (kCutoff));
    CHECK (loaded.param[120] == -1);
    CHECK (loaded.param[123] == -1);
    CHECK (loaded.param[127] == -1);

    // A controller the audio thread does honour still works (the fix did not break mapping).
    c.setMidiMapping (74, kCutoff);
    d.addController (0, 74, 127);
    d.oneBlock (256);
    CHECK (c.getParam (kCutoff) > 50.0f);
}

TEST_CASE ("review: first construction loads the default scene and reports it as default", "[review][factory]")
{
    ScratchConfigDir scratch;
    mw12FProcessor p;
    auto& c = p.controller();
    CHECK (c.sceneName() == "First Squelch");
    PresetData d;
    REQUIRE (parsePreset (findFactoryFile ("default.mw12f")->text(), d).ok);
    int wrong = 0;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        auto& hp = static_cast<HostParam&> (c.parameter (static_cast<ParamId> (i)));
        if (d.hasParam[idx] && hp.natural() != hp.legalValue (d.params[idx])) ++wrong;
        if (hp.getDefaultValue() != hp.getValue()) ++wrong;
    }
    CHECK (wrong == 0);
    CHECK (seq::samePattern (c.pattern (false, 0), [&] { seq::PatternBank b; applyBank (d, b); return b.seq[0]; }()));
}

TEST_CASE ("review: setNonRealtime per block (VST3/LV2 wrappers) while prepareToPlay runs elsewhere", "[review][threads][tsan]")
{
    // The VST3 and LV2 wrappers call setNonRealtime from the audio thread before every
    // processBlock. mw12FProcessor::setNonRealtime writes Engine::Impl::offline (a plain bool)
    // directly, while prepareToPlay on another thread writes and reads it inside
    // Engine::prepare. Meaningful under ThreadSanitizer (build-plugin-review-tsan).
    ScratchConfigDir scratch;
    Driver d;
    d.prepare (kFs, 256);
    std::atomic<bool> stop { false };
    std::thread preparer ([&] {
        int k = 0;
        while (! stop.load())
        {
            d.processor.prepareToPlay (k % 2 == 0 ? 44100.0 : 48000.0, 256);
            ++k;
            std::this_thread::yield();
        }
    });
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    for (int i = 0; i < 3000; ++i)
    {
        d.processor.setNonRealtime ((i / 100) % 2 == 1);
        midi.clear();
        buffer.clear();
        d.processor.processBlock (buffer, midi);
    }
    stop.store (true);
    preparer.join();
    SUCCEED();
}

TEST_CASE ("review: getStateInformation on a background thread during editor edits and processing", "[review][threads][tsan]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    d.prepare (kFs, 256);
    d.playHead.playing = true;
    std::atomic<bool> stop { false };
    std::atomic<int> saves { 0 };
    std::thread saver ([&] {
        while (! stop.load())
        {
            juce::MemoryBlock mb;
            d.processor.getStateInformation (mb);
            PresetData p;
            if (! parsePreset (std::string_view (static_cast<const char*> (mb.getData()), mb.getSize()), p).ok) std::abort();
            saves.fetch_add (1);
        }
    });
    std::thread audio ([&] {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        while (! stop.load())
        {
            midi.clear();
            buffer.clear();
            d.processor.processBlock (buffer, midi);
        }
    });
    Rng rng (9);
    for (int i = 0; i < 1500; ++i)
    {
        c.generate (rng.chance (0.5), rng.nextInt (seq::kNumSlots), rng.nextU64());
        Settings s = c.settings();
        s.internalBpm = static_cast<float> (60 + rng.nextInt (100));
        c.setSettings (s);
        c.setUiValue ("zoom", std::to_string (rng.nextInt (4)));
        c.setMidiMapping (rng.nextInt (100), static_cast<ParamId> (rng.nextInt (kNumParams)));
        c.setParam (static_cast<ParamId> (rng.nextInt (kNumParams)), static_cast<float> (rng.nextDouble() * 10.0));
        c.setSceneName ("s" + std::to_string (i));
        if (rng.chance (0.2)) c.undo();
        d.processor.handlePendingMessages();
    }
    stop.store (true);
    saver.join();
    audio.join();
    CHECK (saves.load() > 10);
}

TEST_CASE ("review: undo after a scene load does not reach back across the load", "[review][undo]")
{
    // Preset loads apply to the model without an undo entry and without clearing the history,
    // so the next Undo applies an entry recorded before the load on top of the loaded scene:
    // here it puts back the settings that existed before the load (a whole-Settings 'before').
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    Settings s = c.settings();
    s.internalBpm = 100.0f;
    c.setSettings (s);                      // undo entry: 120 -> 100
    std::string error;
    Settings sceneSettings = s;
    sceneSettings.internalBpm = 140.0f;
    c.setSettings (sceneSettings);
    const PresetEntry* scene = c.savePreset (PresetType::Scene, "Review Undo", "", "", "", true, &error);
    REQUIRE (scene != nullptr);
    c.undo();                               // back to 100
    REQUIRE (c.settings().internalBpm == 100.0f);
    REQUIRE (c.loadPreset (*scene, &error));
    REQUIRE (c.settings().internalBpm == 140.0f);
    // The load is one undo step: Undo puts back what was there just before it (100, not the
    // 120 from the entry recorded earlier), and Redo brings the scene back.
    REQUIRE (c.canUndo());
    CHECK (c.undoLabel() == "Load Review Undo");
    c.undo();
    INFO ("after the scene load, undo set internal_bpm to " << c.settings().internalBpm);
    CHECK (c.settings().internalBpm == 100.0f);
    REQUIRE (c.canRedo());
    c.redo();
    CHECK (c.settings().internalBpm == 140.0f);
    c.undo();
    c.undo();                               // the earlier entry: back to 120
    CHECK (c.settings().internalBpm == 120.0f);

    // A pattern preset load is a step too, and undoing it restores the slot alone.
    const seq::Pattern wasThere = c.pattern (false, c.selectedSlot());
    seq::Pattern p = wasThere;
    p.steps[0].state = seq::StepState::Note;
    p.steps[0].note = 7;
    p.steps[3].state = seq::StepState::Note;
    c.commitPattern (false, c.selectedSlot(), p);
    const PresetEntry* pat = c.savePreset (PresetType::Pattern, "Review Pattern", "", "", "", true, &error);
    REQUIRE (pat != nullptr);
    c.undo();                               // the slot is back to wasThere
    REQUIRE (seq::samePattern (c.pattern (false, c.selectedSlot()), wasThere));
    REQUIRE (c.loadPreset (*pat, &error));
    CHECK (seq::samePattern (c.pattern (false, c.selectedSlot()), p));
    c.undo();
    CHECK (seq::samePattern (c.pattern (false, c.selectedSlot()), wasThere));
    c.redo();
    CHECK (seq::samePattern (c.pattern (false, c.selectedSlot()), p));
}

TEST_CASE ("review: host bypass while a sequencer note is sounding sends its note-off", "[review][midi][bypass]")
{
    // processBlockBypassed is not overridden: JUCE's default leaves the MIDI buffer as it is
    // (input echoed to the output) and never runs the engine, so a note-on the sequencer sent
    // just before the host engaged bypass (the VST3 wrapper's Bypass parameter) gets no
    // note-off until bypass is released. The latency is also not matched (JUCE asserts).
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    c.commitPattern (false, 0, d.patternFromText ("C . . . . . . . . . . . . . . ."));   // a note, gate about 3065 samples
    d.prepare (kFs, 512);
    d.playHead.playing = true;
    d.playHead.bpm = 120.0;
    const Rendered playing = d.render (1024, 512);       // the note-on, not yet its note-off
    REQUIRE (countNoteOns (playing.midiOut) == 1);
    REQUIRE (countNoteOffs (playing.midiOut) == 0);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    int offs = 0, echoed = 0;
    bool silent = true;
    for (int i = 0; i < 200; ++i)
    {
        midi.clear();
        if (i == 10) midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, 7, 0.5f);   // whatever the host left in the buffer
        d.processor.processBlockBypassed (buffer, midi);
        silent = silent && buffer.getMagnitude (0, 512) == 0.0f;
        for (const auto meta : midi)
        {
            if (meta.getMessage().isNoteOff()) ++offs;
            if (meta.getMessage().isNoteOn()) ++echoed;
        }
        d.playHead.advance (512, kFs);
    }
    INFO ("latency " << d.processor.getLatencySamples() << ", note-offs during 2 s of bypass " << offs
          << ", input notes echoed to MIDI out " << echoed);
    CHECK (offs >= 1);
    CHECK (echoed == 0);
    CHECK (silent);

    // Bypass released: the sequencer plays again on the host's grid (one note per bar here),
    // with balanced MIDI.
    const Rendered resumed = d.render (static_cast<std::int64_t> (kFs * 4), 512);
    CHECK (peakOf (resumed.left) > 0.01f);
    CHECK (countNoteOns (resumed.midiOut) >= 2);
    CHECK (countNoteOffs (resumed.midiOut) >= 1);
}

TEST_CASE ("review: host bypass with held external notes and an arpeggio releases them", "[review][midi][bypass]")
{
    ScratchConfigDir scratch;
    for (const int mode : { 0, 2 })    // External, Arpeggiator
    {
        Driver d;
        auto& c = d.processor.controller();
        c.setParam (kPlayMode, static_cast<float> (mode));
        if (mode == 2) c.commitPattern (true, 0, d.patternFromText ("1 2 3 2+ 1 3- 2 1+"));
        d.prepare (kFs, 512);
        d.playHead.present = false;
        d.addNoteOn (0, 48);
        d.addNoteOn (0, 51);
        d.addNoteOn (0, 55);
        const Rendered held = d.render (2048, 512);
        REQUIRE (peakOf (held.left) > 0.01f);
        REQUIRE (c.engineStatus().gateOpen);
        // External notes are not mirrored to MIDI out (only the sequencer and arpeggiator are).
        const int open = countNoteOns (held.midiOut) - countNoteOffs (held.midiOut);
        if (mode == 2) REQUIRE (open >= 1);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        int offs = 0, ons = 0;
        for (int i = 0; i < 20; ++i)
        {
            midi.clear();
            buffer.clear();
            d.processor.processBlockBypassed (buffer, midi);
            for (const auto meta : midi)
            {
                if (meta.getMessage().isNoteOff()) ++offs;
                if (meta.getMessage().isNoteOn()) ++ons;
            }
        }
        INFO ("play mode " << mode << ": " << open << " notes open at bypass, " << offs << " note-offs during bypass, " << ons << " note-ons");
        CHECK (offs == open);
        CHECK (ons == 0);
        CHECK (! c.engineStatus().gateOpen);
    }
}

TEST_CASE ("review: a quality re-prepare reports the new latency and survives releaseResources", "[review][quality]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    d.prepare (kFs, 512);
    const int latencyNormal = d.processor.getLatencySamples();
    Settings s = c.settings();
    s.realtimeQuality = Quality::Ultra;
    c.setSettings (s);
    d.processor.handlePendingMessages();
    CHECK (d.processor.getLatencySamples() == latencyNormal);   // nothing moves until the host prepares again
    d.prepare (kFs, 512);
    CHECK (c.engineStatus().oversampling == voiceOversamplingFor (Quality::Ultra, kFs));
    CHECK (d.processor.getLatencySamples() == d.processor.engine().latencySamples());
    CHECK (d.processor.getLatencySamples() != latencyNormal);
    d.playHead.present = false;
    const Rendered r = d.render (4096, 512);
    CHECK (allFinite (r.left));

    // Released: the timer must not bring the engine back on its own.
    d.processor.releaseResources();
    s.realtimeQuality = Quality::Draft;
    c.setSettings (s);
    d.processor.handlePendingMessages();
    CHECK (! c.engineStatus().prepared);
    d.prepare (kFs, 512);
    CHECK (c.engineStatus().oversampling == voiceOversamplingFor (Quality::Draft, kFs));
    d.processor.handlePendingMessages();
    CHECK (c.engineStatus().oversampling == voiceOversamplingFor (Quality::Draft, kFs));
}

TEST_CASE ("review: a fresh instance with no transport runs at the default scene's internal tempo", "[review][transport]")
{
    ScratchConfigDir scratch;
    for (const bool emptyPosition : { false, true })
    {
        mw12FProcessor p;
        TestPlayHead head;
        head.present = emptyPosition;     // true: a position with nothing in it (VST3 host with no transport)
        head.playing = false;
        head.sendPpq = false;
        head.sendBpm = false;
        p.setPlayHead (&head);
        auto& c = p.controller();
        const float internal = c.settings().internalBpm;
        c.commitPattern (false, c.selectedSlot(), [] { seq::Pattern q; std::string e; seq::fromStepText ("C . C . C . C . C . C . C . C .", q, &e); return q; }());
        c.setParam (kSeqRun, 1.0f);
        p.setPlayConfigDetails (0, 2, kFs, 512);
        p.prepareToPlay (kFs, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.ensureSize (8192);
        std::vector<std::int64_t> ons;
        for (std::int64_t pos = 0; pos < static_cast<std::int64_t> (kFs * 3); pos += 512)
        {
            midi.clear();
            buffer.clear();
            p.processBlock (buffer, midi);
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn()) ons.push_back (pos + meta.samplePosition);
        }
        REQUIRE (ons.size() >= 4);
        const double perEighth = static_cast<double> (ons[3] - ons[1]) / 2.0;
        const double expected = kFs * 60.0 / static_cast<double> (internal) / 2.0;
        INFO ("empty position " << emptyPosition << ": internal_bpm " << internal << ", note spacing " << perEighth
              << " samples, expected " << expected);
        CHECK (std::abs (perEighth - expected) < 2.0);
    }
}

TEST_CASE ("review: the default scene's internal tempo survives a state round trip of a fresh instance", "[review][factory][state]")
{
    ScratchConfigDir scratch;
    mw12FProcessor p;
    REQUIRE (p.controller().settings().internalBpm == 125.0f);
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    const std::string text (static_cast<const char*> (mb.getData()), mb.getSize());
    CHECK (text.find ("internal_bpm=\"125\"") != std::string::npos);
    mw12FProcessor q;
    q.setStateInformation (mb.getData(), static_cast<int> (mb.getSize()));
    CHECK (q.controller().settings().internalBpm == 125.0f);
    // What a VST3 host does after creating an instance: select the current program.
    p.setCurrentProgram (p.getCurrentProgram());
    p.handlePendingMessages();
    CHECK (p.controller().settings().internalBpm == 125.0f);
}

TEST_CASE ("review: the undo history keeps a bounded number of bank-carrying load entries", "[review][undo]")
{
    ScratchConfigDir scratch;
    Driver d;
    auto& c = d.processor.controller();
    std::string error;
    std::vector<const PresetEntry*> scenes;
    for (int i = 0; i < 3; ++i)
    {
        seq::Pattern p = c.pattern (false, i);
        p.steps[static_cast<std::size_t> (i)].state = seq::StepState::Note;
        c.commitPattern (false, i, p);
        scenes.push_back (c.savePreset (PresetType::Scene, "Bound " + std::to_string (i), "", "", "", true, &error));
        REQUIRE (scenes.back() != nullptr);
    }
    for (int i = 0; i < 60; ++i)
        REQUIRE (c.loadPreset (*scenes[static_cast<std::size_t> (i % 3)], &error));
    int undone = 0;
    while (c.canUndo() && undone < 1000) { c.undo(); ++undone; }
    INFO ("undo steps available after 60 scene loads: " << undone);
    CHECK (undone <= static_cast<int> (Model::kBankUndoDepth));
    CHECK (undone >= 8);
}
