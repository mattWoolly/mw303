// Renders for listening and data for tools/analysis: a canonical 16-step pattern with saw
// and with square, a slow cutoff sweep, and CSV dumps of the control signals, the accent
// curves, the raw oscillator and the filter's response.
// Everything lands in the directory named by ACID_TEST_OUTPUT_DIR.
#include "VoiceTestSupport.h"
#include "Wav.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace acid;
using namespace acid::test;
using Catch::Approx;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kBpm = 125.0;

/** Sixteen steps in C: accents, slides, octave jumps, a rest and a run of four accents. */
std::vector<SeqStep> acidPattern()
{
    const auto n = [] (int note, bool accent = false, bool slide = false) { return SeqStep { note, accent, slide, false, false }; };
    SeqStep rest;
    rest.rest = true;
    return {
        n (36, true),           //  1  C2, accented
        n (36),                 //  2
        n (48, false, true),    //  3  C3, slides
        n (39, true),           //  4  into an accented D#2
        n (36),                 //  5
        rest,                   //  6
        n (36, true),           //  7  four accents in a row
        n (36, true),           //  8
        n (43, true),           //  9
        n (48, true),           // 10
        n (36, false, true),    // 11  slide into the same pitch
        n (36, false, true),    // 12  and on down
        n (34),                 // 13
        n (36),                 // 14
        n (60, true, true),     // 15  two octaves up, accented, slides
        n (36)                  // 16  back down
    };
}

dsp::VoiceParams patternParams (int wave)
{
    dsp::VoiceParams p;
    p.wave = wave;
    p.cutoff = 30.0;
    p.resonance = 85.0;
    p.envMod = 60.0;
    p.decay = 45.0;
    p.accent = 85.0;
    return p;
}

bool saveMono (const std::string& name, const std::vector<float>& audio, double fs)
{
    return writeWav (outputPath (name), audio, 1, fs);
}
}

TEST_CASE ("render: the canonical pattern, saw and square, 8 bars at 125 BPM", "[voice][render]")
{
    const double barSeconds = 16.0 * stepSeconds (kBpm);
    const auto total = static_cast<long long> ((8.0 * barSeconds + 1.0) * kFs);

    for (int wave : { 0, 1 })
    {
        dsp::Voice v;
        start (v, patternParams (wave), kFs, 4);
        const auto audio = render (v, makeEvents (acidPattern(), kBpm, kFs, 8), total, 256);
        const char* name = wave == 0 ? "acid_pattern_saw.wav" : "acid_pattern_square.wav";
        REQUIRE (saveMono (name, audio, kFs));

        const double pk = peakAbs (audio);
        std::printf ("[render] %s: peak %.3f (%.1f dBFS), rms %.4f\n", name, pk, 20.0 * std::log10 (pk), rms (audio, 0, audio.size()));
        CHECK (allFinite (audio));
        CHECK (pk > 0.2);
        CHECK (pk < 2.0);
        // The rest on step 6 is silent, and so is the tail after the last bar.
        const auto restStart = static_cast<std::size_t> ((5.0 * stepSeconds (kBpm) + 0.03) * kFs);
        CHECK (rms (audio, restStart, restStart + 2400) < 0.02 * rms (audio, 0, restStart));
        CHECK (peakAbs (audio, audio.size() - 12000) < 1.0e-3);
        CHECK (v.debugFilter().stats().stateResets == 0);

        WavData back;
        REQUIRE (readWav (outputPath (name), back));
        CHECK (back.interleaved.size() == audio.size());
        CHECK (back.sampleRate == kFs);
    }
}

TEST_CASE ("render: slow cutoff sweep over the pattern", "[voice][render]")
{
    const double barSeconds = 16.0 * stepSeconds (kBpm);
    const auto total = static_cast<long long> (8.0 * barSeconds * kFs);
    const auto events = makeEvents (acidPattern(), kBpm, kFs, 8);

    dsp::Voice v;
    dsp::VoiceParams p = patternParams (0);
    p.resonance = 90.0;
    p.envMod = 35.0;
    p.cutoff = 0.0;
    start (v, p, kFs, 4);

    std::vector<float> audio (static_cast<std::size_t> (total));
    std::size_t next = 0;
    for (long long pos = 0; pos < total;)
    {
        // Cutoff rises over four bars and falls over the next four; one setParams per 64 samples.
        const double phase = static_cast<double> (pos) / static_cast<double> (total);
        p.cutoff = 100.0 * (phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase);
        v.setParams (p);
        const long long blockEnd = std::min<long long> (pos + 64, total);
        while (pos < blockEnd)
        {
            while (next < events.size() && events[next].sample <= pos)
            {
                const SeqEvent& e = events[next++];
                if (e.on) v.noteOn (e.note, e.accent, e.legato, e.vibrato);
                else      v.noteOff();
            }
            const long long until = (next < events.size() && events[next].sample < blockEnd) ? events[next].sample : blockEnd;
            v.process (audio.data() + pos, static_cast<int> (until - pos));
            pos = until;
        }
    }
    REQUIRE (saveMono ("acid_cutoff_sweep.wav", audio, kFs));
    std::printf ("[render] acid_cutoff_sweep.wav: peak %.3f\n", peakAbs (audio));
    CHECK (allFinite (audio));
    CHECK (peakAbs (audio) < 2.5);
    // Brighter in the middle (cutoff open) than at the start (closed): energy above 2 kHz.
    const auto bright = [&] (std::size_t from)
    {
        double acc = 0.0, lp = 0.0;
        for (std::size_t i = from; i < from + 48000; ++i)
        {
            lp += 0.23 * (static_cast<double> (audio[i]) - lp);     // about 2 kHz
            const double hp = static_cast<double> (audio[i]) - lp;
            acc += hp * hp;
        }
        return acc;
    };
    CHECK (bright (audio.size() / 2 - 24000) > 4.0 * bright (0));
}

TEST_CASE ("dump: control signals, accent curves, oscillator and filter data for the plots", "[voice][render][dump]")
{
    // 1. Control path over a short phrase: plain, accented, slide, rest, accent run.
    {
        dsp::Voice v;
        dsp::VoiceParams p = patternParams (0);
        p.resonance = 100.0;
        p.accent = 100.0;
        p.calDrift = 0.0;
        start (v, p, kFs, 4);
        const Trace t = renderTraced (v, makeEvents (acidPattern(), kBpm, kFs, 1), static_cast<long long> (2.2 * kFs));
        std::vector<std::vector<double>> cols (11);
        for (std::size_t i = 0; i < t.size(); ++i)
        {
            const auto& d = t.taps[i];
            const double row[11] = { t.seconds (i), d.envelopeVolts, d.envOctaves, d.accentOctaves, d.accentCapVolts, d.cutoffHz,
                                     d.gateCurrent, d.accentCurrent, d.vcaGain, d.pitchVolts, static_cast<double> (t.audio[i]) };
            for (int c = 0; c < 11; ++c) cols[static_cast<std::size_t> (c)].push_back (row[c]);
        }
        REQUIRE (writeCsv (outputPath ("control_pattern.csv"),
                           { "t", "meg_volts", "env_oct", "accent_oct", "c13_volts", "cutoff_hz", "gate_current", "accent_current", "vca_gain", "pitch_volts", "audio" }, cols));
    }

    // 2. Accent sweep: single accent at five resonance settings, and a run of six accents
    //    then four plain notes at full resonance (voice at schematic scale: CV scale 1).
    {
        const double taps[5] = { 0.0, 25.0, 50.0, 75.0, 100.0 };
        std::vector<std::vector<double>> cols (7);
        for (int k = 0; k < 5; ++k)
        {
            dsp::Voice v;
            dsp::VoiceParams p = quietParams();
            p.accent = 100.0; p.resonance = taps[k]; p.calCvScale = 1.0;
            start (v, p, kFs, 1);
            Trace t;
            v.noteOn (36, true, false, false);
            run (v, t, 0.5);
            for (std::size_t i = 0; i < t.size(); ++i)
            {
                if (k == 0) cols[0].push_back (t.seconds (i));
                cols[static_cast<std::size_t> (k + 1)].push_back (t.taps[i].accentOctaves);
            }
        }
        {
            dsp::Voice v;
            dsp::VoiceParams p = quietParams();
            p.accent = 100.0; p.resonance = 100.0; p.calCvScale = 1.0;
            start (v, p, kFs, 1);
            Trace t;
            v.noteOn (36, true, false, false);
            run (v, t, 0.5);
            for (std::size_t i = 0; i < t.size(); ++i) cols[6].push_back (t.taps[i].accentCurrent * 47.7);
        }
        REQUIRE (writeCsv (outputPath ("accent_single.csv"), { "t", "r0", "r25", "r50", "r75", "r100", "vca_accent_ua" }, cols));

        std::vector<std::vector<double>> train (4);
        dsp::Voice v;
        dsp::VoiceParams p = quietParams();
        p.accent = 100.0; p.resonance = 100.0; p.calCvScale = 1.0;
        start (v, p, kFs, 1);
        std::vector<SeqStep> pattern;
        for (int i = 0; i < 10; ++i) pattern.push_back (SeqStep { 36, i < 6, false, false, false });
        const Trace t = renderTraced (v, makeEvents (pattern, kBpm, kFs, 1), static_cast<long long> (1.4 * kFs));
        for (std::size_t i = 0; i < t.size(); ++i)
        {
            train[0].push_back (t.seconds (i));
            train[1].push_back (t.taps[i].accentOctaves);
            train[2].push_back (t.taps[i].envOctaves);
            train[3].push_back (t.taps[i].vcaGain);
        }
        REQUIRE (writeCsv (outputPath ("accent_train.csv"), { "t", "accent_oct", "env_oct", "vca_gain" }, train));
    }

    // 3. Raw oscillator at the waveform switch, C2 and C4, 192 kHz.
    {
        std::vector<std::vector<double>> cols (7);
        const double pitches[2] = { 65.406, 261.626 };
        for (int k = 0; k < 2; ++k)
        {
            dsp::Oscillator osc;
            osc.prepare (192000.0);
            for (int i = 0; i < 192000; ++i) osc.tick (pitches[k]);
            for (int i = 0; i < 9600; ++i)
            {
                const auto o = osc.tick (pitches[k]);
                if (k == 0) cols[0].push_back (static_cast<double> (i) / 192000.0);
                cols[static_cast<std::size_t> (1 + 3 * k)].push_back (o.saw);
                cols[static_cast<std::size_t> (2 + 3 * k)].push_back (o.square);
                cols[static_cast<std::size_t> (3 + 3 * k)].push_back (o.sub);
            }
        }
        REQUIRE (writeCsv (outputPath ("oscillator.csv"), { "t", "saw_c2", "square_c2", "sub_c2", "saw_c4", "square_c4", "sub_c4" }, cols));
    }

    // 4. Voice output waveforms: saw and square at C2, filter wide open and at the default
    //    setting, held with the Mods sustain so the level is steady.
    {
        std::vector<std::vector<double>> cols (5);
        int column = 1;
        for (int wave : { 0, 1 })
            for (double cutoff : { 100.0, 35.0 })
            {
                dsp::Voice v;
                dsp::VoiceParams p = quietParams();
                p.wave = wave; p.cutoff = cutoff;
                p.resonance = cutoff == 100.0 ? 0.0 : 75.0;
                p.envMod = cutoff == 100.0 ? 100.0 : 45.0;
                p.modOn = true; p.modAmpSustain = 100.0;
                p.decay = 100.0;
                start (v, p, kFs, 4);
                v.noteOn (36, false, false, false);
                skip (v, cutoff == 100.0 ? 0.05 : 1.5);
                Trace t;
                run (v, t, 0.05);
                for (std::size_t i = 0; i < t.size(); ++i)
                {
                    if (column == 1) cols[0].push_back (t.seconds (i));
                    cols[static_cast<std::size_t> (column)].push_back (static_cast<double> (t.audio[i]));
                }
                ++column;
            }
        REQUIRE (writeCsv (outputPath ("voice_waveforms.csv"), { "t", "saw_open", "saw_default", "square_open", "square_default" }, cols));
    }

    // 5. Filter small-signal response at fRes 500 Hz and 2 kHz for five resonance settings,
    //    from the impulse response of the ladder at 192 kHz.
    {
        std::vector<double> freqs;
        for (int i = 0; i <= 240; ++i) freqs.push_back (10.0 * std::pow (2000.0, i / 240.0));
        std::vector<std::vector<double>> cols;
        std::vector<std::string> names { "hz" };
        cols.push_back (freqs);
        for (double fRes : { 500.0, 2000.0 })
            for (double r : { 0.0, 0.25, 0.5, 0.75, 1.0 })
            {
                dsp::DiodeLadder f;
                f.prepare (192000.0);
                const double wf = dsp::DiodeLadder::wiperFraction (r, 51.0e3);
                std::vector<float> ir (192000);
                for (std::size_t i = 0; i < ir.size(); ++i)
                    ir[i] = static_cast<float> (1.0e6 * f.process (i == 0 ? 1.0e-6 : 0.0, fRes, wf));
                std::vector<double> mag;
                for (double hz : freqs)
                {
                    std::complex<double> acc (0.0, 0.0);
                    const double w = kTwoPi * hz / 192000.0;
                    for (std::size_t i = 0; i < ir.size(); ++i)
                        acc += static_cast<double> (ir[i]) * std::polar (1.0, -w * static_cast<double> (i));
                    mag.push_back (20.0 * std::log10 (std::max (std::abs (acc), 1.0e-12)));
                }
                cols.push_back (mag);
                char name[64];
                std::snprintf (name, sizeof name, "f%.0f_r%.0f", fRes, r * 100.0);
                names.push_back (name);
            }
        REQUIRE (writeCsv (outputPath ("filter_response.csv"), names, cols));
    }
    SUCCEED();
}
