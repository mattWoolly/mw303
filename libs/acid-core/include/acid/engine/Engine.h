#pragma once
// The engine: everything the plugin runs, wired together, with no JUCE in it.
//
//     MIDI in + transport -> seq::Player -> dsp::Voice -> fx::FxChain -> master -> safety clip -> stereo out
//                                       \-> MIDI out
//
// Threads (SPEC section 5):
//   - process() and everything it calls run on the audio thread and never allocate,
//     lock or block.
//   - publishBank() and publishSettings() are called from ONE non-audio thread at a time
//     (the plugin serialises them behind its model mutex). They copy into a triple buffer;
//     the audio thread picks the latest copy up at the start of the next block.
//   - status() may be called from any thread; it reads relaxed atomics.
//   - prepare() and release() must not overlap process(). The plugin guarantees this; the
//     engine still outputs silence from process() while it is not prepared.
#include "acid/common/TripleBuffer.h"
#include "acid/engine/ParamTable.h"
#include "acid/engine/Settings.h"
#include "acid/seq/Pattern.h"
#include "acid/seq/PlayerTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace acid
{
/** Where the engine writes MIDI output for the block. Fixed capacity, owned by the caller.

    Size it with Engine::midiOutCapacityFor (numSamples) and nothing from pattern playback is
    ever dropped. When it is too small the stream stays balanced: a message that opens
    something (a note-on, a controller going high) is dropped before the message that closes
    it would not fit, a note-off whose note-on was dropped is dropped with it, and overflowed
    is set so the caller knows. */
struct MidiOutBuffer
{
    seq::MidiMsg* messages = nullptr;
    int capacity = 0;
    int count = 0;              // set by process()
    bool overflowed = false;    // set by process(): at least one message did not fit this block
};

/** Snapshot of what the UI shows. All values from the most recent block. */
struct EngineStatus
{
    bool prepared = false;
    bool running = false;          // a pattern is stepping
    bool hostRolling = false;
    bool internalClock = true;
    int playMode = 1;              // seq::PlayMode
    int playingSlot = -1;          // 0..47 into PatternBank::seq or ::arp, -1 when stopped
    int queuedSlot = -1;
    int currentStep = -1;          // step index in the playing pattern, -1 when stopped
    bool gateOpen = false;
    bool accentLatched = false;
    float accentCharge = 0.0f;     // 0..1, the accent capacitor, for the C13 meter
    float envelope = 0.0f;         // 0..1, main envelope
    float peakLeft = 0.0f;         // linear peak since the last status() read is NOT tracked: per block
    float peakRight = 0.0f;
    float limiterReductionDb = 0.0f;
    bool delayClamped = false;
    int latencySamples = 0;
    int oversampling = 4;          // the voice factor in use
    double cpuLoad = 0.0;          // fraction of the block's real-time budget used, smoothed
    bool midiOutOverflowed = false;   // the last block's MidiOutBuffer was too small
    bool midiInDropped = false;       // the last block's MIDI input exceeded what the engine can stage
};

class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    /** Allocates everything. maxBlockSize is a hint: process() accepts larger blocks and
        splits them internally. */
    void prepare (double sampleRate, int maxBlockSize);
    void release();

    /** Offline rendering selects Settings::offlineQuality instead of realtimeQuality.
        Takes effect at the next prepare() if it changes the oversampling factor.

        Offline reproducibility (SPEC rule 5: render twice, bit-identical). The voice has
        free-running state (oscillator phase, pitch drift, the filter, the couplings, the
        accent capacitor) and the effects have tails, so whatever ran before a render (idle
        realtime blocks, notes played by hand) would otherwise colour it. The rule: while the
        engine is in non-realtime mode, the voice and the effects are reset to their
        post-prepare state (resting, with the block's parameters applied unsmoothed) at the
        top of any block where
          - the host transport starts rolling from stopped, or
          - it was rolling and its position jumped (a seek), unless the block starts at the
            host's loop start while looping (a loop wrap is the song carrying on), or
          - it is the first block after setNonRealtime (true) switched from realtime.
        Patterns, parameters, the sequencer and MIDI output state are not touched; MIDI
        input that arrives in that block plays on the reset voice. In realtime mode nothing
        is reset, so a live performance is never cut. Any thread; the reset itself happens
        on the audio thread inside process(). */
    void setNonRealtime (bool isOffline) noexcept;

    /** Message side. Copies; safe to call while process() runs on another thread. */
    void publishBank (const seq::PatternBank& bank) noexcept;
    void publishSettings (const Settings& settings) noexcept;

    /** Audio thread. params holds every parameter in natural units (see ParamTable.h); a
        value that is not finite is replaced by the row's default and the rest are clamped to
        the row's range before anything reads them. midiIn must be sorted by offset. left and
        right receive the output (they are overwritten, not mixed into).

        A block of 0 samples renders nothing but its MIDI input is not lost: the messages are
        kept (up to kPendingMidiCapacity, closing messages preferred when more arrive) and
        delivered at offset 0 of the next block, ahead of that block's own messages. Hosts
        flush events that way. reset() and prepare() discard what is pending. Before
        prepare() every block, 0 samples included, is dropped. */
    void process (const ParamValues& params, const seq::TransportInfo& transport,
                  std::span<const seq::MidiMsg> midiIn, MidiOutBuffer& midiOut,
                  float* left, float* right, int numSamples) noexcept;

    /** The safety clip after the master volume, the last thing the output passes through.
        Stock settings never reach it: the voice's own output amplifier rails hold a note
        under +6.8 dBFS (dsp::Voice::kWorstCaseModsPeak). It is there for what the gain
        staging allows when it is stacked on purpose or by a hostile or corrupted state
        (pre EQ, distortion trim, four EQ bands, limiter gain and master volume can add up to
        well over 100 dB): exactly linear up to kSafetyKnee (+6 dBFS), then a tanh that never
        passes kSafetyCeiling (+12 dBFS). It cannot be turned off and no parameter moves it.
        A non-finite sample comes out as 0. It aliases where it works, which is acceptable for
        a stage that only a broken gain structure reaches. */
    static constexpr float kSafetyKnee = 2.0f;
    static constexpr float kSafetyCeiling = 3.98107f;

    /** MIDI input messages a zero-length block can carry over to the next block. */
    static constexpr int kPendingMidiCapacity = 1024;

    /** The MidiOutBuffer capacity that pattern playback can never overflow for a block of
        numSamples: one player output per internal chunk. External mode passes input through
        (midiThru) on top of that: allow as many messages again as the block's MIDI input. Any
        thread; before prepare() it assumes the smallest chunk. */
    int midiOutCapacityFor (int numSamples) const noexcept;

    /** Panic: all notes off, voice and effects to rest. Audio thread, or any time the
        audio thread is known to be stopped. */
    void reset() noexcept;

    int latencySamples() const noexcept;
    double tailSeconds() const noexcept;

    /** Any thread. */
    EngineStatus status() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** Oversampling factor the voice uses for a quality tier at a host sample rate.
    Keeps the internal rate near 176.4 to 192 kHz at Normal. */
int voiceOversamplingFor (Quality quality, double sampleRate) noexcept;

/** Oversampling factor for the distortion block at a quality tier (Draft = 1). */
int distortionOversamplingFor (Quality quality, double sampleRate) noexcept;
}
