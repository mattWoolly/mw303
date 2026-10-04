#include "PluginProcessor.h"

#include "FactoryContent.h"
#include "acid/seq/Generator.h"
#include "acid/seq/PatternOps.h"
#include "acid/seq/PlayheadTracker.h"

// A core header compares floats exactly where it means to; JUCE's warning set objects.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#include "acid/fx/DistortionVoicings.h"
#pragma GCC diagnostic pop

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#ifndef MW_PRODUCT_NAME
 #define MW_PRODUCT_NAME "mw12F"
#endif

using namespace acid;
using namespace acid::plugin;

namespace
{
constexpr int kMaxFactoryPrograms = 4;
constexpr std::uint32_t kProgramGuardMs = 500;
/** The session-state element that holds the custom distortion voicing. */
constexpr const char* kCustomVoicingElement = "dist_custom";
/** The rate the constructor measures the latency at: what hosts use most. */
constexpr double kDefaultSampleRate = 48000.0;
/** How long a preview keeps the voice in External mode after its last key went up (the
    release tail), in seconds. */
constexpr double kPreviewHoldSeconds = 1.0;

/** How a MIDI input message ranks when a host block carries more than the stage holds:
    0 closes something (note-off, note-on with velocity 0, sustain off, All Sound Off, Reset
    All Controllers, All Notes Off and the mode messages that imply it, system reset), 1 opens
    a note, 2 is everything else. Closing messages are kept first, so a flood never strands a
    note (the engine's own pending-MIDI path ranks them the same way). */
int midiInPriority (const juce::uint8* data, int size) noexcept
{
    if (size < 1) return 2;
    const int status = data[0] & 0xF0;
    if (data[0] == 0xFF) return 0;
    if (size >= 3 && status == 0x80) return 0;
    if (size >= 3 && status == 0x90) return data[2] == 0 ? 0 : 1;
    if (size >= 3 && status == 0xB0)
    {
        const int cc = data[1];
        if (cc == 64 && data[2] < 64) return 0;
        if (cc == 120 || cc == 121 || cc >= 123) return 0;
    }
    return 2;
}

std::uint32_t nowMs() noexcept
{
    using namespace std::chrono;
    return static_cast<std::uint32_t> (duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count());
}

bool onMessageThread()
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm != nullptr && mm->isThisTheMessageThread();
}
}

//==============================================================================
mw12FProcessor::mw12FProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params(),
      apvts (*this, nullptr, "mw12F_params", std::move (params.layout)),
      presets (factoryFiles(), PresetManager::defaultUserRoot(), PresetManager::defaultUserRoot())
{
    for (auto& m : liveMidiMap)
        m.store (-1, std::memory_order_relaxed);

    ctl = std::make_unique<Controller> (*this);
    loadInitialState();

    // CLAP is not a JUCE wrapper type: the entry point says so (PluginEntry.cpp).
    if (wrapperType == wrapperType_VST3)
        setHostFormat (HostFormat::Vst3);
    else if (wrapperType == wrapperType_LV2)
        setHostFormat (HostFormat::Lv2);
    else if (wrapperType == wrapperType_Standalone)
        setHostFormat (HostFormat::Standalone);

    reportInitialLatency();
    messageWork.startTimerHz (30);
}

void mw12FProcessor::setHostFormat (HostFormat f) noexcept
{
    format = f;
    // Hosts compensate a plugin's audio for its latency but deliver its MIDI output as it
    // comes (REAPER: VST3 and CLAP note-ons arrive the latency early, LV2 on the grid).
    midiOutDelayWanted = f == HostFormat::Vst3 || f == HostFormat::Clap;

    // CLAP: clap-juce-extensions answers any latency change, the one inside the first
    // activate included, with host->request_restart(), and a host may serve that in the
    // middle of a render. The engine's latency depends on the rate (45 samples at 48 kHz,
    // 68 at 44.1 kHz on the Normal tier), so under CLAP the reported latency never goes below
    // the largest of the common rates, measured at construction; prepareToPlay delays the
    // audio by the difference. Called before the host has read anything.
    latencyFloor = f == HostFormat::Clap ? initialLatencyAllRates : 0;
    if (latencyFloor > getLatencySamples())
        setLatencySamples (latencyFloor);
}

void mw12FProcessor::reportInitialLatency()
{
    // What the first prepareToPlay will report, measured on the engine (released again
    // afterwards) for the realtime tier: at the default rate for the host to read now, and
    // at the other common rates for the CLAP floor (setHostFormat).
    std::lock_guard<std::mutex> guard (prepareLock);
    {
        std::lock_guard<std::mutex> lock (modelData.mutex);
        publishModelLocked();
    }
    eng.setNonRealtime (offline.load (std::memory_order_relaxed));
    int atDefault = 0;
    initialLatencyAllRates = 0;
    for (const double rate : { 44100.0, kDefaultSampleRate })
    {
        eng.prepare (rate, kMinPreparedBlock);
        initialLatencyAllRates = std::max (initialLatencyAllRates, eng.latencySamples());
        if (juce::exactlyEqual (rate, kDefaultSampleRate))
            atDefault = eng.latencySamples();
    }
    eng.release();
    setLatencySamples (std::max (atDefault, latencyFloor));
}

mw12FProcessor::~mw12FProcessor()
{
    messageWork.stopTimer();
    prepared.store (false, std::memory_order_seq_cst);
    waitForProcessToLeave();
    ctl.reset();
}

const juce::String mw12FProcessor::getName() const
{
    return MW_PRODUCT_NAME;
}

bool mw12FProcessor::userDefaultState (PresetData& out) const
{
    const auto path = presets.userRoot() / "default.mw12f";
    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec))
        return false;
    std::ifstream f (path, std::ios::binary);
    if (! f)
        return false;
    std::ostringstream text;
    text << f.rdbuf();
    PresetData d;
    if (! parsePreset (text.str(), d).ok || (d.type != PresetType::State && d.type != PresetType::Scene))
        return false;
    out = std::move (d);
    return true;
}

void mw12FProcessor::loadInitialState()
{
    // The user's default (checklist 118) when one was saved, otherwise the factory default.
    PresetData user;
    const bool haveUser = userDefaultState (user);
    const FactoryFile* f = haveUser ? nullptr : findFactoryFile ("default.mw12f");
    if (haveUser || f != nullptr)
    {
        PresetData d;
        if (haveUser)
            d = std::move (user);
        if (haveUser || parsePreset (f->text(), d).ok)
        {
            applyPresetData (d, true);
            // What a new instance holds is what hosts call the default (clap-validator checks
            // the two agree right after construction).
            for (HostParam* p : params.byId)
                p->setDefaultNatural (p->natural());
            std::lock_guard<std::mutex> lock (modelData.mutex);
            modelData.sceneName = d.name.empty() ? "Init" : d.name;
            return;
        }
    }
    std::lock_guard<std::mutex> lock (modelData.mutex);
    publishModelLocked();
    publishMidiMapLocked();
    modelData.distortion.appliedModel = paramAsInt (defaultParamValues(), kDistModel);
    modelData.distortion.written = defaultParamValues();
}

//==============================================================================
// The handshake. Both sides store their own flag and then load the other's: a Dekker
// pattern, which needs seq_cst on those four operations (release/acquire allows the store to
// pass the later load, and then prepare sees inProcess == 0 while the block sees prepared).
void mw12FProcessor::waitForProcessToLeave() noexcept
{
    while (inProcess.load (std::memory_order_seq_cst) != 0)
        std::this_thread::yield();
}

bool mw12FProcessor::enterProcess() noexcept
{
    if (! prepared.load (std::memory_order_seq_cst))
        return false;
    inProcess.fetch_add (1, std::memory_order_seq_cst);
    if (prepared.load (std::memory_order_seq_cst))
        return true;
    leaveProcess();
    return false;
}

void mw12FProcessor::leaveProcess() noexcept
{
    inProcess.fetch_sub (1, std::memory_order_release);
}

Quality mw12FProcessor::wantedQualityLocked() const noexcept
{
    const Quality q = offline.load (std::memory_order_relaxed) ? modelData.settings.offlineQuality
                                                                : modelData.settings.realtimeQuality;
    return static_cast<std::uint8_t> (q) <= static_cast<std::uint8_t> (Quality::Ultra) ? q : Quality::Normal;
}

void mw12FProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    std::lock_guard<std::mutex> guard (prepareLock);
    prepareLocked (sampleRate, samplesPerBlock);
}

void mw12FProcessor::prepareLocked (double sampleRate, int samplesPerBlock)
{
    prepared.store (false, std::memory_order_seq_cst);
    waitForProcessToLeave();

    preparedRate = sampleRate > 0.0 ? sampleRate : kDefaultSampleRate;
    hostBlockHint = samplesPerBlock;
    preparedBlock = std::max (kMinPreparedBlock, samplesPerBlock);

    // The tier is read here and only here: the offline flag as the host has set it so far
    // and the settings. Anything that changes either later waits for the next prepare.
    preparedOffline = offline.load (std::memory_order_relaxed);
    lastBlockOffline = preparedOffline;     // a prepare already starts from rest
    {
        std::lock_guard<std::mutex> lock (modelData.mutex);
        publishModelLocked();
        publishMidiMapLocked();
        preparedQuality = wantedQualityLocked();
    }
    eng.setNonRealtime (preparedOffline);
    eng.prepare (preparedRate, preparedBlock);
    restartRequested = false;

    scratchLeft.assign (static_cast<std::size_t> (preparedBlock), 0.0f);
    scratchRight.assign (static_cast<std::size_t> (preparedBlock), 0.0f);
    midiInStage.assign (static_cast<std::size_t> (kMaxMidiInPerBlock + kMaxPreviewPerBlock), seq::MidiMsg {});
    midiOutStage.assign (static_cast<std::size_t> (eng.midiOutCapacityFor (preparedBlock) + kMaxMidiInPerBlock), seq::MidiMsg {});
    hostMidiStorage.clear();
    hostMidiStorage.ensureSize (kHostMidiStorageBytes);
    hostMidiSwapped = false;
    bypassStopSent = true;
    framePpq = FramePpq {};
    previewHeld.fill (false);
    previewHeldCount = 0;
    previewHoldSamples = 0;
    previewOverride = false;

    // The reported latency: the engine's, raised to the CLAP floor (setHostFormat) by delaying
    // the audio by the difference.
    const int engineLatency = eng.latencySamples();
    const int latency = std::max (engineLatency, latencyFloor);
    padSamples = latency - engineLatency;
    for (auto& line : padLines)
        line.assign (static_cast<std::size_t> (std::max (1, padSamples)), 0.0f);
    padPos = 0;

    // The MIDI delay line: what the engine emitted in the last `latency` samples, at most a
    // block's output budget per block of at least one sample. Messages still waiting from
    // before this prepare are kept (the engine already counts them as sent).
    midiOutDelayed = midiOutDelayWanted && latency > 0;
    midiDelaySamples = midiOutDelayed ? latency : 0;
    const auto delayCapacity = static_cast<std::size_t> (kMaxMidiOutPerBlock) * static_cast<std::size_t> (latency + 2) + 64u;
    if (midiDelay.size() < delayCapacity)
    {
        std::vector<DelayedMidi> bigger (delayCapacity);
        for (std::size_t i = 0; i < midiDelayCount; ++i)
            bigger[i] = midiDelay[(midiDelayHead + i) % midiDelay.size()];
        midiDelay = std::move (bigger);
        midiDelayHead = 0;
    }

    setLatencySamples (latency);
    prepared.store (true, std::memory_order_seq_cst);
}

void mw12FProcessor::releaseResources()
{
    std::lock_guard<std::mutex> guard (prepareLock);
    prepared.store (false, std::memory_order_seq_cst);
    waitForProcessToLeave();
    eng.release();
}

void mw12FProcessor::setNonRealtime (bool isNonRealtime) noexcept
{
    // The VST3 and LV2 wrappers call this from the audio thread before every block, CLAP's
    // render extension from the main thread while active: only an atomic is written. The next
    // prepareToPlay hands it to the engine (followQualityTier never re-prepares while it is set).
    AudioProcessor::setNonRealtime (isNonRealtime);
    offline.store (isNonRealtime, std::memory_order_relaxed);
}

void mw12FProcessor::followQualityTier()
{
    std::unique_lock<std::mutex> guard (prepareLock, std::try_to_lock);
    if (! guard.owns_lock())
        return;    // the host is inside prepareToPlay or releaseResources: it picks the tier
    if (! prepared.load (std::memory_order_relaxed))
        return;
    {
        std::lock_guard<std::mutex> lock (modelData.mutex);
        if (wantedQualityLocked() == preparedQuality)
        {
            restartRequested = false;
            return;
        }
    }

    // Never during a render and never while anything plays: the engine keeps the tier it was
    // prepared with until the host prepares again.
    if (offline.load (std::memory_order_relaxed) || hostPlaying.load (std::memory_order_relaxed))
        return;
    const EngineStatus st = eng.status();
    if (st.running || st.gateOpen)
        return;

    if (format == HostFormat::Standalone)
    {
        // Nobody to ask: the Standalone re-prepares itself while it is silent and stopped.
        prepareLocked (preparedRate, hostBlockHint);
        return;
    }

    if (restartRequested)
        return;
    restartRequested = true;
    guard.unlock();

    // VST3 turns this into restartComponent (kLatencyChanged), clap-juce-extensions into
    // host->request_restart(): the host deactivates and activates the plugin, which runs
    // prepareToPlay. LV2 has no such request; the tier follows at the host's next activate.
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
}

bool mw12FProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

double mw12FProcessor::getTailLengthSeconds() const
{
    return eng.tailSeconds();
}

seq::TransportInfo mw12FProcessor::readTransport (int numSamples) noexcept
{
    seq::TransportInfo t;
    auto* head = getPlayHead();
    if (head == nullptr)
        return t;
    const auto position = head->getPosition();
    if (! position.hasValue())
        return t;

    t.valid = true;
    t.playing = position->getIsPlaying();
    if (const auto ppq = position->getPpqPosition(); ppq.hasValue() && std::isfinite (*ppq))
    {
        t.ppqValid = true;
        t.ppq = *ppq;
    }
    if (const auto bpm = position->getBpm(); bpm.hasValue() && std::isfinite (*bpm) && *bpm > 0.0)
    {
        t.bpmValid = true;
        t.bpm = *bpm;
    }
    t.looping = position->getIsLooping();
    if (const auto loop = position->getLoopPoints(); loop.hasValue())
    {
        t.loopStartPpq = loop->ppqStart;
        t.loopEndPpq = loop->ppqEnd;
    }
    if (const auto sig = position->getTimeSignature(); sig.hasValue() && sig->numerator > 0 && sig->denominator > 0)
    {
        t.timeSigNum = sig->numerator;
        t.timeSigDen = sig->denominator;
    }

    if (t.ppqValid)
        framePpq.active = false;
    else
        reconstructPpq (t, *position, numSamples);
    return t;
}

void mw12FProcessor::reconstructPpq (seq::TransportInfo& t, const juce::AudioPlayHead::PositionInfo& position, int numSamples) noexcept
{
    // The JUCE 8.0.15 LV2 wrapper reads time:beat from the host's time:Position but never
    // passes it on: a playing LV2 transport arrives with a tempo, a bar number and a frame,
    // and no song position. Without one the sequencer free-runs from wherever it was. Rebuild
    // it: at a start or a jump from the frame at the current tempo (exact while the tempo has
    // been constant), checked against the host's bar number; while the host plays on
    // seamlessly, by integrating the tempo block to block (steps and ramps both exactly, see
    // below), bounded by the bar the host reports.
    const auto frame = position.getTimeInSamples();
    if (! t.playing || ! t.bpmValid || ! frame.hasValue() || ! (preparedRate > 0.0))
    {
        framePpq.active = false;
        return;
    }

    const double samplesPerMinute = 60.0 * preparedRate;
    const double barQ = 4.0 * static_cast<double> (t.timeSigNum) / static_cast<double> (t.timeSigDen);
    const auto bar = position.getBarCount();
    const bool barKnown = bar.hasValue() && barQ > 0.0 && *bar >= 0 && *bar < (std::int64_t { 1 } << 40);
    const double barStart = barKnown ? static_cast<double> (*bar) * barQ : 0.0;

    double q = 0.0;
    double change = 0.0;
    if (framePpq.active && *frame == framePpq.nextFrame)
    {
        // The tempo the previous block played at. A host reports the tempo at the start of
        // each block: a step lands on a block boundary (the previous block ran at its own
        // tempo), a ramp moves it every block (the mean of the two ends is exact for a ramp
        // that is linear in time). A change that continues one in the same direction is a ramp.
        change = t.bpm - framePpq.bpm;
        const bool ramp = (change > 0.0 && framePpq.change > 0.0) || (change < 0.0 && framePpq.change < 0.0);
        const double bpmPlayed = ramp ? 0.5 * (framePpq.bpm + t.bpm) : framePpq.bpm;
        q = framePpq.ppq + static_cast<double> (framePpq.nextFrame - framePpq.frame) * bpmPlayed / samplesPerMinute;
        if (barKnown)
            q = std::clamp (q, barStart, std::nextafter (barStart + barQ, barStart));
    }
    else
    {
        q = static_cast<double> (*frame) * t.bpm / samplesPerMinute;
        if (barKnown && (q < barStart - 1.0e-6 || q >= barStart + barQ - 1.0e-6))
            q = barStart;    // the tempo was not constant up to here: the bar line is what is known
    }

    if (! seq::PlayheadTracker::isUsablePpq (q))
    {
        framePpq.active = false;
        return;
    }

    framePpq.active = true;
    framePpq.ppq = q;
    framePpq.bpm = t.bpm;
    framePpq.change = change;
    framePpq.frame = *frame;
    framePpq.nextFrame = *frame + std::max (0, numSamples);
    t.ppqValid = true;
    t.ppq = q;
}

void mw12FProcessor::applyMidiControllers (std::span<const seq::MidiMsg> midiIn) noexcept
{
    for (const seq::MidiMsg& m : midiIn)
    {
        if (m.size != 3 || (m.data[0] & 0xF0) != 0xB0)
            continue;
        const int cc = m.data[1] & 0x7F;
        const int value = m.data[2] & 0x7F;
        if (cc >= MidiMap::kFirstChannelMode)
            continue;    // All Notes Off and the other channel-mode messages never move a parameter

        const int target = learnTarget.load (std::memory_order_relaxed);
        if (target >= 0)
        {
            learnTarget.store (-1, std::memory_order_relaxed);
            learnQueue.push ({ static_cast<std::int16_t> (cc), static_cast<std::int16_t> (target) });
        }

        const int p = liveMidiMap[static_cast<std::size_t> (cc)].load (std::memory_order_relaxed);
        if (p >= 0 && p < kNumParams)
        {
            HostParam* param = params.byId[static_cast<std::size_t> (p)];
            const float v = param->setNaturalFromAudioThread (param->naturalForController (value));
            blockParams[static_cast<std::size_t> (p)] = v;
            echoQueue.push ({ static_cast<std::int16_t> (p), v });
        }
    }
}

void mw12FProcessor::processChunk (const seq::TransportInfo& transport, std::span<const seq::MidiMsg> midiIn,
                                   float* left, float* right, int numSamples, juce::MidiBuffer& midiOutHost, int outOffset,
                                   int& midiOutBudget) noexcept
{
    // The engine's balanced overflow policy applies to what is left of the block's budget, so
    // a note-on that would not fit is dropped rather than left hanging.
    const int capacity = std::clamp (midiOutBudget, 0, static_cast<int> (midiOutStage.size()));
    MidiOutBuffer out { midiOutStage.data(), capacity, 0, false };
    eng.process (blockParams, transport, midiIn, out, left, right, numSamples);
    emitMidiOut (out.messages, out.count, midiOutHost, outOffset);
    midiOutBudget -= out.count;
}

void mw12FProcessor::emitMidiOut (const seq::MidiMsg* messages, int count, juce::MidiBuffer& host, int outOffset) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const seq::MidiMsg& m = messages[i];
        if (m.size < 1 || m.size > 3)
            continue;
        if (! midiOutDelayed)
        {
            host.addEvent (m.data, static_cast<int> (m.size), outOffset + static_cast<int> (m.offset));
            continue;
        }
        // Sized in prepareToPlay so it cannot fill (one block's budget per sample of delay);
        // should it ever, the message goes out undelayed rather than being lost.
        if (midiDelayCount >= midiDelay.size())
        {
            host.addEvent (m.data, static_cast<int> (m.size), outOffset + static_cast<int> (m.offset));
            continue;
        }
        DelayedMidi& d = midiDelay[(midiDelayHead + midiDelayCount) % midiDelay.size()];
        d.due = midiClock + outOffset + m.offset + midiDelaySamples;
        d.msg = m;
        ++midiDelayCount;
    }
}

void mw12FProcessor::flushMidiDelay (juce::MidiBuffer& host, int numSamples, bool all) noexcept
{
    // Entries are in emission order, which is time order (one constant delay).
    const int last = std::max (0, numSamples - 1);
    while (midiDelayCount > 0)
    {
        const DelayedMidi& d = midiDelay[midiDelayHead];
        const std::int64_t at = d.due - midiClock;
        if (! all && at >= numSamples)
            break;
        host.addEvent (d.msg.data, static_cast<int> (d.msg.size), static_cast<int> (std::clamp<std::int64_t> (at, 0, last)));
        midiDelayHead = (midiDelayHead + 1) % midiDelay.size();
        --midiDelayCount;
    }
}

int mw12FProcessor::stagePreviewNotes (bool allowed) noexcept
{
    // Preview notes go in front of the host's MIDI at offset 0. A key goes down only while
    // the sequencer is stopped; a key that is up again is always let through, so a preview
    // never stays on.
    int n = 0;
    seq::MidiMsg m;
    while (n < kMaxPreviewPerBlock && previewQueue.pop (m))
    {
        if (m.size != 3) continue;
        const int status = m.data[0] & 0xF0;
        const int key = m.data[1] & 0x7F;
        const bool on = status == 0x90 && m.data[2] > 0;
        const bool off = status == 0x80 || (status == 0x90 && m.data[2] == 0);
        if (on)
        {
            if (! allowed || previewHeld[static_cast<std::size_t> (key)]) continue;
            previewHeld[static_cast<std::size_t> (key)] = true;
            ++previewHeldCount;
        }
        else if (off)
        {
            if (! previewHeld[static_cast<std::size_t> (key)]) continue;
            previewHeld[static_cast<std::size_t> (key)] = false;
            if (--previewHeldCount == 0)
                previewHoldSamples = static_cast<std::int64_t> (kPreviewHoldSeconds * preparedRate);
        }
        else
        {
            continue;
        }
        m.offset = 0;
        midiInStage[static_cast<std::size_t> (n++)] = m;
    }
    return n;
}

int mw12FProcessor::stageMidiIn (juce::MidiBuffer& midi, int numSamples, int first) noexcept
{
    const int lastOffset = std::max (0, numSamples - 1);
    const int room = kMaxMidiInPerBlock;

    // More than the stage holds (a controller flood): keep the closing messages first, then
    // the note-ons, then the rest, each class in buffer order, so the stage stays sorted and
    // no key released in the dropped tail stays down.
    std::array<int, 3> total {};
    for (const auto meta : midi)
        if (meta.numBytes >= 1 && meta.numBytes <= 3)
            ++total[static_cast<std::size_t> (midiInPriority (meta.data, meta.numBytes))];
    std::array<int, 3> keep = total;
    if (total[0] + total[1] + total[2] > room)
    {
        keep[0] = std::min (total[0], room);
        keep[1] = std::min (total[1], room - keep[0]);
        keep[2] = std::min (total[2], room - keep[0] - keep[1]);
    }

    std::array<int, 3> seen {};
    int midiCount = first;
    for (const auto meta : midi)
    {
        if (meta.numBytes < 1 || meta.numBytes > 3)
            continue;
        const auto rank = static_cast<std::size_t> (midiInPriority (meta.data, meta.numBytes));
        if (seen[rank]++ >= keep[rank])
            continue;
        seq::MidiMsg& m = midiInStage[static_cast<std::size_t> (midiCount++)];
        m.offset = std::clamp (meta.samplePosition, 0, lastOffset);
        m.size = static_cast<std::uint8_t> (meta.numBytes);
        std::memcpy (m.data, meta.data, static_cast<std::size_t> (meta.numBytes));
    }
    midi.clear();

    // The host's buffer has no capacity query and addEvent grows it. Once per prepare, hand
    // it the storage sized in prepareToPlay (a pointer swap, no allocation); from then on
    // the same buffer object carries that capacity through every block.
    if (! hostMidiSwapped)
    {
        midi.swapWith (hostMidiStorage);
        midi.clear();
        hostMidiSwapped = true;
    }
    return midiCount;
}

void mw12FProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (! enterProcess())
    {
        buffer.clear();
        midi.clear();
        return;
    }
    struct Leave
    {
        mw12FProcessor& p;
        ~Leave() { p.leaveProcess(); }
    } leave { *this };

    bypassStopSent = false;    // sounding again: a later bypass has something to stop

    // A render starts here (CLAP's render mode or LV2's freewheel switched on while the plugin
    // was already running): the engine goes back to rest at this block boundary, so the render
    // does not depend on what the voice did while the host idled before it (oscillator phase,
    // drift, filter state). Nothing is re-prepared and the latency does not move.
    const bool isOffline = offline.load (std::memory_order_relaxed);
    if (isOffline && ! lastBlockOffline)
        eng.reset();
    lastBlockOffline = isOffline;

    params.read (blockParams);
    seq::TransportInfo transport = readTransport (numSamples);
    hostPlaying.store (transport.valid && transport.playing, std::memory_order_relaxed);

    // Note preview (Controller::previewNote): only while the sequencer is stopped. The voice
    // plays it in External mode, so for as long as a preview key is down and for its release
    // tail the block runs in External mode; anything that starts the sequencer ends that.
    const bool sequencerStopped = blockParams[static_cast<std::size_t> (kSeqRun)] < 0.5f
                                  && ! (transport.valid && transport.playing)
                                  && ! eng.status().running;
    const int previewCount = stagePreviewNotes (sequencerStopped);
    if (! sequencerStopped && (previewHeldCount > 0 || previewHoldSamples > 0))
    {
        previewHeld.fill (false);
        previewHeldCount = 0;
        previewHoldSamples = 0;
    }
    previewOverride = sequencerStopped && (previewHeldCount > 0 || previewHoldSamples > 0);
    if (previewOverride)
    {
        blockParams[static_cast<std::size_t> (kPlayMode)] = static_cast<float> (static_cast<int> (seq::PlayMode::External));
        if (previewHeldCount == 0)
            previewHoldSamples = std::max<std::int64_t> (0, previewHoldSamples - numSamples);
    }

    // Stage the MIDI input in our own fixed array (after the preview notes), then take the
    // buffer over for the output.
    const int midiCount = stageMidiIn (midi, numSamples, previewCount);
    const std::span<const seq::MidiMsg> midiIn { midiInStage.data(), static_cast<std::size_t> (midiCount) };
    applyMidiControllers (midiIn.subspan (static_cast<std::size_t> (previewCount)));

    float* left = numChannels >= 1 ? buffer.getWritePointer (0) : scratchLeft.data();
    float* right = numChannels >= 2 ? buffer.getWritePointer (1) : scratchRight.data();
    int midiOutBudget = kMaxMidiOutPerBlock;

    if (numSamples <= preparedBlock)
    {
        processChunk (transport, midiIn, left, right, numSamples, midi, 0, midiOutBudget);
        if (numChannels == 1)
            for (int i = 0; i < numSamples; ++i)
                left[i] = 0.5f * (left[i] + right[i]);
    }
    else
    {
        // An oversized block: cut it into prepared-size chunks. The first chunk gets the host's
        // position, the rest say "no news" and the engine's clock extrapolates through them.
        std::size_t midiIndex = 0;
        for (int pos = 0; pos < numSamples;)
        {
            const int len = std::min (preparedBlock, numSamples - pos);
            const std::size_t firstMsg = midiIndex;
            while (midiIndex < midiIn.size() && midiIn[midiIndex].offset < pos + len)
            {
                seq::MidiMsg& m = midiInStage[midiIndex];
                m.offset = std::max (0, static_cast<int> (m.offset) - pos);
                ++midiIndex;
            }
            float* l = numChannels >= 1 ? left + pos : scratchLeft.data();
            float* r = numChannels >= 2 ? right + pos : scratchRight.data();
            processChunk (transport, midiIn.subspan (firstMsg, midiIndex - firstMsg), l, r, len, midi, pos, midiOutBudget);
            if (numChannels == 1)
            {
                // Mono output: every chunk's right channel lands at the start of the scratch,
                // so it is mixed in before the next chunk overwrites it.
                for (int i = 0; i < len; ++i)
                    l[i] = 0.5f * (l[i] + r[i]);
            }
            transport = seq::TransportInfo {};
            pos += len;
        }
    }

    if (padSamples > 0)
        padAudio (buffer.getArrayOfWritePointers(), std::min (2, numChannels), numSamples);

    if (midiOutDelayed)
        flushMidiDelay (midi, numSamples, false);
    midiClock += numSamples;

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);
}

void mw12FProcessor::padAudio (float* const* channels, int numChannels, int numSamples) noexcept
{
    const auto length = static_cast<std::size_t> (padSamples);
    std::size_t pos = padPos;
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float& slot = padLines[static_cast<std::size_t> (ch)][pos];
            const float in = channels[ch][i];
            channels[ch][i] = slot;
            slot = in;
        }
        pos = pos + 1 == length ? 0 : pos + 1;
    }
    padPos = pos;
}

void mw12FProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    if (! enterProcess())
    {
        buffer.clear();
        midi.clear();
        return;
    }
    struct Leave
    {
        mw12FProcessor& p;
        ~Leave() { p.leaveProcess(); }
    } leave { *this };

    // The input is staged and dropped like processBlock does (nothing is echoed), the output
    // is silence: an instrument has no input to delay, so the latency is matched trivially.
    stageMidiIn (midi, numSamples, 0);
    buffer.clear();

    if (bypassStopSent || numSamples <= 0)
    {
        midiClock += std::max (0, numSamples);
        return;
    }
    bypassStopSent = true;

    // A preview still sounding ends with the rest.
    previewHeld.fill (false);
    previewHeldCount = 0;
    previewHoldSamples = 0;
    previewOverride = false;

    // One engine block with the sequencer stopped: Run off, the host transport stopped, and
    // All Notes Off for the modes a held note or trigger drives. The player's stop path then
    // sends the note-offs for whatever is sounding, into the host buffer; the audio is dropped.
    params.read (blockParams);
    blockParams[static_cast<std::size_t> (kSeqRun)] = 0.0f;
    seq::TransportInfo stopped;
    stopped.valid = true;
    stopped.playing = false;
    seq::MidiMsg allNotesOff;
    allNotesOff.offset = 0;
    allNotesOff.size = 3;
    allNotesOff.data[0] = 0xB0;
    allNotesOff.data[1] = 123;
    allNotesOff.data[2] = 0;
    midiInStage[0] = allNotesOff;

    const int len = std::min (numSamples, preparedBlock);
    int midiOutBudget = kMaxMidiOutPerBlock;
    processChunk (stopped, std::span<const seq::MidiMsg> (midiInStage.data(), 1), scratchLeft.data(), scratchRight.data(),
                  len, midi, 0, midiOutBudget);

    // Nothing more comes out while bypassed: what the delay line holds goes now.
    if (midiOutDelayed)
        flushMidiDelay (midi, numSamples, true);
    midiClock += numSamples;
}

//==============================================================================
void mw12FProcessor::publishModelLocked() noexcept
{
    eng.publishBank (*modelData.bank);
    eng.publishSettings (modelData.settings);
}

void mw12FProcessor::publishMidiMapLocked() noexcept
{
    for (int cc = 0; cc < MidiMap::kNumControllers; ++cc)
        liveMidiMap[static_cast<std::size_t> (cc)].store (modelData.midiMap.param[static_cast<std::size_t> (cc)], std::memory_order_relaxed);
}

void mw12FProcessor::handlePendingMessages()
{
    drainQueues();
    followDistortionModel();
    followQualityTier();
    if (ctl != nullptr)
        ctl->pump();
}

void mw12FProcessor::drainQueues()
{
    LearnEvent learned;
    bool mapChanged = false;
    while (learnQueue.pop (learned))
    {
        std::lock_guard<std::mutex> lock (modelData.mutex);
        if (learned.param >= 0 && learned.param < kNumParams && learned.cc >= 0 && learned.cc < MidiMap::kNumControllers)
        {
            modelData.midiMap.clearParam (learned.param);
            modelData.midiMap.param[static_cast<std::size_t> (learned.cc)] = learned.param;
            publishMidiMapLocked();
            modelData.version.fetch_add (1, std::memory_order_relaxed);
            mapChanged = true;
        }
    }
    if (mapChanged && ctl != nullptr)
        ctl->notifyLater (kChangedMidiMap);

    ParamEcho echo;
    while (echoQueue.pop (echo))
    {
        if (echo.param < 0 || echo.param >= kNumParams) continue;
        HostParam* p = params.byId[static_cast<std::size_t> (echo.param)];
        // Tell the host (and every attachment) about the value the audio thread stored. The
        // normalised value maps back to the stored value, so nothing moves.
        p->setValueNotifyingHost (p->getValue());
    }
}

void mw12FProcessor::followDistortionModel()
{
    const auto voicings = fx::distortionVoicings();
    const auto voicingIds = distortionVoicingParams();
    std::vector<std::pair<ParamId, float>> toSet;
    bool resetModel = false;

    // A load on another thread sets the tracker under the mutex and then writes the
    // parameters one by one without it: in between, dist_model is new while the voicing rows
    // are still old, which would read as a hand edit. The counter is raised before the load
    // takes the mutex and dropped after its last write, so checking it under the mutex sees
    // either the untouched tracker or the finished load.
    if (loadsInProgress.load (std::memory_order_acquire) != 0)
        return;
    {
        std::lock_guard<std::mutex> lock (modelData.mutex);
        if (loadsInProgress.load (std::memory_order_acquire) != 0)
            return;
        DistortionTracker& tr = modelData.distortion;
        ParamValues v;
        params.read (v);
        const int model = paramAsInt (v, kDistModel);

        if (tr.appliedModel < 0)
        {
            tr.appliedModel = model;
            tr.written = v;
            if (model == 0)
                rememberCustomVoicingLocked (v);
            return;
        }

        if (model != tr.appliedModel)
        {
            // Leaving Custom: the rows hold the user's voicing (edits made since the last tick
            // included), so it is saved before a factory model writes over them.
            if (tr.appliedModel == 0)
                rememberCustomVoicingLocked (v);

            ParamValues w = v;
            if (model > 0 && model < static_cast<int> (voicings.size()))
                fx::writeVoicing (w, voicings[static_cast<std::size_t> (model)]);
            else if (model == 0 && tr.hasCustom)
                for (const ParamId id : voicingIds)
                    w[static_cast<std::size_t> (id)] = tr.custom[static_cast<std::size_t> (id)];   // back to the user's voicing
            // else: Custom with nothing saved (or an index the table does not have): keep the values

            for (const ParamId id : voicingIds)
                if (! juce::exactlyEqual (w[static_cast<std::size_t> (id)], v[static_cast<std::size_t> (id)]))
                    toSet.emplace_back (id, w[static_cast<std::size_t> (id)]);
            tr.written = w;
            tr.appliedModel = model;
        }
        else if (model > 0)
        {
            bool edited = false;
            for (const ParamId id : voicingIds)
                if (! juce::exactlyEqual (v[static_cast<std::size_t> (id)], tr.written[static_cast<std::size_t> (id)]))
                    edited = true;
            if (edited)
            {
                tr.written = v;
                tr.appliedModel = 0;
                rememberCustomVoicingLocked (v);
                resetModel = true;
            }
        }
        else
        {
            rememberCustomVoicingLocked (v);    // Custom selected: every edit is the custom voicing
        }
    }

    for (const auto& [id, value] : toSet)
        params.byId[static_cast<std::size_t> (id)]->setNatural (value);
    if (resetModel)
        params.byId[kDistModel]->setNatural (0.0f);
    if ((! toSet.empty() || resetModel) && ctl != nullptr)
        ctl->notifyLater (kChangedParams);
}

void mw12FProcessor::rememberCustomVoicingLocked (const ParamValues& v) noexcept
{
    DistortionTracker& tr = modelData.distortion;
    bool changed = ! tr.hasCustom;
    for (const ParamId id : distortionVoicingParams())
    {
        const auto i = static_cast<std::size_t> (id);
        if (! juce::exactlyEqual (tr.custom[i], v[i]))
        {
            tr.custom[i] = v[i];
            changed = true;
        }
    }
    tr.hasCustom = true;
    if (changed)
        modelData.version.fetch_add (1, std::memory_order_relaxed);
}

//==============================================================================
PresetData mw12FProcessor::captureState() const
{
    ParamValues v;
    params.read (v);

    std::lock_guard<std::mutex> lock (modelData.mutex);
    PresetData d = makePreset (PresetType::State, v, modelData.settings, modelData.bank.get());
    d.name = modelData.sceneName;
    d.author = modelData.sceneAuthor;
    d.tags = modelData.sceneTags;
    d.description = modelData.sceneDescription;

    const PresetData& c = modelData.carried;
    d.extraElements = c.extraElements;
    d.extraRootAttributes = c.extraRootAttributes;
    d.extraSettingsAttributes = c.extraSettingsAttributes;
    d.extraParamsAttributes = c.extraParamsAttributes;
    d.extraParamElements = c.extraParamElements;
    d.extraParamAttributes = c.extraParamAttributes;
    d.extraBankAttributes = c.extraBankAttributes;
    d.extraBankElements = c.extraBankElements;

    xml::Element ui = modelData.ui;
    ui.name = "ui";
    ui.setAttribute ("program", std::to_string (modelData.currentProgram));
    d.setExtra (std::move (ui));
    if (! modelData.midiMap.empty())
        d.setExtra (modelData.midiMap.toXml());
    else
        std::erase_if (d.extraElements, [] (const xml::Element& e) { return e.name == "midi_map"; });

    // The custom distortion voicing, while a factory model has written over the rows (with
    // Custom selected the rows are the custom voicing and nothing more is needed).
    std::erase_if (d.extraElements, [] (const xml::Element& e) { return e.name == kCustomVoicingElement; });
    const DistortionTracker& tr = modelData.distortion;
    if (tr.hasCustom && paramAsInt (v, kDistModel) != 0)
    {
        xml::Element custom;
        custom.name = kCustomVoicingElement;
        for (const ParamId id : distortionVoicingParams())
            custom.setAttribute (paramInfo (id).id, formatParamValue (paramInfo (id), tr.custom[static_cast<std::size_t> (id)]));
        d.setExtra (std::move (custom));
    }
    return d;
}

void mw12FProcessor::applyPresetData (const PresetData& d, bool clearUndoHistory, bool hostRestore)
{
    struct LoadScope
    {
        std::atomic<int>& counter;
        explicit LoadScope (std::atomic<int>& c) : counter (c) { counter.fetch_add (1, std::memory_order_acq_rel); }
        ~LoadScope() { counter.fetch_sub (1, std::memory_order_acq_rel); }
    } loading { loadsInProgress };

    ParamValues before;
    params.read (before);
    ParamValues after = before;
    applyParams (d, after);

    bool touchesDistortion = false;
    if (d.hasParam[kDistModel]) touchesDistortion = true;
    for (const ParamId id : distortionVoicingParams())
        if (d.hasParam[static_cast<std::size_t> (id)]) touchesDistortion = true;

    std::uint32_t flags = kChangedParams;
    {
        std::lock_guard<std::mutex> lock (modelData.mutex);

        // A load that is not a session restore is one undo step: the bank and settings as
        // they were and as the file leaves them (parameters are the host's, not undoable).
        // Without it the next Undo would apply an entry recorded before the load.
        const bool recordUndo = ! clearUndoHistory;
        std::unique_ptr<seq::PatternBank> bankBefore;
        const Settings settingsBefore = modelData.settings;
        const bool touchesBank = bankInType (d.type) || d.type == PresetType::Pattern || d.type == PresetType::Arp
                                 || d.type == PresetType::Template;
        if (recordUndo && touchesBank)
            bankBefore = std::make_unique<seq::PatternBank> (*modelData.bank);

        applyBank (d, *modelData.bank);
        applySettings (d, modelData.settings);
        if (bankInType (d.type)) flags |= kChangedPatterns;
        if (settingsFieldsInType (d.type) != 0) flags |= kChangedSettings;

        // A pattern, arp or template preset lands in the slot the Performance parameters select.
        const int bankIndex = std::clamp (paramAsInt (after, kPatternBank), 0, seq::kNumBanks - 1);
        const int slotInBank = std::clamp (paramAsInt (after, kPatternSlot) - 1, 0, seq::kSlotsPerBank - 1);
        const auto slot = static_cast<std::size_t> (seq::slotIndex (bankIndex, slotInBank));
        if (d.type == PresetType::Pattern && applyPattern (d, modelData.bank->seq[slot]))
        {
            seq::sanitizePattern (modelData.bank->seq[slot]);
            flags |= kChangedPatterns;
        }
        else if (d.type == PresetType::Arp && applyPattern (d, modelData.bank->arp[slot]))
        {
            seq::sanitizePattern (modelData.bank->arp[slot]);
            flags |= kChangedPatterns;
        }
        else if (d.type == PresetType::Template && applyTemplate (d, modelData.bank->seq[slot].gen))
        {
            seq::applyDensities (modelData.bank->seq[slot]);
            flags |= kChangedPatterns;
        }

        if (recordUndo)
        {
            const bool settingsChanged = std::memcmp (&settingsBefore, &modelData.settings, sizeof (Settings)) != 0;
            bool bankChanged = false;
            if (bankBefore != nullptr)
                for (int i = 0; i < seq::kNumSlots && ! bankChanged; ++i)
                {
                    const auto k = static_cast<std::size_t> (i);
                    bankChanged = ! seq::samePattern (bankBefore->seq[k], modelData.bank->seq[k])
                                  || ! seq::samePattern (bankBefore->arp[k], modelData.bank->arp[k]);
                }
            if (settingsChanged || bankChanged)
            {
                UndoEntry entry;
                entry.kind = UndoEntry::Kind::Load;
                entry.settingsBefore = settingsBefore;
                entry.settingsAfter = modelData.settings;
                if (bankChanged)
                {
                    entry.bankBefore = std::shared_ptr<const seq::PatternBank> (std::move (bankBefore));
                    entry.bankAfter = std::make_shared<const seq::PatternBank> (*modelData.bank);
                }
                entry.label = "Load " + (d.name.empty() ? std::string (presetTypeName (d.type)) : d.name);
                modelData.pushUndo (std::move (entry));
                flags |= kChangedUndo;
            }
        }

        if (d.type == PresetType::State)
        {
            PresetData carried;
            carried.type = PresetType::State;
            carried.extraElements = d.extraElements;
            std::erase_if (carried.extraElements, [] (const xml::Element& e) {
                return e.name == "ui" || e.name == "midi_map" || e.name == kCustomVoicingElement;
            });
            carried.extraRootAttributes = d.extraRootAttributes;
            carried.extraSettingsAttributes = d.extraSettingsAttributes;
            carried.extraParamsAttributes = d.extraParamsAttributes;
            carried.extraParamElements = d.extraParamElements;
            carried.extraParamAttributes = d.extraParamAttributes;
            carried.extraBankAttributes = d.extraBankAttributes;
            carried.extraBankElements = d.extraBankElements;
            modelData.carried = std::move (carried);

            if (const xml::Element* ui = d.extra ("ui"))
                modelData.ui = *ui;
            else
                modelData.ui = xml::Element {};
            modelData.ui.name = "ui";
            modelData.ui.children.clear();
            if (const std::string* program = modelData.ui.attribute ("program"))
            {
                int p = 0;
                try { p = std::stoi (*program); } catch (...) { p = 0; }
                modelData.currentProgram = std::clamp (p, 0, kMaxFactoryPrograms);
            }
            else
            {
                modelData.currentProgram = 0;
            }
            modelData.ui.removeAttribute ("program");

            if (const xml::Element* map = d.extra ("midi_map"))
                modelData.midiMap.fromXml (*map);
            else
                modelData.midiMap.clear();
            publishMidiMapLocked();
            flags |= kChangedMidiMap | kChangedUi | kChangedUndo;
        }

        if (d.type == PresetType::State || d.type == PresetType::Scene)
        {
            modelData.sceneName = d.name.empty() ? std::string ("Init") : d.name;
            modelData.sceneAuthor = d.author;
            modelData.sceneTags = d.tags;
            modelData.sceneDescription = d.description;
            flags |= kChangedPresets;
        }

        if (touchesDistortion || d.type == PresetType::State)
        {
            DistortionTracker& tr = modelData.distortion;
            tr.appliedModel = paramAsInt (after, kDistModel);
            tr.written = after;

            // The custom voicing: the rows when the document selects Custom; for a session,
            // what it saved (<dist_custom>) or nothing; a preset that selects a factory model
            // leaves the user's custom voicing alone.
            if (d.type == PresetType::State)
                tr.hasCustom = false;
            if (tr.appliedModel == 0)
            {
                for (const ParamId id : distortionVoicingParams())
                    tr.custom[static_cast<std::size_t> (id)] = after[static_cast<std::size_t> (id)];
                tr.hasCustom = true;
            }
            else if (const xml::Element* custom = d.type == PresetType::State ? d.extra (kCustomVoicingElement) : nullptr)
            {
                tr.custom = after;
                for (const ParamId id : distortionVoicingParams())
                    if (const std::string* text = custom->attribute (paramInfo (id).id))
                    {
                        float value = 0.0f;
                        bool clamped = false;
                        if (parseParamValue (paramInfo (id), *text, value, clamped))
                            tr.custom[static_cast<std::size_t> (id)] = value;
                    }
                tr.hasCustom = true;
            }
        }

        publishModelLocked();
        if (clearUndoHistory)
        {
            modelData.clearHistory();
            flags |= kChangedUndo;
        }
        modelData.version.fetch_add (1, std::memory_order_relaxed);
    }

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        if (! juce::exactlyEqual (after[idx], before[idx]) || d.type == PresetType::State)
            params.byId[idx]->setNatural (after[idx], hostRestore);
    }

    if (ctl != nullptr)
        ctl->notifyLater (flags);
}

bool mw12FProcessor::loadStateText (std::string_view text, bool hostRestore)
{
    PresetData d;
    const PresetParseResult r = parsePreset (text, d);
    if (! r.ok)
        return false;
    applyPresetData (d, true, hostRestore);
    return true;
}

void mw12FProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const std::string text = writePreset (captureState());
    destData.replaceAll (text.data(), text.size());
}

void mw12FProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const std::string_view text (static_cast<const char*> (data), static_cast<std::size_t> (sizeInBytes));
    if (! loadStateText (text, true))
        return;

    stateLoadedAt.store (nowMs(), std::memory_order_relaxed);
    stateLoads.fetch_add (1, std::memory_order_acq_rel);

    // CLAP: clap-juce-extensions turns a programChanged notification into params.rescan,
    // which clap-validator's state tests require (docs/research/07 section 5.3). VST3 tells
    // the host the parameter values changed. The call belongs on the message thread.
    const auto tell = [] (juce::AudioProcessor* p) { p->updateHostDisplay (ChangeDetails().withProgramChanged (true)); };
    if (onMessageThread())
        tell (this);
    else if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        mm->callAsync ([safe = juce::WeakReference<mw12FProcessor> (this), tell] {
            if (auto* p = safe.get()) tell (p);
        });
}

std::uint32_t mw12FProcessor::millisecondsSinceStateLoad() const noexcept
{
    const std::uint32_t at = stateLoadedAt.load (std::memory_order_relaxed);
    return at == 0 ? 0xFFFFFFFFu : nowMs() - at;
}

//==============================================================================
std::vector<std::string> mw12FProcessor::programFiles()
{
    std::vector<std::string> out;
    for (const FactoryFile& f : factoryFiles())
    {
        const std::string_view path (f.path);
        if (path.rfind ("scene/", 0) == 0 && path.size() > 6 && path.substr (path.size() - 6) == kPresetFileExtension)
            out.emplace_back (path);
        if (static_cast<int> (out.size()) >= kMaxFactoryPrograms) break;
    }
    return out;
}

int mw12FProcessor::getNumPrograms()
{
    return 1 + static_cast<int> (programFiles().size());
}

int mw12FProcessor::getCurrentProgram()
{
    std::lock_guard<std::mutex> lock (modelData.mutex);
    return modelData.currentProgram;
}

const juce::String mw12FProcessor::getProgramName (int index)
{
    if (index <= 0) return "Init";
    const auto files = programFiles();
    if (index - 1 >= static_cast<int> (files.size())) return {};
    const std::string& path = files[static_cast<std::size_t> (index - 1)];
    const auto slash = path.rfind ('/');
    std::string stem = slash == std::string::npos ? path : path.substr (slash + 1);
    stem = stem.substr (0, stem.size() - 6);
    return juce::String (stem);
}

void mw12FProcessor::setCurrentProgram (int index)
{
    // Guards (docs/research/07 section 5.4): some hosts call this right after setStateInformation
    // with a stale index, which would wipe the restored session.
    if (index < 0 || index >= getNumPrograms()) return;
    if (millisecondsSinceStateLoad() < kProgramGuardMs) return;
    {
        std::lock_guard<std::mutex> lock (modelData.mutex);
        if (index == modelData.currentProgram) return;
    }

    if (index == 0)
    {
        ctl->initScene();
    }
    else
    {
        const auto files = programFiles();
        const FactoryFile* f = findFactoryFile (files[static_cast<std::size_t> (index - 1)]);
        PresetData d;
        if (f == nullptr || ! parsePreset (f->text(), d).ok) return;
        applyPresetData (d, false);
    }

    std::lock_guard<std::mutex> lock (modelData.mutex);
    modelData.currentProgram = index;
}

//==============================================================================
/** Defined in plugin/ui/mw12FEditor.cpp. Weak, so the test executables (which compile the
    processor without the UI sources) link and fall back to the generic editor. The plugin
    formats link the shared code as a static library, where a weak reference would not pull
    the editor's object in: plugin/ui-gallery/CMakeLists.txt adds -Wl,--undefined for it. */
extern "C" juce::AudioProcessorEditor* mw12FCreateEditor (mw12FProcessor&) __attribute__ ((weak));

juce::AudioProcessorEditor* mw12FProcessor::createEditor()
{
    if (mw12FCreateEditor != nullptr)
        return mw12FCreateEditor (*this);

    return new juce::GenericAudioProcessorEditor (*this);
}
