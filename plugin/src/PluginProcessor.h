#pragma once
// mw12FProcessor: the JUCE adapter around acid::Engine.
//
//   host parameters  --(atomics)-->  ParamValues per block  --+
//   host transport   --> seq::TransportInfo                   +--> Engine::process --> audio + MIDI out
//   MIDI in          --> staged seq::MidiMsg array            |
//   Model (message thread, mutex) --publishBank/Settings-->   +
//
// Threads (SPEC section 5, docs/research/07 sections 1.2, 4, 5):
//   - processBlock: no allocation, no locks. It reads parameter atomics, the live MIDI map
//     (atomics) and the engine's triple-buffered snapshots. prepareToPlay can run while a
//     block is in flight in real hosts: `prepared` is dropped first, then prepareToPlay waits
//     for the in-flight block to leave before touching anything, and processBlock outputs
//     silence until `prepared` is set again. The two flags form a Dekker handshake (a store
//     on each side followed by a load of the other side's flag), so those four operations are
//     seq_cst: release/acquire lets a store be reordered after a later load and both sides
//     would proceed at once (review_PluginTests.cpp measures this on x86).
//   - prepareToPlay, releaseResources and the Standalone's quality re-prepare (followQualityTier)
//     serialise against each other with prepareLock. The audio thread never takes it.
//   - Quality tier and latency (SPEC 7): the tier is chosen in prepareToPlay from the offline
//     flag and the settings, and only there. A tier change or an offline flag that arrives
//     after prepare (CLAP's render extension and LV2's freewheel port both set it after
//     activate) takes effect at the next prepareToPlay; the processor asks the host for one
//     only while the transport and the sequencer are stopped and it is not rendering offline.
//     The latency it reports therefore only changes inside prepareToPlay, and the constructor
//     already reports the value the first prepare will produce at the default rate.
//   - MIDI out goes into the host's buffer. Its capacity cannot be read, so the output per
//     block is capped (kMaxMidiOutPerBlock) and, once per prepare, the host buffer's storage
//     is swapped with one sized in prepareToPlay (MidiBuffer::swapWith allocates nothing), so
//     an unsized host buffer (the Standalone's AudioProcessorPlayer) never grows on the audio
//     thread.
//   - The Model is edited on the message thread and read or replaced by state save and load
//     on any non-audio thread, all behind Model::mutex. Every change is published to the
//     engine right away.
//   - Message-thread housekeeping (dist_model following, MIDI learn results, echoing
//     controller moves to the host) runs from a 30 Hz timer; tests call handlePendingMessages()
//     directly because no message loop runs there.
//
// The editor talks to Controller (Controller.h), not to this class.
#include "Controller.h"
#include "Model.h"
#include "Parameters.h"
#include "PresetManager.h"
#include "acid/common/SpscQueue.h"
#include "acid/engine/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

class mw12FProcessor final : public juce::AudioProcessor
{
public:
    mw12FProcessor();
    ~mw12FProcessor() override;

    // ---- What the editor and the tests use ------------------------------------------------
    acid::plugin::Controller& controller() noexcept { return *ctl; }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }
    const acid::plugin::ParameterSet& parameterSet() const noexcept { return params; }
    acid::plugin::Model& model() noexcept { return modelData; }
    acid::plugin::PresetManager& presetManager() noexcept { return presets; }
    const acid::Engine& engine() const noexcept { return eng; }

    /** The message-thread housekeeping the 30 Hz timer runs. Safe to call from tests. */
    void handlePendingMessages();

    /** The whole session as a `state` document (any non-audio thread). */
    acid::PresetData captureState() const;

    /** Applies a parsed document of any type with the SPEC 6.7 rules, publishes, notifies
        (any non-audio thread). A `state` document also restores the UI state and MIDI map.
        hostRestore: the host's setStateInformation (see HostParam::setNatural). */
    void applyPresetData (const acid::PresetData&, bool clearUndoHistory, bool hostRestore = false);

    /** Parses and applies text; false (state unchanged) when the text is not a document. */
    bool loadStateText (std::string_view text, bool hostRestore = false);

    /** Publishes the model's bank and settings to the engine. Model::mutex must be held. */
    void publishModelLocked() noexcept;

    /** Milliseconds since setStateInformation last ran (for the setCurrentProgram guard). */
    std::uint32_t millisecondsSinceStateLoad() const noexcept;

    /** The user's default state (Controller::saveDefaultState), parsed, when there is one. */
    bool userDefaultState (acid::PresetData&) const;

    // ---- juce::AudioProcessor ----------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /** Host bypass (VST3 and LV2 wrappers, when getBypassParameter() is null). The first
        bypassed block runs the engine once with the sequencer stopped so the note-offs reach
        the host; after that the output is silence and an empty MIDI buffer. */
    using AudioProcessor::processBlockBypassed;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void setNonRealtime (bool isNonRealtime) noexcept override;

    /** Plugin format the instance runs in, for what the wrappers do differently (set once,
        right after construction, by the plugin entry point; tests may set it). */
    enum class HostFormat : std::uint8_t { Other, Vst3, Clap, Lv2, Standalone };
    void setHostFormat (HostFormat) noexcept;
    HostFormat hostFormat() const noexcept { return format; }

    /** MIDI output delayed by the reported latency, so it lines up with the audio a host
        compensates. On for VST3 and CLAP (hosts compensate their audio, not their MIDI out);
        off elsewhere. Call before prepareToPlay. */
    void setMidiOutLatencyCompensation (bool on) noexcept { midiOutDelayWanted = on; }
    bool midiOutLatencyCompensation() const noexcept { return midiOutDelayWanted; }

    /** The quality tier the engine was last prepared with (message thread, tests). */
    acid::Quality preparedQualityTier() const noexcept { return preparedQuality; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- MIDI learn plumbing used by Controller ------------------------------------------------
    /** Learn: the next controller seen on the audio thread is reported through the learn queue. */
    void setLearnTarget (int paramIndex) noexcept { learnTarget.store (paramIndex, std::memory_order_relaxed); }
    int learnTargetIndex() const noexcept { return learnTarget.load (std::memory_order_relaxed); }
    /** Copies the model's map into the atomics the audio thread reads. Model::mutex held. */
    void publishMidiMapLocked() noexcept;

private:
    friend class acid::plugin::Controller;

    struct LearnEvent { std::int16_t cc; std::int16_t param; };
    struct ParamEcho { std::int16_t param; float natural; };

    void processChunk (const acid::seq::TransportInfo&, std::span<const acid::seq::MidiMsg> midiIn,
                       float* left, float* right, int numSamples, juce::MidiBuffer& midiOutHost, int outOffset,
                       int& midiOutBudget) noexcept;
    /** Copies the host's MIDI into midiInStage from index `first` on (returns the new count)
        and empties the buffer. Over kMaxMidiInPerBlock events, closing messages are kept first. */
    int stageMidiIn (juce::MidiBuffer& midi, int numSamples, int first) noexcept;
    /** Both sides of the prepare handshake, for processBlock and processBlockBypassed:
        false means output silence. Call leaveProcess() afterwards when it returned true. */
    bool enterProcess() noexcept;
    void leaveProcess() noexcept;
    acid::seq::TransportInfo readTransport (int numSamples) noexcept;
    /** Fills in a missing song position from the host's frame, bar and tempo (the JUCE 8 LV2
        wrapper drops time:beat). Audio thread. */
    void reconstructPpq (acid::seq::TransportInfo&, const juce::AudioPlayHead::PositionInfo&, int numSamples) noexcept;
    /** Stages the preview notes the Controller queued (audio thread). Returns how many were
        written at the front of midiInStage. */
    int stagePreviewNotes (bool allowed) noexcept;
    /** Writes the engine's MIDI output into the host buffer, through the latency delay when it
        is on. Audio thread. */
    void emitMidiOut (const acid::seq::MidiMsg* messages, int count, juce::MidiBuffer& host, int outOffset) noexcept;
    void flushMidiDelay (juce::MidiBuffer& host, int numSamples, bool all) noexcept;
    /** Delays the output by padSamples (the CLAP latency floor). Audio thread. */
    void padAudio (float* const* channels, int numChannels, int numSamples) noexcept;
    void applyMidiControllers (std::span<const acid::seq::MidiMsg> midiIn) noexcept;
    void waitForProcessToLeave() noexcept;
    /** prepareToPlay's body. prepareLock must be held. */
    void prepareLocked (double sampleRate, int samplesPerBlock);
    /** The tier the current mode (realtime or offline) selects. Model::mutex must be held. */
    acid::Quality wantedQualityLocked() const noexcept;
    /** When the settings ask for another tier than the engine was prepared with, asks the
        host for a new prepareToPlay, only while nothing plays and the host is not rendering
        offline (the Standalone, which has no one to ask, re-prepares itself then). Message
        thread. */
    void followQualityTier();
    /** The latency the first prepareToPlay will report, measured on a scratch engine at the
        default rate (constructor). */
    void reportInitialLatency();
    void followDistortionModel();
    void drainQueues();
    void loadInitialState();
    /** Saves the custom distortion voicing (dist_model 0) into the tracker. Model::mutex held. */
    void rememberCustomVoicingLocked (const acid::ParamValues&) noexcept;
    static std::vector<std::string> programFiles();

    struct MessageWork final : juce::Timer
    {
        explicit MessageWork (mw12FProcessor& p) : owner (p) {}
        void timerCallback() override { owner.handlePendingMessages(); }
        mw12FProcessor& owner;
    };

    acid::plugin::ParameterSet params;
    juce::AudioProcessorValueTreeState apvts;
    acid::Engine eng;
    acid::plugin::Model modelData;
    acid::plugin::PresetManager presets;
    std::unique_ptr<acid::plugin::Controller> ctl;
    MessageWork messageWork { *this };

    // ---- audio side ----
    std::atomic<bool> prepared { false };
    std::atomic<int> inProcess { 0 };
    std::atomic<bool> offline { false };
    bool preparedOffline = false;              // the offline flag prepareToPlay saw
    bool lastBlockOffline = false;             // audio thread: the offline flag the last block saw
    std::atomic<bool> hostPlaying { false };   // the last block's transport (for followQualityTier)
    bool restartRequested = false;             // message thread: a re-prepare was asked for this mismatch
    HostFormat format = HostFormat::Other;
    std::mutex prepareLock;                    // prepareToPlay, releaseResources, followQualityTier
    int preparedBlock = 0;
    int hostBlockHint = 0;                     // what the host passed to prepareToPlay
    double preparedRate = 48000.0;
    acid::Quality preparedQuality = acid::Quality::Normal;   // the tier the engine was prepared with
    acid::ParamValues blockParams {};
    std::vector<float> scratchLeft, scratchRight;
    std::vector<acid::seq::MidiMsg> midiInStage;
    std::vector<acid::seq::MidiMsg> midiOutStage;
    juce::MidiBuffer hostMidiStorage;          // sized in prepareToPlay, swapped into the host's buffer once
    bool hostMidiSwapped = false;
    bool bypassStopSent = true;                // false: the next bypassed block must stop the sequencer
    std::array<std::atomic<std::int16_t>, acid::plugin::MidiMap::kNumControllers> liveMidiMap;
    std::atomic<int> learnTarget { -1 };
    acid::SpscQueue<LearnEvent, 64> learnQueue;
    acid::SpscQueue<ParamEcho, 1024> echoQueue;

    // ---- note preview (Controller::previewNote): message thread -> audio thread ----
    acid::SpscQueue<acid::seq::MidiMsg, 64> previewQueue;
    std::array<bool, 128> previewHeld {};      // audio thread: preview keys sounding
    int previewHeldCount = 0;
    std::int64_t previewHoldSamples = 0;       // audio thread: External override left after the last key went up
    bool previewOverride = false;              // audio thread: the block plays in External mode for a preview

    // ---- song position reconstruction (hosts that give a frame but no ppq) ----
    struct FramePpq
    {
        bool active = false;
        double ppq = 0.0;
        double bpm = 120.0;
        double change = 0.0;                   // the tempo change seen at the last block start
        std::int64_t frame = 0;
        std::int64_t nextFrame = 0;
    } framePpq;

    // ---- latency: the engine's, raised to a floor under CLAP by an audio delay ----
    int initialLatencyAllRates = 0;            // the realtime tier's largest latency at 44.1 and 48 kHz
    int latencyFloor = 0;
    int padSamples = 0;
    std::array<std::vector<float>, 2> padLines;
    std::size_t padPos = 0;

    // ---- MIDI out delayed by the latency (VST3, CLAP) ----
    struct DelayedMidi { std::int64_t due; acid::seq::MidiMsg msg; };
    bool midiOutDelayWanted = false;
    bool midiOutDelayed = false;               // what prepareToPlay settled on
    int midiDelaySamples = 0;
    std::vector<DelayedMidi> midiDelay;        // ring, sized in prepareToPlay
    std::size_t midiDelayHead = 0, midiDelayCount = 0;
    std::int64_t midiClock = 0;                // samples since prepare, for the delay
    std::atomic<std::uint32_t> stateLoadedAt { 0 };
    std::atomic<std::uint32_t> stateLoads { 0 };   // setStateInformation calls (ends a preset audition)
    std::atomic<int> loadsInProgress { 0 };   // applyPresetData is writing parameters: followDistortionModel waits

    static constexpr int kMaxMidiInPerBlock = 4096;
    static constexpr int kMaxPreviewPerBlock = 16;
    static constexpr int kMinPreparedBlock = 1024;
    /** MIDI output events per host block (docs/research/07 section 4.2). 9 bytes each in a
        MidiBuffer, so 576 bytes: under every JUCE wrapper's pre-sized buffer. */
    static constexpr int kMaxMidiOutPerBlock = 64;
    /** Storage handed to the host's MIDI buffer: room for the input flood plus the output. */
    static constexpr std::size_t kHostMidiStorageBytes = 65536;

    JUCE_DECLARE_WEAK_REFERENCEABLE (mw12FProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (mw12FProcessor)
};
