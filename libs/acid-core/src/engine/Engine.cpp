// The engine (docs/SPEC.md section 6.5): Player -> Voice -> FxChain -> master, with the
// triple buffers, the parameter mapping, the block splitting and the UI status.
//
// Block splitting. A host block is cut into chunks of at most min (prepared size, the
// player's overflow-free size). Each chunk runs the player once, then the voice in
// sub-blocks between the player's events (so every event lands on its exact sample), then
// the effects on that chunk. The first chunk is handed the host's transport; every later
// chunk is handed the position the player's own clock reached at the end of the chunk
// before it (PlayerStatus::ppq), with the host's flags unchanged. That position already
// includes any loop wrap the tracker made inside the chunk, and it never differs from what
// the tracker extrapolates, so a split block can never look like a relocation and a host
// that keeps reporting its loop after the playhead has left it is treated exactly as it is
// in an unsplit block.
//
// MIDI input. An unsplit block is handed to the player untouched. A split block's messages
// are staged chunk by chunk in a fixed scratch with their offsets rebased; a chunk is cut
// short where its messages would not fit, so the only time anything is dropped is when more
// messages than the scratch holds sit on one sample, and then the closing ones (note-offs,
// all notes off, all sound off, reset) are kept first, then note-ons, then the rest. The
// input of a zero-length block is kept and delivered at offset 0 of the next block.
//
// MIDI output. Every chunk's player output is copied into the caller's buffer. The copy
// keeps the stream balanced when the buffer is too small (Engine.h, MidiOutBuffer): it
// tracks which notes and which controllers it has turned on at the host, reserves a slot
// for each of their closing messages, refuses opening messages that would eat into that
// reserve, and drops a closing message whose opening one it never delivered.
//
// Parameters. The snapshot is sanitised once per block into a member copy (non-finite
// values become the row default, the rest are clamped to the row range) and only that copy
// is read, so a host that sends NaN or infinity cannot reach a float-to-int conversion.
//
// Vibrato. The voice gives full vibrato to any note flagged vibrato and adds the wheel's
// share to every note. The player flags every External note and sends the wheel as
// VibratoDepth, and in the pattern modes sends VibratoDepth 1 and flags only the vibrato
// steps. Taken literally that would make every note trill in every mode, so the engine
// reconciles the two: in External mode the flag is dropped and the wheel rules; in the
// pattern modes the wheel is held at 0 and the step flag rules (SPEC 6.1).
//
// First block. After prepare() and after reset() the effects chain is given the block's
// parameters and then reset, so it starts at the parameters instead of fading from the
// module defaults over its bypass crossfade; the voice does the same by itself (its
// smoothers snap on the first block after a reset) and the master gain snaps too.
//
// Quality tiers (voiceOversamplingFor, distortionOversamplingFor):
//
//     host rate         Draft    Normal   High    Ultra        internal rate at Normal
//     44.1 / 48 kHz       2        4        8       8          176.4 / 192 kHz
//     88.2 / 96 kHz       1        2        4       8          176.4 / 192 kHz
//     176.4 / 192 kHz     1        1        2       4          176.4 / 192 kHz
//
// Draft's distortion factor is 1 at every rate (it is the cheap tier). Ultra is 8x wherever
// 8x keeps the internal rate within kUltraMaxInternalRate, otherwise the largest factor that
// does. The factor is chosen in prepare() from Settings::realtimeQuality or offlineQuality
// (setNonRealtime) and never changes between prepares.
#include "acid/engine/Engine.h"

#include "acid/common/Math.h"
#include "acid/dsp/Voice.h"
#include "acid/fx/FxChain.h"
#include "acid/seq/Player.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace acid
{
namespace
{
/** Master volume at or below this is silence (gain exactly 0). */
constexpr double kSilenceDb = -60.0;
constexpr double kMasterSmoothingSeconds = 0.010;
constexpr double kCpuSmoothingSeconds = 0.5;
/** Ultra stays at 8x while the oversampled rate fits under this. */
constexpr double kUltraMaxInternalRate = 800000.0;
/** MIDI messages a split chunk can carry. Whole blocks are passed through untouched. */
constexpr int kMidiScratchCapacity = 8192;
static_assert (Engine::kPendingMidiCapacity <= kMidiScratchCapacity);
/** The accent capacitor charge that reads as a full meter, in volts above rest. */
constexpr float kAccentChargeFullVolts = 1.6f;
/** Tail guesses for the host (seconds): the voice alone, plus the delay and the reverb. */
constexpr double kVoiceTailSeconds = 4.0;
constexpr double kDelayTailSeconds = 8.0;
constexpr double kReverbTailSeconds = 10.0;

/** The factor that keeps the internal rate near 176.4 to 192 kHz. */
int normalFactorFor (double sampleRate) noexcept
{
    if (! (sampleRate > 0.0)) return 4;
    if (sampleRate < 60000.0) return 4;
    if (sampleRate < 120000.0) return 2;
    return 1;
}

Quality sanitiseQuality (Quality q) noexcept
{
    return static_cast<std::uint8_t> (q) <= static_cast<std::uint8_t> (Quality::Ultra) ? q : Quality::Normal;
}

int clampFactor (int f) noexcept
{
    return f >= 8 ? 8 : f >= 4 ? 4 : f >= 2 ? 2 : 1;
}

/** The performance controls from a sanitised snapshot (every value finite and in range) and
    the published settings (the internal tempo, checked here because it arrives unchecked). */
seq::PlayerControls controlsFrom (const ParamValues& p, const Settings& settings) noexcept
{
    seq::PlayerControls c;
    c.playMode = static_cast<seq::PlayMode> (clamp (paramAsInt (p, kPlayMode), 0, 2));
    c.run = paramAsBool (p, kSeqRun);
    c.bank = clamp (paramAsInt (p, kPatternBank), 0, seq::kNumBanks - 1);
    c.slot = clamp (paramAsInt (p, kPatternSlot) - 1, 0, seq::kSlotsPerBank - 1);
    c.liveTranspose = clamp (paramAsInt (p, kLiveTranspose), -24, 24);
    c.ovrNoteSet = paramAsBool (p, kOvrNoteSet);
    c.ovrNoteClr = paramAsBool (p, kOvrNoteClr);
    c.ovrSlideSet = paramAsBool (p, kOvrSlideSet);
    c.ovrSlideClr = paramAsBool (p, kOvrSlideClr);
    c.ovrAccentSet = paramAsBool (p, kOvrAccentSet);
    c.ovrAccentClr = paramAsBool (p, kOvrAccentClr);
    c.ovrVibSet = paramAsBool (p, kOvrVibSet);
    c.ovrVibClr = paramAsBool (p, kOvrVibClr);
    const auto bpm = static_cast<double> (settings.internalBpm);
    c.internalBpm = std::isfinite (bpm) ? clamp (bpm, 20.0, 300.0) : 120.0;
    return c;
}

/** How a MIDI input message ranks when not all of them can be kept: 0 closes something
    (note-off, all notes off, all sound off, reset), 1 opens a note, 2 is everything else. */
int midiInPriority (const seq::MidiMsg& m) noexcept
{
    const int status = m.data[0] & 0xF0;
    if (m.data[0] == 0xFF) return 0;
    if (m.size >= 3 && status == 0x80) return 0;
    if (m.size >= 3 && status == 0x90) return m.data[2] == 0 ? 0 : 1;
    if (m.size >= 3 && status == 0xB0 && (m.data[1] == 120 || m.data[1] == 121 || m.data[1] == 123)) return 0;
    return 2;
}

/** The last line (Engine.h, kSafetyKnee): exactly x up to the knee, then a tanh towards
    the ceiling. A non-finite sample becomes silence. */
float safetyClip (float x) noexcept
{
    const float ax = std::abs (x);
    if (ax <= Engine::kSafetyKnee)
        return x;
    if (! std::isfinite (x))
        return 0.0f;
    constexpr float room = Engine::kSafetyCeiling - Engine::kSafetyKnee;
    return std::copysign (Engine::kSafetyKnee + room * std::tanh ((ax - Engine::kSafetyKnee) / room), x);
}

/** A bit per (channel, note or controller number): 16 x 128 bits. */
struct MidiBits
{
    std::array<std::uint64_t, 32> words {};

    static std::size_t wordOf (int channel, int number) noexcept { return static_cast<std::size_t> (channel * 2 + number / 64); }
    static std::uint64_t bitOf (int number) noexcept { return std::uint64_t { 1 } << (number % 64); }

    bool test (int channel, int number) const noexcept { return (words[wordOf (channel, number)] & bitOf (number)) != 0; }
    void set (int channel, int number) noexcept { words[wordOf (channel, number)] |= bitOf (number); }
    void clear (int channel, int number) noexcept { words[wordOf (channel, number)] &= ~bitOf (number); }
};
}

int voiceOversamplingFor (Quality quality, double sampleRate) noexcept
{
    const int normal = normalFactorFor (sampleRate);

    switch (sanitiseQuality (quality))
    {
        case Quality::Draft:  return clampFactor (std::max (1, normal / 2));
        case Quality::Normal: return normal;
        case Quality::High:   return clampFactor (std::min (8, normal * 2));
        case Quality::Ultra:
        {
            int f = 8;
            while (f > normal && sampleRate * static_cast<double> (f) > kUltraMaxInternalRate)
                f /= 2;
            return clampFactor (std::max (f, normal));
        }
    }

    return normal;
}

int distortionOversamplingFor (Quality quality, double sampleRate) noexcept
{
    return sanitiseQuality (quality) == Quality::Draft ? 1 : voiceOversamplingFor (quality, sampleRate);
}

struct Engine::Impl
{
    // ---- configuration (prepare) ----
    double sampleRate = 48000.0;
    int maxBlock = 0;
    std::atomic<int> chunkSize { 0 };
    int voiceFactor = 4;
    int distFactor = 4;
    std::atomic<bool> offline { false };    // setNonRealtime may arrive from the audio thread while prepare() reads it
    std::atomic<bool> enteredOffline { false };   // set by setNonRealtime (true) after realtime; the audio thread resets the sound
    std::atomic<bool> prepared { false };

    // ---- modules ----
    dsp::Voice voice;
    fx::FxChain fx;
    seq::Player player;
    seq::PlayerOutput playerOut;

    // ---- message to audio ----
    TripleBuffer<seq::PatternBank> bankBuffer;
    TripleBuffer<Settings> settingsBuffer;

    // ---- audio-thread scratch and state ----
    std::vector<float> mono;
    std::vector<seq::MidiMsg> midiScratch;
    std::array<seq::MidiMsg, kPendingMidiCapacity> pendingMidi {};
    int pendingMidiCount = 0;
    ParamValues safeParams = defaultParamValues();
    double masterGain = 1.0;
    double masterTarget = 1.0;
    double masterCoeff = 1.0;
    bool snapMaster = true;
    bool snapFx = true;
    double lastBpm = 120.0;
    double cpuLoad = 0.0;
    int latency = 0;
    bool midiInDropped = false;

    // What the MIDI output has turned on at the host and not yet turned off: one reserved
    // slot per bit. Kept across prepare() and reset(): the player closes them on its next block.
    MidiBits outNotesOn;
    MidiBits outControllersHigh;
    int outReserved = 0;

    // ---- audio to UI ----
    std::atomic<bool> stRunning { false }, stHostRolling { false }, stInternalClock { true };
    std::atomic<bool> stGateOpen { false }, stAccentLatched { false }, stDelayClamped { false };
    std::atomic<bool> stMidiOutOverflowed { false }, stMidiInDropped { false };
    std::atomic<int> stPlayMode { 1 }, stPlayingSlot { -1 }, stQueuedSlot { -1 }, stCurrentStep { -1 };
    std::atomic<int> stLatency { 0 }, stOversampling { 4 };
    std::atomic<float> stAccentCharge { 0.0f }, stEnvelope { 0.0f }, stPeakLeft { 0.0f }, stPeakRight { 0.0f };
    std::atomic<float> stLimiterDb { 0.0f };
    std::atomic<double> stCpu { 0.0 };
    std::atomic<double> stTail { kVoiceTailSeconds };

    static_assert (std::atomic<double>::is_always_lock_free);
    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<int>::is_always_lock_free);
    static_assert (std::atomic<bool>::is_always_lock_free);

    void prepare (double newSampleRate, int maxBlockSize)
    {
        prepared.store (false, std::memory_order_release);

        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        maxBlock = std::max (1, maxBlockSize);

        settingsBuffer.update();
        const bool isOffline = offline.load (std::memory_order_relaxed);
        const Quality q = sanitiseQuality (isOffline ? settingsBuffer.read().offlineQuality : settingsBuffer.read().realtimeQuality);
        voiceFactor = voiceOversamplingFor (q, sampleRate);
        distFactor = distortionOversamplingFor (q, sampleRate);

        voice.prepare (sampleRate, maxBlock, voiceFactor);
        fx.setDistortionOversampling (distFactor);
        fx.prepare (sampleRate, maxBlock);
        player.prepare (sampleRate, maxBlock);
        playerOut.clear();

        chunkSize.store (std::max (1, std::min (maxBlock, seq::Player::maxBlockWithoutOverflow (sampleRate))), std::memory_order_relaxed);
        mono.assign (static_cast<std::size_t> (maxBlock), 0.0f);
        midiScratch.assign (static_cast<std::size_t> (kMidiScratchCapacity), seq::MidiMsg {});
        pendingMidiCount = 0;

        masterCoeff = onePoleCoeff (kMasterSmoothingSeconds, sampleRate);
        snapMaster = true;
        snapFx = true;
        lastBpm = 120.0;
        cpuLoad = 0.0;
        voice.setVibratoWheel (0.0);

        latency = voice.latencySamples() + fx.latencySamples();
        stLatency.store (latency, std::memory_order_relaxed);
        stOversampling.store (voiceFactor, std::memory_order_relaxed);
        stCpu.store (0.0, std::memory_order_relaxed);
        clearStatus();

        prepared.store (true, std::memory_order_release);
    }

    void reset() noexcept
    {
        player.reset();
        playerOut.clear();
        voice.reset();
        voice.setVibratoWheel (0.0);
        fx.reset();
        snapMaster = true;
        snapFx = true;
        pendingMidiCount = 0;
        cpuLoad = 0.0;
        clearStatus();
    }

    /** The sound to its post-prepare state: voice and effects at rest with the current
        parameters applied unsmoothed. Patterns, parameters, the player and the MIDI out
        bookkeeping are left alone. Called on the audio thread inside process(), after the
        block's parameters have been handed to the voice and the effects. */
    void resetSound() noexcept
    {
        voice.reset();
        fx.reset();
        snapFx = false;
        masterGain = masterTarget;
    }

    void clearStatus() noexcept
    {
        stRunning.store (false, std::memory_order_relaxed);
        stGateOpen.store (false, std::memory_order_relaxed);
        stAccentLatched.store (false, std::memory_order_relaxed);
        stPlayingSlot.store (-1, std::memory_order_relaxed);
        stQueuedSlot.store (-1, std::memory_order_relaxed);
        stCurrentStep.store (-1, std::memory_order_relaxed);
        stAccentCharge.store (0.0f, std::memory_order_relaxed);
        stEnvelope.store (0.0f, std::memory_order_relaxed);
        stPeakLeft.store (0.0f, std::memory_order_relaxed);
        stPeakRight.store (0.0f, std::memory_order_relaxed);
        stLimiterDb.store (0.0f, std::memory_order_relaxed);
        stMidiOutOverflowed.store (false, std::memory_order_relaxed);
        stMidiInDropped.store (false, std::memory_order_relaxed);
    }

    /** The host's snapshot made safe: non-finite values take the row default, the rest are
        clamped to the row range. */
    void sanitiseParams (const ParamValues& in) noexcept
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto idx = static_cast<std::size_t> (i);
            const ParamInfo& info = kParamTable[idx];
            const float v = in[idx];
            safeParams[idx] = std::isfinite (v) ? clamp (v, info.min, info.max) : info.def;
        }
    }

    /** Keeps the input of a zero-length block for the next block. When full, a closing
        message takes the place of the last message that is not one. */
    void stashMidi (std::span<const seq::MidiMsg> midiIn) noexcept
    {
        for (const auto& m : midiIn)
        {
            if (pendingMidiCount < kPendingMidiCapacity)
            {
                pendingMidi[static_cast<std::size_t> (pendingMidiCount++)] = m;
                continue;
            }

            midiInDropped = true;
            if (midiInPriority (m) != 0) continue;

            for (int i = pendingMidiCount - 1; i >= 0; --i)
                if (midiInPriority (pendingMidi[static_cast<std::size_t> (i)]) != 0)
                {
                    pendingMidi[static_cast<std::size_t> (i)] = m;
                    break;
                }
        }
    }

    /** A block that starts at the host's loop start while looping: a wrap, not a seek. */
    static bool loopWrap (const seq::TransportInfo& t) noexcept
    {
        return t.valid && t.looping && t.ppqValid && std::abs (t.ppq - t.loopStartPpq) < seq::PlayheadTracker::kRelocateToleranceQ;
    }

    /** Runs the voice over one chunk, applying each player event on its own sample. */
    void renderVoice (float* out, int len, std::span<const seq::VoiceEvent> events, bool external) noexcept
    {
        int pos = 0;

        for (const auto& e : events)
        {
            const int at = clamp (static_cast<int> (e.offset), pos, len);
            if (at > pos)
            {
                voice.process (out + pos, at - pos);
                pos = at;
            }

            switch (e.type)
            {
                case seq::VoiceEvent::Type::NoteOn:
                    voice.noteOn (e.note, e.accent, e.legato, external ? false : e.vibrato);
                    break;
                case seq::VoiceEvent::Type::NoteOff:
                case seq::VoiceEvent::Type::AllOff:
                    voice.noteOff();
                    break;
                case seq::VoiceEvent::Type::PitchBend:
                    voice.setPitchBend (static_cast<double> (e.value));
                    break;
                case seq::VoiceEvent::Type::VibratoDepth:
                    voice.setVibratoWheel (external ? static_cast<double> (e.value) : 0.0);
                    break;
            }
        }

        if (pos < len)
            voice.process (out + pos, len - pos);
    }

    /** The offset a message lands on: late ones (at or past the end of the block) land on
        the last sample, as the player would put them. */
    static int effectiveOffset (const seq::MidiMsg& m, int numSamples) noexcept
    {
        return std::min (static_cast<int> (m.offset), numSamples - 1);
    }

    /** First index at or after `from` whose message lies outside [.., end): the end of the
        run of messages that belong to a chunk ending at `end`. The last chunk takes everything. */
    static std::size_t chunkMidiEnd (std::span<const seq::MidiMsg> all, std::size_t from, int end, int numSamples) noexcept
    {
        std::size_t i = from;
        while (i < all.size() && (end >= numSamples || effectiveOffset (all[i], numSamples) < end))
            ++i;
        return i;
    }

    /** The MIDI messages of the chunk [start, start + len) with offsets rebased to the chunk,
        the pending ones of a zero-length block first (chunk 0 only). May shorten len so that
        the messages fit the scratch: the chunk then ends at the message that did not fit.
        When more messages than the scratch holds sit on the chunk's first sample they are
        kept by priority (midiInPriority) and the rest are dropped. A whole, unsplit block
        with nothing pending is handed over as is. */
    std::span<const seq::MidiMsg> sliceMidi (std::span<const seq::MidiMsg> all, std::size_t& index,
                                             int start, int& len, int numSamples) noexcept
    {
        const int pendingHere = start == 0 ? pendingMidiCount : 0;

        if (start == 0 && len == numSamples && pendingHere == 0)
        {
            index = all.size();
            return all;
        }

        const int cap = static_cast<int> (midiScratch.size());
        std::size_t endIndex = chunkMidiEnd (all, index, start + len, numSamples);
        int count = pendingHere + static_cast<int> (endIndex - index);

        if (count > cap)
        {
            // The message that would not fit: cut the chunk in front of it when it sits on a
            // later sample; otherwise cut after the chunk's first sample so that only that
            // sample's flood is subject to dropping.
            const std::size_t firstOut = index + static_cast<std::size_t> (cap - pendingHere);
            int cutAt = effectiveOffset (all[firstOut], numSamples);

            if (cutAt <= start)
            {
                std::size_t k = firstOut;
                while (k < endIndex && effectiveOffset (all[k], numSamples) <= start)
                    ++k;
                cutAt = k < endIndex ? effectiveOffset (all[k], numSamples) : start + len;
            }

            if (cutAt > start && cutAt < start + len)
            {
                len = cutAt - start;
                endIndex = chunkMidiEnd (all, index, start + len, numSamples);
                count = pendingHere + static_cast<int> (endIndex - index);
            }
        }

        std::size_t n = 0;
        const auto put = [&] (seq::MidiMsg m, bool rebase) {
            m.offset = rebase ? clamp (static_cast<int> (m.offset) - start, 0, len - 1) : 0;
            midiScratch[n++] = m;
        };

        if (count <= cap)
        {
            for (int i = 0; i < pendingHere; ++i)
                put (pendingMidi[static_cast<std::size_t> (i)], false);
            for (std::size_t i = index; i < endIndex; ++i)
                put (all[i], true);
        }
        else
        {
            midiInDropped = true;
            for (int priority = 0; priority <= 2 && static_cast<int> (n) < cap; ++priority)
            {
                for (int i = 0; i < pendingHere && static_cast<int> (n) < cap; ++i)
                    if (midiInPriority (pendingMidi[static_cast<std::size_t> (i)]) == priority)
                        put (pendingMidi[static_cast<std::size_t> (i)], false);
                for (std::size_t i = index; i < endIndex && static_cast<int> (n) < cap; ++i)
                    if (midiInPriority (all[i]) == priority)
                        put (all[i], true);
            }
        }

        if (start == 0)
            pendingMidiCount = 0;
        index = endIndex;
        return { midiScratch.data(), n };
    }

    /** Copies one player message into the caller's buffer, keeping the stream balanced. */
    void emitMidi (MidiOutBuffer& out, seq::MidiMsg m, int blockOffset) noexcept
    {
        if (out.messages == nullptr || out.capacity <= 0)
            return;    // the caller wants no MIDI output

        const int status = m.data[0] & 0xF0;
        const int channel = m.data[0] & 0x0F;
        const int number = m.data[1] & 0x7F;
        bool opening = false, closing = false;
        MidiBits* bits = nullptr;

        if (m.size >= 3 && (status == 0x80 || status == 0x90))
        {
            bits = &outNotesOn;
            const bool on = status == 0x90 && m.data[2] > 0;
            const bool isOn = bits->test (channel, number);
            if (on) opening = ! isOn;
            else if (isOn) closing = true;
            else return;    // a note-off whose note-on was never delivered
        }
        else if (m.size >= 3 && status == 0xB0)
        {
            bits = &outControllersHigh;
            const bool isHigh = bits->test (channel, number);
            if (m.data[2] >= 64) opening = ! isHigh;
            else closing = isHigh;
        }

        const int needed = out.count + 1 + (closing ? 0 : outReserved + (opening ? 1 : 0));
        if (needed > out.capacity)
        {
            out.overflowed = true;
            return;
        }

        m.offset += blockOffset;
        out.messages[out.count++] = m;

        if (opening)
        {
            bits->set (channel, number);
            ++outReserved;
        }
        else if (closing)
        {
            bits->clear (channel, number);
            --outReserved;
        }
    }

    void process (const ParamValues& params, const seq::TransportInfo& transport,
                  std::span<const seq::MidiMsg> midiIn, MidiOutBuffer& midiOut,
                  float* left, float* right, int numSamples) noexcept
    {
        midiOut.count = 0;
        midiOut.overflowed = false;

        if (! prepared.load (std::memory_order_acquire))
        {
            if (numSamples > 0 && left != nullptr) std::fill_n (left, numSamples, 0.0f);
            if (numSamples > 0 && right != nullptr) std::fill_n (right, numSamples, 0.0f);
            return;
        }

        if (numSamples <= 0)
        {
            if (numSamples == 0)
            {
                midiInDropped = false;
                stashMidi (midiIn);
                stMidiInDropped.store (midiInDropped, std::memory_order_relaxed);
            }
            return;
        }

        if (left == nullptr || right == nullptr)
            return;

        const auto t0 = std::chrono::steady_clock::now();

        bankBuffer.update();
        settingsBuffer.update();
        const seq::PatternBank& bank = bankBuffer.read();
        const Settings& settings = settingsBuffer.read();

        sanitiseParams (params);
        const seq::PlayerControls controls = controlsFrom (safeParams, settings);
        const seq::PlayerConfig config = seq::playerConfigFrom (settings);
        const bool external = controls.playMode == seq::PlayMode::External;

        voice.setParams (dsp::voiceParamsFrom (safeParams));

        const bool hostBpm = transport.valid && transport.bpmValid && std::isfinite (transport.bpm) && transport.bpm > 0.0;
        const double bpm = hostBpm ? transport.bpm : lastBpm;
        const fx::FxParams fxParams = fx::fxParamsFrom (safeParams, settings, bpm);
        fx.setParams (fxParams);
        if (snapFx)
        {
            fx.reset();
            snapFx = false;
        }

        const double masterDb = static_cast<double> (safeParams[static_cast<std::size_t> (kMasterVolume)]);
        masterTarget = ! (masterDb > kSilenceDb) ? 0.0 : dbToGain (std::min (masterDb, 12.0));
        if (snapMaster)
        {
            masterGain = masterTarget;
            snapMaster = false;
        }

        // Offline reproducibility (Engine.h): entering non-realtime mode starts from rest.
        const bool isOffline = offline.load (std::memory_order_relaxed);
        if (enteredOffline.exchange (false, std::memory_order_relaxed) && isOffline)
            resetSound();

        const int chunk = chunkSize.load (std::memory_order_relaxed);
        const bool followPosition = transport.valid && transport.playing && transport.ppqValid;
        seq::TransportInfo t = transport;
        float peakL = 0.0f, peakR = 0.0f;
        std::size_t midiIndex = 0;
        int done = 0;
        midiInDropped = false;

        while (done < numSamples)
        {
            int len = std::min (chunk, numSamples - done);
            const auto chunkMidi = sliceMidi (midiIn, midiIndex, done, len, numSamples);

            player.process (t, chunkMidi, bank, controls, config, len, playerOut);
            lastBpm = playerOut.status.bpm;

            // Offline reproducibility (Engine.h): a render starts from rest whatever ran
            // before it. Only the first chunk can see a start or a jump (later chunks are
            // handed the player's own position). A loop wrap is the song carrying on.
            if (done == 0 && isOffline && (playerOut.status.hostStarted || (playerOut.status.relocated && ! loopWrap (transport))))
                resetSound();

            float* l = left + done;
            float* r = right + done;
            renderVoice (mono.data(), len, playerOut.voice(), external);
            std::memcpy (l, mono.data(), static_cast<std::size_t> (len) * sizeof (float));
            std::memcpy (r, mono.data(), static_cast<std::size_t> (len) * sizeof (float));

            fx.process (l, r, len);

            for (int i = 0; i < len; ++i)
            {
                masterGain += masterCoeff * (masterTarget - masterGain);
                const auto g = static_cast<float> (masterGain);
                l[i] = safetyClip (l[i] * g);
                r[i] = safetyClip (r[i] * g);
                peakL = std::max (peakL, std::abs (l[i]));
                peakR = std::max (peakR, std::abs (r[i]));
            }
            if (std::abs (masterTarget - masterGain) < 1.0e-9)
                masterGain = masterTarget;

            for (const auto& m : playerOut.midi())
                emitMidi (midiOut, m, done);

            done += len;

            // The next chunk starts where the player's clock got to: no recomputed wrap, no
            // apparent relocation, the host's flags as they were.
            if (followPosition)
                t.ppq = playerOut.status.ppq;
        }

        publishStatus (controls, fxParams, peakL, peakR, midiOut.overflowed);

        const auto t1 = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double> (t1 - t0).count();
        const double load = seconds * sampleRate / static_cast<double> (numSamples);
        const double a = 1.0 - std::exp (-static_cast<double> (numSamples) / (sampleRate * kCpuSmoothingSeconds));
        cpuLoad += a * (load - cpuLoad);
        stCpu.store (cpuLoad, std::memory_order_relaxed);
    }

    void publishStatus (const seq::PlayerControls& controls, const fx::FxParams& fxParams, float peakL, float peakR,
                        bool midiOutOverflowed) noexcept
    {
        const seq::PlayerStatus& ps = playerOut.status;
        const dsp::VoiceMeters vm = voice.meters();
        const fx::FxMeters fm = fx.meters();

        stRunning.store (ps.running, std::memory_order_relaxed);
        stHostRolling.store (ps.hostRolling, std::memory_order_relaxed);
        stInternalClock.store (ps.internalClock, std::memory_order_relaxed);
        stPlayMode.store (static_cast<int> (controls.playMode), std::memory_order_relaxed);
        stPlayingSlot.store (ps.playingSlot, std::memory_order_relaxed);
        stQueuedSlot.store (ps.queuedSlot, std::memory_order_relaxed);
        stCurrentStep.store (ps.currentStep, std::memory_order_relaxed);
        stGateOpen.store (ps.gateOpen, std::memory_order_relaxed);
        stAccentLatched.store (vm.accent, std::memory_order_relaxed);
        stAccentCharge.store (clamp (vm.accentCharge / kAccentChargeFullVolts, 0.0f, 1.0f), std::memory_order_relaxed);
        stEnvelope.store (clamp (vm.envelope, 0.0f, 1.0f), std::memory_order_relaxed);
        stPeakLeft.store (peakL, std::memory_order_relaxed);
        stPeakRight.store (peakR, std::memory_order_relaxed);
        stLimiterDb.store (fm.limiterReductionDb, std::memory_order_relaxed);
        stDelayClamped.store (fm.delayClamped, std::memory_order_relaxed);
        stMidiOutOverflowed.store (midiOutOverflowed, std::memory_order_relaxed);
        stMidiInDropped.store (midiInDropped, std::memory_order_relaxed);

        double tail = kVoiceTailSeconds;
        if (! fxParams.bypass)
        {
            if (fxParams.delay.on) tail += kDelayTailSeconds;
            if (fxParams.reverb.on) tail += kReverbTailSeconds;
        }
        stTail.store (tail, std::memory_order_relaxed);
    }

    EngineStatus status() const noexcept
    {
        EngineStatus s;
        s.prepared = prepared.load (std::memory_order_acquire);
        s.running = stRunning.load (std::memory_order_relaxed);
        s.hostRolling = stHostRolling.load (std::memory_order_relaxed);
        s.internalClock = stInternalClock.load (std::memory_order_relaxed);
        s.playMode = stPlayMode.load (std::memory_order_relaxed);
        s.playingSlot = stPlayingSlot.load (std::memory_order_relaxed);
        s.queuedSlot = stQueuedSlot.load (std::memory_order_relaxed);
        s.currentStep = stCurrentStep.load (std::memory_order_relaxed);
        s.gateOpen = stGateOpen.load (std::memory_order_relaxed);
        s.accentLatched = stAccentLatched.load (std::memory_order_relaxed);
        s.accentCharge = stAccentCharge.load (std::memory_order_relaxed);
        s.envelope = stEnvelope.load (std::memory_order_relaxed);
        s.peakLeft = stPeakLeft.load (std::memory_order_relaxed);
        s.peakRight = stPeakRight.load (std::memory_order_relaxed);
        s.limiterReductionDb = stLimiterDb.load (std::memory_order_relaxed);
        s.delayClamped = stDelayClamped.load (std::memory_order_relaxed);
        s.latencySamples = stLatency.load (std::memory_order_relaxed);
        s.oversampling = stOversampling.load (std::memory_order_relaxed);
        s.cpuLoad = stCpu.load (std::memory_order_relaxed);
        s.midiOutOverflowed = stMidiOutOverflowed.load (std::memory_order_relaxed);
        s.midiInDropped = stMidiInDropped.load (std::memory_order_relaxed);
        return s;
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlockSize) { impl->prepare (sampleRate, maxBlockSize); }

void Engine::release()
{
    impl->prepared.store (false, std::memory_order_release);
}

void Engine::setNonRealtime (bool isOffline) noexcept
{
    const bool was = impl->offline.exchange (isOffline, std::memory_order_relaxed);
    if (isOffline && ! was)
        impl->enteredOffline.store (true, std::memory_order_relaxed);
}

void Engine::publishBank (const seq::PatternBank& bank) noexcept { impl->bankBuffer.write (bank); }
void Engine::publishSettings (const Settings& settings) noexcept { impl->settingsBuffer.write (settings); }

void Engine::process (const ParamValues& params, const seq::TransportInfo& transport,
                      std::span<const seq::MidiMsg> midiIn, MidiOutBuffer& midiOut,
                      float* left, float* right, int numSamples) noexcept
{
    impl->process (params, transport, midiIn, midiOut, left, right, numSamples);
}

void Engine::reset() noexcept
{
    if (impl->prepared.load (std::memory_order_acquire))
        impl->reset();
}

int Engine::latencySamples() const noexcept { return impl->stLatency.load (std::memory_order_relaxed); }
double Engine::tailSeconds() const noexcept { return impl->stTail.load (std::memory_order_relaxed); }
EngineStatus Engine::status() const noexcept { return impl->status(); }

int Engine::midiOutCapacityFor (int numSamples) const noexcept
{
    const int chunk = std::max (1, impl->chunkSize.load (std::memory_order_relaxed));
    const int chunks = numSamples <= 0 ? 1 : (numSamples + chunk - 1) / chunk;
    return chunks * seq::PlayerOutput::kMaxMidiOut;
}
}
