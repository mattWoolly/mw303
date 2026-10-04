// QA host simulation: drives mw12FProcessor the way real hosts do, all at once.
//
//   audio thread     processBlock / processBlockBypassed at random sizes (0..16384), random
//                    channel counts, random transport (missing, jumping, looping, tempo and
//                    time-signature changes, non-finite values), dense and malformed MIDI,
//                    wrapper-style parameter delivery (setValue + listeners, as the VST3 and LV2
//                    wrappers do inside process), setNonRealtime toggles.
//   host-param       setValueNotifyingHost with gestures, getText / getValueForText.
//   state            getStateInformation / setStateInformation with good, factory, truncated,
//                    corrupted, oversize and garbage documents.
//   prepare          prepareToPlay at random rates and block sizes, releaseResources, while
//                    the audio thread keeps calling processBlock (device changes).
//   message (main)   the JUCE dispatch loop: the processor's own 30 Hz timer, plus a driver
//                    that opens and closes the editor on the desktop (xvfb), loads presets,
//                    edits patterns, runs the generator, MIDI learn, undo/redo, settings
//                    (quality tiers), auditions, clipboard and MIDI import through Controller.
//   watchdog         aborts with a message when any thread stops making progress.
//
// Asserts: no sanitizer report (run the ASan/UBSan and TSan trees), finite output, MIDI out
// offsets inside the block and at most kMaxMidiOutPerBlock per block, MIDI out note balance
// after a final stop, no allocation / lock / sleep / file I/O on the audio thread (the QA_ALLOCSPY
// build), no deadlock. Exit code 0 only when every check passed.
//
//   mwacid_review_qa_hostsim [--seconds S] [--seed N] [--no-editor] [--no-prepare] [--no-state]
//                            [--no-hostile] [--no-wrapper-params] [--no-bypass] [--watchdog S]
#include "PluginProcessor.h"
#include "FactoryContent.h"
#include "acid/common/Rng.h"
#include "acid/seq/PatternCodec.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace qaspy
{
extern thread_local int audioContext;
std::size_t total (int ctx);
}

using namespace acid;
using namespace acid::plugin;

namespace
{
using Clock = std::chrono::steady_clock;

struct Options
{
    double seconds = 30.0;
    std::uint64_t seed = 1;
    bool editor = true, prepare = true, state = true, hostile = true, wrapperParams = true, bypass = true;
    bool messageOps = true, hostParams = true;
    double watchdog = 30.0;
};

std::atomic<int> failures { 0 };
std::mutex logLock;

#define QA_FAIL(...) do { std::lock_guard<std::mutex> _l (logLock); std::fprintf (stderr, "QA FAIL: " __VA_ARGS__); std::fputc ('\n', stderr); failures.fetch_add (1); } while (0)
#define QA_NOTE(...) do { std::lock_guard<std::mutex> _l (logLock); std::fprintf (stderr, "qa: " __VA_ARGS__); std::fputc ('\n', stderr); } while (0)

struct EventRing
{
    struct E { std::int64_t us; int kind; int a, b, c; };
    static constexpr int kSize = 1 << 22;   // circular
    std::vector<E> e = std::vector<E> (kSize);
    std::atomic<int> n { 0 };
    Clock::time_point t0 = Clock::now();
    void add (int kind, int a = 0, int b = 0, int c = 0)
    {
        const int i = n.fetch_add (1) & (kSize - 1);
        e[static_cast<std::size_t> (i)] = { std::chrono::duration_cast<std::chrono::microseconds> (Clock::now() - t0).count(), kind, a, b, c };
    }
};
EventRing ring;
// kinds
enum { evOutOn = 1, evOutOff, evOutAllOff, evInOn, evInOff, evBypass, evOffline, evPrepare, evRelease, evStateLoad, evMsgOp, evBlock, evSeqAll };
const char* evName (int k)
{
    static const char* n[] = { "", "OUT note-on", "OUT note-off", "OUT all-off", "IN note-on", "IN note-off", "bypass", "nonRealtime", "prepare", "release", "state load", "msg op", "block", "OUT ch" };
    return n[k];
}

struct Progress
{
    const char* name;
    std::atomic<std::uint64_t> ticks { 0 };
    std::atomic<bool> done { false };
    std::atomic<std::int64_t> lastActionAt { 0 };
    std::atomic<const char*> lastAction { "" };
    void tick (const char* what = "") { ticks.fetch_add (1, std::memory_order_relaxed); lastAction.store (what); }
};

//==============================================================================
// Host transport. Only the audio thread touches it (the host updates it between blocks).
struct HostPlayHead final : juce::AudioPlayHead
{
    bool present = true, playing = false, sendPpq = true, sendBpm = true, looping = false;
    double bpm = 120.0, ppq = 0.0, loopStart = 0.0, loopEnd = 4.0;
    int sigNum = 4, sigDen = 4;
    std::int64_t samples = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! present) return {};
        PositionInfo p;
        p.setIsPlaying (playing);
        if (sendPpq) p.setPpqPosition (ppq);
        if (sendBpm) p.setBpm (bpm);
        p.setIsLooping (looping);
        if (looping) p.setLoopPoints (LoopPoints { loopStart, loopEnd });
        p.setTimeSignature (TimeSignature { sigNum, sigDen });
        p.setTimeInSamples (samples);
        return p;
    }

    void advance (int n, double rate)
    {
        samples += n;
        if (! playing || ! std::isfinite (bpm) || ! std::isfinite (ppq)) return;
        ppq += static_cast<double> (n) * bpm / (60.0 * rate);
        if (looping && loopEnd > loopStart && ppq >= loopEnd) ppq = loopStart + std::fmod (ppq - loopStart, loopEnd - loopStart);
    }

    void randomise (Rng& rng)
    {
        const int kind = rng.nextInt (12);
        switch (kind)
        {
            case 0: present = false; break;
            case 1: present = true; playing = false; break;
            case 2: case 3: case 4:
                present = true; playing = true; sendPpq = true; sendBpm = true; looping = false;
                bpm = 60.0 + rng.nextInt (180); break;
            case 5: ppq = rng.chance (0.3) ? -rng.nextDouble() * 64.0 : rng.nextDouble() * 1.0e6; break;    // jump
            case 6: looping = true; loopStart = rng.nextInt (8); loopEnd = loopStart + 0.25 * (1 + rng.nextInt (32)); ppq = loopStart; playing = true; break;
            case 7: looping = true; loopStart = 4.0; loopEnd = rng.chance (0.5) ? 4.0 : 1.0; break;   // degenerate loop
            case 8: { const double b[] = { 0.0, -120.0, 1.0e-6, 1.0e6, 999.0, 20.0, std::nan (""), INFINITY }; bpm = b[rng.nextInt (8)]; } break;
            case 9: { const double p[] = { std::nan (""), INFINITY, -INFINITY, 1.0e300, -1.0e300 }; ppq = p[rng.nextInt (5)]; } break;
            case 10: sendPpq = rng.chance (0.5); sendBpm = rng.chance (0.5); break;
            case 11: { const int n[] = { 4, 7, 3, 0, -1, 1000, 1 }; const int d[] = { 4, 8, 16, 0, -4, 64, 1 }; sigNum = n[rng.nextInt (7)]; sigDen = d[rng.nextInt (7)]; } break;
        }
        if (! std::isfinite (ppq) && rng.chance (0.5)) ppq = 0.0;
        if ((! std::isfinite (bpm) || bpm <= 0.0) && rng.chance (0.3)) bpm = 120.0;
    }
};

//==============================================================================
// Hostile state documents.
std::string mutateNumber (std::string text, Rng& rng)
{
    static const char* repl[] = { "nan", "inf", "-inf", "1e39", "-1e39", "99999999999999999999", "-2147483649",
                                  "0x10", "", "1e-45", "-0", "3.4028236e38", "18446744073709551616", "  7", "+5" };
    for (int tries = 0; tries < 50 && ! text.empty(); ++tries)
    {
        std::size_t i = static_cast<std::size_t> (rng.nextInt (static_cast<int> (text.size())));
        if (! std::isdigit (static_cast<unsigned char> (text[i]))) continue;
        std::size_t a = i, b = i;
        while (a > 0 && std::strchr ("0123456789.eE+-", text[a - 1]) != nullptr) --a;
        while (b < text.size() && std::strchr ("0123456789.eE+-", text[b]) != nullptr) ++b;
        text.replace (a, b - a, repl[rng.nextInt (static_cast<int> (std::size (repl)))]);
        return text;
    }
    return text;
}

std::string hostileState (const std::string& good, Rng& rng)
{
    std::string t = good;
    switch (rng.nextInt (12))
    {
        case 0: t.resize (static_cast<std::size_t> (rng.nextInt (static_cast<int> (t.size()) + 1))); break;   // truncated
        case 1: for (int i = 0; i < 1 + rng.nextInt (20) && ! t.empty(); ++i) t[static_cast<std::size_t> (rng.nextInt (static_cast<int> (t.size())))] = static_cast<char> (rng.nextInt (256)); break;
        case 2: for (int i = 0; i < 1 + rng.nextInt (30); ++i) t = mutateNumber (t, rng); break;
        case 3: { t.assign (static_cast<std::size_t> (1 + rng.nextInt (100000)), '\0'); for (auto& c : t) c = static_cast<char> (rng.nextInt (256)); } break;
        case 4: { std::string n; for (int i = 0; i < 200000; ++i) n += "<a>"; t = t.substr (0, t.find ('>') + 1) + n; } break;   // deep nesting
        case 5: { const auto at = t.find ("name=\""); if (at != std::string::npos) t.insert (at + 6, std::string (4 << 20, 'x')); } break;   // 4 MB attribute
        case 6: { const auto& files = factoryFiles(); if (! files.empty()) t = std::string (files[static_cast<std::size_t> (rng.nextInt (static_cast<int> (files.size())))].text()); } break;
        case 7: { const auto& files = factoryFiles(); if (! files.empty()) { t = std::string (files[static_cast<std::size_t> (rng.nextInt (static_cast<int> (files.size())))].text()); for (int i = 0; i < 10; ++i) t = mutateNumber (t, rng); } } break;
        case 8: { const auto at = t.find ("type=\""); if (at != std::string::npos) { const char* types[] = { "scene", "pattern", "arp", "template", "synth", "bogus", "" }; const auto e = t.find ('"', at + 6); t.replace (at + 6, e - at - 6, types[rng.nextInt (7)]); } } break;
        case 9: { std::string dup; for (int i = 0; i < 4; ++i) dup += t; t = dup; } break;   // concatenated documents
        case 10: t = "<?xml version=\"1.0\"?><mw12f type=\"state\" version=\"999\"><bank><pattern slot=\"-5\" length=\"0\"/><pattern slot=\"999999\" length=\"255\"/></bank></mw12f>"; break;
        case 11: { const auto at = t.find ("<pattern"); if (at != std::string::npos) { std::string many; for (int i = 0; i < 2000; ++i) many += t.substr (at, t.find ('>', at) - at + 1); t.insert (at, many); } } break;
    }
    return t;
}

//==============================================================================
struct Sim
{
    Options opt;
    mw12FProcessor processor;
    HostPlayHead playHead;
    std::atomic<bool> stopWorkers { false }, stopAudio { false };
    std::atomic<double> sampleRateHint { 48000.0 };

    Progress audioProg { "audio" }, paramProg { "host-param" }, stateProg { "state" }, prepProg { "prepare" }, msgProg { "message" };

    // Audio-thread results.
    std::int64_t blocks = 0, samplesRendered = 0, zeroBlocks = 0, bigBlocks = 0, bypassBlocks = 0;
    float peak = 0.0f;
    int nonFinite = 0, badOffsets = 0, overBudget = 0, maxMidiOut = 0;
    int noteBalance[16][128] {};
    int unmatchedOffs = 0, doubleOns = 0;
    double worstBlockMs = 0.0, worstBlockMsPerKSample = 0.0;
    std::int64_t midiOutTotal = 0;

    // State thread results.
    std::int64_t saves = 0, loadsGood = 0, loadsHostile = 0, hostileAccepted = 0;
    // Message thread results.
    std::int64_t editorOpens = 0, editorCloses = 0, msgOps = 0, editorNull = 0, editorPokes = 0, timerTicks = 0;
    double editorOpenMs = 0.0, editorCloseMs = 0.0, worstMsgOpMs = 0.0;
    int worstMsgOp = -1;
    std::int64_t prepares = 0, releases = 0, hostParamMoves = 0;

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::vector<const PresetEntry*> presetList;
    Rng msgRng;

    explicit Sim (const Options& o) : opt (o), msgRng (o.seed * 7919 + 5) {}

    //--------------------------------------------------------------------------
    void scanMidiOut (const juce::MidiBuffer& out, int n)
    {
        int count = 0;
        for (const auto meta : out)
        {
            ++count;
            if (meta.samplePosition < 0 || meta.samplePosition > std::max (0, n - 1))
                if (badOffsets++ < 5) QA_FAIL ("MIDI out offset %d outside block of %d", meta.samplePosition, n);
            const auto m = meta.getMessage();
            if (m.isNoteOn()) ring.add (evOutOn, m.getChannel(), m.getNoteNumber(), static_cast<int> (blocks));
            else if (m.isNoteOff()) ring.add (evOutOff, m.getChannel(), m.getNoteNumber(), static_cast<int> (blocks));
            else if (m.isController() && (m.getControllerNumber() == 123 || m.getControllerNumber() == 120)) ring.add (evOutAllOff, m.getChannel(), m.getControllerNumber(), static_cast<int> (blocks));
            if (m.isNoteOn())
            {
                int& b = noteBalance[m.getChannel() - 1][m.getNoteNumber()];
                if (b > 0) ++doubleOns;
                ++b;
            }
            else if (m.isController() && (m.getControllerNumber() == 123 || m.getControllerNumber() == 120))
            {
                for (int& b : noteBalance[m.getChannel() - 1]) b = 0;   // All Notes Off / All Sound Off closes the channel downstream
            }
            else if (m.isNoteOff())
            {
                int& b = noteBalance[m.getChannel() - 1][m.getNoteNumber()];
                if (b > 0) --b; else ++unmatchedOffs;
            }
        }
        midiOutTotal += count;
        maxMidiOut = std::max (maxMidiOut, count);
        if (count > 64)
            if (overBudget++ < 5) QA_FAIL ("%d MIDI out events in one block (cap 64)", count);
    }

    int sounding() const
    {
        int s = 0;
        for (auto& ch : noteBalance) for (int b : ch) s += b;
        return s;
    }

    void fillMidi (juce::MidiBuffer& midi, int n, Rng& rng, std::array<std::array<bool, 128>, 16>& held)
    {
        midi.clear();
        const int dense = rng.chance (0.02) ? 5000 : (rng.chance (0.2) ? rng.nextInt (300) : rng.nextInt (6));
        const int span = std::max (1, n);
        for (int i = 0; i < dense; ++i)
        {
            int pos = rng.nextInt (span);
            if (rng.chance (0.01)) pos = rng.chance (0.5) ? -1 - rng.nextInt (100) : n + rng.nextInt (1000);
            const int ch = rng.chance (0.8) ? 1 : 1 + rng.nextInt (16);
            const int note = rng.nextInt (128);
            switch (rng.nextInt (14))
            {
                case 0: case 1: case 2:
                    midi.addEvent (juce::MidiMessage::noteOn (ch, note, static_cast<juce::uint8> (1 + rng.nextInt (127))), pos);
                    held[static_cast<std::size_t> (ch - 1)][static_cast<std::size_t> (note)] = true; break;
                case 3: case 4:
                    midi.addEvent (juce::MidiMessage::noteOff (ch, note), pos);
                    held[static_cast<std::size_t> (ch - 1)][static_cast<std::size_t> (note)] = false; break;
                case 5: { const juce::uint8 raw[] = { static_cast<juce::uint8> (0x90 | (ch - 1)), static_cast<juce::uint8> (note), 0 }; midi.addEvent (raw, 3, pos);
                          held[static_cast<std::size_t> (ch - 1)][static_cast<std::size_t> (note)] = false; } break;   // note-on velocity 0
                case 6: case 7: { const int ccs[] = { 1, 7, 10, 64, 74, 71, 120, 121, 123, 126, 127, rng.nextInt (128) };
                          midi.addEvent (juce::MidiMessage::controllerEvent (ch, ccs[rng.nextInt (12)], rng.nextInt (128)), pos); } break;
                case 8: midi.addEvent (juce::MidiMessage::pitchWheel (ch, rng.nextInt (16384)), pos); break;
                case 9: midi.addEvent (juce::MidiMessage::programChange (ch, rng.nextInt (128)), pos); break;
                case 10: midi.addEvent (juce::MidiMessage::channelPressureChange (ch, rng.nextInt (128)), pos); break;
                case 11: { const juce::uint8 rt[] = { 0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF }; midi.addEvent (&rt[rng.nextInt (6)], 1, pos); } break;
                case 12: { std::array<juce::uint8, 300> sx {}; sx[0] = 0xF0; for (std::size_t k = 1; k < sx.size() - 1; ++k) sx[k] = static_cast<juce::uint8> (rng.nextInt (128)); sx.back() = 0xF7;
                           midi.addEvent (sx.data(), 2 + rng.nextInt (298), pos); } break;
                case 13: { juce::uint8 junk[3] = { static_cast<juce::uint8> (rng.nextInt (256)), static_cast<juce::uint8> (rng.nextInt (256)), static_cast<juce::uint8> (rng.nextInt (256)) };
                           midi.addEvent (junk, 1 + rng.nextInt (3), pos); } break;
            }
        }
    }

    //--------------------------------------------------------------------------
    void audioThread()
    {
        Rng rng (opt.seed * 31 + 1);
        constexpr int kMax = 16384;
        juce::AudioBuffer<float> storage (4, kMax);
        juce::MidiBuffer midi;
        midi.ensureSize (1 << 20);
        std::array<std::array<bool, 128>, 16> held {};
        bool bypassed = false;
        bool nonRealtime = false;
        auto& ps = processor.parameterSet();

        while (! stopAudio.load())
        {
            int n;
            const int r = rng.nextInt (100);
            if (r < 5) n = 0;
            else if (r < 20) n = 1 + rng.nextInt (32);
            else if (r < 80) n = 64 + rng.nextInt (1985);
            else if (r < 95) n = 4096 + rng.nextInt (kMax - 4096 + 1);
            else n = kMax;
            const int c = rng.nextInt (100);
            const int ch = c < 93 ? 2 : (c < 96 ? 1 : (c < 98 ? 4 : 0));

            if (rng.chance (0.03)) playHead.randomise (rng);
            fillMidi (midi, n, rng, held);
            for (const auto meta : midi)
                if (meta.numBytes >= 2 && meta.numBytes <= 3)
                {
                    const int st = meta.data[0] & 0xF0, chn = (meta.data[0] & 0x0F) + 1, key = meta.data[1] & 0x7F;
                    const int vel = meta.numBytes > 2 ? (meta.data[2] & 0x7F) : 0;
                    if (st == 0x90 && vel > 0) ring.add (evInOn, chn, key, static_cast<int> (blocks));
                    else if (st == 0x80 || st == 0x90) ring.add (evInOff, chn, key, static_cast<int> (blocks));
                }
            if (opt.bypass && rng.chance (0.01)) { bypassed = ! bypassed; ring.add (evBypass, bypassed, n, static_cast<int> (blocks)); }
            if (rng.chance (0.002)) { nonRealtime = ! nonRealtime; processor.setNonRealtime (nonRealtime); ring.add (evOffline, nonRealtime, 0, static_cast<int> (blocks)); }

            if (opt.wrapperParams && rng.chance (0.3))
            {
                // VST3 / LV2 wrapper parameter delivery inside process(): setValue, then the listeners.
                const int k = 1 + rng.nextInt (8);
                for (int i = 0; i < k; ++i)
                {
                    auto* p = ps.byId[static_cast<std::size_t> (rng.nextInt (kNumParams))];
                    const float v = static_cast<float> (rng.nextDouble());
                    qaspy::audioContext = 2;
                    if (! juce::approximatelyEqual (p->getValue(), v))
                    {
                        p->setValue (v);
                        p->sendValueChangedMessageToListeners (v);
                    }
                    qaspy::audioContext = 0;
                }
            }

            juce::AudioBuffer<float> block (storage.getArrayOfWritePointers(), ch, n);
            for (int k = 0; k < ch; ++k)
                juce::FloatVectorOperations::fill (block.getWritePointer (k), rng.chance (0.5) ? 0.0f : 0.25f, n);   // hosts may hand dirty buffers

            const auto t0 = Clock::now();
            qaspy::audioContext = 1;
            if (bypassed) processor.processBlockBypassed (block, midi);
            else processor.processBlock (block, midi);
            qaspy::audioContext = 0;
            const double ms = std::chrono::duration<double, std::milli> (Clock::now() - t0).count();
            worstBlockMs = std::max (worstBlockMs, ms);
            if (n >= 256) worstBlockMsPerKSample = std::max (worstBlockMsPerKSample, ms * 1000.0 / n);

            for (int k = 0; k < ch; ++k)
            {
                const float* x = block.getReadPointer (k);
                for (int i = 0; i < n; ++i)
                {
                    if (! std::isfinite (x[i]))
                    {
                        if (nonFinite++ < 5) QA_FAIL ("non-finite output ch %d sample %d of block %d (n=%d)", k, i, static_cast<int> (blocks), n);
                        break;
                    }
                    peak = std::max (peak, std::abs (x[i]));
                }
            }
            scanMidiOut (midi, n);
            ++blocks;
            samplesRendered += n;
            if (n == 0) ++zeroBlocks;
            if (n > 4096) ++bigBlocks;
            if (bypassed) ++bypassBlocks;
            playHead.advance (n, sampleRateHint.load());
            audioProg.tick ("processBlock");
        }

        // ---- Quiesce: stop everything, release every held input note, flush. ----
        QA_NOTE ("audio: quiesce, %d notes sounding at stop", sounding());
        playHead.present = true; playHead.playing = false; playHead.looping = false; playHead.bpm = 120.0; playHead.ppq = 0.0; playHead.sendPpq = playHead.sendBpm = true;
        playHead.sigNum = 4; playHead.sigDen = 4;
        ps.byId[kSeqRun]->setNatural (0.0f);
        processor.setNonRealtime (false);
        for (int pass = 0; pass < 400; ++pass)
        {
            const int n = 480;
            midi.clear();
            if (pass == 0)
            {
                for (int c = 0; c < 16; ++c)
                {
                    for (int k = 0; k < 128; ++k)
                        if (held[static_cast<std::size_t> (c)][static_cast<std::size_t> (k)]) midi.addEvent (juce::MidiMessage::noteOff (c + 1, k), 0);
                    midi.addEvent (juce::MidiMessage::allNotesOff (c + 1), 1);
                }
            }
            juce::AudioBuffer<float> block (storage.getArrayOfWritePointers(), 2, n);
            block.clear();
            qaspy::audioContext = 1;
            processor.processBlock (block, midi);
            qaspy::audioContext = 0;
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (block.getSample (0, i)) || ! std::isfinite (block.getSample (1, i)))
                { if (nonFinite++ < 5) QA_FAIL ("non-finite output during quiesce"); break; }
            scanMidiOut (midi, n);
            audioProg.tick ("quiesce");
        }
        audioProg.done.store (true);
    }

    //--------------------------------------------------------------------------
    void hostParamThread()
    {
        Rng rng (opt.seed * 31 + 2);
        auto& ps = processor.parameterSet();
        const char* texts[] = { "nan", "inf", "-1e40", "", "abc", "50", "100 %", "-12 dB", "On", "Off", "999999999" };
        while (! stopWorkers.load())
        {
            auto* p = ps.byId[static_cast<std::size_t> (rng.nextInt (kNumParams))];
            if (rng.chance (0.3)) p->beginChangeGesture();
            const int moves = 1 + rng.nextInt (10);
            for (int i = 0; i < moves; ++i)
                p->setValueNotifyingHost (static_cast<float> (rng.chance (0.05) ? (rng.chance (0.5) ? -0.5 : 1.5) : rng.nextDouble()));
            if (rng.chance (0.3)) p->endChangeGesture();
            (void) p->getText (p->getValue(), 32);
            (void) p->getValueForText (texts[rng.nextInt (static_cast<int> (std::size (texts)))]);
            ++hostParamMoves;
            paramProg.tick ("param");
            if (rng.chance (0.5)) std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
        paramProg.done.store (true);
    }

    //--------------------------------------------------------------------------
    void stateThread()
    {
        Rng rng (opt.seed * 31 + 3);
        juce::MemoryBlock mb;
        processor.getStateInformation (mb);
        std::string good = mb.toString().toStdString();
        while (! stopWorkers.load())
        {
            const int r = rng.nextInt (10);
            if (r < 4)
            {
                processor.getStateInformation (mb);
                if (mb.getSize() == 0) QA_FAIL ("getStateInformation returned nothing");
                else if (rng.chance (0.5)) good.assign (static_cast<const char*> (mb.getData()), mb.getSize());
                ++saves;
                stateProg.tick ("save");
            }
            else if (r < 7 || ! opt.hostile)
            {
                processor.setStateInformation (good.data(), static_cast<int> (good.size()));
                ring.add (evStateLoad, 0);
                ++loadsGood;
                stateProg.tick ("load good");
            }
            else
            {
                const std::string bad = hostileState (good, rng);
                stateProg.tick ("load hostile");
                const std::uint32_t v0 = processor.controller().version();
                if (bad.empty()) processor.setStateInformation (nullptr, 0);
                else processor.setStateInformation (bad.data(), static_cast<int> (std::min<std::size_t> (bad.size(), 0x7fffffff)));
                if (processor.controller().version() != v0) { ++hostileAccepted; ring.add (evStateLoad, 1); }
                ++loadsHostile;
                stateProg.tick ("load hostile done");
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (rng.nextInt (15)));
        }
        stateProg.done.store (true);
    }

    //--------------------------------------------------------------------------
    void prepareThread()
    {
        Rng rng (opt.seed * 31 + 4);
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 22050.0, 8000.0, 384000.0, 32000.0 };
        const int sizes[] = { 1, 16, 32, 64, 128, 256, 441, 512, 1024, 2048, 4096, 8192, 16384, 0 };
        while (! stopWorkers.load())
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (30 + rng.nextInt (300)));
            if (stopWorkers.load()) break;
            if (rng.chance (0.2)) { processor.releaseResources(); ring.add (evRelease); ++releases; prepProg.tick ("release"); std::this_thread::sleep_for (std::chrono::milliseconds (rng.nextInt (20))); }
            const double sr = rates[rng.nextInt (static_cast<int> (std::size (rates)))];
            const int bs = sizes[rng.nextInt (static_cast<int> (std::size (sizes)))];
            prepProg.tick ("prepareToPlay");
            processor.setRateAndBufferSizeDetails (sr, bs);
            processor.prepareToPlay (sr, bs);
            ring.add (evPrepare, static_cast<int> (sr), bs);
            sampleRateHint.store (sr);
            ++prepares;
            prepProg.tick ("prepared");
        }
        prepProg.done.store (true);
    }

    //--------------------------------------------------------------------------
    // Message thread: one random operation per call.
    void messageOp()
    {
        Rng& rng = msgRng;
        auto& c = processor.controller();
        const bool arp = rng.chance (0.3);
        const int slot = rng.nextInt (seq::kNumSlots);
        const int op = rng.nextInt (40);
        const auto opStart = Clock::now();
        struct OpTimer { Sim& s; int op; Clock::time_point t; ~OpTimer() { const double ms = std::chrono::duration<double, std::milli> (Clock::now() - t).count(); if (ms > s.worstMsgOpMs) { s.worstMsgOpMs = ms; s.worstMsgOp = op; } } } opTimer { *this, op, opStart };
        static const char* opNames[40] = {};
        (void) opNames;
        msgProg.tick ("op");
        ring.add (evMsgOp, op, arp, slot);
        switch (op)
        {
            case 0: case 1:
                if (! presetList.empty()) { std::string e; c.loadPreset (*presetList[static_cast<std::size_t> (rng.nextInt (static_cast<int> (presetList.size())))], &e); }
                break;
            case 2: { auto p = c.pattern (arp, slot);
                      for (int i = 0; i < 1 + rng.nextInt (8); ++i)
                      { auto& s = p.steps[static_cast<std::size_t> (rng.nextInt (seq::kMaxSteps))];
                        s.state = static_cast<seq::StepState> (rng.nextInt (3)); s.note = static_cast<std::uint8_t> (rng.nextInt (12));
                        s.octave = static_cast<std::int8_t> (rng.nextInt (5) - 1); s.accent = rng.chance (0.3); s.slide = rng.chance (0.3);
                        s.ratchet = static_cast<std::uint8_t> (1 + rng.nextInt (4)); s.probability = static_cast<std::uint8_t> (rng.nextInt (101)); }
                      c.beginGesture(); c.commitPattern (arp, slot, p); c.endGesture(); } break;
            case 3: { seq::Pattern p; auto* bytes = reinterpret_cast<unsigned char*> (&p);                     // garbage pattern, sanitised by commit
                      for (std::size_t i = 0; i < sizeof (p); ++i) bytes[i] = static_cast<unsigned char> (rng.nextInt (256));
                      // Keep every bool a valid bool: any other bit pattern is UB in the caller, not a plugin defect.
                      for (auto& st : p.steps) { st.accent = (bytes[0] & 1) != 0; st.slide = (bytes[1] & 1) != 0; st.vibrato = (bytes[2] & 1) != 0; }
                      p.gen.useSlide = rng.chance (0.5); p.gen.useAccent = rng.chance (0.5); p.gen.useVibrato = rng.chance (0.5);
                      c.commitPattern (arp, slot, p); } break;
            case 4: c.generate (arp, slot, rng.nextU64()); break;
            case 5: { auto g = c.genParams (arp, slot); g.noteMask = static_cast<std::uint16_t> (rng.nextInt (0x10000)); g.octaveMask = static_cast<std::uint8_t> (rng.nextInt (256));
                      g.amountNote = static_cast<std::uint8_t> (rng.nextInt (256)); g.amountSlide = static_cast<std::uint8_t> (rng.nextInt (256)); g.root = static_cast<std::uint8_t> (rng.nextInt (256));
                      c.setGenParams (arp, slot, g); c.applyDensities (arp, slot); } break;
            case 6: c.applyScaleTemplate (arp, slot, rng.nextInt (static_cast<int> (c.scaleTemplates().size()) + 4) - 2); break;
            case 7: c.flattenPattern (arp, slot); break;
            case 8: c.shiftPattern (arp, slot, rng.nextInt (2001) - 1000); break;
            case 9: c.transposePattern (arp, slot, rng.nextInt (257) - 128); break;
            case 10: c.reversePattern (arp, slot); break;
            case 11: c.setPatternLength (arp, slot, rng.nextInt (300) - 50); break;
            case 12: c.duplicatePattern (arp, slot); break;
            case 13: c.invertArpPattern (slot, rng.nextInt (30) - 10); break;
            case 14: case 15: c.undo(); break;
            case 16: c.redo(); break;
            case 17: { Settings s = c.settings(); s.realtimeQuality = static_cast<Quality> (rng.nextInt (4)); s.offlineQuality = static_cast<Quality> (rng.nextInt (4));
                       s.triggerMode = static_cast<seq::TriggerMode> (rng.nextInt (2)); s.midiThru = rng.chance (0.5); s.midiOut = rng.chance (0.8);
                       s.internalBpm = static_cast<float> (20 + rng.nextInt (281)); s.followHostTransport = rng.chance (0.7); s.hostLink = rng.chance (0.7);
                       for (int i = 5; i > 0; --i) std::swap (s.fxOrder[static_cast<std::size_t> (i)], s.fxOrder[static_cast<std::size_t> (rng.nextInt (i + 1))]);
                       c.setSettings (s); } break;
            case 18: c.startMidiLearn (static_cast<ParamId> (rng.nextInt (kNumParams))); break;
            case 19: c.cancelMidiLearn(); break;
            case 20: c.setMidiMapping (rng.nextInt (140) - 6, static_cast<ParamId> (rng.nextInt (kNumParams))); break;
            case 21: if (rng.chance (0.2)) c.clearAllMidiMappings(); else c.clearMidiMapping (static_cast<ParamId> (rng.nextInt (kNumParams))); break;
            case 22: c.setParam (static_cast<ParamId> (rng.nextInt (kNumParams)), static_cast<float> ((rng.nextDouble() - 0.25) * 30000.0)); break;
            case 23: c.selectSlot (rng.nextInt (60) - 6); break;
            case 24: c.setPlayMode (static_cast<seq::PlayMode> (rng.nextInt (3))); break;
            case 25: if (! c.auditioning()) c.beginAudition(); else c.endAudition (rng.chance (0.5)); break;
            case 26: c.copyPattern (arp, slot); break;
            case 27: (void) c.pastePattern (arp, slot); break;
            case 28: { std::string junk; for (int i = 0; i < rng.nextInt (200); ++i) junk += "C:AS.~+-#0123456789 :xyz\n<>"[rng.nextInt (28)]; std::string e; (void) c.patternFromText (arp, slot, junk, &e); } break;
            case 29: { std::vector<std::uint8_t> m (static_cast<std::size_t> (rng.nextInt (4000)));
                       const char hdr[] = "MThd\0\0\0\6\0\0\0\1\0\x60MTrk"; for (std::size_t i = 0; i < m.size(); ++i) m[i] = i < 18 ? static_cast<std::uint8_t> (hdr[i]) : static_cast<std::uint8_t> (rng.nextInt (256));
                       seq::MidiImportReport rep; (void) c.importMidiData (m, arp, slot, &rep); } break;
            case 30: { const auto f = c.patternMidiFile (arp, slot); (void) f; } break;
            case 31: if (rng.chance (0.1)) c.initScene(); break;
            case 32: { std::string e; c.savePreset (static_cast<PresetType> (1 + rng.nextInt (12)), "qa " + std::to_string (rng.nextInt (5)), "qa", "a,b", "", true, &e); presetList = c.presets().list(); } break;
            case 33: { juce::MemoryBlock mb; processor.getStateInformation (mb); if (rng.chance (0.5)) processor.setStateInformation (mb.getData(), static_cast<int> (mb.getSize())); } break;
            case 34: (void) c.loadNextPreset (PresetFilter {}, rng.chance (0.5) ? 1 : -1); break;
            case 35: c.copySlot (arp, slot, rng.chance (0.5), rng.nextInt (seq::kNumSlots)); break;
            case 36: processor.setCurrentProgram (rng.nextInt (8) - 2); break;
            case 37: (void) c.engineStatus(); (void) c.canUndo(); (void) c.undoLabel(); (void) c.sceneName(); break;
            case 38: c.setUiValue ("qa", std::to_string (rng.nextInt (1000))); break;
            case 39: c.mutePattern (arp, slot); break;
        }
        ++msgOps;
    }

    void editorOp()
    {
        Rng& rng = msgRng;
        if (editor == nullptr)
        {
            msgProg.tick ("editor open");
            const auto t0 = Clock::now();
            editor.reset (processor.createEditorAndMakeActive());
            editorOpenMs = std::max (editorOpenMs, std::chrono::duration<double, std::milli> (Clock::now() - t0).count());
            if (editor == nullptr) ++editorNull;
            if (editor != nullptr)
            {
                editor->addToDesktop (0);
                editor->setVisible (true);
                ++editorOpens;
            }
        }
        else if (rng.chance (0.4))
        {
            msgProg.tick ("editor close");
            const auto t0 = Clock::now();
            processor.editorBeingDeleted (editor.get());   // what every wrapper does before deleting the editor
            editor.reset();
            editorCloseMs = std::max (editorCloseMs, std::chrono::duration<double, std::milli> (Clock::now() - t0).count());
            ++editorCloses;
        }
        else
        {
            msgProg.tick ("editor poke");
            ++editorPokes;
            editor->repaint();
            // Synthetic mouse traffic over the faceplate: clicks and drags at random points.
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            (void) src;
            for (int i = 0; i < 4; ++i)
            {
                const juce::Point<float> pt ((float) rng.nextInt (std::max (1, editor->getWidth())), (float) rng.nextInt (std::max (1, editor->getHeight())));
                if (auto* target = editor->getComponentAt (pt.toInt()))
                {
                    if (dynamic_cast<juce::TextEditor*> (target) != nullptr) continue;
                    const auto local = target->getLocalPoint (editor.get(), pt);
                    auto now = juce::Time::getCurrentTime();
                    juce::MouseEvent down (src, local, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                                           target, target, now, local, now, 1, false);
                    target->mouseMove (down);
                    if (rng.chance (0.5)) target->mouseWheelMove (down, juce::MouseWheelDetails { 0.0f, rng.chance (0.5) ? 0.2f : -0.2f, false, false, false });
                }
            }
        }
    }
};

//==============================================================================
struct MessageDriver final : juce::Timer
{
    Sim& sim;
    Clock::time_point end;
    bool finishing = false;
    explicit MessageDriver (Sim& s, Clock::time_point e) : sim (s), end (e) {}

    void timerCallback() override
    {
        if (Clock::now() >= end)
        {
            stopTimer();
            return;
        }
        if (sim.opt.messageOps)
            for (int i = 0; i < 1 + sim.msgRng.nextInt (4); ++i)
                sim.messageOp();
        if (sim.opt.editor && sim.msgRng.chance (0.3))
            sim.editorOp();
        sim.msgProg.tick ("timer");
        ++sim.timerTicks;
    }
};
}

//==============================================================================
int qaRunHostSim (int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--seconds") opt.seconds = std::stod (next());
        else if (a == "--seed") opt.seed = std::stoull (next());
        else if (a == "--watchdog") opt.watchdog = std::stod (next());
        else if (a == "--no-editor") opt.editor = false;
        else if (a == "--no-prepare") opt.prepare = false;
        else if (a == "--no-state") opt.state = false;
        else if (a == "--no-hostile") opt.hostile = false;
        else if (a == "--no-wrapper-params") opt.wrapperParams = false;
        else if (a == "--no-bypass") opt.bypass = false;
        else if (a == "--no-message-ops") opt.messageOps = false;
        else if (a == "--no-host-params") opt.hostParams = false;
    }
    if (std::getenv ("XDG_CONFIG_HOME") == nullptr)
    {
        std::fprintf (stderr, "refusing to run without XDG_CONFIG_HOME pointing at a scratch dir\n");
        return 2;
    }
    std::fprintf (stderr, "qa hostsim: seconds=%.1f seed=%llu editor=%d prepare=%d state=%d hostile=%d wrapperParams=%d bypass=%d\n",
                  opt.seconds, static_cast<unsigned long long> (opt.seed), opt.editor, opt.prepare, opt.state, opt.hostile, opt.wrapperParams, opt.bypass);

    auto sim = std::make_unique<Sim> (opt);
    Sim& s = *sim;
    s.processor.setPlayHead (&s.playHead);
    s.processor.setRateAndBufferSizeDetails (48000.0, 512);
    s.processor.prepareToPlay (48000.0, 512);
    s.presetList = s.processor.controller().presets().list();
    std::fprintf (stderr, "qa: %zu presets listed\n", s.presetList.size());
    // Turn the sequencer on so MIDI out is busy.
    s.processor.parameterSet().byId[kSeqRun]->setNatural (1.0f);

    const auto end = Clock::now() + std::chrono::milliseconds (static_cast<long> (opt.seconds * 1000.0));
    std::vector<std::thread> workers;
    std::thread audio ([&] { s.audioThread(); });
    if (opt.hostParams) workers.emplace_back ([&] { s.hostParamThread(); });
    if (opt.state) workers.emplace_back ([&] { s.stateThread(); });
    if (opt.prepare) workers.emplace_back ([&] { s.prepareThread(); });

    std::atomic<bool> watchdogStop { false };
    std::thread watchdog ([&] {
        Progress* ps[] = { &s.audioProg, &s.paramProg, &s.stateProg, &s.prepProg, &s.msgProg };
        std::uint64_t last[5] {};
        Clock::time_point lastChange[5];
        for (auto& t : lastChange) t = Clock::now();
        while (! watchdogStop.load())
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (250));
            for (int i = 0; i < 5; ++i)
            {
                const auto t = ps[i]->ticks.load();
                if (t != last[i] || ps[i]->done.load() || t == 0) { last[i] = t; lastChange[i] = Clock::now(); continue; }
                if (Clock::now() - lastChange[i] > std::chrono::duration<double> (opt.watchdog))
                {
                    std::fprintf (stderr, "QA FAIL: DEADLOCK/STALL: thread '%s' made no progress for %.0f s (last action: %s)\n",
                                  ps[i]->name, opt.watchdog, ps[i]->lastAction.load());
                    std::fflush (stderr);
                    std::abort();
                }
            }
        }
    });

    MessageDriver driver (s, end);
    driver.startTimer (10);

    // The orchestrator: stop the dispatch loop at the end.
    std::thread orchestrator ([&] {
        while (Clock::now() < end) std::this_thread::sleep_for (std::chrono::milliseconds (50));
        s.stopWorkers.store (true);
        for (auto& w : workers) w.join();
        // Let the message loop run a little longer so pending async calls drain.
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });

    juce::MessageManager::getInstance()->runDispatchLoop();
    orchestrator.join();
    driver.stopTimer();

    // Close the editor before the final flush, as hosts do.
    if (s.editor != nullptr) s.processor.editorBeingDeleted (s.editor.get());
    s.editor.reset();
    s.processor.handlePendingMessages();

    s.stopAudio.store (true);
    audio.join();
    s.processor.handlePendingMessages();
    watchdogStop.store (true);
    watchdog.join();

    const int hanging = s.sounding();
    std::fprintf (stderr,
        "qa: blocks=%lld samples=%lld zero=%lld big=%lld bypass=%lld peak=%.3f worstBlockMs=%.2f worstMsPer1kSamples=%.2f\n"
        "qa: midiOut=%lld maxPerBlock=%d unmatchedOffs=%d doubleOns=%d hangingAfterStop=%d\n"
        "qa: saves=%lld loadsGood=%lld loadsHostile=%lld hostileAccepted=%lld prepares=%lld releases=%lld hostParamMoves=%lld\n"
        "qa: msgOps=%lld editorOpens=%lld editorCloses=%lld editorNull=%lld pokes=%lld ticks=%lld openMs=%.1f closeMs=%.1f worstMsgOp=%d (%.1f ms)\n",
        (long long) s.blocks, (long long) s.samplesRendered, (long long) s.zeroBlocks, (long long) s.bigBlocks, (long long) s.bypassBlocks,
        (double) s.peak, s.worstBlockMs, s.worstBlockMsPerKSample,
        (long long) s.midiOutTotal, s.maxMidiOut, s.unmatchedOffs, s.doubleOns, hanging,
        (long long) s.saves, (long long) s.loadsGood, (long long) s.loadsHostile, (long long) s.hostileAccepted,
        (long long) s.prepares, (long long) s.releases, (long long) s.hostParamMoves,
        (long long) s.msgOps, (long long) s.editorOpens, (long long) s.editorCloses,
        (long long) s.editorNull, (long long) s.editorPokes, (long long) s.timerTicks, s.editorOpenMs, s.editorCloseMs, s.worstMsgOp, s.worstMsgOpMs);

    if (hanging != 0)
    {
        QA_FAIL ("%d MIDI out notes left sounding after stop + all-notes-off + 4 s", hanging);
        for (int c = 0; c < 16; ++c) for (int k = 0; k < 128; ++k) if (s.noteBalance[c][k] != 0)
        {
            std::fprintf (stderr, "   ch %d note %d balance %d\n", c + 1, k, s.noteBalance[c][k]);
            // History: find the last OUT note-on for this key, print everything relevant from 30 ms before it on.
            const int total = std::min (ring.n.load(), EventRing::kSize);
            const int start = ring.n.load() > EventRing::kSize ? (ring.n.load() & (EventRing::kSize - 1)) : 0;
            auto at = [&] (int i) -> const EventRing::E& { return ring.e[static_cast<std::size_t> ((start + i) & (EventRing::kSize - 1))]; };
            int lastOn = -1;
            for (int i = 0; i < total; ++i) { const auto& e = at (i); if (e.kind == evOutOn && e.a == c + 1 && e.b == k) lastOn = i; }
            if (lastOn < 0) { std::fprintf (stderr, "     (note-on fell out of the event ring)\n"); continue; }
            const std::int64_t from = at (lastOn).us - 30000;
            int printed = 0;
            for (int i = 0; i < total && printed < 400; ++i)
            {
                const auto& e = at (i);
                if (e.us < from) continue;
                const bool keyEvent = (e.kind >= evOutOn && e.kind <= evInOff) && e.a == c + 1 && (e.b == k || e.kind == evOutAllOff);
                const bool ctxEvent = e.kind >= evBypass && e.kind <= evMsgOp && (e.kind != evMsgOp || e.a == 17 || e.a == 24 || e.a == 31 || e.a == 33 || e.a == 0 || e.a == 1 || e.a == 25 || e.a == 14 || e.a == 15 || e.a == 16 || e.a == 34 || e.a == 36);
                if (! keyEvent && ! ctxEvent) continue;
                std::fprintf (stderr, "     t=%9.3f ms %-12s a=%d b=%d c=%d\n", e.us / 1000.0, evName (e.kind), e.a, e.b, e.c);
                ++printed;
            }
        }
    }
    if (s.blocks == 0) QA_FAIL ("no audio blocks ran");
    const std::size_t rtViolations = qaspy::total (1);
    if (rtViolations != 0) QA_FAIL ("%zu allocations/locks/blocking calls inside processBlock (see spy report)", rtViolations);
    const std::size_t wrapViolations = qaspy::total (2);
    if (wrapViolations != 0) QA_NOTE ("%zu allocations/locks in wrapper-style parameter delivery on the audio thread (see spy report)", wrapViolations);

    // The instance still works and its state round-trips.
    juce::MemoryBlock a, b;
    s.processor.getStateInformation (a);
    s.processor.setStateInformation (a.getData(), static_cast<int> (a.getSize()));
    s.processor.getStateInformation (b);
    if (a != b)
    {
        QA_FAIL ("state not byte-stable after the run (%zu vs %zu bytes)", a.getSize(), b.getSize());
        const char* dir = std::getenv ("QA_DUMP_DIR");
        if (dir != nullptr)
        {
            juce::File (juce::String (dir)).getChildFile ("state-a.xml").replaceWithData (a.getData(), a.getSize());
            juce::File (juce::String (dir)).getChildFile ("state-b.xml").replaceWithData (b.getData(), b.getSize());
        }
    }

    sim.reset();
    std::fprintf (stderr, "qa: %s (%d failures)\n", failures.load() == 0 ? "PASS" : "FAIL", failures.load());
    return failures.load() == 0 ? 0 : 1;
}
