// End-to-end renders: determinism, block-size invariance, quality tiers, latency, real-time
// rules, every parameter reaching the engine, and the CPU benchmark.
#include "EngineTestSupport.h"
#include "Wav.h"
#include "acid/dsp/Voice.h"
#include "acid/fx/FxChain.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <set>

using namespace etest;

namespace
{
const std::string kLine = "C C+:S D#:A . ~ G-:AS C . C:S C+ . A# ~ ~ . C:A";

bool sameGroupName (const ParamInfo& info, const char* group) { return std::strcmp (info.group, group) == 0; }

Rendered renderLine (const ParamValues& params, double sr, int maxBlock, int blockSize, double seconds, const Settings& settings = {})
{
    Session s;
    s.params = params;
    s.params[kSeqRun] = 1.0f;
    s.settings = settings;
    s.setPattern (0, kLine);
    s.prepare (sr, maxBlock);
    s.host.bpm = 125.0;
    s.host.play();
    return s.render (static_cast<std::int64_t> (seconds * sr), blockSize);
}
}

TEST_CASE ("engine: a pattern renders deterministically through the whole engine", "[engine][render]")
{
    const Rendered a = renderLine (typicalParams(), kFs, 512, 512, 4.0);
    const Rendered b = renderLine (typicalParams(), kFs, 512, 512, 4.0);
    REQUIRE (allFinite (a));
    REQUIRE (hashOf (a) == hashOf (b));
    REQUIRE (peakOf (a.left) > 0.05f);
    REQUIRE (peakOf (a.left) < 2.0f);     // accented notes with the distortion and reverb peak well above the plain -12 dBFS
    REQUIRE (a.left != a.right);          // the reverb is stereo
    REQUIRE (a.status.back().running);
    REQUIRE (a.status.back().playingSlot == 0);
    std::printf ("[render] typical line hash %016llx, peak %.3f\n", static_cast<unsigned long long> (hashOf (a)), static_cast<double> (peakOf (a.left)));

    std::vector<float> inter (a.left.size() * 2);
    for (std::size_t i = 0; i < a.left.size(); ++i)
    {
        inter[2 * i] = a.left[i];
        inter[2 * i + 1] = a.right[i];
    }
    test::writeWav (std::string (ACID_TEST_OUTPUT_DIR) + "/engine_typical.wav", inter, 2, kFs);
}

TEST_CASE ("engine: the output does not depend on the block size", "[engine][blocksize]")
{
    ParamValues p = typicalParams();
    p[kChoOn] = 1.0f;     // a stereo block, so left and right differ
    p[kDelayOn] = 1.0f;
    p[kCalCpuJitter] = 1.0f;
    p[kCalVcaNoise] = 20.0f;
    p[kModOn] = 1.0f;
    p[kModSubLevel] = 30.0f;

    const double seconds = 2.5;
    const Rendered ref = renderLine (p, kFs, 512, 512, seconds);
    REQUIRE (allFinite (ref));
    REQUIRE (ref.left != ref.right);

    for (const int blockSize : { 1, 64, 4096 })
    {
        const Rendered r = renderLine (p, kFs, 512, blockSize, seconds);
        INFO ("block size " << blockSize);
        CHECK (hashOf (r) == hashOf (ref));
    }

    // Random block sizes, and a prepared size smaller than the player's overflow-free size so
    // that oversized blocks are split both ways.
    {
        Session s;
        s.params = p;
        s.params[kSeqRun] = 1.0f;
        s.setPattern (0, kLine);
        s.prepare (kFs, 64);
        s.host.bpm = 125.0;
        s.host.play();
        const Rendered r = s.render (static_cast<std::int64_t> (seconds * kFs), randomBlocks (7, 1, 9000));
        CHECK (hashOf (r) == hashOf (ref));
    }
}

TEST_CASE ("engine: block-size invariance with MIDI input in External mode", "[engine][blocksize][external]")
{
    const auto run = [] (const std::function<int()>& blocks) {
        Session s;
        s.params = plainParams();
        s.params[kPlayMode] = 0.0f;
        s.params[kChoOn] = 1.0f;
        s.prepare (kFs, 256);
        s.midiIn = { noteOn (1000, 48, 100), noteOn (20000, 55, 120), noteOff (21000, 48), controller (25000, 1, 90),
                     noteOff (40000, 55), noteOn (40100, 60, 40), noteOff (60000, 60) };
        return s.render (70000, blocks);
    };
    const Rendered ref = run ([] { return 256; });
    REQUIRE (allFinite (ref));
    REQUIRE (peakOf (ref.left) > 0.05f);
    CHECK (hashOf (run ([] { return 1; })) == hashOf (ref));
    CHECK (hashOf (run ([] { return 64; })) == hashOf (ref));
    CHECK (hashOf (run ([] { return 4096; })) == hashOf (ref));
    CHECK (hashOf (run (randomBlocks (11, 1, 3000))) == hashOf (ref));
}

TEST_CASE ("engine: quality tiers give the documented factors", "[engine][quality]")
{
    struct Row { double sr; int draft, normal, high, ultra; };
    const Row rows[] = {
        { 44100.0, 2, 4, 8, 8 },
        { 48000.0, 2, 4, 8, 8 },
        { 88200.0, 1, 2, 4, 8 },
        { 96000.0, 1, 2, 4, 8 },
        { 176400.0, 1, 1, 2, 4 },
        { 192000.0, 1, 1, 2, 4 },
    };
    for (const Row& r : rows)
    {
        INFO ("rate " << r.sr);
        CHECK (voiceOversamplingFor (Quality::Draft, r.sr) == r.draft);
        CHECK (voiceOversamplingFor (Quality::Normal, r.sr) == r.normal);
        CHECK (voiceOversamplingFor (Quality::High, r.sr) == r.high);
        CHECK (voiceOversamplingFor (Quality::Ultra, r.sr) == r.ultra);
        CHECK (distortionOversamplingFor (Quality::Draft, r.sr) == 1);
        CHECK (distortionOversamplingFor (Quality::Normal, r.sr) == r.normal);
        CHECK (distortionOversamplingFor (Quality::High, r.sr) == r.high);
        CHECK (distortionOversamplingFor (Quality::Ultra, r.sr) == r.ultra);
        // Normal keeps the internal rate near 176.4 to 192 kHz.
        CHECK (r.sr * r.normal >= 176400.0);
        CHECK (r.sr * r.normal <= 192000.0);
    }
    CHECK (voiceOversamplingFor (static_cast<Quality> (99), 48000.0) == 4);

    // The engine applies the tier chosen for real time or offline rendering at prepare().
    Session s;
    s.settings.realtimeQuality = Quality::Draft;
    s.settings.offlineQuality = Quality::Ultra;
    s.prepare (48000.0, 256);
    CHECK (s.engine.status().oversampling == 2);
    s.engine.setNonRealtime (true);
    s.engine.prepare (48000.0, 256);
    CHECK (s.engine.status().oversampling == 8);
    s.engine.prepare (192000.0, 256);
    CHECK (s.engine.status().oversampling == 4);
    s.engine.setNonRealtime (false);
    s.engine.prepare (96000.0, 256);
    CHECK (s.engine.status().oversampling == 1);
}

TEST_CASE ("engine: latency is the voice plus the effects, and matches the modules", "[engine][latency]")
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const Quality q : { Quality::Draft, Quality::Normal, Quality::High, Quality::Ultra })
        {
            Session s;
            s.settings.realtimeQuality = q;
            s.prepare (sr, 256);

            dsp::Voice v;
            v.prepare (sr, 256, voiceOversamplingFor (q, sr));
            fx::FxChain chain;
            chain.setDistortionOversampling (distortionOversamplingFor (q, sr));
            chain.prepare (sr, 256);

            INFO ("rate " << sr << " quality " << static_cast<int> (q));
            CHECK (s.engine.latencySamples() == v.latencySamples() + chain.latencySamples());
            CHECK (s.engine.status().latencySamples == s.engine.latencySamples());
            CHECK (s.engine.status().prepared);
            CHECK (s.engine.tailSeconds() > 0.0);
        }
    }
}

TEST_CASE ("engine: every sample rate renders finite audio with notes at the right level", "[engine][rates]")
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const Rendered r = renderLine (typicalParams(), sr, 512, 512, 1.5);
        INFO ("rate " << sr);
        CHECK (allFinite (r));
        CHECK (peakOf (r.left) > 0.05f);
        CHECK (peakOf (r.left) < 2.0f);
    }
}

TEST_CASE ("engine: nothing in process() allocates, before or after publishes", "[engine][realtime]")
{
    Session s;
    s.params = typicalParams();
    s.params[kSeqRun] = 1.0f;
    s.params[kChoOn] = 1.0f;
    s.params[kDelayOn] = 1.0f;
    s.params[kEqOn] = 1.0f;
    s.params[kLimOn] = 1.0f;
    s.setPattern (0, kLine);
    s.setPattern (1, "C . E . G . B . C+ . . . A:S G . .");
    s.prepare (kFs, 512);
    s.host.bpm = 130.0;
    s.host.play();
    s.guardAllocations = true;

    const auto perBlock = [&s] (std::int64_t pos) {
        if (pos == 512 * 20)
        {
            s.bank->seq[2] = patternFromText ("D . F . A . . . D+:A . . . C:S D . .");
            s.params[kPatternSlot] = 3.0f;
            s.publish();
        }
        if (pos == 512 * 40)
        {
            s.settings.fxOrder = { 5, 4, 3, 2, 1, 0 };
            s.publish();
        }
        if (pos == 512 * 60)
            s.params[kFxBypass] = 1.0f;
        if (pos == 512 * 70)
        {
            s.params[kFxBypass] = 0.0f;
            s.params[kPlayMode] = 0.0f;
            s.midiIn.push_back (noteOn (pos + 10, 50, 110));
            s.midiIn.push_back (noteOff (pos + 4000, 50));
        }
        if (pos == 512 * 90)
        {
            s.params[kPlayMode] = 2.0f;
            s.midiIn.push_back (noteOn (pos + 10, 48, 100));
            s.midiIn.push_back (noteOn (pos + 20, 55, 100));
        }
        if (pos == 512 * 100)
            s.params[kMasterVolume] = -60.0f;
    };
    const Rendered r = s.render (512 * 120, randomBlocks (3, 1, 3000), perBlock);
    CHECK (r.allocations == 0);
    CHECK (allFinite (r));

    // A huge block, far larger than the prepared size.
    {
        std::vector<float> l (200000), rr (200000);
        std::vector<seq::MidiMsg> mo (64);
        MidiOutBuffer midiOut { mo.data(), 64, 0 };
        seq::TransportInfo t = s.host.info();
        test::ScopedAllocGuard guard;
        s.engine.process (s.params, t, {}, midiOut, l.data(), rr.data(), 200000);
        s.engine.reset();
        CHECK (guard.count() == 0);
    }
}

TEST_CASE ("engine: process before prepare and after release outputs silence", "[engine][realtime]")
{
    Engine e;
    std::vector<float> l (256, 1.0f), r (256, 1.0f);
    std::vector<seq::MidiMsg> mo (8);
    MidiOutBuffer midiOut { mo.data(), 8, 3 };
    seq::TransportInfo t;
    {
        test::ScopedAllocGuard guard;
        e.process (defaultParamValues(), t, {}, midiOut, l.data(), r.data(), 256);
        CHECK (guard.count() == 0);
    }
    CHECK (midiOut.count == 0);
    CHECK (peakOf (l) == 0.0f);
    CHECK (peakOf (r) == 0.0f);
    CHECK_FALSE (e.status().prepared);

    e.prepare (kFs, 256);
    CHECK (e.status().prepared);
    e.release();
    CHECK_FALSE (e.status().prepared);
    std::fill (l.begin(), l.end(), 1.0f);
    e.process (defaultParamValues(), t, {}, midiOut, l.data(), r.data(), 256);
    CHECK (peakOf (l) == 0.0f);
}

TEST_CASE ("engine: master volume, silence at -60 dB and fx_bypass", "[engine][master]")
{
    ParamValues p = plainParams();
    const Rendered unity = renderLine (p, kFs, 512, 512, 1.0);
    p[kMasterVolume] = -6.0f;
    const Rendered quieter = renderLine (p, kFs, 512, 512, 1.0);
    p[kMasterVolume] = -60.0f;
    const Rendered silent = renderLine (p, kFs, 512, 512, 1.0);
    p[kMasterVolume] = 12.0f;
    const Rendered louder = renderLine (p, kFs, 512, 512, 1.0);

    const float pu = peakOf (unity.left), pq = peakOf (quieter.left), pl = peakOf (louder.left);
    CHECK (pu > 0.05f);
    CHECK (std::abs (20.0 * std::log10 (pq / pu) + 6.0) < 0.2);
    CHECK (std::abs (20.0 * std::log10 (pl / pu) - 12.0) < 0.2);
    CHECK (peakOf (silent.left) == 0.0f);
    CHECK (unity.status.back().peakLeft > 0.0f);
    CHECK (silent.status.back().peakLeft == 0.0f);

    // fx_bypass: with the effects on, bypass equals the voice alone (delayed by the latency, which is 0 here).
    ParamValues fxOn = typicalParams();
    fxOn[kDistDrive] = 90.0f;
    const Rendered withFx = renderLine (fxOn, kFs, 512, 512, 1.0);
    fxOn[kFxBypass] = 1.0f;
    const Rendered bypassed = renderLine (fxOn, kFs, 512, 512, 1.0);
    const Rendered plain = renderLine (plainParams(), kFs, 512, 512, 1.0);
    const auto afterFade = [] (const std::vector<float>& v) { return std::vector<float> (v.begin() + 4800, v.end()); };
    CHECK (maxAbsDiff (afterFade (withFx.left), afterFade (plain.left)) > 0.01);
    CHECK (maxAbsDiff (afterFade (bypassed.left), afterFade (plain.left)) < 1.0e-6);
}

TEST_CASE ("engine: every parameter in the table reaches the engine", "[engine][params]")
{
    // Each row is perturbed away from its default in a context where it can act (its block
    // on, mods on); the output must change. Rows that are carried for the UI alone are
    // checked on the module parameter struct instead.
    const double seconds = 1.2;
    const std::string line = "C:A C+:S D#:A . ~ G-:AS C:V . C:S C+ . A# ~ ~ . C:A";

    const auto render = [&] (const ParamValues& p, bool withSecondSlot, bool hostPlaying = true) {
        Session s;
        s.params = p;
        s.setPattern (0, line);
        if (withSecondSlot)
        {
            s.setPattern (12, "G . . . G . . . G . . . G . . .");
            s.setPattern (1, "A- . . . A- . . . A- . . . A- . . .");
        }
        s.settings.midiOut = true;
        s.prepare (kFs, 512);
        s.host.bpm = 125.0;
        if (hostPlaying) s.host.play();
        return s.render (static_cast<std::int64_t> (seconds * kFs), 512);
    };

    ParamValues base = plainParams();
    base[kSeqRun] = 1.0f;
    base[kDistOn] = 1.0f;
    base[kEqOn] = 1.0f;
    base[kDelayOn] = 1.0f;
    base[kDelayMix] = 50.0f;
    base[kRevOn] = 1.0f;
    base[kChoOn] = 1.0f;
    base[kLimOn] = 1.0f;
    base[kLimThreshold] = -20.0f;
    base[kLimSoftClip] = 1.0f;
    base[kModOn] = 1.0f;
    base[kModSubLevel] = 30.0f;
    base[kModOverdrive] = 20.0f;
    base[kModFilterFm] = 20.0f;
    base[kDistPreEqOn] = 0.0f;
    base[kDistTiltOn] = 0.0f;
    base[kEqOn] = 0.0f;
    base[kDistColor] = 40.0f;
    base[kDistDynamics] = 40.0f;
    base[kDistLowKeep] = 80.0f;
    base[kDistPreHp] = 40.0f;
    base[kDistPostHp] = 40.0f;
    base[kCalDrift] = 10.0f;
    base[kCalVcaNoise] = 5.0f;
    base[kVibDepth] = 50.0f;
    base[kDelayFiltType] = 1.0f;
    base[kDelaySync] = 1.0f;
    base[kCalCpuJitter] = 0.0f;

    const Rendered ref = render (base, true);
    REQUIRE (allFinite (ref));

    const Settings settings {};
    const fx::FxParams fxRef = fx::fxParamsFrom (base, settings, 120.0);

    std::vector<std::string> unreached;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<ParamId> (i);
        const ParamInfo& info = kParamTable[static_cast<std::size_t> (i)];
        ParamValues p = base;
        float value = base[static_cast<std::size_t> (i)];

        switch (id)
        {
            case kTune: value = 7.0f; break;
            case kTuneCoarse: p[kTuneExt] = 1.0f; value = -5.0f; break;
            case kTuneFine: p[kTuneExt] = 1.0f; value = 60.0f; break;
            case kSweepAmt: p[kSweepCouple] = 0.0f; value = 10.0f; break;
            case kAccVol: p[kAccVolCouple] = 0.0f; value = 20.0f; break;
            case kDelayTime: p[kDelaySync] = 0.0f; value = 0.5f; break;
            case kModSweepSpeed: value = 2.0f; break;
            case kModMuffler: value = 2.0f; break;
            case kModSubOct: value = 1.0f; break;
            case kPatternSlot: value = 2.0f; break;
            case kPatternBank: value = 1.0f; break;
            case kPlayMode: value = 0.0f; break;
            case kLiveTranspose: value = 7.0f; break;
            case kCalGateDelay: value = 0.0f; break;
            case kRevPreDelay: value = 200.0f; break;
            case kDistModel: value = 5.0f; break;
            case kDistClipper: value = 7.0f; break;
            case kDelayLMod: case kDelayRMod: value = 1.0f; break;
            case kDelayFiltType: value = 3.0f; break;
            case kWave: value = 1.0f; break;
            default:
                if (info.kind == ParamKind::Bool || info.kind == ParamKind::Choice)
                    value = value >= 0.5f ? 0.0f : 1.0f;
                else if (std::abs (value - info.max) < std::abs (value - info.min))
                    value = info.min;
                else
                    value = info.max;
                break;
        }
        if (id == kSeqRun) value = 0.0f;
        if (id == kTuneExt) p[kTuneCoarse] = 5.0f;
        if (id == kDistPreEqOn) p[kDistPreEqGain] = 12.0f;
        if (id == kDistTiltOn) p[kDistTiltAmt] = 12.0f;
        if (id == kEqOn) { p[kEqLowGain] = 12.0f; p[kEqHiMidGain] = 12.0f; }
        if (sameGroupName (info, "EQ") && id != kEqOn) p[kEqOn] = 1.0f;
        if (id == kDistPreEqGain || id == kDistPreEqFreq || id == kDistPreEqWidth) p[kDistPreEqOn] = 1.0f;
        if (id == kDistTiltAmt || id == kDistTiltFreq) p[kDistTiltOn] = 1.0f;
        if (id == kLimSoftClip) { p[kLimThreshold] = 0.0f; p[kLimGain] = 24.0f; }
        if (id == kFxBypass) value = 1.0f;
        if (id == kDistDynPost) { p[kDistDynamics] = 60.0f; value = 1.0f; }
        if (id == kChoHp) value = 100.0f;
        if (id == kEqLowWidth || id == kEqLoMidWidth || id == kEqHiMidWidth || id == kEqHighWidth)
        {
            p[kEqLowGain] = 12.0f; p[kEqLoMidGain] = 12.0f; p[kEqHiMidGain] = 12.0f; p[kEqHighGain] = 12.0f;
        }
        if (id == kEqLowFreq || id == kEqLoMidFreq || id == kEqHiMidFreq || id == kEqHighFreq)
        {
            p[kEqLowGain] = 12.0f; p[kEqLoMidGain] = 12.0f; p[kEqHiMidGain] = 12.0f; p[kEqHighGain] = 12.0f;
            value = 1000.0f;
        }
        if (id == kDistPreEqGain) value = 18.0f;
        if (id == kDistPreEqFreq || id == kDistPreEqWidth) { p[kDistPreEqGain] = 18.0f; }
        if (id == kDistTiltFreq) { p[kDistTiltAmt] = 18.0f; value = 300.0f; }
        if (id == kDistColorFreq || id == kDistColorReso || id == kDistColorRange) { p[kDistColor] = 90.0f; }
        if (id == kDistColorFreq) value = 300.0f;
        if (id == kLimKnee) { value = 0.0f; }
        if (id == kLimRatio) { value = 2.0f; }
        if (id == kLimAttack) { value = 100.0f; }
        if (id == kLimRelease) { value = 500.0f; }
        if (id == kDelayFiltReso) { value = 100.0f; }
        if (id == kVibSpeed || id == kVibDepth) { value = id == kVibSpeed ? 100.0f : 100.0f; }

        const bool needsSecondSlot = id == kPatternSlot || id == kPatternBank;
        p[static_cast<std::size_t> (i)] = value;

        // The UI-only row: the DSP reads the voicing rows, so check the struct.
        if (id == kDistModel)
        {
            const fx::FxParams fxp = fx::fxParamsFrom (p, settings, 120.0);
            if (fxp.distortion.model == fxRef.distortion.model) unreached.push_back (info.id);
            continue;
        }

        const Rendered r = id == kSeqRun ? render (p, false, false) : render (p, needsSecondSlot);
        const bool changedAudio = maxAbsDiff (r.left, ref.left) > 1.0e-7 || maxAbsDiff (r.right, ref.right) > 1.0e-7;
        if (! changedAudio)
            unreached.push_back (info.id);
    }

    for (const auto& id : unreached)
        WARN ("parameter did not change the output: " << id);
    CHECK (unreached.empty());
}

TEST_CASE ("engine: CPU, whole engine at 48 kHz with the distortion and reverb on", "[engine][.benchmark]")
{
    struct Config { const char* name; ParamValues params; Quality quality; };
    ParamValues typical = typicalParams();
    ParamValues everything = typical;
    everything[kEqOn] = 1.0f;
    everything[kDelayOn] = 1.0f;
    everything[kChoOn] = 1.0f;
    everything[kLimOn] = 1.0f;
    const Config configs[] = {
        { "Normal, distortion + reverb", typical, Quality::Normal },
        { "Normal, everything on", everything, Quality::Normal },
        { "Draft, distortion + reverb", typical, Quality::Draft },
        { "High, distortion + reverb", typical, Quality::High },
    };
    const double seconds = 15.36;   // eight bars at 125 BPM

    for (const Config& c : configs)
    {
        double best = 1.0e9;
        double cpu = 0.0;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            Settings settings;
            settings.realtimeQuality = c.quality;
            Session s;
            s.params = c.params;
            s.params[kSeqRun] = 1.0f;
            s.settings = settings;
            s.setPattern (0, kLine);
            s.prepare (kFs, 256);
            s.host.bpm = 125.0;
            s.host.play();
            const auto t0 = std::chrono::steady_clock::now();
            const Rendered r = s.render (static_cast<std::int64_t> (seconds * kFs), 256);
            const auto t1 = std::chrono::steady_clock::now();
            best = std::min (best, std::chrono::duration<double> (t1 - t0).count());
            cpu = r.status.back().cpuLoad;
            REQUIRE (allFinite (r));
        }
        const double factor = seconds / best;
        std::printf ("[benchmark] %-32s %.1f x real time (%.2f %% of one core; engine's own smoothed load %.2f %%)\n",
                     c.name, factor, 100.0 / factor, 100.0 * cpu);
        if (c.quality == Quality::Normal)
            CHECK (factor > 8.0);
    }
}
