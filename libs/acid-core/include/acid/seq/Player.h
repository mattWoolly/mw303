#pragma once
// Playback: the one object between (host transport + MIDI in + pattern bank) and the voice.
// It owns the playhead tracker, the step engine that plays sequencer and arpeggiator
// patterns, and the external-note handler, and it switches between them without leaving a
// note on. Everything here runs on the audio thread: no allocation, no locks, fixed-size
// outputs.
//
// Musical time comes from PlayheadTracker. Step times come from StepClock in closed form,
// so the absolute sample of every event is the same for any block size.
#include "acid/engine/Settings.h"
#include "acid/seq/Arpeggiator.h"
#include "acid/seq/ExternalNotes.h"
#include "acid/seq/Pattern.h"
#include "acid/seq/PlayerTypes.h"
#include "acid/seq/PlayheadTracker.h"
#include "acid/seq/StepClock.h"
#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace acid::seq
{
/** The performance controls, read once per block (the Performance rows of the parameter table). */
struct PlayerControls
{
    PlayMode playMode = PlayMode::Sequencer;
    bool run = false;             // Live trigger mode: play even while the host is stopped
    int bank = 0;                 // 0..kNumBanks-1
    int slot = 0;                 // 0..kSlotsPerBank-1, zero-based (the parameter is 1..12)
    int liveTranspose = 0;        // semitones, applied to each NoteOn as it is played

    // Live overrides: while held, Set forces the attribute on every step, Clr removes it.
    // Clr wins when both are held. Note Set makes every step sound, Note Clr mutes.
    // Note Clr is a hard mute: no new step sounds, the note that is sounding ends at its own
    // gate time (no slide, tie or slide-into-rest policy holds it), a gate that was already
    // being held open closes at the next step boundary, and pending ratchet hits are dropped.
    bool ovrNoteSet = false;
    bool ovrNoteClr = false;
    bool ovrSlideSet = false;
    bool ovrSlideClr = false;
    bool ovrAccentSet = false;
    bool ovrAccentClr = false;
    bool ovrVibSet = false;
    bool ovrVibClr = false;

    double internalBpm = 120.0;   // tempo until the host has reported one (standalone: always)
};

/** The sequencer-related fields of Settings. */
struct PlayerConfig
{
    TriggerMode triggerMode = TriggerMode::Live;
    LaunchQuant launchQuant = LaunchQuant::EndOfPattern;
    SlideIntoRest slideIntoRest = SlideIntoRest::Release;
    NotePriority notePriority = NotePriority::Last;

    bool followHostTransport = true;
    bool hostLink = true;
    bool restartOnTrigger = true;

    std::uint8_t hostNoteBase = 24;
    std::uint8_t accentThreshold = 64;    // External mode: velocity above this accents
    std::uint8_t bendRange = 2;           // semitones

    bool midiOut = true;
    bool midiThru = false;
    std::uint8_t outAccentVelocity = 96;
    std::uint8_t outNormalVelocity = 32;

    constexpr PlayerConfig() noexcept = default;

    constexpr explicit PlayerConfig (const Settings& s) noexcept
        : triggerMode (s.triggerMode), launchQuant (s.launchQuant), slideIntoRest (s.slideIntoRest),
          notePriority (s.notePriority), followHostTransport (s.followHostTransport), hostLink (s.hostLink),
          restartOnTrigger (s.restartOnTrigger), hostNoteBase (s.hostNoteBase), accentThreshold (s.accentThreshold),
          bendRange (s.bendRange), midiOut (s.midiOut), midiThru (s.midiThru),
          outAccentVelocity (s.outAccentVelocity), outNormalVelocity (s.outNormalVelocity)
    {
    }
};

constexpr PlayerConfig playerConfigFrom (const Settings& s) noexcept { return PlayerConfig (s); }

/** What the UI shows, as of the end of the block. */
struct PlayerStatus
{
    bool running = false;             // a sequencer or arpeggiator pattern is stepping
    bool gateOpen = false;            // the voice gate is open
    bool hostRolling = false;         // the host transport is playing
    bool internalClock = true;        // the clock is counting samples, not following the host
    std::int16_t playingSlot = -1;    // 0..kNumSlots-1, the pattern that is (or would be) playing; -1 in External mode
    std::int16_t queuedSlot = -1;     // the pattern waiting to take over, or -1
    std::int16_t currentStep = -1;    // pattern step that fired last, or -1 when not running
    std::int16_t heldNotes = 0;       // keys down (External, Arpeggiator) or trigger notes down (Host Note)
    double ppq = 0.0;                 // clock position at the end of the block
    double bpm = 120.0;
    bool hostStarted = false;         // the host transport started at the top of this block
    bool relocated = false;           // it was already playing and its position jumped (seek or loop) at the top of this block
};

/** Everything process() produces. Reuse one instance: process() resets the counts, not the arrays.

    The arrays are fixed, so a block is limited in how many events it can carry. The densest
    pattern there is (1/64 triplet steps, four ratchets, at the tracker's tempo ceiling)
    makes 3200 voice events a second, and no pattern can overflow a block of up to
    Player::maxBlockWithoutOverflow (sampleRate) samples (an eighth of a second). A longer
    block is still safe, but a dense pattern can then overflow it: see overflowed. */
struct PlayerOutput
{
    static constexpr int kMaxVoiceEvents = 512;
    static constexpr int kMaxMidiOut = 1024;

    /** Ordered by offset, already in the order the voice must apply them: do not re-sort. */
    std::array<VoiceEvent, kMaxVoiceEvents> voiceEvents {};
    int numVoiceEvents = 0;

    /** Ordered by offset. */
    std::array<MidiMsg, kMaxMidiOut> midiOut {};
    int numMidiOut = 0;

    /** Set when an event did not fit. Events that open notes are dropped first and the last
        slots are kept for the ones that close them, so an overflow never leaves a note on. */
    bool overflowed = false;

    PlayerStatus status {};

    std::span<const VoiceEvent> voice() const noexcept { return { voiceEvents.data(), static_cast<std::size_t> (numVoiceEvents) }; }
    std::span<const MidiMsg> midi() const noexcept { return { midiOut.data(), static_cast<std::size_t> (numMidiOut) }; }

    void clear() noexcept
    {
        numVoiceEvents = 0;
        numMidiOut = 0;
        overflowed = false;
    }
};

/** The single entry point for playback.

    Call order: prepare() once, then process() for every audio block, on one thread. Nothing
    here is thread-safe: the caller hands in snapshots (bank, controls, config) that do not
    change during the call. Controls, config and pattern edits take effect at the top of a
    block; MIDI input and every generated event are sample-accurate.

    What the voice is told:
      - NoteOn, NoteOff: at most one gate. A NoteOn with legato set arrives while the gate is
        open (a slide: no retrigger); a plain NoteOn only while it is closed. Where a note
        ends and another starts on one sample, the NoteOff is first in the list.
      - PitchBend (semitones, already scaled by the bend range) in every play mode.
      - VibratoDepth: sent at the top of the first block and on every mode change. In
        External mode it is the mod wheel (0..1) and every NoteOn carries the vibrato flag;
        in the pattern modes it is 1 and the step's flag decides.
      - AllOff: only on the block after reset() or a repeated prepare().

    Pattern changes: a newly selected pattern waits for its launch point (LaunchQuant).
    For NextBar that is the first bar line at or after the instant the pattern was queued,
    in engine time (song time when locked to the host). The line is fixed at that instant;
    it is worked out again only if another pattern is queued, the time signature changes or
    a host-locked relocation moves the song. The new pattern is anchored on the bar line and
    its first step takes its own shuffle delay from there.

    Slide into a rest follows SlideIntoRest. Under HoldThroughRest the old pitch lasts for
    the rest's gate time and no longer: a tie after that rest is silent, as any tie after a
    rest is.

    MIDI out (channel 1) mirrors the pattern modes: configured velocities, a slide as the new
    NoteOn followed one sample later by the old NoteOff, CC1 127/0 around vibrato steps, and
    a same-pitch slide with a changed accent as a re-strike under CC64. In External mode MIDI
    out carries only what midiThru passes through. */
class Player
{
public:
    /** Must not run concurrently with process(). Allocates nothing. Called again after the
        first time it acts as reset() does: the next block turns off whatever was sounding. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Panic. Playback state is cleared at once; the next process() starts with an AllOff
        for the voice and note-offs on MIDI out for anything that was sounding there. */
    void reset() noexcept;

    /** One audio block. midiIn must be ordered by offset. Before prepare(), produces nothing.

        numSamples may be larger than the prepared size: nothing here is sized by it. The
        limit is the fixed output. Up to maxBlockWithoutOverflow() samples, pattern playback
        always fits. Beyond that a dense pattern can fill the output: the block is then
        flagged (PlayerOutput::overflowed), notes are dropped, and no note is left on. A
        caller that is handed longer blocks (an offline render, a host with a huge buffer)
        must cut them into chunks of at most maxBlockWithoutOverflow() samples, advance the
        transport position it passes in accordingly, and use the events of each chunk before
        the next call. Events made directly from MIDI input (External mode, thru, bend,
        wheel) come on top of that: a handful per input message. */
    void process (const TransportInfo& transport, std::span<const MidiMsg> midiIn, const PatternBank& patternBank,
                  const PlayerControls& controls, const PlayerConfig& config, int numSamples, PlayerOutput& output) noexcept;

    /** The longest block, in samples, that pattern playback can never overflow: an eighth of
        a second. (The worst case is 400 steps a second with eight voice events each.) */
    static constexpr int maxBlockWithoutOverflow (double sampleRate) noexcept
    {
        return sampleRate >= 8.0 ? static_cast<int> (sampleRate * kOverflowFreeSeconds) : 1;
    }

    static constexpr double kOverflowFreeSeconds = 0.125;

private:
    enum class StartAlign : std::uint8_t
    {
        Grid,        // keep the grid of the clock origin: the first step is the next one on it
        Immediate,   // step 1 sounds now and the grid starts here
        Catch        // arpeggiator: the nearest grid step, played at once when the key is just late
    };

    /** The step that fires next, and the pattern change that comes with it if one is due. */
    struct NextStep
    {
        bool valid = false;
        bool switches = false;
        int slot = 0;
        double onsetS = 0.0;
        double anchorS = 0.0;
        std::int64_t gridIndex = 0;
        std::int64_t g = 0;
        std::int64_t cycleStart = 0;
    };

    /** A step as it will play: probability rolled, overrides and transposition applied. */
    struct Resolved
    {
        StepState state = StepState::Rest;
        bool muted = false;       // a rest because Note Clr is held: no slide or tie may hold a gate into it
        const Step* step = nullptr;
        int position = 0;
        int note = 0;
        bool accent = false;
        bool slide = false;
        bool vibrato = false;
        int ratchet = 1;
    };

    // block start
    void applyBlockStartChanges() noexcept;
    void beginSegment (const ClockSegment&) noexcept;
    void updateRunState() noexcept;
    void refreshGrid (int sample) noexcept;
    void fillStatus() noexcept;

    // step engine
    bool engineMode() const noexcept { return mode != PlayMode::External; }
    const Pattern& patternFor (int slotIndexToPlay) const noexcept;
    StepClock clockFor (int slotIndexToPlay, double anchor, std::int64_t index) const noexcept;
    int desiredFromControls() const noexcept;
    double sAt (int sample) const noexcept { return segStartPpq - originQ + static_cast<double> (sample - segOffset) * qps; }
    double ppqAt (int sample) const noexcept { return segStartPpq + static_cast<double> (sample - segOffset) * qps; }
    double lowerBoundAt (int sample) const noexcept { return sAt (sample) - (1.0 - kSampleEpsilon) * qps; }
    double barLengthQ() const noexcept;
    void setLaunchLine (int sample) noexcept;
    void skipLateSteps (int sample) noexcept;
    std::int64_t cycleIndexFor (const Pattern&, std::int64_t g, std::int64_t cycleStart) const noexcept;
    NextStep peekNext() const noexcept;
    Resolved resolve (const NextStep&) const noexcept;
    void advance (int from, int to) noexcept;
    void fireStep (const NextStep&, int sample) noexcept;
    void gateOffDecision (int sample) noexcept;
    void ratchetHit (int sample) noexcept;
    void startEngine (int sample, StartAlign) noexcept;
    void stopEngine (int sample) noexcept;
    void releaseArp (int sample) noexcept;
    void relocateEngine (int sample) noexcept;
    void applyDesiredSlot (int sample) noexcept;
    void switchImmediately (int sample) noexcept;
    void clearGateState() noexcept;
    void stopEverything (int sample) noexcept;

    // MIDI in
    void handleMidi (const MidiMsg&, int sample) noexcept;
    void onNoteOn (int note, int velocity, int sample) noexcept;
    void onNoteOff (int note, int sample) noexcept;
    void onController (int number, int value, int sample) noexcept;
    void applyExternal (const ExternalNotes::Action&, int sample) noexcept;
    void panic (int sample) noexcept;
    void triggerPush (int index) noexcept;
    bool triggerRemove (int index) noexcept;

    // voice events
    bool pushVoice (const VoiceEvent&, bool closing) noexcept;
    void noteOn (int sample, int note, bool accent, bool legato, bool vibrato) noexcept;
    void noteOff (int sample) noexcept;
    void sendPitchBend (int sample) noexcept;
    void sendVibratoDepth (int sample) noexcept;

    // MIDI out
    bool mirrorEnabled() const noexcept { return cfg->midiOut && mode != PlayMode::External; }
    bool thruEnabled() const noexcept { return cfg->midiThru && mode == PlayMode::External; }
    bool pushMidiRaw (int sample, int status, int d1, int d2, bool closing) noexcept;
    bool sendMidi (int sample, int status, int d1, int d2, bool closing) noexcept;
    void flushPendingOffs (int upToSample) noexcept;
    void mirrorNoteOn (int sample, int note, bool accent, bool legato, bool vibrato) noexcept;
    void mirrorNoteOff (int sample) noexcept;
    void mirrorWheelToRest (int sample) noexcept;
    void mirrorFlush (int sample) noexcept;
    void thru (const MidiMsg&, int sample) noexcept;
    void thruFlush (int sample) noexcept;
    void thruPayOwed (int sample) noexcept;
    bool thruRelease (int channel, int note, int sample, int status, int velocity) noexcept;
    void finishMidiBlock() noexcept;

    static constexpr double kNever = std::numeric_limits<double>::infinity();

    // ---- set up by prepare ----
    bool prepared = false;

    PlayheadTracker tracker;
    ExternalNotes external;
    Arpeggiator arp;

    // ---- valid only inside process() ----
    const PatternBank* bank = nullptr;
    const PlayerControls* ctl = nullptr;
    const PlayerConfig* cfg = nullptr;
    PlayerOutput* out = nullptr;
    const ClockBlock* clock = nullptr;
    int blockSize = 0;
    int segOffset = 0;
    double segStartPpq = 0.0;
    double qps = 120.0 / (60.0 * 48000.0);    // quarter notes per sample
    double bpm = 120.0;

    // ---- mode ----
    bool modeKnown = false;
    PlayMode mode = PlayMode::Sequencer;
    TriggerMode trig = TriggerMode::Live;
    bool panicPending = false;

    // ---- voice ----
    bool gateOpen = false;
    int bendRaw = 0;                  // -8192..8191
    int lastBendRange = -1;
    float wheel = 0.0f;               // mod wheel 0..1

    // ---- step engine ----
    // Engine time "s" is clock time minus originQ. The origin is the song origin when the
    // engine is locked to the host, and the trigger point when it runs free.
    bool running = false;
    bool originIsSong = false;
    double originQ = 0.0;
    bool haveClockEnd = false;
    double clockEndPpq = 0.0;

    int slot = 0;                     // playing pattern
    int queued = -1;                  // pattern waiting to take over
    int desiredSlot = 0;
    double launchLineS = 0.0;         // NextBar: the bar line the queued pattern starts on, fixed when it is queued
    double launchBarQ = 0.0;          // the bar length that line was worked out with; 0 = no line
    double anchorS = 0.0;
    std::int64_t gridIndex = 0;
    std::int64_t nextG = 0;
    std::int64_t cycleStartG = 0;
    double curStepQ = 0.25;
    int lastStep = -1;

    bool slidePending = false;        // the sounding note carries a slide: hold the gate into the next note
    bool heldByRest = false;          // HoldThroughRest: the gate is open over a rest step, no longer over a note
    double pendingOffS = kNever;      // when the gate would close; the decision is taken then, not earlier

    int ratchetLeft = 0;
    double nextRatchetS = kNever;
    double ratchetPeriodS = 0.0;
    double ratchetGateS = 0.0;
    bool ratchetSlide = false;
    int curNote = 0;
    bool curAccent = false;
    bool curVibrato = false;

    std::array<std::uint8_t, kNumSlots> triggers {};   // Host Note: trigger notes down, oldest first
    int numTriggers = 0;

    // ---- MIDI out mirror ----
    struct PendingOff { int note; int due; };
    static constexpr int kMaxPendingOffs = 8;

    int midiNote = -1;
    int midiVelocity = 0;
    bool midiSustain = false;         // CC64 sent: a same-pitch slide changed the accent
    bool midiVibrato = false;         // CC1 sent high
    std::array<PendingOff, kMaxPendingOffs> pendingOffs {};
    int numPendingOffs = 0;

    std::array<std::uint64_t, 32> thruHeld {};   // [channel * 2 + note / 64]: notes passed through and still on
    bool thruAnyHeld = false;
    int thruHeldCount = 0;            // bits set in thruHeld: each keeps one output slot free for its note-off
    std::array<std::uint64_t, 32> thruOwed {};   // held notes whose note-off arrived but did not fit: sent first next block
    bool thruAnyOwed = false;
};
}
