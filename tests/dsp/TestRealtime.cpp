// Real-time rules, parameter abuse and the CPU benchmark.
#include "AllocGuard.h"
#include "VoiceTestSupport.h"

#include "acid/common/Rng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
/** Any double the host could conceivably hand over, mostly in range, sometimes far out
    of it, occasionally not a number at all. */
double wild (Rng& rng, double lo, double hi)
{
    const double roll = rng.nextDouble();
    if (roll < 0.02) return std::numeric_limits<double>::quiet_NaN();
    if (roll < 0.04) return std::numeric_limits<double>::infinity();
    if (roll < 0.06) return -std::numeric_limits<double>::infinity();
    if (roll < 0.12) return (rng.nextDouble() - 0.5) * 2.0e6;
    return lo + (hi - lo) * rng.nextDouble();
}

dsp::VoiceParams wildParams (Rng& rng)
{
    dsp::VoiceParams p;
    p.wave = rng.nextInt (4) - 1;
    p.tuneExt = rng.chance (0.3);
    p.tune = wild (rng, -12, 12);
    p.tuneCoarse = rng.nextInt (60) - 30;
    p.tuneFine = wild (rng, -100, 100);
    p.cutoff = wild (rng, 0, 100);
    p.resonance = wild (rng, 0, 100);
    p.envMod = wild (rng, 0, 100);
    p.decay = wild (rng, 0, 100);
    p.accent = wild (rng, 0, 100);
    p.slideTime = wild (rng, 0, 100);
    p.sweepCouple = rng.chance (0.5);
    p.sweepAmt = wild (rng, 0, 100);
    p.envAttack = wild (rng, 0, 100);
    p.accDecay = wild (rng, 0, 100);
    p.accVolCouple = rng.chance (0.5);
    p.accVol = wild (rng, 0, 100);
    p.vibSpeed = wild (rng, 0, 100);
    p.vibDepth = wild (rng, 0, 100);
    p.calTm3 = wild (rng, -500, 500);
    p.calC21 = wild (rng, 7.9, 31800);
    p.calAccEnv = wild (rng, 0, 4000);
    p.calAccAmp = wild (rng, 0, 3.5);
    p.calC13 = wild (rng, 0.33, 4.7);
    p.calTm5 = wild (rng, 0.9, 1.1);
    p.calPw = wild (rng, -100, 100);
    p.calVcaClicks = rng.chance (0.5);
    p.calVcaNoise = wild (rng, 0, 100);
    p.calRb = wild (rng, 10, 35);
    p.calKmax = wild (rng, 17, 20);
    p.calCvScale = wild (rng, 0.75, 1.05);
    p.calGateDelay = wild (rng, 0, 6);
    p.calDrift = wild (rng, 0, 100);
    p.calCpuJitter = rng.chance (0.3);
    p.modOn = rng.chance (0.7);
    p.modOverdrive = wild (rng, 0, 100);
    p.modFilterFm = wild (rng, 0, 100);
    p.modTracking = wild (rng, 0, 100);
    p.modAmpDecay = wild (rng, 7, 1500);
    p.modAmpSustain = wild (rng, 0, 100);
    p.modSoftAttack = wild (rng, 0.3, 30);
    p.modHiRes = rng.chance (0.5);
    p.modSweepSpeed = rng.nextInt (5) - 1;
    p.modMuffler = rng.nextInt (5) - 1;
    p.modExtRange = rng.chance (0.5);
    p.modSubLevel = wild (rng, 0, 100);
    p.modSubOct = rng.nextInt (4) - 1;
    return p;
}

void randomEvent (dsp::Voice& v, Rng& rng)
{
    const double roll = rng.nextDouble();
    if (roll < 0.45)      v.noteOn (rng.nextInt (160) - 16, rng.chance (0.4), rng.chance (0.3), rng.chance (0.2));
    else if (roll < 0.75) v.noteOff();
    else if (roll < 0.85) v.setPitchBend (wild (rng, -12, 12));
    else if (roll < 0.95) v.setVibratoWheel (wild (rng, 0, 1));
    else if (roll < 0.96) v.reset();
}
}

TEST_CASE ("parameter abuse: random values every block, the output stays finite and bounded", "[voice][realtime][abuse]")
{
    struct Config { double fs; int os; };
    for (const Config& c : { Config { 48000.0, 4 }, Config { 44100.0, 1 }, Config { 96000.0, 2 }, Config { 192000.0, 1 }, Config { 48000.0, 8 } })
    {
        Rng rng (static_cast<std::uint64_t> (c.fs) + static_cast<std::uint64_t> (c.os));
        dsp::Voice v;
        v.prepare (c.fs, 512, c.os);
        std::vector<float> block (512);
        double worst = 0.0;
        bool finite = true;
        const int blocks = c.os >= 4 ? 4000 : 6000;
        for (int b = 0; b < blocks; ++b)
        {
            v.setParams (wildParams (rng));
            if (rng.chance (0.25)) randomEvent (v, rng);
            const int n = 1 + rng.nextInt (512);
            v.process (block.data(), n);
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (block[static_cast<std::size_t> (i)]);
                worst = std::max (worst, static_cast<double> (std::abs (block[static_cast<std::size_t> (i)])));
            }
            const auto m = v.meters();
            finite = finite && std::isfinite (m.accentCharge) && std::isfinite (m.envelope) && std::isfinite (m.amplitude)
                            && std::isfinite (m.cutoffHz) && std::isfinite (m.pitchHz);
        }
        const auto& stats = v.debugFilter().stats();
        std::printf ("[abuse] %.0f Hz x%d: worst |out| %.2f, ladder mean %.2f iterations, worst %u, not converged %llu of %llu, resets %llu\n",
                     c.fs, c.os, worst, stats.meanIterations(), stats.maxIterations,
                     static_cast<unsigned long long> (stats.nonConverged), static_cast<unsigned long long> (stats.samples),
                     static_cast<unsigned long long> (stats.stateResets));
        INFO ("fs " << c.fs << " os " << c.os);
        CHECK (finite);
        CHECK (worst < 60.0);
        CHECK (stats.stateResets == 0);

        // And it recovers: back to defaults, a plain note sounds at its usual level.
        v.setParams (quietParams());
        v.reset();
        const std::vector<SeqStep> pattern (4, SeqStep {});
        const auto audio = render (v, makeEvents (pattern, 125.0, c.fs, 1), static_cast<long long> (0.5 * c.fs), 256);
        CHECK (20.0 * std::log10 (peakAbs (audio)) == Approx (-12.0).margin (2.5));
    }
}

TEST_CASE ("nothing on the audio path allocates", "[voice][realtime]")
{
    dsp::Voice v;
    v.prepare (48000.0, 512, 4);
    std::vector<float> block (512);
    Rng rng (11);
    std::vector<dsp::VoiceParams> params;
    for (int i = 0; i < 64; ++i) params.push_back (wildParams (rng));
    const ParamValues table = defaultParamValues();
    std::size_t count = 1, prepareCount = 1;
    double sink = 0.0;
    {
        ScopedAllocGuard guard;
        for (int b = 0; b < 600; ++b)
        {
            v.setParams (params[static_cast<std::size_t> (b % 64)]);
            if (b % 3 == 0) v.noteOn (24 + b % 48, b % 2 == 0, b % 5 == 0, b % 7 == 0);
            if (b % 3 == 2) v.noteOff();
            if (b % 11 == 0) v.setPitchBend (0.1 * (b % 20));
            if (b % 13 == 0) v.setVibratoWheel (0.05 * (b % 20));
            if (b % 97 == 0) v.reset();
            v.process (block.data(), 1 + (b * 37) % 512);
            sink += static_cast<double> (v.meters().cutoffHz) + v.latencySamples();
            sink += dsp::voiceParamsFrom (table).cutoff;
        }
        count = guard.count();
    }
    CHECK (count == 0);
    {
        // prepare() is not on the audio path, but it does not allocate either, so a host
        // changing the oversampling tier costs no heap traffic.
        ScopedAllocGuard guard;
        v.prepare (96000.0, 1024, 2);
        v.prepare (48000.0, 512, 8);
        prepareCount = guard.count();
    }
    CHECK (prepareCount == 0);
    CHECK (std::isfinite (sink));
}

TEST_CASE ("denormals: long silence costs no more than signal and leaves no residue", "[voice][realtime]")
{
    dsp::Voice v;
    start (v, quietParams(), 48000.0, 4);
    v.noteOn (36, true, false, false);
    skip (v, 0.06);
    v.noteOff();
    std::vector<float> block (4800);
    // Let every tail run down for 60 s, then time a second of silence against a second of sound.
    skip (v, 60.0);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) v.process (block.data(), 4800);
    const auto t1 = std::chrono::steady_clock::now();
    CHECK (peakAbs (block) == 0.0);
    v.noteOn (36, true, false, false);
    for (int i = 0; i < 10; ++i) v.process (block.data(), 4800);
    const auto t2 = std::chrono::steady_clock::now();
    const double silent = std::chrono::duration<double> (t1 - t0).count(), sounding = std::chrono::duration<double> (t2 - t1).count();
    std::printf ("[denormals] one second of silence after 60 s of decay: %.2f ms; one second of sound: %.2f ms\n", silent * 1e3, sounding * 1e3);
    CHECK (silent < 3.0 * sounding);
}

namespace
{
/** render() with the controls in motion: every 128-sample block moves the cutoff knob, the
    pitch bend and the vibrato wheel, so every smoothed control stays in motion and the knob
    laws run on every block, as they do under host automation. */
std::vector<float> renderAutomated (dsp::Voice& voice, dsp::VoiceParams p, const std::vector<SeqEvent>& events, long long totalSamples)
{
    constexpr int kBlock = 128;
    std::vector<float> out (static_cast<std::size_t> (totalSamples), 0.0f);
    std::size_t next = 0;
    long long pos = 0;
    int block = 0;
    while (pos < totalSamples)
    {
        const double phase = 0.05 * static_cast<double> (block++);
        p.cutoff = 50.0 + 40.0 * std::sin (phase);
        voice.setParams (p);
        voice.setPitchBend (2.0 * std::sin (0.37 * phase));
        voice.setVibratoWheel (0.5 + 0.5 * std::sin (0.11 * phase));
        const long long blockEnd = std::min<long long> (pos + kBlock, totalSamples);
        while (pos < blockEnd)
        {
            while (next < events.size() && events[next].sample <= pos)
            {
                const SeqEvent& e = events[next++];
                if (e.on) voice.noteOn (e.note, e.accent, e.legato, e.vibrato);
                else      voice.noteOff();
            }
            long long until = blockEnd;
            if (next < events.size() && events[next].sample < until)
                until = events[next].sample;
            voice.process (out.data() + pos, static_cast<int> (until - pos));
            pos = until;
        }
    }
    return out;
}
}

TEST_CASE ("CPU: faster than 15 times real time at 48 kHz with 4x oversampling, static and automated", "[voice][realtime][.benchmark]")
{
    // Hidden by default (timing): run with the [benchmark] tag on a release build.
    // SPEC 6.1 asks for better than 15x at 48 kHz x4 on one core. The automated rows move
    // the cutoff knob, the pitch bend and the vibrato wheel on every 128-sample block, which
    // is what host automation does; the review found the knob laws cost 60 % on top of the
    // static case before they were split into groups and a control-rate grid.
    const auto n = [] (int note, bool accent = false, bool slide = false) { return SeqStep { note, accent, slide, false, false }; };
    SeqStep rest;
    rest.rest = true;
    const std::vector<SeqStep> pattern { n (36, true), n (36), n (48, false, true), n (39, true), n (36), rest, n (36, true), n (36, true),
                                         n (43, true), n (48, true), n (36, false, true), n (36, false, true), n (34), n (36), n (60, true, true), n (36) };
    struct Config { const char* name; double fs; int os; int wave; bool automate; };
    double staticFactor = 0.0, automatedFactor = 0.0;
    for (const Config& c : { Config { "48 kHz x4 saw", 48000.0, 4, 0, false }, Config { "48 kHz x4 saw, automated", 48000.0, 4, 0, true },
                             Config { "48 kHz x4 square", 48000.0, 4, 1, false }, Config { "48 kHz x4 square, automated", 48000.0, 4, 1, true },
                             Config { "48 kHz x8 saw", 48000.0, 8, 0, false }, Config { "48 kHz x2 saw", 48000.0, 2, 0, false },
                             Config { "44.1 kHz x4 saw", 44100.0, 4, 0, false }, Config { "44.1 kHz x4 saw, automated", 44100.0, 4, 0, true },
                             Config { "96 kHz x2 saw", 96000.0, 2, 0, false } })
    {
        dsp::VoiceParams p;
        p.wave = c.wave;
        p.resonance = 85.0;
        p.accent = 85.0;
        const double seconds = 15.36;       // eight bars
        const auto events = makeEvents (pattern, 125.0, c.fs, 8);
        double best = 1.0e9;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            dsp::Voice v;
            start (v, p, c.fs, c.os);
            const auto t0 = std::chrono::steady_clock::now();
            const auto audio = c.automate ? renderAutomated (v, p, events, static_cast<long long> (seconds * c.fs))
                                          : render (v, events, static_cast<long long> (seconds * c.fs), 256);
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
            REQUIRE (allFinite (audio));
        }
        const double factor = seconds / best;
        std::printf ("[benchmark] %-28s %.1f x real time (%.2f %% of one core, %.0f ns per oversampled sample)\n",
                     c.name, factor, 100.0 / factor, 1e9 * best / (seconds * c.fs * c.os));
        if (c.fs == 48000.0 && c.os == 4)
        {
            INFO (c.name);
            CHECK (factor > 15.0);
            if (c.wave == 0) (c.automate ? automatedFactor : staticFactor) = factor;
        }
    }
    // Automation may not cost more than a quarter on top of the static case.
    std::printf ("[benchmark] 48 kHz x4 saw: %.1f x static, %.1f x automated (ratio %.2f)\n",
                 staticFactor, automatedFactor, staticFactor / automatedFactor);
    CHECK (automatedFactor > 0.8 * staticFactor);
}
