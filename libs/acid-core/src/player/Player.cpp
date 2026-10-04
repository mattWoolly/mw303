#include "acid/seq/Player.h"
#include "acid/common/Math.h"
#include <algorithm>
#include <cmath>

namespace acid::seq
{
namespace
{
/** On a start or a relocation, a step that began no more than this long ago (in quarter
    notes) is still played, late, instead of being skipped. Hosts land a hair past the bar
    line they were sent to. */
constexpr double kChaseQ = 1.0 / 64.0;

/** An arpeggio key that arrives this long after a grid step still plays that step. */
constexpr double kCatchSeconds = 0.030;

/** The most events the step engine puts on one sample. In normal running a sample carries
    three at the very most. More than this means the engine is behind the clock (a damaged
    state) or that steps are shorter than a sample (an absurd sample rate): it then moves on
    to the next sample and skips the steps it has missed instead of playing them all at once.
    This is what bounds the step loop: a block of n samples makes at most 8 n events. */
constexpr int kMaxEventsPerSample = 8;

constexpr int kVoiceReserve = 8;     // voice-event slots kept for events that close notes
constexpr int kMidiReserve = 16;     // the same for MIDI out

constexpr int kNoteOff = 0x80;
constexpr int kNoteOnStatus = 0x90;
constexpr int kControlChange = 0xB0;
constexpr int kPitchBendStatus = 0xE0;

constexpr int kCcModWheel = 1;
constexpr int kCcSustain = 64;
}

//==============================================================================
void Player::prepare (double sampleRate, int /* maxBlockSize: every buffer here has a fixed size */)
{
    const bool wasPrepared = prepared;

    prepared = false;
    tracker.prepare (sampleRate);

    // Preparing again in the middle of a note is a panic like any other: whatever was on is
    // turned off by the next block. The first time, there is nothing to turn off.
    reset();
    panicPending = wasPrepared;

    prepared = true;
}

void Player::reset() noexcept
{
    tracker.reset();
    external.reset();
    arp.reset();

    modeKnown = false;
    panicPending = true;     // the MIDI out mirror keeps its state until the next block turns it off

    gateOpen = false;
    bendRaw = 0;
    lastBendRange = -1;
    wheel = 0.0f;

    running = false;
    originIsSong = false;
    originQ = 0.0;
    haveClockEnd = false;
    clockEndPpq = 0.0;
    slot = 0;
    queued = -1;
    desiredSlot = 0;
    launchLineS = 0.0;
    launchBarQ = 0.0;
    anchorS = 0.0;
    gridIndex = 0;
    nextG = 0;
    cycleStartG = 0;
    curStepQ = 0.25;
    lastStep = -1;
    numTriggers = 0;
    clearGateState();
}

//==============================================================================
void Player::process (const TransportInfo& transport, std::span<const MidiMsg> midiIn, const PatternBank& patternBank,
                      const PlayerControls& controls, const PlayerConfig& config, int numSamples, PlayerOutput& output) noexcept
{
    output.clear();

    out = &output;
    bank = &patternBank;
    ctl = &controls;
    cfg = &config;

    if (! prepared || numSamples <= 0)
    {
        clock = nullptr;
        fillStatus();
        return;
    }

    blockSize = numSamples;
    clock = &tracker.process (transport, numSamples, controls.internalBpm);
    qps = clock->quartersPerSample;
    bpm = clock->bpm;
    segOffset = 0;
    segStartPpq = clock->segments[0].startPpq;

    if (panicPending)
    {
        panicPending = false;

        VoiceEvent e;
        e.type = VoiceEvent::Type::AllOff;
        pushVoice (e, true);

        e.type = VoiceEvent::Type::PitchBend;
        pushVoice (e, false);

        mirrorFlush (0);
        thruFlush (0);
    }

    thruPayOwed (0);
    applyBlockStartChanges();

    std::size_t midiIndex = 0;

    for (int i = 0; i < clock->numSegments; ++i)
    {
        const ClockSegment& seg = clock->segments[static_cast<std::size_t> (i)];
        const int segEnd = seg.offset + seg.numSamples;
        const bool lastSegment = i + 1 == clock->numSegments;

        beginSegment (seg);

        if (i == 0)
            updateRunState();

        int cursor = seg.offset;

        while (midiIndex < midiIn.size())
        {
            const MidiMsg& m = midiIn[midiIndex];

            if (m.offset >= segEnd && ! lastSegment)
                break;

            const int at = clamp (static_cast<int> (m.offset), cursor, segEnd - 1);
            advance (cursor, at);
            cursor = at;
            handleMidi (m, at);
            ++midiIndex;
        }

        advance (cursor, segEnd);

        clockEndPpq = seg.startPpq + static_cast<double> (seg.numSamples) * qps;
        haveClockEnd = true;
    }

    finishMidiBlock();
    fillStatus();
}

//==============================================================================
void Player::applyBlockStartChanges() noexcept
{
    PlayMode wantedMode = PlayMode::Sequencer;

    if (ctl->playMode == PlayMode::External) wantedMode = PlayMode::External;
    else if (ctl->playMode == PlayMode::Arpeggiator) wantedMode = PlayMode::Arpeggiator;

    const TriggerMode wantedTrig = cfg->triggerMode == TriggerMode::HostNote ? TriggerMode::HostNote : TriggerMode::Live;

    if (! modeKnown || wantedMode != mode || (wantedMode == PlayMode::Sequencer && wantedTrig != trig))
    {
        if (modeKnown)
            stopEverything (0);

        mode = wantedMode;
        modeKnown = true;

        // External mode scales vibrato by the mod wheel. Pattern playback uses full depth
        // on the steps that are flagged.
        sendVibratoDepth (0);
    }

    trig = wantedTrig;

    if (! mirrorEnabled())
        mirrorFlush (0);

    if (! thruEnabled() && thruAnyHeld)
        thruFlush (0);

    const int range = std::min (static_cast<int> (cfg->bendRange), 24);

    if (range != lastBendRange)
    {
        const bool first = lastBendRange < 0;
        lastBendRange = range;

        if (! first && bendRaw != 0)
            sendPitchBend (0);
    }
}

void Player::beginSegment (const ClockSegment& seg) noexcept
{
    segOffset = seg.offset;
    const double previousEnd = clockEndPpq;
    segStartPpq = seg.startPpq;

    if (! seg.discontinuity || ! haveClockEnd || ! engineMode() || ! (running || gateOpen))
        return;

    if (cfg->hostLink)
        relocateEngine (seg.offset);
    else
        originQ += seg.startPpq - previousEnd;    // running free: the jump is not ours, time carries on
}

void Player::updateRunState() noexcept
{
    switch (mode)
    {
        case PlayMode::External:
            return;

        case PlayMode::Arpeggiator:
            desiredSlot = desiredFromControls();
            break;

        case PlayMode::Sequencer:
            if (trig == TriggerMode::Live)
            {
                desiredSlot = desiredFromControls();

                const bool wanted = ctl->run || (cfg->followHostTransport && clock->hostRolling);

                if (wanted && ! running)
                    startEngine (0, StartAlign::Grid);
                else if (! wanted && running)
                    stopEngine (0);
            }
            break;
    }

    applyDesiredSlot (0);
    refreshGrid (0);
}

void Player::refreshGrid (int sample) noexcept
{
    if (! running)
        return;

    const Pattern& p = patternFor (slot);
    const double stepQ = stepLengthQ (p.div, p.divMod);

    if (stepQ == curStepQ)
        return;

    // The division changed under the playhead (an edit, or an immediate pattern change):
    // keep the anchor and carry on with the first step of the new grid that has not passed.
    curStepQ = stepQ;
    gridIndex = gridIndexFor (anchorS, stepQ);
    nextG = clockFor (slot, anchorS, gridIndex).firstStepAtOrAfter (lowerBoundAt (sample));
    cycleStartG = nextG - floorMod (nextG, cycleLength (patternLength (p), p.direction));
}

void Player::fillStatus() noexcept
{
    PlayerStatus& s = out->status;
    s.running = running;
    s.gateOpen = gateOpen;
    s.hostRolling = clock != nullptr && clock->hostRolling;
    s.internalClock = clock == nullptr || clock->source == ClockSource::Internal;
    s.playingSlot = static_cast<std::int16_t> (modeKnown && engineMode() ? slot : -1);
    s.queuedSlot = static_cast<std::int16_t> (running ? queued : -1);
    s.currentStep = static_cast<std::int16_t> (running ? lastStep : -1);
    s.ppq = tracker.position();
    s.bpm = tracker.bpm();
    s.hostStarted = clock != nullptr && clock->hostStarted;
    s.relocated = clock != nullptr && clock->relocated;

    switch (mode)
    {
        case PlayMode::External:    s.heldNotes = static_cast<std::int16_t> (external.heldCount()); break;
        case PlayMode::Arpeggiator: s.heldNotes = static_cast<std::int16_t> (arp.heldCount()); break;
        case PlayMode::Sequencer:   s.heldNotes = static_cast<std::int16_t> (numTriggers); break;
    }
}

//==============================================================================
const Pattern& Player::patternFor (int slotIndexToPlay) const noexcept
{
    const auto i = static_cast<std::size_t> (clamp (slotIndexToPlay, 0, kNumSlots - 1));
    return mode == PlayMode::Arpeggiator ? bank->arp[i] : bank->seq[i];
}

StepClock Player::clockFor (int slotIndexToPlay, double anchor, std::int64_t index) const noexcept
{
    return StepClock::forPattern (patternFor (slotIndexToPlay), anchor, index);
}

int Player::desiredFromControls() const noexcept
{
    return slotIndex (clamp (ctl->bank, 0, kNumBanks - 1), clamp (ctl->slot, 0, kSlotsPerBank - 1));
}

double Player::barLengthQ() const noexcept
{
    const double bar = static_cast<double> (clock->timeSigNum) * 4.0 / static_cast<double> (clock->timeSigDen);
    return bar >= 0.25 ? bar : 4.0;
}

std::int64_t Player::cycleIndexFor (const Pattern& p, std::int64_t g, std::int64_t cycleStart) const noexcept
{
    const std::int64_t length = cycleLength (patternLength (p), p.direction);

    // Locked to the host, the place in the pattern is a pure function of the step index, so a
    // relocation and an uninterrupted run agree. Running free, a pass that is cut short by a
    // length edit wraps to the first step.
    if (cfg->hostLink)
        return floorMod (g, length);

    const std::int64_t i = g - cycleStart;
    return (i < 0 || i >= length) ? 0 : i;
}

void Player::setLaunchLine (int sample) noexcept
{
    // The first bar line that has not passed. Worked out once, when the pattern is queued (or
    // when the bar grid changes under it), from that instant and from nothing else: whether a
    // step boundary is the launch point then depends only on the step, never on where the
    // audio blocks happen to fall.
    launchBarQ = barLengthQ();
    launchLineS = std::ceil (lowerBoundAt (sample) / launchBarQ) * launchBarQ;
}

void Player::skipLateSteps (int sample) noexcept
{
    if (! running)
        return;

    const Pattern& p = patternFor (slot);
    const std::int64_t g = clockFor (slot, anchorS, gridIndex).firstStepAtOrAfter (lowerBoundAt (sample));

    if (g <= nextG)
        return;

    nextG = g;
    cycleStartG = nextG - floorMod (nextG, cycleLength (patternLength (p), p.direction));
}

Player::NextStep Player::peekNext() const noexcept
{
    NextStep nx;

    if (! running)
        return nx;

    const Pattern& p = patternFor (slot);
    const StepClock c = clockFor (slot, anchorS, gridIndex);

    nx.valid = true;
    nx.slot = slot;
    nx.anchorS = anchorS;
    nx.gridIndex = gridIndex;
    nx.g = nextG;
    nx.cycleStart = cycleStartG;
    nx.onsetS = c.onsetQ (nextG);

    if (queued < 0 || queued == slot)
        return nx;

    // Is the queued pattern due at this boundary? The answer is a function of the step alone,
    // so the look-ahead at a gate-off and the boundary itself always agree, in any blocking.
    double launch = 0.0;
    bool due = false;

    if (cfg->launchQuant == LaunchQuant::NextBar)
    {
        // Due once the playing pattern's next step would start on or after the bar line. The
        // new pattern is anchored on the line itself, so it starts there even when the old
        // grid does not touch it, and its first step takes its own shuffle delay from there.
        if (launchBarQ > 0.0 && launchLineS <= nx.onsetS + 1.0e-9)
        {
            due = true;
            launch = launchLineS;
        }
    }
    else if (cycleIndexFor (p, nextG, cycleStartG) == 0)
    {
        due = true;
        launch = c.gridQ (nextG);
    }

    if (due)
    {
        const Pattern& q = patternFor (queued);
        const StepClock nc = clockFor (queued, launch, gridIndexFor (launch, stepLengthQ (q.div, q.divMod)));

        nx.switches = true;
        nx.slot = queued;
        nx.anchorS = launch;
        nx.gridIndex = nc.gridIndex;
        nx.g = 0;
        nx.cycleStart = 0;
        nx.onsetS = nc.onsetQ (0);
    }

    return nx;
}

Player::Resolved Player::resolve (const NextStep& nx) const noexcept
{
    Resolved r;

    const Pattern& p = patternFor (nx.slot);
    const int length = patternLength (p);
    const std::int64_t absIndex = nx.g + nx.gridIndex;
    const int cycleIndex = static_cast<int> (cycleIndexFor (p, nx.g, nx.cycleStart));

    r.position = clamp (stepPosition (p.direction, length, cycleIndex, p.gen.seed, absIndex), 0, length - 1);

    const Step& s = p.steps[static_cast<std::size_t> (r.position)];
    r.step = &s;
    r.state = s.state == StepState::Note ? StepState::Note : (s.state == StepState::Tie ? StepState::Tie : StepState::Rest);

    if (ctl->ovrNoteClr)
    {
        r.state = StepState::Rest;
        r.muted = true;
    }
    else if (ctl->ovrNoteSet)
    {
        if (r.state == StepState::Rest)
            r.state = StepState::Note;
    }
    else if (r.state != StepState::Rest && ! stepRollPasses (p.gen.seed, absIndex, s.probability))
    {
        r.state = StepState::Rest;
    }

    r.accent = (s.accent || ctl->ovrAccentSet) && ! ctl->ovrAccentClr;
    r.slide = (s.slide || ctl->ovrSlideSet) && ! ctl->ovrSlideClr;
    r.vibrato = (s.vibrato || ctl->ovrVibSet) && ! ctl->ovrVibClr;
    r.ratchet = clamp (static_cast<int> (s.ratchet), 1, 4);

    const int transpose = static_cast<int> (p.transpose) + clamp (ctl->liveTranspose, -127, 127);

    if (mode == PlayMode::Arpeggiator)
    {
        const int n = arp.stepNote (s, transpose);    // already clamped to 0..127, as below

        if (n < 0)
            r.state = StepState::Rest;    // no key is held
        else
            r.note = n;
    }
    else
    {
        r.note = clamp (stepMidiNote (s) + transpose, 0, 127);
    }

    return r;
}

void Player::advance (int from, int to) noexcept
{
    enum class Kind { Off, Ratchet, Step };

    // Every pass either moves the cursor forward or counts against kMaxEventsPerSample, so
    // the loop makes at most (kMaxEventsPerSample + 1) passes per sample, whatever state it
    // is entered with.
    int eventsOnThisSample = 0;

    while (from < to)
    {
        const NextStep nx = peekNext();

        // The earliest of: the gate-off instant, a ratchet hit, the next step. At equal
        // times they are taken in that order, so a NoteOff precedes a NoteOn.
        Kind kind = Kind::Off;
        double t = pendingOffS;

        if (nextRatchetS < t)
        {
            kind = Kind::Ratchet;
            t = nextRatchetS;
        }

        if (nx.valid && nx.onsetS < t)
        {
            kind = Kind::Step;
            t = nx.onsetS;
        }

        if (! (t < kNever))
            return;

        // Counted from the segment start. Compared before it is added to anything: for a far
        // away event it is the largest integer there is.
        const std::int64_t index = sampleIndexAtOrAfter (t, sAt (segOffset), qps);

        if (index >= static_cast<std::int64_t> (to - segOffset))
            return;

        // An event whose time has already passed (a start just after a step, clock jitter)
        // plays now.
        const int sample = index > static_cast<std::int64_t> (from - segOffset) ? segOffset + static_cast<int> (index) : from;

        if (sample > from)
            eventsOnThisSample = 0;

        if (++eventsOnThisSample > kMaxEventsPerSample)
        {
            // This sample is full. Carry on from the next one, and if it is steps that are
            // piling up, drop the ones that are already in the past.
            eventsOnThisSample = 0;
            from = sample + 1;

            if (kind == Kind::Step && ! nx.switches)
                skipLateSteps (from);

            continue;
        }

        from = sample;

        switch (kind)
        {
            case Kind::Off:     gateOffDecision (sample); break;
            case Kind::Ratchet: ratchetHit (sample); break;
            case Kind::Step:    fireStep (nx, sample); break;
        }
    }
}

void Player::fireStep (const NextStep& next, int sample) noexcept
{
    if (next.switches)
    {
        // The queued pattern takes over here, from its first step. A slide that is pending
        // carries into it: the gate state is not touched.
        slot = next.slot;
        queued = -1;
        anchorS = next.anchorS;
        gridIndex = next.gridIndex;
        nextG = 0;
        cycleStartG = 0;
    }

    const Pattern& p = patternFor (slot);
    const StepClock c = clockFor (slot, anchorS, gridIndex);
    const std::int64_t g = nextG;
    const std::int64_t length = cycleLength (patternLength (p), p.direction);

    curStepQ = c.stepQ;

    if (cfg->hostLink)
        cycleStartG = g - floorMod (g, length);
    else if (g < cycleStartG || g - cycleStartG >= length)
        cycleStartG = g;

    NextStep here = next;
    here.slot = slot;
    here.g = g;
    here.cycleStart = cycleStartG;

    const Resolved r = resolve (here);

    const double t = next.onsetS;
    const double stepDuration = c.stepDurationQ (g);
    const double earliestOff = sAt (sample + 1);    // a gate is at least one sample long

    // A gate that is not held never reaches the next onset: it closes a sample before.
    const auto clampGate = [this] (double gate, double window) noexcept
    {
        return std::max (std::min (gate, window - qps), qps);
    };

    ratchetLeft = 0;
    nextRatchetS = kNever;

    switch (r.state)
    {
        case StepState::Note:
        {
            const bool legato = gateOpen && slidePending;
            const double gate = gateLengthQ (p, *r.step, bpm);

            heldByRest = false;
            noteOn (sample, r.note, r.accent, legato, r.vibrato);

            curNote = r.note;
            curAccent = r.accent;
            curVibrato = r.vibrato;

            if (r.ratchet > 1)
            {
                // Evenly spaced re-strikes inside the step. A slide leaves from the last one.
                ratchetPeriodS = stepDuration / static_cast<double> (r.ratchet);
                ratchetGateS = clampGate (gate / static_cast<double> (r.ratchet), ratchetPeriodS);
                ratchetLeft = r.ratchet - 1;
                nextRatchetS = t + ratchetPeriodS;
                ratchetSlide = r.slide;
                slidePending = false;
                pendingOffS = t + ratchetGateS;
            }
            else
            {
                slidePending = r.slide;
                pendingOffS = t + clampGate (gate, stepDuration);
            }

            pendingOffS = std::max (pendingOffS, earliestOff);
            break;
        }

        case StepState::Tie:
            // Holds an open gate for this step's gate time. No new pitch, no accent change, and
            // a pending slide stays pending. With nothing sounding a tie is silent, and so is
            // a tie after a rest, even while that rest's held gate has not closed yet.
            if (gateOpen && ! heldByRest)
                pendingOffS = std::max (t + clampGate (gateLengthQ (p, *r.step, bpm), stepDuration), earliestOff);
            break;

        case StepState::Rest:
            if (r.muted)
            {
                // Note Clr: nothing sounds past this boundary. A gate that is waiting for its
                // own gate time closes there (the decision does not hold into a muted step);
                // one that a slide or a tie was holding open closes here.
                if (gateOpen && ! (pendingOffS < kNever))
                {
                    noteOff (sample);
                    clearGateState();
                }
            }
            else if (gateOpen && slidePending)
            {
                // The gate was held into a rest by the slide-into-rest policy.
                switch (cfg->slideIntoRest)
                {
                    case SlideIntoRest::HoldThroughRest:
                        // The old pitch sounds for the rest's gate time and no further: a tie
                        // after this rest is a tie after a rest, which is silent.
                        slidePending = false;
                        heldByRest = true;
                        pendingOffS = std::max (t + clampGate (gateLengthQ (p, *r.step, bpm), stepDuration), earliestOff);
                        break;

                    case SlideIntoRest::SustainToNextNote:
                        pendingOffS = kNever;
                        break;

                    case SlideIntoRest::Release:
                        // Only reached when the step was edited after the hold was decided.
                        noteOff (sample);
                        clearGateState();
                        break;
                }
            }
            else if (gateOpen && ! (pendingOffS < kNever))
            {
                noteOff (sample);
            }
            break;
    }

    nextG = g + 1;
    lastStep = r.position;
}

void Player::gateOffDecision (int sample) noexcept
{
    pendingOffS = kNever;

    const bool overRest = heldByRest;
    heldByRest = false;

    if (! gateOpen)
        return;

    // Decided now, not when the note started, so an edit made while the note sounds is heard.
    bool hold = false;

    if (running && ratchetLeft == 0 && ! overRest)
    {
        const Resolved r = resolve (peekNext());

        // Nothing holds a gate into a step that Note Clr mutes: the note ends at its own
        // gate time, whatever the slide-into-rest policy says.
        if (r.muted)
            hold = false;
        else if (r.state == StepState::Tie)
            hold = true;
        else if (slidePending)
            hold = r.state == StepState::Note || cfg->slideIntoRest != SlideIntoRest::Release;
    }

    if (hold)
        return;

    noteOff (sample);
    slidePending = false;
}

void Player::ratchetHit (int sample) noexcept
{
    const double t = nextRatchetS;

    if (ctl->ovrNoteClr)
    {
        // Muted mid-step: the hits that are left do not sound. The one that is sounding ends
        // at its own gate time.
        ratchetLeft = 0;
        nextRatchetS = kNever;
        return;
    }

    noteOn (sample, curNote, curAccent, false, curVibrato);

    --ratchetLeft;
    pendingOffS = std::max (t + ratchetGateS, sAt (sample + 1));

    if (ratchetLeft > 0)
    {
        nextRatchetS = t + ratchetPeriodS;
    }
    else
    {
        nextRatchetS = kNever;
        slidePending = ratchetSlide;
    }
}

void Player::startEngine (int sample, StartAlign align) noexcept
{
    if (gateOpen)
        noteOff (sample);

    clearGateState();

    const double startPpq = ppqAt (sample);

    // Locked to the host, engine time is song time and the pattern phase follows the song
    // position. Otherwise engine time starts here. The arpeggiator always uses the host grid
    // when there is one, so that it lands on the beat.
    originIsSong = clock->source == ClockSource::Host && (cfg->hostLink || mode == PlayMode::Arpeggiator);
    originQ = originIsSong ? 0.0 : startPpq;

    const double s = startPpq - originQ;

    slot = clamp (desiredSlot, 0, kNumSlots - 1);
    queued = -1;

    const Pattern& p = patternFor (slot);
    curStepQ = stepLengthQ (p.div, p.divMod);

    switch (align)
    {
        case StartAlign::Grid:
            anchorS = 0.0;
            gridIndex = 0;
            nextG = clockFor (slot, anchorS, gridIndex).firstStepAtOrAfter (s - kChaseQ);
            break;

        case StartAlign::Immediate:
            anchorS = s;
            gridIndex = gridIndexFor (s, curStepQ);
            nextG = 0;
            break;

        case StartAlign::Catch:
        {
            const double before = std::floor (s / curStepQ);
            const double catchQ = std::min (0.25 * curStepQ, kCatchSeconds * bpm / 60.0);
            const double index = (s - before * curStepQ <= catchQ) ? before : before + 1.0;

            anchorS = index * curStepQ;
            gridIndex = gridIndexFor (anchorS, curStepQ);
            nextG = 0;
            break;
        }
    }

    cycleStartG = nextG - floorMod (nextG, cycleLength (patternLength (p), p.direction));
    lastStep = -1;
    running = true;
}

void Player::stopEngine (int sample) noexcept
{
    running = false;
    queued = -1;
    lastStep = -1;

    if (gateOpen)
        noteOff (sample);

    clearGateState();
    mirrorWheelToRest (sample);
}

void Player::releaseArp (int sample) noexcept
{
    // All keys are up: the cursor resets and no further step fires. The sounding note ends
    // at its own gate time. A gate that was being held open with no end in sight closes now.
    running = false;
    queued = -1;
    lastStep = -1;
    slidePending = false;
    ratchetLeft = 0;
    nextRatchetS = kNever;

    if (gateOpen && ! (pendingOffS < kNever))
        noteOff (sample);
}

void Player::relocateEngine (int sample) noexcept
{
    if (gateOpen)
        noteOff (sample);

    clearGateState();

    if (! running)
        return;

    if (! originIsSong)
    {
        if (clock->source != ClockSource::Host)
        {
            originQ += segStartPpq - clockEndPpq;
            return;
        }

        // Started on the internal clock, and now the host is playing: take up the song grid.
        originIsSong = true;
        originQ = 0.0;
        anchorS = 0.0;
        gridIndex = 0;
    }

    const Pattern& p = patternFor (slot);

    nextG = clockFor (slot, anchorS, gridIndex).firstStepAtOrAfter (sAt (sample) - kChaseQ);
    cycleStartG = nextG - floorMod (nextG, cycleLength (patternLength (p), p.direction));

    // Engine time jumped with the song: a queued pattern waits for the next bar line from here.
    if (queued >= 0 && launchBarQ > 0.0)
        setLaunchLine (sample);
}

void Player::applyDesiredSlot (int sample) noexcept
{
    desiredSlot = clamp (desiredSlot, 0, kNumSlots - 1);

    if (! running)
    {
        slot = desiredSlot;
        queued = -1;
        return;
    }

    if (desiredSlot == slot)
    {
        queued = -1;
        return;
    }

    const bool newlyQueued = queued != desiredSlot;
    queued = desiredSlot;

    switch (cfg->launchQuant)
    {
        case LaunchQuant::Immediate:
            switchImmediately (sample);
            break;

        case LaunchQuant::NextBar:
            // The bar line is fixed here and now. It is worked out again only when what it
            // was worked out from changes: another pattern is queued, the quantisation is
            // switched to NextBar, or the time signature changes.
            if (newlyQueued || ! (launchBarQ > 0.0) || launchBarQ != barLengthQ())
                setLaunchLine (sample);
            break;

        case LaunchQuant::EndOfPattern:
            launchBarQ = 0.0;
            break;
    }
}

void Player::switchImmediately (int sample) noexcept
{
    // The new pattern takes over at once and keeps the place: same anchor, same step
    // counter. The sounding note finishes under its own gate.
    slot = queued;
    queued = -1;
    refreshGrid (sample);
}

void Player::clearGateState() noexcept
{
    slidePending = false;
    heldByRest = false;
    pendingOffS = kNever;
    ratchetLeft = 0;
    nextRatchetS = kNever;
    ratchetPeriodS = 0.0;
    ratchetGateS = 0.0;
    ratchetSlide = false;
}

void Player::stopEverything (int sample) noexcept
{
    stopEngine (sample);     // closes the gate whichever mode opened it
    external.reset();
    arp.reset();
    numTriggers = 0;
    mirrorFlush (sample);
    thruFlush (sample);
}

//==============================================================================
void Player::handleMidi (const MidiMsg& m, int sample) noexcept
{
    if (m.size == 0)
        return;

    const int status = m.data[0];

    if (status == 0xFF)
    {
        panic (sample);
        return;
    }

    if (status < 0x80 || status >= 0xF0)
        return;

    if (thruEnabled())
        thru (m, sample);

    const int d1 = m.size > 1 ? (m.data[1] & 0x7F) : 0;
    const int d2 = m.size > 2 ? (m.data[2] & 0x7F) : 0;

    switch (status & 0xF0)
    {
        case kNoteOnStatus:
            if (d2 > 0) onNoteOn (d1, d2, sample);
            else        onNoteOff (d1, sample);
            break;

        case kNoteOff:
            onNoteOff (d1, sample);
            break;

        case kControlChange:
            onController (d1, d2, sample);
            break;

        case kPitchBendStatus:
            bendRaw = ((d2 << 7) | d1) - 8192;
            sendPitchBend (sample);
            break;

        default:
            break;
    }
}

void Player::onNoteOn (int note, int velocity, int sample) noexcept
{
    switch (mode)
    {
        case PlayMode::External:
            applyExternal (external.noteOn (note, velocity, cfg->notePriority), sample);
            break;

        case PlayMode::Arpeggiator:
            if (arp.noteOn (note))
            {
                desiredSlot = desiredFromControls();
                startEngine (sample, StartAlign::Catch);
            }
            break;

        case PlayMode::Sequencer:
        {
            if (trig != TriggerMode::HostNote)
                break;

            const int index = note - static_cast<int> (cfg->hostNoteBase);

            if (index < 0 || index >= kNumSlots)
                break;

            triggerPush (index);
            desiredSlot = index;

            if (! running)
            {
                startEngine (sample, cfg->restartOnTrigger ? StartAlign::Immediate : StartAlign::Grid);
            }
            else if (cfg->restartOnTrigger)
            {
                stopEngine (sample);
                startEngine (sample, StartAlign::Immediate);
            }
            else
            {
                applyDesiredSlot (sample);    // a legato pattern change: the phase is kept
            }
            break;
        }
    }
}

void Player::onNoteOff (int note, int sample) noexcept
{
    switch (mode)
    {
        case PlayMode::External:
            applyExternal (external.noteOff (note, cfg->notePriority), sample);
            break;

        case PlayMode::Arpeggiator:
            if (arp.noteOff (note))
                releaseArp (sample);
            break;

        case PlayMode::Sequencer:
        {
            if (trig != TriggerMode::HostNote)
                break;

            const int index = note - static_cast<int> (cfg->hostNoteBase);

            if (index < 0 || index >= kNumSlots || ! triggerRemove (index))
                break;

            if (numTriggers == 0)
            {
                stopEngine (sample);
            }
            else
            {
                // Back to the trigger note that is still down.
                desiredSlot = triggers[static_cast<std::size_t> (numTriggers - 1)];
                applyDesiredSlot (sample);
            }
            break;
        }
    }
}

void Player::onController (int number, int value, int sample) noexcept
{
    switch (number)
    {
        case kCcModWheel:
            wheel = static_cast<float> (value) / 127.0f;

            if (mode == PlayMode::External)
                sendVibratoDepth (sample);
            break;

        case kCcSustain:
            if (mode == PlayMode::External)
                applyExternal (external.sustain (value >= 64), sample);
            break;

        case 121:    // reset all controllers
            wheel = 0.0f;

            if (mode == PlayMode::External)
            {
                sendVibratoDepth (sample);
                applyExternal (external.sustain (false), sample);
            }

            if (bendRaw != 0)
            {
                bendRaw = 0;
                sendPitchBend (sample);
            }
            break;

        case 120:    // all sound off
        case 123:    // all notes off
        case 124:
        case 125:
        case 126:
        case 127:    // the mode messages also mean all notes off
            panic (sample);
            break;

        default:
            break;
    }
}

void Player::applyExternal (const ExternalNotes::Action& a, int sample) noexcept
{
    switch (a.kind)
    {
        case ExternalNotes::Action::Kind::NoteOn:
            // Vibrato is always armed in External mode: the mod wheel sets how much there is.
            noteOn (sample, a.note, a.velocity > cfg->accentThreshold, a.legato, true);
            break;

        case ExternalNotes::Action::Kind::NoteOff:
            noteOff (sample);
            break;

        case ExternalNotes::Action::Kind::None:
            break;
    }
}

void Player::panic (int sample) noexcept
{
    switch (mode)
    {
        case PlayMode::External:
            external.allNotesOff();
            noteOff (sample);
            break;

        case PlayMode::Arpeggiator:
            arp.reset();
            stopEngine (sample);
            break;

        case PlayMode::Sequencer:
            if (trig == TriggerMode::HostNote)
            {
                numTriggers = 0;
                stopEngine (sample);
            }
            else
            {
                // The pattern keeps running; only the sounding note is cut.
                noteOff (sample);
                clearGateState();
            }
            break;
    }
}

void Player::triggerPush (int index) noexcept
{
    triggerRemove (index);

    if (numTriggers < kNumSlots)
        triggers[static_cast<std::size_t> (numTriggers++)] = static_cast<std::uint8_t> (index);
}

bool Player::triggerRemove (int index) noexcept
{
    for (int i = 0; i < numTriggers; ++i)
    {
        if (triggers[static_cast<std::size_t> (i)] != index)
            continue;

        for (int j = i + 1; j < numTriggers; ++j)
            triggers[static_cast<std::size_t> (j - 1)] = triggers[static_cast<std::size_t> (j)];

        --numTriggers;
        return true;
    }

    return false;
}

//==============================================================================
bool Player::pushVoice (const VoiceEvent& e, bool closing) noexcept
{
    const int limit = closing ? PlayerOutput::kMaxVoiceEvents : PlayerOutput::kMaxVoiceEvents - kVoiceReserve;

    if (out->numVoiceEvents >= limit)
    {
        out->overflowed = true;
        return false;
    }

    out->voiceEvents[static_cast<std::size_t> (out->numVoiceEvents++)] = e;
    return true;
}

void Player::noteOn (int sample, int note, bool accent, bool legato, bool vibrato) noexcept
{
    if (gateOpen && ! legato)
        noteOff (sample);

    VoiceEvent e;
    e.type = VoiceEvent::Type::NoteOn;
    e.offset = sample;
    e.note = static_cast<std::uint8_t> (clamp (note, 0, 127));
    e.accent = accent;
    e.legato = legato && gateOpen;
    e.vibrato = vibrato;

    if (! pushVoice (e, false))
        return;    // out of room: a legato target is skipped, a fresh note stays silent

    gateOpen = true;

    if (mirrorEnabled())
        mirrorNoteOn (sample, e.note, accent, e.legato, vibrato);
}

void Player::noteOff (int sample) noexcept
{
    if (! gateOpen)
        return;

    VoiceEvent e;
    e.type = VoiceEvent::Type::NoteOff;
    e.offset = sample;
    pushVoice (e, true);

    gateOpen = false;
    mirrorNoteOff (sample);

    if (! running)
        mirrorWheelToRest (sample);    // the last note of a pattern that has stopped
}

void Player::sendPitchBend (int sample) noexcept
{
    const double unit = bendRaw >= 0 ? static_cast<double> (bendRaw) / 8191.0 : static_cast<double> (bendRaw) / 8192.0;

    VoiceEvent e;
    e.type = VoiceEvent::Type::PitchBend;
    e.offset = sample;
    e.value = static_cast<float> (unit * static_cast<double> (std::min (static_cast<int> (cfg->bendRange), 24)));
    pushVoice (e, false);
}

void Player::sendVibratoDepth (int sample) noexcept
{
    VoiceEvent e;
    e.type = VoiceEvent::Type::VibratoDepth;
    e.offset = sample;
    e.value = mode == PlayMode::External ? wheel : 1.0f;
    pushVoice (e, false);
}

//==============================================================================
bool Player::pushMidiRaw (int sample, int status, int d1, int d2, bool closing) noexcept
{
    // Closing messages may use every slot. Everything else leaves the reserve free, and one
    // more slot for each note passed through and still on, so a flood of controllers or
    // bends can never leave a thru note without room for its note-off.
    const int limit = closing ? PlayerOutput::kMaxMidiOut : PlayerOutput::kMaxMidiOut - kMidiReserve - thruHeldCount;

    if (out->numMidiOut >= limit)
    {
        out->overflowed = true;
        return false;
    }

    const int type = status & 0xF0;

    MidiMsg m;
    m.offset = clamp (sample, 0, blockSize - 1);
    m.size = static_cast<std::uint8_t> ((type == 0xC0 || type == 0xD0) ? 2 : 3);
    m.data[0] = static_cast<std::uint8_t> (status);
    m.data[1] = static_cast<std::uint8_t> (d1 & 0x7F);
    m.data[2] = static_cast<std::uint8_t> (d2 & 0x7F);

    out->midiOut[static_cast<std::size_t> (out->numMidiOut++)] = m;
    return true;
}

bool Player::sendMidi (int sample, int status, int d1, int d2, bool closing) noexcept
{
    flushPendingOffs (sample);
    return pushMidiRaw (sample, status, d1, d2, closing);
}

void Player::flushPendingOffs (int upToSample) noexcept
{
    int kept = 0;

    for (int i = 0; i < numPendingOffs; ++i)
    {
        const PendingOff p = pendingOffs[static_cast<std::size_t> (i)];

        if (p.due > upToSample || ! pushMidiRaw (p.due, kNoteOff, p.note, 0, true))
            pendingOffs[static_cast<std::size_t> (kept++)] = p;
    }

    numPendingOffs = kept;
}

void Player::mirrorNoteOn (int sample, int note, bool accent, bool legato, bool vibrato) noexcept
{
    if (out->numMidiOut + 4 > PlayerOutput::kMaxMidiOut - kMidiReserve)
    {
        out->overflowed = true;
        return;
    }

    const int velocity = clamp (static_cast<int> (accent ? cfg->outAccentVelocity : cfg->outNormalVelocity), 1, 127);

    if (vibrato != midiVibrato)
    {
        sendMidi (sample, kControlChange, kCcModWheel, vibrato ? 127 : 0, false);
        midiVibrato = vibrato;
    }

    if (legato && midiNote == note)
    {
        // A slide to the same pitch cannot overlap itself. With the same velocity the note
        // simply carries on. A changed accent is sent as a re-strike under the sustain pedal.
        if (velocity == midiVelocity)
            return;

        if (! midiSustain)
        {
            sendMidi (sample, kControlChange, kCcSustain, 127, false);
            midiSustain = true;
        }

        sendMidi (sample, kNoteOff, note, 0, true);
        sendMidi (sample, kNoteOnStatus, note, velocity, false);
        midiVelocity = velocity;
        return;
    }

    // If this pitch is still waiting for the note-off of an earlier overlap, end that first.
    for (int i = 0; i < numPendingOffs; ++i)
        if (pendingOffs[static_cast<std::size_t> (i)].note == note)
            pendingOffs[static_cast<std::size_t> (i)].due = std::min (pendingOffs[static_cast<std::size_t> (i)].due, sample);

    const int previous = midiNote;

    if (previous >= 0 && ! legato)
        mirrorNoteOff (sample);    // not expected: the voice NoteOff comes first

    sendMidi (sample, kNoteOnStatus, note, velocity, false);
    midiNote = note;
    midiVelocity = velocity;

    if (previous >= 0 && legato)
    {
        // A slide: the new note starts, and the old one ends one sample later.
        if (numPendingOffs == kMaxPendingOffs)
        {
            pushMidiRaw (sample, kNoteOff, pendingOffs[0].note, 0, true);

            for (int i = 1; i < numPendingOffs; ++i)
                pendingOffs[static_cast<std::size_t> (i - 1)] = pendingOffs[static_cast<std::size_t> (i)];

            --numPendingOffs;
        }

        pendingOffs[static_cast<std::size_t> (numPendingOffs++)] = { previous, sample + 1 };
    }
}

void Player::mirrorNoteOff (int sample) noexcept
{
    if (midiNote < 0)
        return;

    sendMidi (sample, kNoteOff, midiNote, 0, true);
    midiNote = -1;

    if (midiSustain)
    {
        sendMidi (sample, kControlChange, kCcSustain, 0, true);
        midiSustain = false;
    }
}

void Player::mirrorWheelToRest (int sample) noexcept
{
    if (! midiVibrato)
        return;

    sendMidi (sample, kControlChange, kCcModWheel, 0, true);
    midiVibrato = false;
}

void Player::mirrorFlush (int sample) noexcept
{
    for (int i = 0; i < numPendingOffs; ++i)
        pendingOffs[static_cast<std::size_t> (i)].due = std::min (pendingOffs[static_cast<std::size_t> (i)].due, sample);

    flushPendingOffs (sample);
    mirrorNoteOff (sample);
    mirrorWheelToRest (sample);
}

void Player::thru (const MidiMsg& m, int sample) noexcept
{
    const int status = m.data[0];
    const int type = status & 0xF0;
    const int channel = status & 0x0F;
    const int d1 = m.size > 1 ? (m.data[1] & 0x7F) : 0;
    const int d2 = m.size > 2 ? (m.data[2] & 0x7F) : 0;

    std::uint64_t& word = thruHeld[static_cast<std::size_t> (channel * 2 + d1 / 64)];
    const std::uint64_t bit = std::uint64_t { 1 } << (d1 % 64);

    if (type == kNoteOnStatus && d2 > 0)
    {
        // Passed through as played, but never doubled: a key that is already on is ended first.
        if ((word & bit) != 0 && ! thruRelease (channel, d1, sample, kNoteOff | channel, 0))
            return;    // its note-off is owed: the new note-on would double it, drop it

        // A note-on goes out only with a slot left over for its note-off.
        if (out->numMidiOut + 1 < PlayerOutput::kMaxMidiOut - kMidiReserve - thruHeldCount
            && sendMidi (sample, status, d1, d2, false))
        {
            word |= bit;
            ++thruHeldCount;
            thruAnyHeld = true;
        }
        else
        {
            out->overflowed = true;
        }
    }
    else if (type == kNoteOff || type == kNoteOnStatus)
    {
        // A note-off for a key that was never passed through is not passed either.
        if ((word & bit) != 0)
            thruRelease (channel, d1, sample, status, d2);
    }
    else
    {
        sendMidi (sample, status, d1, d2, false);
    }
}

/** Sends the note-off of a held thru note and forgets it. The slot is always there (a held
    note keeps one free); should it not be, the note-off is owed and goes out at the top of
    the next block, ahead of anything else. Returns false when it is owed. */
bool Player::thruRelease (int channel, int note, int sample, int status, int velocity) noexcept
{
    const auto w = static_cast<std::size_t> (channel * 2 + note / 64);
    const std::uint64_t bit = std::uint64_t { 1 } << (note % 64);

    if (! sendMidi (sample, status, note, velocity, true))
    {
        thruOwed[w] |= bit;
        thruAnyOwed = true;
        return false;
    }

    thruHeld[w] &= ~bit;
    thruOwed[w] &= ~bit;
    --thruHeldCount;
    return true;
}

void Player::thruPayOwed (int sample) noexcept
{
    if (! thruAnyOwed)
        return;

    bool left = false;
    for (int w = 0; w < 32; ++w)
    {
        std::uint64_t& owed = thruOwed[static_cast<std::size_t> (w)];
        for (int b = 0; b < 64 && owed != 0; ++b)
        {
            const std::uint64_t bit = std::uint64_t { 1 } << b;
            if ((owed & bit) != 0)
                thruRelease (w / 2, (w % 2) * 64 + b, sample, kNoteOff | (w / 2), 0);
        }
        left = left || owed != 0;
    }
    thruAnyOwed = left;
}

void Player::thruFlush (int sample) noexcept
{
    if (! thruAnyHeld)
        return;

    flushPendingOffs (sample);

    bool left = false;

    for (int w = 0; w < 32; ++w)
    {
        std::uint64_t& word = thruHeld[static_cast<std::size_t> (w)];

        for (int b = 0; b < 64 && word != 0; ++b)
        {
            const std::uint64_t bit = std::uint64_t { 1 } << b;

            if ((word & bit) != 0 && pushMidiRaw (sample, kNoteOff | (w / 2), (w % 2) * 64 + b, 0, true))
            {
                word &= ~bit;
                thruOwed[static_cast<std::size_t> (w)] &= ~bit;
                --thruHeldCount;
            }
        }

        if (word != 0)
        {
            // What did not fit is owed: ended at the top of the next block.
            thruOwed[static_cast<std::size_t> (w)] |= word;
            thruAnyOwed = true;
        }
        left = left || word != 0;
    }

    thruAnyHeld = left;
}

void Player::finishMidiBlock() noexcept
{
    flushPendingOffs (blockSize - 1);

    for (int i = 0; i < numPendingOffs; ++i)
        pendingOffs[static_cast<std::size_t> (i)].due = std::max (0, pendingOffs[static_cast<std::size_t> (i)].due - blockSize);
}
}
