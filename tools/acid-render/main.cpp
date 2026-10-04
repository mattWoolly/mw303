// acid-render: renders a pattern through the whole engine to a WAV file (SPEC 6.5).
//
//   acid-render [--preset a.mw12f ...] [--steps "<compact step notation>"] [--param id=value ...]
//               [--slot A1] [--mode seq|arp|ext] [--hold "C3,E3,G3"] [--bpm 125] [--bars 8]
//               [--rate 48000] [--quality draft|normal|high|ultra] [--block 512] [--tail 0]
//               [--quiet] --out x.wav [--midi-out x.mid]
//
// Modes (--mode, default seq):
//   seq   the selected slot's sequencer pattern in Sequencer mode, Run on.
//   arp   the selected slot's arpeggiator pattern in Arpeggiator mode, with the --hold notes
//         (required, 1 to 10, comma separated) held from the first sample to the end of the
//         song. --steps then edits the arpeggiator pattern (indices 1..10 for the held
//         notes: "1 2 3 2:A"), and an arp preset lands in the arp slot as in every mode.
//   ext   the selected slot's sequencer pattern played as external MIDI in External mode:
//         the notes the pattern exports to a MIDI file (slides as overlaps, ties merged,
//         accents as velocity 127 and plain notes at the accent threshold, vibrato as CC1),
//         looped for the song, with every note ended at its end. The .mid file holds what the
//         engine sends, which in External mode is only MIDI thru (midi_thru in a scene).
// Note names are C-1 = 0, so C2 is MIDI 36 (the voice's reference note) and C3 is 48; a
// sharp is written C#3 and plain numbers 0..127 are accepted too.
//
// Presets are applied in order: parameter types (scene, synth, calibration, the effect
// types) onto the parameters, settings and bank; pattern and arp types into the selected
// slot; template presets are read but have nothing to act on here. --steps replaces the
// steps of the selected slot's pattern (its header fields are kept unless the text carries a
// header). --param overrides single parameters by their table id. The render plays the
// selected slot (see Modes) under a host transport that rolls from
// bar 1 at the given tempo in 4/4, for the given number of bars plus --tail seconds of
// release with the transport stopped.
//
// The selected slot is --slot when given, otherwise the pattern_bank and pattern_slot
// parameters as the presets and --param leave them (A1 by default). play_mode and seq_run
// are always set by the tool (from --mode); giving them with --param earns a warning, as
// does rendering a slot that has no note in it.
//
// Numbers on the command line must be whole tokens ("120abc" is an error); --rate and
// --block are integers. A render that would not fit the 4 GiB WAV limit is refused up front.
//
// Output is deterministic: the same arguments give the same bytes on every run.
#include "Wav.h"
#include "acid/engine/Engine.h"
#include "acid/engine/StateCodec.h"
#include "acid/fx/DistortionVoicings.h"
#include "acid/seq/MidiFile.h"
#include "acid/seq/PatternCodec.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

using namespace acid;

namespace
{
/** The RIFF size fields are 32 bits: the data chunk plus the 44-byte header must fit. */
constexpr std::uint64_t kMaxWavDataBytes = 0xFFFFFFFFull - 44ull;

struct Options
{
    std::vector<std::string> presets;
    std::string steps;
    std::vector<std::pair<std::string, std::string>> params;
    bool haveSlot = false;
    int bank = 0;
    int slot = 0;
    double bpm = 120.0;
    double bars = 4.0;
    long long rate = 48000;
    int block = 512;
    double tailSeconds = 0.0;
    bool haveQuality = false;
    Quality quality = Quality::High;
    std::string out;
    std::string midiOut;
    bool quiet = false;
    std::string mode = "seq";       // seq, arp or ext
    std::vector<int> hold;          // arp: the held MIDI notes
};

void usage()
{
    std::fprintf (stderr,
                  "usage: acid-render [--preset FILE ...] [--steps \"C C+:S D#:A . ~ ...\"] [--param id=value ...]\n"
                  "                   [--slot A1] [--mode seq|arp|ext] [--hold \"C3,E3,G3\"] [--bpm 125] [--bars 8]\n"
                  "                   [--rate 48000] [--quality draft|normal|high|ultra] [--block 512] [--tail SECONDS]\n"
                  "                   [--quiet] --out FILE.wav [--midi-out FILE.mid]\n");
}

/** A whole-token decimal number: nothing before, nothing after. */
bool parseNumber (const std::string& text, double& out)
{
    if (text.empty()) return false;
    const char* begin = text.c_str();
    const char* end = begin + text.size();
    if (*begin == '+') ++begin;
    double d = 0.0;
    const auto r = std::from_chars (begin, end, d);
    return r.ec == std::errc {} && r.ptr == end && std::isfinite (d) && (out = d, true);
}

/** A whole-token integer. */
bool parseNumber (const std::string& text, long long& out)
{
    if (text.empty()) return false;
    const char* begin = text.c_str();
    const char* end = begin + text.size();
    if (*begin == '+') ++begin;
    long long v = 0;
    const auto r = std::from_chars (begin, end, v);
    return r.ec == std::errc {} && r.ptr == end && (out = v, true);
}

bool parseNumber (const std::string& text, int& out)
{
    long long v = 0;
    if (! parseNumber (text, v) || v < -2147483647LL || v > 2147483647LL) return false;
    out = static_cast<int> (v);
    return true;
}

bool parseSlot (const std::string& text, int& bank, int& slot)
{
    if (text.size() < 2) return false;
    const char b = static_cast<char> (std::toupper (static_cast<unsigned char> (text[0])));
    if (b < 'A' || b > 'D') return false;
    int n = 0;
    if (! parseNumber (text.substr (1), n) || n < 1 || n > seq::kSlotsPerBank) return false;
    bank = b - 'A';
    slot = n - 1;
    return true;
}

/** A MIDI note: a number 0..127, or a name with C-1 = 0 ("C3" is 48, "F#2" 42, "Bb1" 34). */
bool parseNote (std::string text, int& note)
{
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.front()))) text.erase (text.begin());
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.back()))) text.pop_back();
    if (text.empty()) return false;
    if (std::isdigit (static_cast<unsigned char> (text[0])))
        return parseNumber (text, note) && note >= 0 && note <= 127;

    static constexpr int kPitch[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const char letter = static_cast<char> (std::toupper (static_cast<unsigned char> (text[0])));
    if (letter < 'A' || letter > 'G') return false;
    int pc = kPitch[letter - 'A'];
    std::size_t i = 1;
    if (i < text.size() && text[i] == '#') { ++pc; ++i; }
    else if (i < text.size() && text[i] == 'b') { --pc; ++i; }
    int octave = 0;
    if (! parseNumber (text.substr (i), octave)) return false;
    note = 12 * (octave + 1) + pc;
    return note >= 0 && note <= 127;
}

bool parseHold (const std::string& text, std::vector<int>& notes)
{
    notes.clear();
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t comma = text.find (',', start);
        const std::string item = text.substr (start, comma == std::string::npos ? std::string::npos : comma - start);
        int n = 0;
        if (! parseNote (item, n)) return false;
        notes.push_back (n);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return ! notes.empty() && notes.size() <= 10;
}

bool parseQuality (const std::string& text, Quality& q)
{
    if (text == "draft") q = Quality::Draft;
    else if (text == "normal") q = Quality::Normal;
    else if (text == "high") q = Quality::High;
    else if (text == "ultra") q = Quality::Ultra;
    else return false;
    return true;
}

bool parseArgs (int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] (std::string& v) {
            if (i + 1 >= argc) { std::fprintf (stderr, "%s needs a value\n", a.c_str()); return false; }
            v = argv[++i];
            return true;
        };
        const auto number = [&] (auto& target) {
            std::string v;
            if (! next (v)) return false;
            if (! parseNumber (v, target)) { std::fprintf (stderr, "%s: '%s' is not a %s\n", a.c_str(), v.c_str(),
                                                           std::is_floating_point_v<std::remove_reference_t<decltype (target)>> ? "number" : "whole number"); return false; }
            return true;
        };
        std::string v;

        if (a == "--preset") { if (! next (v)) return false; o.presets.push_back (v); }
        else if (a == "--steps") { if (! next (v)) return false; o.steps = v; }
        else if (a == "--param")
        {
            if (! next (v)) return false;
            const auto eq = v.find ('=');
            if (eq == std::string::npos) { std::fprintf (stderr, "--param wants id=value, got %s\n", v.c_str()); return false; }
            o.params.emplace_back (v.substr (0, eq), v.substr (eq + 1));
        }
        else if (a == "--slot")
        {
            if (! next (v)) return false;
            if (! parseSlot (v, o.bank, o.slot)) { std::fprintf (stderr, "bad slot %s (A1..D12)\n", v.c_str()); return false; }
            o.haveSlot = true;
        }
        else if (a == "--bpm") { if (! number (o.bpm)) return false; }
        else if (a == "--bars") { if (! number (o.bars)) return false; }
        else if (a == "--rate") { if (! number (o.rate)) return false; }
        else if (a == "--block") { if (! number (o.block)) return false; }
        else if (a == "--tail") { if (! number (o.tailSeconds)) return false; }
        else if (a == "--quality") { if (! next (v)) return false; if (! parseQuality (v, o.quality)) { std::fprintf (stderr, "bad quality %s\n", v.c_str()); return false; } o.haveQuality = true; }
        else if (a == "--mode")
        {
            if (! next (v)) return false;
            if (v != "seq" && v != "arp" && v != "ext") { std::fprintf (stderr, "bad mode %s (seq, arp or ext)\n", v.c_str()); return false; }
            o.mode = v;
        }
        else if (a == "--hold")
        {
            if (! next (v)) return false;
            if (! parseHold (v, o.hold)) { std::fprintf (stderr, "--hold wants 1 to 10 notes, comma separated (\"C3,E3,G3\" or \"48,52,55\"), got %s\n", v.c_str()); return false; }
        }
        else if (a == "--out") { if (! next (v)) return false; o.out = v; }
        else if (a == "--midi-out") { if (! next (v)) return false; o.midiOut = v; }
        else if (a == "--quiet") { o.quiet = true; }
        else if (a == "--help" || a == "-h") { usage(); return false; }
        else { std::fprintf (stderr, "unknown argument %s\n", a.c_str()); usage(); return false; }
    }

    if (o.out.empty()) { std::fprintf (stderr, "--out is required\n"); usage(); return false; }
    if (o.mode == "arp" && o.hold.empty()) { std::fprintf (stderr, "--mode arp needs --hold (the notes the arpeggiator plays)\n"); return false; }
    if (o.mode != "arp" && ! o.hold.empty()) { std::fprintf (stderr, "--hold is only for --mode arp\n"); return false; }
    if (! (o.bpm >= 10.0 && o.bpm <= 1000.0)) { std::fprintf (stderr, "--bpm must be 10..1000\n"); return false; }
    if (! (o.bars > 0.0 && o.bars <= 10000.0)) { std::fprintf (stderr, "--bars must be positive, at most 10000\n"); return false; }
    if (o.rate < 8000 || o.rate > 384000) { std::fprintf (stderr, "--rate must be a whole number of Hz, 8000..384000\n"); return false; }
    if (o.block < 1 || o.block > 65536) { std::fprintf (stderr, "--block must be 1..65536\n"); return false; }
    if (! (o.tailSeconds >= 0.0 && o.tailSeconds <= 600.0)) { std::fprintf (stderr, "--tail must be 0..600\n"); return false; }
    return true;
}

bool readFile (const std::string& path, std::string& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

double dbfs (double linear)
{
    return 20.0 * std::log10 (std::max (linear, 1.0e-12));
}

bool hasNote (const seq::Pattern& p)
{
    const int length = std::clamp<int> (p.length, 1, seq::kMaxSteps);
    for (int i = 0; i < length; ++i)
        if (p.steps[static_cast<std::size_t> (i)].state == seq::StepState::Note)
            return true;
    return false;
}

/** One sounding note on MIDI out, for the .mid file. */
struct OpenNote
{
    int note = -1;
    std::int64_t startSample = 0;
    bool accent = false;
    bool vibrato = false;
};

int run (int argc, char** argv)
{
    Options o;
    if (! parseArgs (argc, argv, o))
        return 2;

    const double rate = static_cast<double> (o.rate);
    ParamValues params = defaultParamValues();
    Settings settings;
    auto bank = std::make_unique<seq::PatternBank>();

    // The slot the presets and the render act on: --slot, or what the parameters say now.
    const auto selectedSlot = [&] {
        if (o.haveSlot) return seq::slotIndex (o.bank, o.slot);
        const int b = std::clamp (paramAsInt (params, kPatternBank), 0, seq::kNumBanks - 1);
        const int s = std::clamp (paramAsInt (params, kPatternSlot) - 1, 0, seq::kSlotsPerBank - 1);
        return seq::slotIndex (b, s);
    };
    bool arpTarget = o.mode == "arp";

    for (const auto& path : o.presets)
    {
        std::string text;
        if (! readFile (path, text))
        {
            std::fprintf (stderr, "cannot read %s\n", path.c_str());
            return 1;
        }

        PresetData preset;
        const PresetParseResult r = parsePreset (text, preset);
        if (! r.ok)
        {
            std::fprintf (stderr, "%s: %s (at byte %zu)\n", path.c_str(), r.error.c_str(), r.offset);
            return 1;
        }
        if (r.adjusted > 0 && ! o.quiet)
            std::fprintf (stderr, "%s: %d value(s) repaired\n", path.c_str(), r.adjusted);
        if (r.ignored > 0 && ! o.quiet)
            std::fprintf (stderr, "%s: %d item(s) ignored (unknown, or outside the %s type)\n", path.c_str(), r.ignored, presetTypeName (preset.type));

        switch (preset.type)
        {
            case PresetType::Pattern:
                applyPattern (preset, bank->seq[static_cast<std::size_t> (selectedSlot())]);
                break;
            case PresetType::Arp:
                applyPattern (preset, bank->arp[static_cast<std::size_t> (selectedSlot())]);
                arpTarget = true;
                break;
            case PresetType::Template:
                if (! o.quiet) std::fprintf (stderr, "%s: a template preset has nothing to render\n", path.c_str());
                break;
            default:
                applyPreset (preset, params, settings, *bank);
                break;
        }
    }

    for (const auto& [id, value] : o.params)
    {
        const int index = findParam (id);
        if (index < 0)
        {
            std::fprintf (stderr, "--param: unknown parameter %s\n", id.c_str());
            return 1;
        }
        float v = 0.0f;
        bool clamped = false;
        if (! parseParamValue (kParamTable[static_cast<std::size_t> (index)], value, v, clamped))
        {
            std::fprintf (stderr, "--param: %s is not a number for %s\n", value.c_str(), id.c_str());
            return 1;
        }
        if (clamped && ! o.quiet)
            std::fprintf (stderr, "--param: %s clamped to %g\n", id.c_str(), static_cast<double> (v));
        if ((index == kPlayMode || index == kSeqRun) && ! o.quiet)
            std::fprintf (stderr, "--param: %s is set by acid-render (from --mode); the value given is ignored\n", id.c_str());
        if ((index == kPatternBank || index == kPatternSlot) && o.haveSlot && ! o.quiet)
            std::fprintf (stderr, "--param: %s is overridden by --slot\n", id.c_str());
        params[static_cast<std::size_t> (index)] = v;

        // dist_model picks a voicing, as the plugin does when the user picks a model: its values
        // land in the Distortion Voicing parameters (0 is Custom and leaves them alone). A later
        // --param can still override single voicing values.
        if (index == kDistModel)
        {
            const auto voicings = fx::distortionVoicings();
            const int model = paramAsInt (params, kDistModel);
            if (model > 0 && model < static_cast<int> (voicings.size()))
                fx::writeVoicing (params, voicings[static_cast<std::size_t> (model)]);
            else if (model != 0 && ! o.quiet)
                std::fprintf (stderr, "--param: dist_model %d has no voicing (the table has %zu); the voicing parameters are left as they are\n",
                              model, voicings.size());
        }
    }

    const int slotIndex = selectedSlot();
    const int slotBank = slotIndex / seq::kSlotsPerBank;
    const int slotInBank = slotIndex % seq::kSlotsPerBank;

    if (! o.steps.empty())
    {
        seq::Pattern& p = arpTarget ? bank->arp[static_cast<std::size_t> (slotIndex)] : bank->seq[static_cast<std::size_t> (slotIndex)];
        std::string error;
        if (! seq::fromStepText (o.steps, p, &error))
        {
            std::fprintf (stderr, "--steps: %s\n", error.c_str());
            return 1;
        }
    }

    const bool arpMode = o.mode == "arp", extMode = o.mode == "ext";
    const seq::Pattern& played = arpMode ? bank->arp[static_cast<std::size_t> (slotIndex)] : bank->seq[static_cast<std::size_t> (slotIndex)];
    if (! hasNote (played) && ! o.quiet)
        std::fprintf (stderr, "warning: %sslot %c%d has no note in it; the render will be silent\n", arpMode ? "arp " : "", 'A' + slotBank, slotInBank + 1);

    // The performance controls: the selected slot, in the mode's play mode. Only Sequencer
    // mode needs Run; the arpeggiator runs while keys are held.
    params[kPlayMode] = arpMode ? 2.0f : (extMode ? 0.0f : 1.0f);
    params[kSeqRun] = arpMode || extMode ? 0.0f : 1.0f;
    params[kPatternBank] = static_cast<float> (slotBank);
    params[kPatternSlot] = static_cast<float> (slotInBank + 1);

    if (o.haveQuality)
        settings.offlineQuality = o.quality;

    const auto totalSamples = static_cast<std::int64_t> (std::llround (o.bars * 4.0 * 60.0 / o.bpm * rate));
    const auto tailSamples = static_cast<std::int64_t> (std::llround (o.tailSeconds * rate));
    const auto totalFrames = static_cast<std::uint64_t> (totalSamples + tailSamples);
    const std::uint64_t dataBytes = totalFrames * 2ull * sizeof (float);
    if (dataBytes > kMaxWavDataBytes)
    {
        std::fprintf (stderr, "the render would be %.1f GiB of audio; a WAV file holds 4 GiB. Use fewer bars, a lower rate or a shorter tail\n",
                      static_cast<double> (dataBytes) / (1024.0 * 1024.0 * 1024.0));
        return 2;
    }

    // The MIDI input of the render: arp holds its keys for the song; ext plays the pattern.
    const double quartersPerSample = o.bpm / (60.0 * rate);
    std::vector<std::pair<std::int64_t, seq::MidiMsg>> input;
    const auto message = [] (int status, int d1, int d2) {
        seq::MidiMsg m;
        m.size = 3;
        m.data[0] = static_cast<std::uint8_t> (status);
        m.data[1] = static_cast<std::uint8_t> (d1 & 0x7F);
        m.data[2] = static_cast<std::uint8_t> (d2 & 0x7F);
        return m;
    };
    if (arpMode)
    {
        for (const int n : o.hold) input.push_back ({ 0, message (0x90, n, 100) });
        for (const int n : o.hold) input.push_back ({ std::max<std::int64_t> (0, totalSamples - 1), message (0x80, n, 0) });
    }
    else if (extMode && hasNote (played))
    {
        seq::MidiExportOptions mo;
        mo.bpm = o.bpm;
        mo.ppq = 960;
        mo.embedPattern = false;
        mo.slideIntoRest = settings.slideIntoRest;
        mo.accentVelocity = 127;
        mo.normalVelocity = static_cast<std::uint8_t> (std::clamp<int> (settings.accentThreshold, 1, 126));
        seq::SmfFile smf;
        if (! seq::parseSmf (seq::writeMidiFile (played, mo), smf) || smf.tracks.empty())
        {
            std::fprintf (stderr, "the pattern in slot %c%d could not be turned into MIDI\n", 'A' + slotBank, slotInBank + 1);
            return 1;
        }
        const seq::SmfTrack& track = smf.tracks.front();
        const std::int64_t clipTicks = std::max<std::int64_t> (1, track.endTick);
        const double samplesPerTick = 1.0 / (quartersPerSample * static_cast<double> (smf.ppq));
        for (std::int64_t pass = 0;; ++pass)
        {
            const std::int64_t base = pass * clipTicks;
            if (static_cast<double> (base) * samplesPerTick >= static_cast<double> (totalSamples)) break;
            for (const auto& e : track.events)
            {
                if (e.status < 0x80 || e.status >= 0xF0) continue;
                const auto at = static_cast<std::int64_t> (std::llround (static_cast<double> (base + e.tick) * samplesPerTick));
                if (at >= totalSamples) continue;
                input.push_back ({ at, message (e.status, e.data1, e.data2) });
            }
        }
        // Whatever the last pass left on ends with the song: all notes off, CC1 and CC64 to rest.
        const std::int64_t end = std::max<std::int64_t> (0, totalSamples - 1);
        input.push_back ({ end, message (0xB0, 64, 0) });
        input.push_back ({ end, message (0xB0, 1, 0) });
        input.push_back ({ end, message (0xB0, 123, 0) });
        std::stable_sort (input.begin(), input.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    }

    Engine engine;
    engine.setNonRealtime (true);
    engine.publishSettings (settings);
    engine.publishBank (*bank);
    engine.prepare (rate, o.block);

    std::vector<float> left (static_cast<std::size_t> (o.block)), right (static_cast<std::size_t> (o.block));
    std::vector<float> interleaved;
    interleaved.reserve (static_cast<std::size_t> (2 * totalFrames));

    // Room for everything the player makes, plus MIDI thru of the input (ext mode).
    std::vector<seq::MidiMsg> midiScratch (static_cast<std::size_t> (engine.midiOutCapacityFor (o.block)) + input.size());
    MidiOutBuffer midiOut { midiScratch.data(), static_cast<int> (midiScratch.size()), 0 };

    std::vector<seq::MidiNote> notes;
    std::vector<OpenNote> open;
    bool cc1High = false;
    bool midiOverflowed = false;

    const auto t0 = std::chrono::steady_clock::now();
    double peak = 0.0, sumSquares = 0.0;
    std::int64_t rendered = 0;
    std::size_t inputCursor = 0;
    std::vector<seq::MidiMsg> blockInput;
    blockInput.reserve (input.size());

    while (rendered < totalSamples + tailSamples)
    {
        const bool inSong = rendered < totalSamples;
        const int n = static_cast<int> (std::min<std::int64_t> (o.block, (inSong ? totalSamples : totalSamples + tailSamples) - rendered));

        seq::TransportInfo t;
        t.valid = true;
        t.playing = inSong;
        t.ppqValid = true;
        t.ppq = static_cast<double> (rendered) * quartersPerSample;
        t.bpmValid = true;
        t.bpm = o.bpm;

        ParamValues blockParams = params;
        if (! inSong) blockParams[kSeqRun] = 0.0f;

        blockInput.clear();
        while (inputCursor < input.size() && input[inputCursor].first < rendered + n)
        {
            seq::MidiMsg m = input[inputCursor].second;
            m.offset = static_cast<std::int32_t> (input[inputCursor].first - rendered);
            blockInput.push_back (m);
            ++inputCursor;
        }

        engine.process (blockParams, t, { blockInput.data(), blockInput.size() }, midiOut, left.data(), right.data(), n);
        midiOverflowed = midiOverflowed || midiOut.overflowed;

        for (int i = 0; i < midiOut.count; ++i)
        {
            const seq::MidiMsg& m = midiOut.messages[i];
            if (m.size < 3) continue;
            const std::int64_t at = rendered + m.offset;
            const int status = m.data[0] & 0xF0;
            const int note = m.data[1];

            if (status == 0xB0 && note == 1)
            {
                cc1High = m.data[2] >= 64;
                continue;
            }

            const bool noteOn = status == 0x90 && m.data[2] > 0;
            const bool noteOff = status == 0x80 || (status == 0x90 && m.data[2] == 0);
            if (! noteOn && ! noteOff) continue;

            // Close an open note of that pitch (a note-off, or a re-strike).
            for (std::size_t k = 0; k < open.size(); ++k)
            {
                if (open[k].note != note) continue;
                notes.push_back ({ static_cast<double> (open[k].startSample) * quartersPerSample, static_cast<double> (at) * quartersPerSample,
                                   static_cast<std::uint8_t> (note), open[k].accent, open[k].vibrato, 0 });
                open.erase (open.begin() + static_cast<std::ptrdiff_t> (k));
                break;
            }

            // A slide leaves the old note open for one more sample; overlaps are written as given.
            if (noteOn)
                open.push_back ({ note, at, m.data[2] >= settings.outAccentVelocity, cc1High });
        }

        for (int i = 0; i < n; ++i)
        {
            const double l = static_cast<double> (left[static_cast<std::size_t> (i)]);
            const double r = static_cast<double> (right[static_cast<std::size_t> (i)]);
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
            sumSquares += 0.5 * (l * l + r * r);
            interleaved.push_back (left[static_cast<std::size_t> (i)]);
            interleaved.push_back (right[static_cast<std::size_t> (i)]);
        }

        rendered += n;
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double> (t1 - t0).count();
    const double seconds = static_cast<double> (rendered) / rate;

    for (const auto& n : open)
        notes.push_back ({ static_cast<double> (n.startSample) * quartersPerSample, static_cast<double> (rendered) * quartersPerSample,
                           static_cast<std::uint8_t> (n.note), n.accent, n.vibrato, 0 });

    if (! test::writeWav (o.out, interleaved, 2, rate))
    {
        std::fprintf (stderr, "cannot write %s\n", o.out.c_str());
        return 1;
    }

    if (midiOverflowed && ! o.quiet)
        std::fprintf (stderr, "warning: the MIDI output buffer overflowed; the .mid file is incomplete\n");

    if (! o.midiOut.empty())
    {
        seq::MidiExportOptions mo;
        mo.bpm = o.bpm;
        mo.accentVelocity = settings.outAccentVelocity;
        mo.normalVelocity = settings.outNormalVelocity;
        const auto bytes = seq::writeMidiFile (notes, static_cast<double> (rendered) * quartersPerSample, mo, "acid-render");
        std::ofstream f (o.midiOut, std::ios::binary);
        if (! f || ! f.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size())))
        {
            std::fprintf (stderr, "cannot write %s\n", o.midiOut.c_str());
            return 1;
        }
    }

    const double rms = rendered > 0 ? std::sqrt (sumSquares / static_cast<double> (rendered)) : 0.0;
    const EngineStatus status = engine.status();
    std::printf ("%s: %.2f s at %lld Hz, voice x%d, peak %.2f dBFS, rms %.2f dBFS, %.1fx real time (engine cpu %.1f %%), %zu MIDI notes\n",
                 o.out.c_str(), seconds, o.rate, status.oversampling, dbfs (peak), dbfs (rms),
                 elapsed > 0.0 ? seconds / elapsed : 0.0, 100.0 * status.cpuLoad, notes.size());
    return 0;
}
}

int main (int argc, char** argv)
{
    try
    {
        return run (argc, argv);
    }
    catch (const std::bad_alloc&)
    {
        std::fprintf (stderr, "out of memory: the render does not fit in RAM. Use fewer bars, a lower rate or a shorter tail\n");
        return 1;
    }
}
