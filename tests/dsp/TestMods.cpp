// The Mods layer: inert when switched off, and each modification's behaviour when on
// (docs/research/05 part A; docs/research/02 target F21).
#include "VoiceTestSupport.h"

#include "acid/common/Rng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
double db (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1e-12)); }

std::vector<SeqStep> pattern()
{
    const auto n = [] (int note, bool accent = false, bool slide = false) { return SeqStep { note, accent, slide, false, false }; };
    return { n (36, true), n (36), n (48, false, true), n (39, true), n (36), n (31), n (36, true), n (36, true),
             n (43, true), n (48, true), n (36, false, true), n (36, false, true), n (34), n (36), n (60, true, true), n (36) };
}

std::vector<float> renderPattern (const dsp::VoiceParams& p, int bars = 1)
{
    dsp::Voice v;
    start (v, p, 48000.0, 4);
    return render (v, makeEvents (pattern(), 125.0, 48000.0, bars), static_cast<long long> (bars * 1.92 * 48000.0 + 4800), 256);
}

void randomiseMods (dsp::VoiceParams& p, Rng& rng)
{
    p.modOverdrive = 100.0 * rng.nextDouble();
    p.modFilterFm = 100.0 * rng.nextDouble();
    p.modTracking = 100.0 * rng.nextDouble();
    p.modAmpDecay = expMap (rng.nextDouble(), 7.0, 1500.0);
    p.modAmpSustain = 100.0 * rng.nextDouble();
    p.modSoftAttack = expMap (rng.nextDouble(), 0.3, 30.0);
    p.modHiRes = rng.chance (0.5);
    p.modSweepSpeed = rng.nextInt (3);
    p.modMuffler = rng.nextInt (3);
    p.modExtRange = rng.chance (0.5);
    p.modSubLevel = 100.0 * rng.nextDouble();
    p.modSubOct = rng.nextInt (2);
}
}

TEST_CASE ("mods are inert when the master switch is off: sample-identical to stock", "[voice][mods]")
{
    dsp::VoiceParams stock;
    stock.resonance = 90.0;
    const auto reference = renderPattern (stock);
    REQUIRE (peakAbs (reference) > 0.1);

    Rng rng (2024);
    for (int trial = 0; trial < 6; ++trial)
    {
        dsp::VoiceParams p = stock;
        randomiseMods (p, rng);
        p.modOn = false;
        const auto audio = renderPattern (p);
        INFO ("trial " << trial);
        REQUIRE (audio.size() == reference.size());
        CHECK (std::memcmp (audio.data(), reference.data(), audio.size() * sizeof (float)) == 0);
    }

    // Changing the mod knobs block by block while the switch is off changes nothing either.
    {
        dsp::Voice v;
        start (v, stock, 48000.0, 4);
        const auto events = makeEvents (pattern(), 125.0, 48000.0, 1);
        std::vector<float> audio (reference.size());
        std::size_t next = 0;
        for (std::size_t pos = 0; pos < audio.size();)
        {
            dsp::VoiceParams p = stock;
            randomiseMods (p, rng);
            p.modOn = false;
            v.setParams (p);
            const std::size_t blockEnd = std::min (pos + 256, audio.size());
            while (pos < blockEnd)
            {
                while (next < events.size() && events[next].sample <= static_cast<long long> (pos))
                {
                    const SeqEvent& e = events[next++];
                    if (e.on) v.noteOn (e.note, e.accent, e.legato, e.vibrato);
                    else      v.noteOff();
                }
                const std::size_t until = (next < events.size() && events[next].sample < static_cast<long long> (blockEnd))
                                              ? static_cast<std::size_t> (events[next].sample) : blockEnd;
                v.process (audio.data() + pos, static_cast<int> (until - pos));
                pos = until;
            }
        }
        CHECK (std::memcmp (audio.data(), reference.data(), audio.size() * sizeof (float)) == 0);
    }

    // The switch on with every mod control at its default is the stock sound too.
    dsp::VoiceParams on = stock;
    on.modOn = true;
    const auto audio = renderPattern (on);
    CHECK (std::memcmp (audio.data(), reference.data(), audio.size() * sizeof (float)) == 0);

    // And each mod does change the sound once the switch is on.
    const auto differs = [&] (const dsp::VoiceParams& p)
    {
        const auto a = renderPattern (p);
        double diff = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) diff = std::max (diff, static_cast<double> (std::abs (a[i] - reference[i])));
        return diff;
    };
    dsp::VoiceParams p = on; p.modOverdrive = 60.0;    CHECK (differs (p) > 0.01);
    p = on; p.modFilterFm = 80.0;                      CHECK (differs (p) > 0.01);
    p = on; p.modTracking = 50.0;                      CHECK (differs (p) > 0.01);
    p = on; p.modAmpDecay = 30.0;                      CHECK (differs (p) > 0.01);
    p = on; p.modAmpSustain = 80.0;                    CHECK (differs (p) > 0.001);
    p = on; p.modSoftAttack = 20.0;                    CHECK (differs (p) > 0.01);
    p = on; p.modHiRes = true;                         CHECK (differs (p) > 0.01);
    p = on; p.modSweepSpeed = 1;                       CHECK (differs (p) > 0.01);
    p = on; p.modSweepSpeed = 2;                       CHECK (differs (p) > 0.01);
    p = on; p.modMuffler = 1;                          CHECK (differs (p) > 0.01);
    p = on; p.modMuffler = 2;                          CHECK (differs (p) > 0.01);
    p = on; p.modExtRange = true;                      CHECK (differs (p) > 0.01);
    p = on; p.modSubLevel = 50.0;                      CHECK (differs (p) > 0.01);
}

TEST_CASE ("overdrive quenches the resonance (02 F21)", "[voice][mods]")
{
    // Peak-to-plateau ratio of the harmonic spectrum at full resonance, nominal drive
    // against 36 dB more.
    const auto prominence = [] (double overdrivePercent)
    {
        auto p = quietParams();
        p.cutoff = 90.0;
        p.resonance = 100.0;
        p.modOn = true;
        p.modAmpSustain = 100.0;
        p.modOverdrive = overdrivePercent;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        v.noteOn (24, false, false, false);
        skip (v, 2.5);
        Trace t;
        run (v, t, 1.0);
        const double f0 = midiNoteToHz (24), fRes = t.taps.back().cutoffHz;
        double peak = 0.0, plateau = 0.0;
        int count = 0;
        for (int k = 1; k * f0 < 2.0 * fRes; ++k)
        {
            const double a = std::abs (hannBin (t.audio, 0, t.size(), k * f0, 48000.0));
            if (k * f0 > 0.7 * fRes) peak = std::max (peak, a);
            else if (k * f0 > 0.15 * fRes && k * f0 < 0.4 * fRes) { plateau += a * a; ++count; }
        }
        return db (peak / std::sqrt (plateau / count));
    };
    const double nominal = prominence (0.0);
    const double driven = prominence (100.0 * std::log (63.1) / std::log (66.6));      // +36 dB
    std::printf ("[overdrive] resonant peak over the plateau: %.1f dB at stock drive, %.1f dB with 36 dB of overdrive\n", nominal, driven);
    CHECK (nominal > 3.0);
    CHECK (nominal - driven >= 15.0);
}

TEST_CASE ("filter tracking is linear in Hz and pivots at C2", "[voice][mods]")
{
    const auto nominal = [] (double tracking, int note, bool modsOn = true)
    {
        auto p = quietParams();
        p.modOn = modsOn;
        p.modTracking = tracking;
        dsp::Voice v;
        start (v, p, 48000.0, 1);
        v.noteOn (note, false, false, false);
        skip (v, 0.01);
        return v.debugTaps().nominalCutoffHz;
    };
    const double base = nominal (0.0, 36);
    CHECK (nominal (100.0, 36) == Approx (base).epsilon (1e-12));               // the pivot
    CHECK (nominal (100.0, 48) - base == Approx (2700.0).epsilon (1e-9));       // one volt up
    CHECK (nominal (100.0, 60) - base == Approx (5400.0).epsilon (1e-9));       // two volts: linear in Hz
    CHECK (nominal (50.0, 48) - base == Approx (1350.0).epsilon (1e-9));
    CHECK (nominal (20.0, 24) < base);                                          // low notes close the filter
    CHECK (nominal (100.0, 24) == Approx (5.0));                                // all the way: the note is gated out
    CHECK (nominal (100.0, 60, false) == Approx (base).epsilon (1e-12));        // stock: no tracking at all

    // The tracking follows the slid CV, so a slide drags the cutoff along.
    auto p = quietParams();
    p.modOn = true; p.modTracking = 100.0;
    dsp::Voice v;
    start (v, p, 48000.0, 1);
    v.noteOn (36, false, false, false);
    skip (v, 0.05);
    v.noteOn (48, false, true, false);
    Trace t;
    run (v, t, 0.1);
    CHECK (t.taps[t.at (0.01)].nominalCutoffHz > base + 500.0);
    CHECK (t.taps[t.at (0.01)].nominalCutoffHz < base + 1500.0);
}

TEST_CASE ("amp decay, sustain and soft attack", "[voice][mods]")
{
    // Amp decay: the amplitude envelope's time constant.
    auto p = quietParams();
    p.modOn = true;
    p.modAmpDecay = 100.0;
    dsp::Voice v;
    start (v, p, 96000.0, 1);
    Trace t;
    v.noteOn (36, false, false, false);
    run (v, t, 0.5);
    CHECK (t.taps[t.at (0.1)].ampEnvelope == Approx (1.118 * std::exp (-1.0) - 0.118).margin (1e-3));
    const std::size_t silent = firstIndex (t, t.at (0.02), [] (const dsp::VoiceDebugTaps& d, float) { return d.gateCurrent == 0.0; });
    CHECK (t.seconds (silent) == Approx (0.1 * std::log (1.118 / 0.118)).margin (0.002));

    // Sustain: the note holds at that level indefinitely.
    p.modAmpDecay = 1500.0;
    p.modAmpSustain = 50.0;
    start (v, p, 48000.0, 1);
    v.noteOn (36, false, false, false);
    skip (v, 12.0);
    CHECK (v.debugTaps().gateCurrent == Approx (-0.118 + 1.118 * 0.5).margin (1e-3));
    p.modAmpSustain = 100.0;
    start (v, p, 48000.0, 1);
    v.noteOn (36, false, false, false);
    skip (v, 5.0);
    CHECK (v.debugTaps().gateCurrent == Approx (1.0).margin (1e-6));
    v.noteOff();
    skip (v, 0.05);
    CHECK (v.debugTaps().gateCurrent == 0.0);

    // Soft attack: the gate's 10 to 90 % rise time, on plain notes; accents still snap.
    for (double ms : { 0.3, 3.0, 30.0 })
    {
        p = quietParams();
        p.modOn = true;
        p.modSoftAttack = ms;
        start (v, p, 96000.0, 1);
        Trace r;
        v.noteOn (36, false, false, false);
        run (v, r, 0.1);
        const std::size_t i10 = firstIndex (r, 0, [] (const dsp::VoiceDebugTaps& d, float) { return d.gateCurrent >= 0.1; });
        const std::size_t i90 = firstIndex (r, 0, [] (const dsp::VoiceDebugTaps& d, float) { return d.gateCurrent >= 0.9; });
        INFO ("soft attack " << ms);
        CHECK (r.seconds (i90 - i10) == Approx (ms * 1e-3).epsilon (0.12));
    }
    p.modSoftAttack = 30.0;
    p.accent = 100.0;
    start (v, p, 96000.0, 1);
    Trace a;
    v.noteOn (36, true, false, false);
    run (v, a, 0.01);
    CHECK (a.taps[a.at (0.003)].vcaGain > 2.0);
}

TEST_CASE ("high resonance doubles the feedback and stays stable", "[voice][mods]")
{
    auto p = quietParams();
    p.resonance = 100.0;
    p.cutoff = 80.0;
    p.modOn = true;
    p.modHiRes = true;
    dsp::Voice v;
    start (v, p, 48000.0, 4);
    v.noteOn (36, true, false, false);
    Trace t;
    run (v, t, 0.06);
    v.noteOff();
    run (v, t, 3.0);
    CHECK (t.taps.back().resonance == Approx (2.0).epsilon (1e-9));
    CHECK (allFinite (t.audio));
    CHECK (peakAbs (t.audio) < 4.0);
    CHECK (v.debugFilter().stats().stateResets == 0);

    // The filter now sings on its own: with the oscillator a low C and the cutoff high,
    // the strongest component of the filter's own output sits at the resonant frequency.
    std::vector<float> filterOut;
    p.modAmpSustain = 100.0;
    start (v, p, 48000.0, 4);
    v.noteOn (24, false, false, false);
    skip (v, 2.5);
    for (int i = 0; i < 48000; ++i)
    {
        float s;
        v.process (&s, 1);
        filterOut.push_back (static_cast<float> (v.debugFilter().output()));
    }
    const double fRes = v.debugTaps().cutoffHz;
    const double strongest = peakFrequency (filterOut, 0, filterOut.size(), 48000.0, 200.0, 6000.0, 400);
    std::printf ("[hires] strongest component of the filter output %.0f Hz, resonant frequency %.0f Hz\n", strongest, fRes);
    // Driven this far past the threshold the loop oscillates a little above its
    // small-signal resonant frequency.
    CHECK (strongest > 0.95 * fRes);
    CHECK (strongest < 1.2 * fRes);
    CHECK (v.debugFilter().stats().stateResets == 0);
}

TEST_CASE ("accent sweep speed: fast puts the first accent on top, slow climbs further and cools slowly", "[voice][mods]")
{
    const auto peaks = [] (int speed, std::vector<double>* afterglow = nullptr)
    {
        auto p = quietParams();
        p.resonance = 100.0; p.accent = 100.0;
        p.modOn = true; p.modSweepSpeed = speed;
        dsp::Voice v;
        start (v, p, 48000.0, 1);
        std::vector<SeqStep> pat;
        for (int i = 0; i < 12; ++i) pat.push_back (SeqStep { 36, i < 8, false, false, false });
        const Trace t = renderTraced (v, makeEvents (pat, 125.0, 48000.0, 1), static_cast<long long> (1.6 * 48000));
        std::vector<double> pk;
        for (int k = 0; k < 8; ++k)
        {
            double m = 0.0;
            for (std::size_t i = t.at (k * 0.12); i < t.at ((k + 1) * 0.12); ++i) m = std::max (m, t.taps[i].accentOctaves);
            pk.push_back (m);
        }
        if (afterglow != nullptr)
            for (double s : { 1.2, 1.5 }) afterglow->push_back (t.taps[t.at (s)].accentOctaves);
        return pk;
    };
    std::vector<double> glowNormal, glowSlow;
    const auto normal = peaks (0, &glowNormal), fast = peaks (1), slow = peaks (2, &glowSlow);
    std::printf ("[sweep speed] normal %.2f %.2f %.2f .. %.2f | fast %.2f %.2f %.2f .. %.2f | slow %.2f %.2f %.2f .. %.2f\n",
                 normal[0], normal[1], normal[2], normal[7], fast[0], fast[1], fast[2], fast[7], slow[0], slow[1], slow[2], slow[7]);
    // Fast: the first accent is the strongest, later ones smaller.
    CHECK (fast[0] > 1.5 * normal[0]);
    CHECK (fast[1] < 0.8 * fast[0]);
    CHECK (fast[7] < 0.8 * fast[0]);
    // Slow: starts lower, keeps climbing for longer, ends higher, and cools down slowly.
    CHECK (slow[0] < normal[0]);
    CHECK (slow[7] > 1.1 * normal[7]);
    CHECK (slow[4] > slow[3]);
    CHECK (slow[7] > slow[6]);
    CHECK (normal[7] == Approx (normal[6]).epsilon (0.002));
    CHECK (glowSlow[1] > 4.0 * glowNormal[1]);
    CHECK (glowSlow[1] > 0.3);
}

TEST_CASE ("muffler softens loud notes and leaves ordinary ones and the bass alone", "[voice][mods]")
{
    const auto notePeak = [] (int muffler, bool accent, double accentKnob)
    {
        auto p = quietParams();
        p.accent = accentKnob;
        p.modOn = true;
        p.modMuffler = muffler;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        skip (v, 0.2);
        const std::vector<SeqStep> pat (8, SeqStep { 36, accent, false, false, false });
        return peakAbs (render (v, makeEvents (pat, 125.0, 48000.0, 1), 48000, 256));
    };
    const double plainOff = notePeak (0, false, 70.0), plainSoft = notePeak (1, false, 70.0), plainHard = notePeak (2, false, 70.0);
    const double loudOff = notePeak (0, true, 100.0), loudSoft = notePeak (1, true, 100.0), loudHard = notePeak (2, true, 100.0);
    std::printf ("[muffler] plain note %.1f / %.1f / %.1f dBFS, full accent %.1f / %.1f / %.1f dBFS (off / soft / hard)\n",
                 db (plainOff), db (plainSoft), db (plainHard), db (loudOff), db (loudSoft), db (loudHard));
    CHECK (db (plainSoft) == Approx (db (plainOff)).margin (2.0));
    CHECK (db (loudSoft) < db (loudOff) - 3.0);
    CHECK (db (loudHard) < db (loudSoft) + 0.2);
    CHECK (db (loudHard) > db (plainOff) - 3.0);        // it clips, it does not mute

    // Bass passes: the fundamental of a held low note is unchanged within 1 dB.
    const auto fundamental = [] (int muffler)
    {
        auto p = quietParams();
        p.modOn = true; p.modMuffler = muffler; p.modAmpSustain = 100.0; p.accent = 100.0; p.accVolCouple = true;
        dsp::Voice v;
        start (v, p, 48000.0, 4);
        v.noteOn (24, false, false, false);
        skip (v, 2.0);
        Trace t;
        run (v, t, 1.0);
        return db (std::abs (hannBin (t.audio, 0, t.size(), midiNoteToHz (24), 48000.0)));
    };
    CHECK (fundamental (2) == Approx (fundamental (0)).margin (1.0));
}

TEST_CASE ("extended ranges and the sub oscillator", "[voice][mods]")
{
    // Extended cutoff: up to 5 kHz nominal, down to 100 Hz; Env Mod from true zero to three times the depth.
    auto p = quietParams();
    p.modOn = true; p.modExtRange = true;
    p.cutoff = 100.0; p.envMod = 0.0;
    dsp::Voice v;
    start (v, p, 48000.0, 1);
    Trace t;
    v.noteOn (36, false, false, false);
    run (v, t, 0.05);
    CHECK (t.taps.back().nominalCutoffHz == Approx (5000.0).epsilon (1e-9));
    for (const auto& d : t.taps) REQUIRE (d.envOctaves == Approx (0.0).margin (1e-12));
    p.cutoff = 0.0; p.envMod = 100.0;
    start (v, p, 48000.0, 1);
    Trace u;
    v.noteOn (36, false, false, false);
    run (v, u, 0.05);
    CHECK (u.taps.back().nominalCutoffHz == Approx (100.0).epsilon (1e-9));
    double depth = 0.0;
    for (const auto& d : u.taps) depth = std::max (depth, d.envOctaves);
    CHECK (depth == Approx (1.69 * 0.88 * 6.7).epsilon (0.01));

    // Sub oscillator: a component one octave below (or two), through the filter.
    const auto component = [] (double level, int oct, double hz)
    {
        auto q = quietParams();
        q.cutoff = 70.0; q.resonance = 20.0;
        q.modOn = true; q.modAmpSustain = 100.0; q.modSubLevel = level; q.modSubOct = oct;
        dsp::Voice w;
        start (w, q, 48000.0, 4);
        w.noteOn (48, false, false, false);
        skip (w, 2.0);
        Trace r;
        run (w, r, 1.0);
        return db (std::abs (hannBin (r.audio, 0, r.size(), hz, 48000.0)));
    };
    const double f = midiNoteToHz (48);
    CHECK (component (0.0, 0, 0.5 * f) < component (0.0, 0, f) - 50.0);
    CHECK (component (100.0, 0, 0.5 * f) > component (100.0, 0, f) - 12.0);
    CHECK (component (100.0, 1, 0.25 * f) > component (100.0, 1, f) - 20.0);
    CHECK (component (100.0, 1, 0.5 * f) < component (100.0, 0, 0.5 * f) - 20.0);
}

TEST_CASE ("filter FM follows the amplifier output and stays bounded", "[voice][mods]")
{
    auto p = quietParams();
    p.accent = 100.0; p.resonance = 90.0;
    p.modOn = true; p.modFilterFm = 100.0; p.modOverdrive = 50.0;
    dsp::Voice v;
    start (v, p, 48000.0, 4);
    const auto audio = render (v, makeEvents (pattern(), 125.0, 48000.0, 2), 4 * 48000, 256);
    CHECK (allFinite (audio));
    CHECK (peakAbs (audio) < 8.0);
    CHECK (peakAbs (audio) > 0.1);
    CHECK (v.debugFilter().stats().stateResets == 0);
    // Silent input to the FM path (no note) leaves the filter where the knobs put it.
    start (v, p, 48000.0, 4);
    std::vector<float> quiet (4800);
    v.process (quiet.data(), 4800);
    CHECK (peakAbs (quiet) < 1e-6);
}
