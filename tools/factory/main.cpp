// factory-build: writes the mw12F factory library, or checks one.
//
//   factory-build --out assets/factory [--levels tools/factory/levels.tsv] [--manifest m.json]
//                 [--rejects rejects.txt] [--prune]
//   factory-build --verify assets/factory
//
// Both modes check names, descriptions, tags and pattern names against forbidden_names.txt
// (records, artists, labels, trademarks); --names points at another copy of the list.
//
// Build mode makes every preset from the recipes in this directory, writes it with the
// StateCodec writer, reads it back and writes it again, and stops if the reader had to adjust
// or ignore anything or the bytes differ. Files whose bytes are unchanged are not rewritten.
// --prune removes .mw12f files under the type folders that the build no longer makes.
// --manifest writes a JSON index (type, tempo, level key and the steps of each pattern) for
// qa.py. --rejects lists the generator draws the curation rules turned down.
//
// Verify mode reads every .mw12f under a folder, parses it, writes it back and compares the
// bytes; it reports any file that fails, needs a repair (adjusted), carries content outside
// its type (ignored) or uses a forbidden name. Exit status 0 when every file passes.
#include "Common.h"
#include "Patterns.h"

#include "acid/fx/DistortionVoicings.h"
#include "acid/seq/Pattern.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace factory;

namespace
{
bool readFile (const fs::path& path, std::string& out)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileIfChanged (const fs::path& path, const std::string& text)
{
    std::string old;
    if (readFile (path, old) && old == text) return false;
    fs::create_directories (path.parent_path());
    std::ofstream f (path, std::ios::binary);
    if (! f || ! f.write (text.data(), static_cast<std::streamsize> (text.size())))
        fail ("cannot write " + path.string());
    return true;
}

/** Round trip through the reader and writer. Empty string when the text is canonical. */
std::string roundTrip (const std::string& text, acid::PresetData* parsed = nullptr)
{
    acid::PresetData d;
    const acid::PresetParseResult r = acid::parsePreset (text, d);
    if (! r.ok) return "parse error: " + r.error + " at byte " + std::to_string (r.offset);
    if (r.adjusted != 0) return std::to_string (r.adjusted) + " value(s) adjusted by the reader";
    if (r.ignored != 0) return std::to_string (r.ignored) + " item(s) ignored by the reader";
    if (acid::writePreset (d) != text) return "writing the parsed document gives different bytes";
    if (parsed) *parsed = std::move (d);
    return {};
}

std::string jsonEscape (std::string_view s)
{
    std::string o;
    for (const char c : s)
    {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char> (c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

const char* divText (const acid::seq::Pattern& p)
{
    switch (p.div)
    {
        case acid::seq::StepDiv::d1_64: return "1/64";
        case acid::seq::StepDiv::d1_32: return "1/32";
        case acid::seq::StepDiv::d1_16: return "1/16";
        case acid::seq::StepDiv::d1_8: return "1/8";
        case acid::seq::StepDiv::d1_4: return "1/4";
        case acid::seq::StepDiv::d1_2: return "1/2";
        case acid::seq::StepDiv::d1_1: return "1/1";
    }
    return "1/16";
}

const char* modText (const acid::seq::Pattern& p)
{
    switch (p.divMod)
    {
        case acid::seq::DivMod::Full: return "full";
        case acid::seq::DivMod::Dotted: return "dotted";
        case acid::seq::DivMod::Triplet: return "triplet";
    }
    return "full";
}

std::string patternJson (const acid::seq::Pattern& p, int slot)
{
    std::string o = "{\"slot\":" + std::to_string (slot) + ",\"name\":\"" + jsonEscape (p.name) + "\",\"length\":" + std::to_string (p.length)
                    + ",\"div\":\"" + divText (p) + "\",\"mod\":\"" + modText (p) + "\",\"shuffle\":" + std::to_string (p.shuffle)
                    + ",\"transpose\":" + std::to_string (p.transpose) + ",\"steps\":[";
    for (int i = 0; i < p.length; ++i)
    {
        const acid::seq::Step& s = p.steps[static_cast<std::size_t> (i)];
        const char state = s.state == acid::seq::StepState::Note ? 'N' : s.state == acid::seq::StepState::Tie ? 'T' : 'R';
        if (i > 0) o += ',';
        o += "[\"" + std::string (1, state) + "\"," + std::to_string (acid::seq::stepMidiNote (s)) + "," + (s.accent ? "1" : "0") + ","
             + (s.slide ? "1" : "0") + "," + std::to_string (s.ratchet) + "," + std::to_string (s.probability) + "," + std::to_string (s.arpIndex) + "]";
    }
    return o + "]}";
}

/** A document carrying a distortion model other than Custom (0) must carry exactly that
    model's voicing values, so the plugin shows the model the sound really uses. The one
    exception is dist_volume, the voicing's output trim: distortion presets set it so they
    match the sound without them in loudness (qa.py --calibrate), which changes no tone. */
std::string checkVoicing (const acid::PresetData& d)
{
    if (! d.hasParam[acid::kDistModel] || ! d.hasParam[acid::kDistClipper]) return {};
    const int model = static_cast<int> (param (d.params, acid::kDistModel));
    if (model == 0) return {};
    const auto voicings = acid::fx::distortionVoicings();
    if (model < 0 || model >= static_cast<int> (voicings.size())) return "dist_model out of the voicing table";
    acid::ParamValues expected = d.params;
    acid::fx::writeVoicing (expected, voicings[static_cast<std::size_t> (model)]);
    for (int i = 0; i < acid::kNumParams; ++i)
        if (std::string_view (acid::kParamTable[static_cast<std::size_t> (i)].group) == "Distortion Voicing" && i != acid::kDistVolume
            && expected[static_cast<std::size_t> (i)] != d.params[static_cast<std::size_t> (i)])
            return std::string ("voicing row ") + acid::kParamTable[static_cast<std::size_t> (i)].id + " differs from model "
                   + voicings[static_cast<std::size_t> (model)].name;
    return {};
}

void writeManifest (const Library& lib, const fs::path& path)
{
    std::string o = "{\"entries\":[\n";
    bool first = true;
    for (const Entry& e : lib.entries())
    {
        const acid::PresetData& d = e.data;
        if (! first) o += ",\n";
        first = false;
        o += "{\"path\":\"" + jsonEscape (e.path) + "\",\"type\":\"" + acid::presetTypeName (d.type) + "\",\"name\":\"" + jsonEscape (d.name)
             + "\",\"group\":\"" + jsonEscape (e.group) + "\",\"tags\":\"" + jsonEscape (d.tags) + "\",\"bpm\":" + std::to_string (e.bpm)
             + ",\"level_key\":\"" + jsonEscape (e.levelKey) + "\"";
        if (d.hasParam[acid::kLimOn]) o += std::string (",\"limiter\":") + (param (d.params, acid::kLimOn) >= 0.5f ? "true" : "false");
        if (d.hasParam[acid::kMasterVolume]) o += ",\"master_volume\":" + std::to_string (param (d.params, acid::kMasterVolume));
        if (e.levelParam >= 0)
        {
            // The parameter the level trim moves, its value as written and its range, so
            // qa.py --calibrate knows when the build has clamped a trim.
            const auto id = static_cast<acid::ParamId> (e.levelParam);
            const acid::ParamInfo& info = acid::paramInfo (id);
            o += std::string (",\"level_param\":\"") + info.id + "\",\"level_value\":" + std::to_string (param (d.params, id))
                 + ",\"level_min\":" + std::to_string (info.min) + ",\"level_max\":" + std::to_string (info.max);
        }
        if (d.hasBank)
        {
            o += ",\"seq\":[";
            for (std::size_t i = 0; i < d.seqSlots.size(); ++i)
                o += (i ? "," : "") + patternJson (d.seqSlots[i].pattern, d.seqSlots[i].slot);
            o += "]";
        }
        if (d.hasPattern) o += ",\"pattern\":" + patternJson (d.pattern, 0);
        o += "}";
    }
    o += "\n]}\n";
    writeFileIfChanged (path, o);
}

int verify (const fs::path& root)
{
    if (! fs::is_directory (root)) { std::fprintf (stderr, "not a folder: %s\n", root.string().c_str()); return 2; }
    std::vector<fs::path> files;
    for (const auto& de : fs::recursive_directory_iterator (root))
        if (de.is_regular_file() && de.path().extension() == acid::kPresetFileExtension)
            files.push_back (de.path());
    std::sort (files.begin(), files.end());

    std::map<std::string, int> byType;
    int bad = 0;
    for (const fs::path& f : files)
    {
        std::string text;
        if (! readFile (f, text)) { std::printf ("FAIL %s: cannot read\n", f.string().c_str()); ++bad; continue; }
        acid::PresetData d;
        const std::string why = roundTrip (text, &d);
        if (! why.empty()) { std::printf ("FAIL %s: %s\n", f.string().c_str(), why.c_str()); ++bad; continue; }
        if (const std::string v = checkVoicing (d); ! v.empty()) { std::printf ("FAIL %s: %s\n", f.string().c_str(), v.c_str()); ++bad; continue; }
        if (const std::string v = checkNames (d); ! v.empty()) { std::printf ("FAIL %s: %s\n", f.string().c_str(), v.c_str()); ++bad; continue; }
        ++byType[acid::presetTypeName (d.type)];
    }
    for (const auto& [type, n] : byType) std::printf ("%-12s %4d\n", type.c_str(), n);
    std::printf ("verified %zu file(s), %d failure(s)\n", files.size(), bad);
    return bad == 0 ? 0 : 1;
}

int build (const fs::path& out, const std::string& levelsPath, const std::string& manifestPath, const std::string& rejectsPath, bool prune)
{
    Levels levels;
    if (! levelsPath.empty() && ! levels.load (levelsPath))
        std::fprintf (stderr, "factory-build: no level file at %s, levels untrimmed\n", levelsPath.c_str());

    Library lib (levels);
    addEffects (lib);
    addSounds (lib);
    addScenes (lib);
    addPatterns (lib);
    addTemplates (lib);

    int written = 0;
    std::set<std::string> made;
    for (const Entry& e : lib.entries())
    {
        const std::string text = acid::writePreset (e.data);
        if (const std::string why = roundTrip (text); ! why.empty())
            fail (e.path + ": " + why);
        if (const std::string why = checkVoicing (e.data); ! why.empty())
            fail (e.path + ": " + why);
        made.insert (e.path);
        if (writeFileIfChanged (out / e.path, text)) ++written;
    }

    int pruned = 0;
    if (prune)
        for (int t = 0; t < acid::kNumPresetTypes; ++t)
        {
            const fs::path dir = out / typeDirectory (static_cast<acid::PresetType> (t));
            if (! fs::is_directory (dir)) continue;
            for (const auto& de : fs::directory_iterator (dir))
            {
                const std::string rel = typeDirectory (static_cast<acid::PresetType> (t)) + "/" + de.path().filename().string();
                if (de.is_regular_file() && de.path().extension() == acid::kPresetFileExtension && ! made.contains (rel))
                {
                    std::printf ("pruned %s\n", rel.c_str());
                    fs::remove (de.path());
                    ++pruned;
                }
            }
        }

    if (! manifestPath.empty()) writeManifest (lib, manifestPath);
    if (! rejectsPath.empty())
    {
        std::string r;
        for (const auto& line : generatedRejects()) r += line + "\n";
        writeFileIfChanged (rejectsPath, r);
    }

    std::map<std::string, int> byType;
    std::map<std::string, std::map<std::string, int>> byGroup;
    for (const Entry& e : lib.entries())
    {
        if (e.path == "default.mw12f") continue;
        ++byType[acid::presetTypeName (e.data.type)];
        ++byGroup[acid::presetTypeName (e.data.type)][e.group];
    }
    for (const auto& [type, n] : byType)
    {
        std::printf ("%-12s %4d  ", type.c_str(), n);
        for (const auto& [g, k] : byGroup[type]) std::printf (" %s %d,", g.c_str(), k);
        std::printf ("\n");
    }
    std::printf ("%zu files (plus default.mw12f), %d written, %d pruned, %zu level trims, %zu generator draws rejected\n",
                 lib.entries().size() - 1, written, pruned, levels.size(), generatedRejects().size());
    return 0;
}

void usage()
{
    std::fprintf (stderr, "usage: factory-build --out DIR [--levels FILE] [--manifest FILE] [--rejects FILE] [--prune] [--names FILE]\n"
                          "       factory-build --verify DIR [--names FILE]\n");
}
}

int main (int argc, char** argv)
{
    std::string out, levels, manifest, rejects, verifyDir, names = FACTORY_NAMES_FILE;
    bool prune = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] (std::string& v) {
            if (i + 1 >= argc) { usage(); std::exit (2); }
            v = argv[++i];
        };
        if (a == "--out") next (out);
        else if (a == "--levels") next (levels);
        else if (a == "--manifest") next (manifest);
        else if (a == "--rejects") next (rejects);
        else if (a == "--prune") prune = true;
        else if (a == "--verify") next (verifyDir);
        else if (a == "--names") next (names);
        else { usage(); return 2; }
    }
    if (! loadForbiddenNames (names))
    {
        std::fprintf (stderr, "factory-build: cannot read the forbidden names list %s\n", names.c_str());
        return 2;
    }
    if (! verifyDir.empty()) return verify (verifyDir);
    if (out.empty()) { usage(); return 2; }
    return build (out, levels, manifest, rejects, prune);
}
