#pragma once
// Shared rig for the engine tests: a host model (transport, loop, tempo), a session that
// drives Engine::process block by block with timed MIDI input and collects audio and MIDI
// output with absolute sample positions, plus onset detection and hashing.
#include "AllocGuard.h"
#include "acid/common/Rng.h"
#include "acid/engine/Engine.h"
#include "acid/seq/PatternCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace etest
{
using namespace acid;

constexpr double kFs = 48000.0;

/** A host transport. The position is a sample count from a base, so it is exact at any
    block boundary. Blocks are cut at the loop end, as real hosts do. */
struct Host
{
    double sampleRate = kFs;
    bool present = true;              // false: no transport at all (standalone)
    bool playing = false;
    bool sendPpq = true;
    bool sendBpm = true;
    bool newsOnlyOnChange = false;    // reports a position only when something changed (LV2 style)
    double bpm = 120.0;
    double ppqBase = 0.0;
    std::int64_t samplesFromBase = 0;
    bool looping = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    bool splitsAtLoop = true;         // the host cuts its blocks at the loop point (most do)
    bool changed = true;

    double qps() const { return bpm / (60.0 * sampleRate); }
    double ppq() const { return ppqBase + static_cast<double> (samplesFromBase) * qps(); }

    void locate (double q) { ppqBase = q; samplesFromBase = 0; changed = true; }
    void play() { playing = true; changed = true; }
    void stop() { playing = false; changed = true; }

    int limitBlock (int n) const
    {
        if (! (playing && looping && splitsAtLoop)) return n;
        const double toEnd = (loopEnd - ppq()) / qps() - 1.0e-6;
        const auto s = static_cast<std::int64_t> (std::ceil (toEnd));
        return s <= 0 ? n : static_cast<int> (std::min<std::int64_t> (n, s));
    }

    seq::TransportInfo info()
    {
        seq::TransportInfo t;
        if (! present || (newsOnlyOnChange && ! changed))
            return t;
        changed = false;
        t.valid = true;
        t.playing = playing;
        t.ppqValid = sendPpq;
        t.ppq = ppq();
        t.bpmValid = sendBpm;
        t.bpm = bpm;
        t.looping = looping;
        t.loopStartPpq = loopStart;
        t.loopEndPpq = loopEnd;
        return t;
    }

    void advance (int n)
    {
        if (! playing) return;
        samplesFromBase += n;
        if (looping && ppq() >= loopEnd - 1.0e-9)
        {
            const double over = ppq() - loopEnd;
            locate (loopStart + std::max (0.0, over));
        }
    }
};

struct TimedMidi
{
    std::int64_t sample = 0;
    seq::MidiMsg msg {};
};

inline TimedMidi noteOn (std::int64_t sample, int note, int velocity = 100)
{
    TimedMidi m;
    m.sample = sample;
    m.msg.size = 3;
    m.msg.data[0] = 0x90;
    m.msg.data[1] = static_cast<std::uint8_t> (note);
    m.msg.data[2] = static_cast<std::uint8_t> (velocity);
    return m;
}

inline TimedMidi noteOff (std::int64_t sample, int note)
{
    TimedMidi m;
    m.sample = sample;
    m.msg.size = 3;
    m.msg.data[0] = 0x80;
    m.msg.data[1] = static_cast<std::uint8_t> (note);
    m.msg.data[2] = 0;
    return m;
}

inline TimedMidi controller (std::int64_t sample, int number, int value)
{
    TimedMidi m;
    m.sample = sample;
    m.msg.size = 3;
    m.msg.data[0] = 0xB0;
    m.msg.data[1] = static_cast<std::uint8_t> (number);
    m.msg.data[2] = static_cast<std::uint8_t> (value);
    return m;
}

struct Rendered
{
    std::vector<float> left, right;
    std::vector<TimedMidi> midiOut;      // absolute samples
    std::vector<EngineStatus> status;    // one per block
    std::size_t allocations = 0;         // operator new/delete seen inside process()
};

/** A pattern from the compact step notation ("C C+:S D#:A . ~ G-:AS ..."). */
inline seq::Pattern patternFromText (const std::string& steps)
{
    seq::Pattern p;
    std::string error;
    if (! seq::fromStepText (steps, p, &error))
        throw std::runtime_error ("bad step text: " + error);
    return p;
}

/** A parameter set with the effects off: the plain voice, for timing tests. */
inline ParamValues plainParams()
{
    ParamValues p = defaultParamValues();
    p[kPlayMode] = 1.0f;
    p[kSeqRun] = 0.0f;
    p[kCalDrift] = 0.0f;
    return p;
}

inline ParamValues typicalParams()
{
    ParamValues p = plainParams();
    p[kDistOn] = 1.0f;
    p[kDistDrive] = 50.0f;
    p[kRevOn] = 1.0f;
    return p;
}

/** The engine with its model: publishes the bank and settings, then renders. */
struct Session
{
    Engine engine;
    ParamValues params = plainParams();
    Settings settings {};
    std::unique_ptr<seq::PatternBank> bank = std::make_unique<seq::PatternBank>();
    Host host;
    std::vector<TimedMidi> midiIn;      // absolute samples, sorted by sample
    double sampleRate = kFs;
    int maxBlock = 512;
    bool guardAllocations = false;

    void setPattern (int slot, const std::string& steps) { bank->seq[static_cast<std::size_t> (slot)] = patternFromText (steps); }

    void prepare (double sr = kFs, int blockSize = 512)
    {
        sampleRate = sr;
        maxBlock = blockSize;
        host.sampleRate = sr;
        engine.publishSettings (settings);
        engine.publishBank (*bank);
        engine.prepare (sr, blockSize);
    }

    void publish()
    {
        engine.publishSettings (settings);
        engine.publishBank (*bank);
    }

    /** Renders numSamples. nextBlock gives each block's size; perBlock (position) may edit
        params, the host or the model before each block. */
    Rendered render (std::int64_t numSamples, const std::function<int()>& nextBlock,
                     const std::function<void (std::int64_t)>& perBlock = {})
    {
        Rendered out;
        out.left.assign (static_cast<std::size_t> (numSamples), 0.0f);
        out.right.assign (static_cast<std::size_t> (numSamples), 0.0f);

        std::vector<seq::MidiMsg> midiOutScratch (2048);
        std::vector<seq::MidiMsg> midiInBlock;
        midiInBlock.reserve (256);
        std::size_t midiIndex = 0;
        std::int64_t pos = 0;

        while (pos < numSamples)
        {
            int n = std::max (1, nextBlock());
            n = static_cast<int> (std::min<std::int64_t> (n, numSamples - pos));
            n = host.limitBlock (n);

            if (perBlock) perBlock (pos);

            midiInBlock.clear();
            while (midiIndex < midiIn.size() && midiIn[midiIndex].sample < pos + n)
            {
                seq::MidiMsg m = midiIn[midiIndex].msg;
                m.offset = static_cast<std::int32_t> (std::max<std::int64_t> (0, midiIn[midiIndex].sample - pos));
                midiInBlock.push_back (m);
                ++midiIndex;
            }

            MidiOutBuffer midiOut { midiOutScratch.data(), static_cast<int> (midiOutScratch.size()), 0 };
            const seq::TransportInfo t = host.info();

            {
                std::unique_ptr<test::ScopedAllocGuard> guard;
                if (guardAllocations) guard = std::make_unique<test::ScopedAllocGuard>();
                engine.process (params, t, { midiInBlock.data(), midiInBlock.size() }, midiOut,
                                out.left.data() + pos, out.right.data() + pos, n);
                if (guard) out.allocations += guard->count();
            }

            for (int i = 0; i < midiOut.count; ++i)
                out.midiOut.push_back ({ pos + midiOut.messages[i].offset, midiOut.messages[i] });
            out.status.push_back (engine.status());

            host.advance (n);
            pos += n;
        }

        return out;
    }

    Rendered render (std::int64_t numSamples, int blockSize, const std::function<void (std::int64_t)>& perBlock = {})
    {
        return render (numSamples, [blockSize] { return blockSize; }, perBlock);
    }
};

inline std::function<int()> randomBlocks (std::uint64_t seed, int lo = 1, int hi = 1000)
{
    auto rng = std::make_shared<Rng> (seed);
    return [rng, lo, hi] { return lo + rng->nextInt (hi - lo + 1); };
}

// ---- Analysis -----------------------------------------------------------------------------

inline std::uint64_t hashOf (const std::vector<float>& a, const std::vector<float>& b)
{
    std::uint64_t h = 1469598103934665603ull;
    const auto feed = [&h] (const std::vector<float>& v) {
        for (const float f : v)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &f, 4);
            for (int i = 0; i < 4; ++i)
            {
                h ^= (bits >> (8 * i)) & 0xFFu;
                h *= 1099511628211ull;
            }
        }
    };
    feed (a);
    feed (b);
    return h;
}

inline std::uint64_t hashOf (const Rendered& r) { return hashOf (r.left, r.right); }

inline bool allFinite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
}

inline bool allFinite (const Rendered& r) { return allFinite (r.left) && allFinite (r.right); }

inline float peakOf (const std::vector<float>& v, std::size_t from = 0, std::size_t to = static_cast<std::size_t> (-1))
{
    float m = 0.0f;
    for (std::size_t i = from; i < std::min (to, v.size()); ++i)
        m = std::max (m, std::abs (v[i]));
    return m;
}

inline double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0.0;
    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        m = std::max (m, static_cast<double> (std::abs (a[i] - b[i])));
    return m;
}

/** Note onsets in a mono signal: the sample at which a short RMS window first reaches
    threshold after having been quiet (below threshold / 4) for at least quietSeconds. The
    reported sample is up to one window late, never early. */
inline std::vector<std::int64_t> detectOnsets (const std::vector<float>& x, double sampleRate,
                                               double threshold = 0.02, double windowSeconds = 0.002,
                                               double quietSeconds = 0.010)
{
    std::vector<std::int64_t> onsets;
    const auto w = std::max<std::size_t> (1, static_cast<std::size_t> (windowSeconds * sampleRate));
    const auto quietNeeded = static_cast<std::int64_t> (quietSeconds * sampleRate);
    double acc = 0.0;
    std::int64_t quiet = quietNeeded;
    bool armed = true;

    for (std::size_t i = 0; i < x.size(); ++i)
    {
        acc += static_cast<double> (x[i]) * static_cast<double> (x[i]);
        if (i >= w) acc -= static_cast<double> (x[i - w]) * static_cast<double> (x[i - w]);
        const double rms = std::sqrt (std::max (0.0, acc) / static_cast<double> (w));

        if (rms < threshold / 4.0)
        {
            if (++quiet >= quietNeeded) armed = true;
        }
        else
        {
            quiet = 0;
        }

        if (armed && rms >= threshold)
        {
            onsets.push_back (static_cast<std::int64_t> (i));
            armed = false;
        }
    }

    return onsets;
}

/** Fundamental estimate over [from, from + length) by normalised autocorrelation, searched
    between fMin and fMax, with parabolic refinement. 0 when the window is too quiet. */
inline double estimatePitchHz (const std::vector<float>& x, std::size_t from, std::size_t length, double sampleRate,
                               double fMin = 100.0, double fMax = 2000.0)
{
    if (from + length > x.size()) return 0.0;
    const auto minLag = static_cast<std::size_t> (sampleRate / fMax);
    const auto maxLag = std::min (static_cast<std::size_t> (sampleRate / fMin), length / 2);
    if (minLag >= maxLag) return 0.0;

    double energy = 0.0;
    for (std::size_t i = 0; i < length; ++i) energy += static_cast<double> (x[from + i]) * static_cast<double> (x[from + i]);
    if (energy < 1.0e-6) return 0.0;

    std::vector<double> corr (maxLag + 1, 0.0);
    double best = -1.0;
    std::size_t bestLag = minLag;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double num = 0.0, a = 0.0, b = 0.0;
        for (std::size_t i = 0; i + lag < length; ++i)
        {
            const double p = static_cast<double> (x[from + i]);
            const double q = static_cast<double> (x[from + i + lag]);
            num += p * q;
            a += p * p;
            b += q * q;
        }
        corr[lag] = (a > 0.0 && b > 0.0) ? num / std::sqrt (a * b) : -1.0;
        if (corr[lag] > best)
        {
            best = corr[lag];
            bestLag = lag;
        }
    }

    // The first strong peak, not a multiple of the period: walk down from the best lag while
    // a shorter lag is nearly as good.
    for (std::size_t lag = minLag; lag < bestLag; ++lag)
        if (corr[lag] > 0.85 * best && corr[lag] >= corr[lag + 1] && (lag == minLag || corr[lag] >= corr[lag - 1]))
        {
            bestLag = lag;
            break;
        }

    double refined = static_cast<double> (bestLag);
    if (bestLag > minLag && bestLag < maxLag)
    {
        const double y0 = corr[bestLag - 1], y1 = corr[bestLag], y2 = corr[bestLag + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12) refined += 0.5 * (y0 - y2) / denom;
    }
    return sampleRate / refined;
}

inline std::int64_t sampleOfQuarter (double q, double bpm, double sr)
{
    return static_cast<std::int64_t> (std::ceil (q * 60.0 * sr / bpm - 1.0e-3));
}
}
