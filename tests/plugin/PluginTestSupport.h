#pragma once
// Shared rig for the plugin tests: a scripted host playhead, a processor driver that runs
// processBlock with MIDI in and collects audio and MIDI out, and small helpers.
#include "PluginProcessor.h"
#include "acid/common/Rng.h"
#include "acid/seq/PatternCodec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ptest
{
using namespace acid;
using namespace acid::plugin;

constexpr double kFs = 48000.0;

/** A host playhead the test scripts. present == false: getPosition() returns nothing. */
struct TestPlayHead final : juce::AudioPlayHead
{
    bool present = true;
    bool playing = false;
    bool sendPpq = true;
    bool sendBpm = true;
    double bpm = 120.0;
    double ppq = 0.0;
    bool looping = false;
    double loopStart = 0.0, loopEnd = 0.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! present) return {};
        PositionInfo p;
        p.setIsPlaying (playing);
        if (sendPpq) p.setPpqPosition (ppq);
        if (sendBpm) p.setBpm (bpm);
        p.setIsLooping (looping);
        if (looping) p.setLoopPoints (LoopPoints { loopStart, loopEnd });
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }

    void advance (int numSamples, double sampleRate)
    {
        if (! playing) return;
        ppq += static_cast<double> (numSamples) * bpm / (60.0 * sampleRate);
        if (looping && ppq >= loopEnd) ppq = loopStart + (ppq - loopEnd);
    }
};

struct TimedMidi
{
    std::int64_t sample = 0;
    juce::MidiMessage message;
};

struct Rendered
{
    std::vector<float> left, right;
    std::vector<TimedMidi> midiOut;
    std::size_t blocks = 0;
};

/** Points XDG_CONFIG_HOME at a fresh scratch directory so no test touches the real user files. */
struct ScratchConfigDir
{
    std::filesystem::path dir;

    ScratchConfigDir()
    {
        static int counter = 0;
        dir = std::filesystem::path (MW_TEST_OUTPUT_DIR) / ("config-" + std::to_string (static_cast<long> (juce::Time::currentTimeMillis()) ) + "-" + std::to_string (counter++));
        std::error_code ec;
        std::filesystem::remove_all (dir, ec);
        std::filesystem::create_directories (dir, ec);
        ::setenv ("XDG_CONFIG_HOME", dir.c_str(), 1);
    }

    ~ScratchConfigDir()
    {
        std::error_code ec;
        std::filesystem::remove_all (dir, ec);
    }
};

/** Drives one processor. Owns the playhead and the buffers. */
struct Driver
{
    mw12FProcessor processor;
    TestPlayHead playHead;
    double sampleRate = kFs;
    int blockSize = 512;
    int numChannels = 2;
    std::vector<TimedMidi> midiIn;      // absolute samples, sorted
    juce::MidiBuffer midiBuffer;

    /** A session document holding the table defaults, default settings and an empty bank. */
    static std::string cleanStateText()
    {
        seq::PatternBank empty;
        return writePreset (makePreset (PresetType::State, defaultParamValues(), Settings {}, &empty));
    }

    /** Starts from the clean state, not from the factory default scene, so expectations do not
        depend on the factory content. Tests about the factory default construct mw12FProcessor
        themselves. */
    Driver()
    {
        processor.setPlayHead (&playHead);
        midiBuffer.ensureSize (65536);
        processor.loadStateText (cleanStateText());
        processor.handlePendingMessages();
    }

    void prepare (double sr = kFs, int block = 512)
    {
        sampleRate = sr;
        blockSize = block;
        processor.setPlayConfigDetails (0, 2, sr, block);
        processor.prepareToPlay (sr, block);
    }

    void addNoteOn (std::int64_t sample, int note, int velocity = 100)  { midiIn.push_back ({ sample, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)) }); }
    void addNoteOff (std::int64_t sample, int note)                    { midiIn.push_back ({ sample, juce::MidiMessage::noteOff (1, note) }); }
    void addController (std::int64_t sample, int cc, int value)       { midiIn.push_back ({ sample, juce::MidiMessage::controllerEvent (1, cc, value) }); }

    /** Renders numSamples. nextBlock gives each block's size; perBlock (position) may change
        anything before each block. */
    Rendered render (std::int64_t numSamples, const std::function<int()>& nextBlock,
                     const std::function<void (std::int64_t)>& perBlock = {})
    {
        std::stable_sort (midiIn.begin(), midiIn.end(), [] (const TimedMidi& a, const TimedMidi& b) { return a.sample < b.sample; });
        Rendered out;
        out.left.assign (static_cast<std::size_t> (numSamples), 0.0f);
        out.right.assign (static_cast<std::size_t> (numSamples), 0.0f);
        juce::AudioBuffer<float> buffer (numChannels, std::max (1, blockSize * 16));
        std::size_t midiIndex = 0;
        std::int64_t pos = 0;

        while (pos < numSamples)
        {
            int n = std::max (0, nextBlock());
            n = static_cast<int> (std::min<std::int64_t> (n, numSamples - pos));
            if (perBlock) perBlock (pos);
            if (n > buffer.getNumSamples()) buffer.setSize (numChannels, n, false, false, true);

            midiBuffer.clear();
            while (midiIndex < midiIn.size() && midiIn[midiIndex].sample < pos + std::max (n, 1))
            {
                midiBuffer.addEvent (midiIn[midiIndex].message, static_cast<int> (std::max<std::int64_t> (0, midiIn[midiIndex].sample - pos)));
                ++midiIndex;
            }

            juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), numChannels, n);
            block.clear();
            processor.processBlock (block, midiBuffer);
            ++out.blocks;

            for (int i = 0; i < n; ++i)
            {
                out.left[static_cast<std::size_t> (pos + i)] = block.getSample (0, i);
                if (numChannels > 1) out.right[static_cast<std::size_t> (pos + i)] = block.getSample (1, i);
            }
            for (const auto meta : midiBuffer)
                out.midiOut.push_back ({ pos + meta.samplePosition, meta.getMessage() });

            playHead.advance (n, sampleRate);
            pos += n;
            if (n == 0) break;   // a zero-length block renders nothing; the caller drives these one at a time
        }
        return out;
    }

    Rendered render (std::int64_t numSamples, int block, const std::function<void (std::int64_t)>& perBlock = {})
    {
        return render (numSamples, [block] { return block; }, perBlock);
    }

    /** One block of exactly n samples (0 allowed) with the MIDI queued so far at offset 0. */
    void oneBlock (int n, juce::AudioBuffer<float>* captured = nullptr)
    {
        juce::AudioBuffer<float> buffer (numChannels, std::max (1, n));
        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), numChannels, n);
        block.clear();
        midiBuffer.clear();
        for (const auto& m : midiIn) midiBuffer.addEvent (m.message, 0);
        midiIn.clear();
        processor.processBlock (block, midiBuffer);
        if (captured != nullptr) captured->makeCopyOf (block);
        playHead.advance (n, sampleRate);
    }

    std::string stateText()
    {
        juce::MemoryBlock mb;
        processor.getStateInformation (mb);
        return mb.toString().toStdString();
    }

    void setStateText (const std::string& text)
    {
        processor.setStateInformation (text.data(), static_cast<int> (text.size()));
    }

    seq::Pattern patternFromText (const std::string& steps)
    {
        seq::Pattern p;
        std::string error;
        if (! seq::fromStepText (steps, p, &error)) throw std::runtime_error ("bad step text: " + error);
        return p;
    }
};

inline float peakOf (const std::vector<float>& v)
{
    float m = 0.0f;
    for (const float x : v) m = std::max (m, std::abs (x));
    return m;
}

inline bool allFinite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
}

inline int countNoteOns (const std::vector<TimedMidi>& midi)
{
    int n = 0;
    for (const auto& m : midi) if (m.message.isNoteOn()) ++n;
    return n;
}

inline int countNoteOffs (const std::vector<TimedMidi>& midi)
{
    int n = 0;
    for (const auto& m : midi) if (m.message.isNoteOff()) ++n;
    return n;
}

/** Note onsets by a short RMS window, as the engine tests do. */
inline std::vector<std::int64_t> detectOnsets (const std::vector<float>& x, double sampleRate, double threshold = 0.02)
{
    std::vector<std::int64_t> onsets;
    const auto w = std::max<std::size_t> (1, static_cast<std::size_t> (0.002 * sampleRate));
    const auto quietNeeded = static_cast<std::int64_t> (0.010 * sampleRate);
    double acc = 0.0;
    std::int64_t quiet = quietNeeded;
    bool armed = true;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        acc += static_cast<double> (x[i]) * static_cast<double> (x[i]);
        if (i >= w) acc -= static_cast<double> (x[i - w]) * static_cast<double> (x[i - w]);
        const double rms = std::sqrt (std::max (0.0, acc) / static_cast<double> (w));
        if (rms < threshold / 4.0) { if (++quiet >= quietNeeded) armed = true; }
        else quiet = 0;
        if (armed && rms >= threshold) { onsets.push_back (static_cast<std::int64_t> (i)); armed = false; }
    }
    return onsets;
}
}
