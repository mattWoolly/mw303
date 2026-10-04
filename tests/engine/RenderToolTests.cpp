// acid-render: two runs with the same arguments give identical files. Needs the acid-render
// target built in the same build tree; otherwise the test is skipped.
#include "Wav.h"
#include "acid/engine/StateCodec.h"
#include "acid/seq/MidiFile.h"
#include "acid/seq/PatternCodec.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <sys/wait.h>

namespace
{
bool readAll (const std::string& path, std::string& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    out.assign (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>());
    return true;
}
}

TEST_CASE ("acid-render: repeated runs produce identical WAV and MIDI files", "[engine][render-tool]")
{
    const std::string outDir = ACID_TEST_OUTPUT_DIR;
    const std::string tool = outDir + "/../tools/acid-render/acid-render";
    if (! std::ifstream (tool).good())
    {
        SKIP ("acid-render is not built in this tree: " << tool);
    }

    const std::string args = " --steps \"C C+:S D#:A . ~ G-:AS C . C:S C+ . A# ~ ~ . C:A\" --bpm 125 --bars 2 --rate 44100"
                             " --param dist_on=1 --param rev_on=1 --param delay_on=1 --quality normal --quiet";
    const std::string a = outDir + "/tool_a.wav", b = outDir + "/tool_b.wav";
    const std::string am = outDir + "/tool_a.mid", bm = outDir + "/tool_b.mid";
    REQUIRE (std::system ((tool + args + " --out " + a + " --midi-out " + am + " > /dev/null").c_str()) == 0);
    REQUIRE (std::system ((tool + args + " --out " + b + " --midi-out " + bm + " > /dev/null").c_str()) == 0);

    std::string wa, wb, ma, mb;
    REQUIRE (readAll (a, wa));
    REQUIRE (readAll (b, wb));
    REQUIRE (readAll (am, ma));
    REQUIRE (readAll (bm, mb));
    CHECK (wa == wb);
    CHECK (ma == mb);
    CHECK (ma.size() > 50);

    acid::test::WavData wav;
    REQUIRE (acid::test::readWav (a, wav));
    CHECK (wav.numChannels == 2);
    CHECK (wav.sampleRate == 44100.0);
    const std::size_t expectedFrames = static_cast<std::size_t> (2.0 * 4.0 * 60.0 / 125.0 * 44100.0 + 0.5);
    CHECK (wav.interleaved.size() == 2 * expectedFrames);
    float peak = 0.0f;
    for (const float s : wav.interleaved) peak = std::max (peak, std::abs (s));
    CHECK (peak > 0.05f);
    CHECK (peak < 2.0f);

    // Bad arguments fail cleanly, with exit code 2 (an argument error), never a crash or an abort.
    const auto exitCode = [] (int status) { return WIFEXITED (status) ? WEXITSTATUS (status) : -1; };
    CHECK (exitCode (std::system ((tool + " --out " + a + " --bpm 5 > /dev/null 2>&1").c_str())) == 2);
    CHECK (std::system ((tool + " --steps \"C D\" > /dev/null 2>&1").c_str()) != 0);
    CHECK (std::system ((tool + " --steps \"C D\" --param nope=1 --out " + a + " > /dev/null 2>&1").c_str()) != 0);
    CHECK (std::system ((tool + " --preset /nonexistent.mw12f --out " + a + " > /dev/null 2>&1").c_str()) != 0);
    // Numbers must be whole tokens, rates and block sizes whole numbers.
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --bpm 120abc > /dev/null 2>&1").c_str())) == 2);
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --block 3.7 > /dev/null 2>&1").c_str())) == 2);
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --rate 44100.5 > /dev/null 2>&1").c_str())) == 2);
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --bars 2x > /dev/null 2>&1").c_str())) == 2);
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --slot A1.5 > /dev/null 2>&1").c_str())) == 2);
    // A render beyond the 4 GiB WAV limit is refused before anything is allocated.
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --bars 10000 --bpm 10 --rate 384000 > /dev/null 2>&1").c_str())) == 2);
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --bars 100 --tail 600 --rate 384000 --bpm 10 > /dev/null 2>&1").c_str())) == 2);
    // An unknown flag (the header once advertised --seq-only) is an argument error.
    CHECK (exitCode (std::system ((tool + " --steps \"C D\" --out " + a + " --seq-only > /dev/null 2>&1").c_str())) == 2);
}

TEST_CASE ("acid-render: the slot comes from the preset unless --slot says otherwise, and empty slots warn", "[engine][render-tool]")
{
    const std::string outDir = ACID_TEST_OUTPUT_DIR;
    const std::string tool = outDir + "/../tools/acid-render/acid-render";
    if (! std::ifstream (tool).good())
    {
        SKIP ("acid-render is not built in this tree: " << tool);
    }

    // A scene whose pattern_bank and pattern_slot select B3, with a pattern only in B3.
    const std::string scene = outDir + "/tool_scene.mw12f";
    {
        acid::ParamValues values = acid::defaultParamValues();
        values[acid::kPatternBank] = 1.0f;
        values[acid::kPatternSlot] = 3.0f;
        auto bank = std::make_unique<acid::seq::PatternBank>();
        REQUIRE (acid::seq::fromStepText ("C D E F", bank->seq[acid::seq::slotIndex (1, 2)]));
        const acid::PresetData d = acid::makePreset (acid::PresetType::Scene, values, acid::Settings {}, bank.get());
        std::ofstream f (scene);
        f << acid::writePreset (d);
    }
    const std::string wav = outDir + "/tool_slot.wav", mid = outDir + "/tool_slot.mid", err = outDir + "/tool_slot.err";

    // Without --slot: B3 plays.
    REQUIRE (std::system ((tool + " --preset " + scene + " --bars 1 --out " + wav + " --midi-out " + mid + " > /dev/null 2> " + err).c_str()) == 0);
    std::string midi, stderrText;
    REQUIRE (readAll (mid, midi));
    REQUIRE (readAll (err, stderrText));
    const std::size_t withPresetSlot = midi.size();
    CHECK (stderrText.find ("warning") == std::string::npos);

    // With --slot A1 (empty): the tool warns and the render is silent.
    REQUIRE (std::system ((tool + " --preset " + scene + " --slot A1 --bars 1 --out " + wav + " --midi-out " + mid + " > /dev/null 2> " + err).c_str()) == 0);
    REQUIRE (readAll (mid, midi));
    REQUIRE (readAll (err, stderrText));
    CHECK (midi.size() < withPresetSlot);
    CHECK (stderrText.find ("warning: slot A1 has no note") != std::string::npos);

    // --param play_mode and seq_run are overridden, and say so.
    REQUIRE (std::system ((tool + " --steps \"C D\" --param play_mode=0 --param seq_run=0 --bars 1 --out " + wav + " > /dev/null 2> " + err).c_str()) == 0);
    REQUIRE (readAll (err, stderrText));
    CHECK (stderrText.find ("play_mode is set by acid-render") != std::string::npos);
    CHECK (stderrText.find ("seq_run is set by acid-render") != std::string::npos);
}

TEST_CASE ("acid-render: --mode arp plays the held notes, --mode ext plays the pattern as external MIDI", "[engine][render-tool]")
{
    const std::string outDir = ACID_TEST_OUTPUT_DIR;
    const std::string tool = outDir + "/../tools/acid-render/acid-render";
    if (! std::ifstream (tool).good())
    {
        SKIP ("acid-render is not built in this tree: " << tool);
    }
    const auto exitCode = [] (int status) { return WIFEXITED (status) ? WEXITSTATUS (status) : -1; };
    const auto readWav = [] (const std::string& path) {
        acid::test::WavData w;
        REQUIRE (acid::test::readWav (path, w));
        std::vector<float> left;
        for (std::size_t i = 0; i < w.interleaved.size(); i += 2) left.push_back (w.interleaved[i]);
        return left;
    };
    const auto rms = [] (const std::vector<float>& x) {
        double s = 0.0;
        for (const float v : x) s += static_cast<double> (v) * static_cast<double> (v);
        return std::sqrt (s / static_cast<double> (std::max<std::size_t> (1, x.size())));
    };

    // Arpeggiator: three held notes, an arp pattern of indices. The MIDI out carries only
    // pitches of the held notes (C3 E3 G3 = 48 52 55, an octave up on the "1+" step), and the
    // render is repeatable.
    {
        const std::string args = " --mode arp --hold \"C3,E3,G3\" --steps \"1 2 3 2:A 1+ 3:S 2 1\" --bars 2 --rate 44100 --quality normal --quiet";
        const std::string a = outDir + "/tool_arp_a.wav", b = outDir + "/tool_arp_b.wav", mid = outDir + "/tool_arp.mid";
        REQUIRE (std::system ((tool + args + " --out " + a + " --midi-out " + mid + " > /dev/null").c_str()) == 0);
        REQUIRE (std::system ((tool + args + " --out " + b + " > /dev/null").c_str()) == 0);
        std::string wa, wb, midi;
        REQUIRE (readAll (a, wa));
        REQUIRE (readAll (b, wb));
        REQUIRE (readAll (mid, midi));
        CHECK (wa == wb);
        CHECK (rms (readWav (a)) > 0.01);

        acid::seq::SmfFile smf;
        REQUIRE (acid::seq::parseSmf (std::vector<std::uint8_t> (midi.begin(), midi.end()), smf));
        int ons = 0;
        bool onlyHeld = true;
        for (const auto& track : smf.tracks)
            for (const auto& e : track.events)
                if ((e.status & 0xF0) == 0x90 && e.data2 > 0)
                {
                    ++ons;
                    const int n = e.data1;
                    onlyHeld = onlyHeld && (n == 48 || n == 52 || n == 55 || n == 60);
                }
        CHECK (ons >= 16);       // eight 16ths a bar, two bars
        CHECK (onlyHeld);

        // The same pattern with different held notes gives a different render.
        const std::string c = outDir + "/tool_arp_c.wav";
        REQUIRE (std::system ((tool + " --mode arp --hold \"36,43\" --steps \"1 2 3 2:A 1+ 3:S 2 1\" --bars 2 --rate 44100 --quality normal --quiet --out " + c + " > /dev/null").c_str()) == 0);
        std::string wc;
        REQUIRE (readAll (c, wc));
        CHECK (wc != wa);
    }

    // External: the pattern's notes sent as MIDI play the same line as the sequencer does
    // (the same pitches, accents, slides and gates, so the audio all but matches).
    {
        const std::string steps = " --steps \"C C+:S D#:A . ~ G-:AS C . C:S C+ . A# ~ ~ . C:A\" --bars 2 --rate 44100 --quality normal --quiet";
        const std::string s = outDir + "/tool_seq.wav", e = outDir + "/tool_ext.wav";
        REQUIRE (std::system ((tool + steps + " --out " + s + " > /dev/null").c_str()) == 0);
        REQUIRE (std::system ((tool + steps + " --mode ext --out " + e + " > /dev/null").c_str()) == 0);
        const auto x = readWav (s), y = readWav (e);
        REQUIRE (x.size() == y.size());
        double xy = 0.0, xx = 0.0, yy = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            xy += static_cast<double> (x[i]) * static_cast<double> (y[i]);
            xx += static_cast<double> (x[i]) * static_cast<double> (x[i]);
            yy += static_cast<double> (y[i]) * static_cast<double> (y[i]);
        }
        const double correlation = xy / std::sqrt (xx * yy);
        INFO ("correlation " << correlation << ", rms " << rms (x) << " / " << rms (y));
        CHECK (correlation > 0.99);
        CHECK (std::abs (20.0 * std::log10 (rms (y) / rms (x))) < 0.5);
    }

    // Argument errors.
    const std::string a = outDir + "/tool_arp_err.wav";
    CHECK (exitCode (std::system ((tool + " --mode arp --out " + a + " > /dev/null 2>&1").c_str())) == 2);              // no --hold
    CHECK (exitCode (std::system ((tool + " --hold C3 --out " + a + " > /dev/null 2>&1").c_str())) == 2);              // --hold without arp
    CHECK (exitCode (std::system ((tool + " --mode arp --hold H3 --out " + a + " > /dev/null 2>&1").c_str())) == 2);   // not a note
    CHECK (exitCode (std::system ((tool + " --mode arp --hold 1,2,3,4,5,6,7,8,9,10,11 --out " + a + " > /dev/null 2>&1").c_str())) == 2);
    CHECK (exitCode (std::system ((tool + " --mode arp --hold 128 --out " + a + " > /dev/null 2>&1").c_str())) == 2);
    CHECK (exitCode (std::system ((tool + " --mode poly --out " + a + " > /dev/null 2>&1").c_str())) == 2);
}
