#include "Common.h"

#include "acid/fx/DistortionVoicings.h"
#include "acid/seq/PatternCodec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace factory
{
void fail (const std::string& message)
{
    std::fprintf (stderr, "factory-build: %s\n", message.c_str());
    std::exit (1);
}

float param (const acid::ParamValues& v, acid::ParamId id)
{
    return v[static_cast<std::size_t> (id)];
}

void setParam (acid::ParamValues& v, acid::ParamId id, float value)
{
    v[static_cast<std::size_t> (id)] = value;
}

// ---- Parameters -------------------------------------------------------------------------

int voicingIndex (std::string_view name)
{
    const auto voicings = acid::fx::distortionVoicings();
    for (std::size_t i = 0; i < voicings.size(); ++i)
        if (name == voicings[i].name)
            return static_cast<int> (i);
    fail ("unknown distortion voicing '" + std::string (name) + "'");
}

void setVoicing (acid::ParamValues& values, int index)
{
    const auto voicings = acid::fx::distortionVoicings();
    if (index < 0 || index >= static_cast<int> (voicings.size()))
        fail ("voicing index out of range");
    acid::fx::writeVoicing (values, voicings[static_cast<std::size_t> (index)]);
    setParam (values, acid::kDistModel, static_cast<float> (index));
}

Patch baseSound()
{
    Patch p;
    setVoicing (p.values, static_cast<int> (std::lround (param (p.values, acid::kDistModel))));
    return p;
}

namespace
{
std::vector<std::string_view> splitSpaces (std::string_view s)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && std::isspace (static_cast<unsigned char> (s[i]))) ++i;
        std::size_t j = i;
        while (j < s.size() && ! std::isspace (static_cast<unsigned char> (s[j]))) ++j;
        if (j > i) out.push_back (s.substr (i, j - i));
        i = j;
    }
    return out;
}

void copyGroup (Patch& patch, acid::PresetType type, std::string_view slug)
{
    const Patch& src = findEffect (type, slug);
    for (int i = 0; i < acid::kNumParams; ++i)
        if (acid::paramInType (type, static_cast<acid::ParamId> (i)))
            patch.values[static_cast<std::size_t> (i)] = src.values[static_cast<std::size_t> (i)];
}
}

void applySpec (Patch& patch, std::string_view spec, std::string_view context)
{
    for (const std::string_view token : splitSpaces (spec))
    {
        const auto eq = token.find ('=');
        if (eq == std::string_view::npos || eq == 0)
            fail (std::string (context) + ": bad recipe token '" + std::string (token) + "'");
        const std::string_view key = token.substr (0, eq);
        const std::string value (token.substr (eq + 1));

        if (key == "voicing")
        {
            std::string name = value;
            std::replace (name.begin(), name.end(), '_', ' ');
            setVoicing (patch.values, voicingIndex (name));
            setParam (patch.values, acid::kDistOn, 1.0f);
            continue;
        }
        if (key == "order")
        {
            std::array<std::uint8_t, acid::kNumFxSlots> order {};
            std::set<int> seen;
            std::stringstream ss (value);
            std::string item;
            int n = 0;
            while (std::getline (ss, item, ','))
            {
                const int slot = std::atoi (item.c_str());
                if (n >= acid::kNumFxSlots || slot < 0 || slot >= acid::kNumFxSlots || ! seen.insert (slot).second)
                    fail (std::string (context) + ": bad effects order '" + value + "'");
                order[static_cast<std::size_t> (n++)] = static_cast<std::uint8_t> (slot);
            }
            if (n != acid::kNumFxSlots)
                fail (std::string (context) + ": effects order needs six slots");
            patch.fxOrder = order;
            continue;
        }
        if (key.front() == '+')
        {
            const std::string_view group = key.substr (1);
            acid::PresetType type {};
            if (! acid::parsePresetType (group, type) || type == acid::PresetType::Synth || type == acid::PresetType::Scene
                || type == acid::PresetType::State || type == acid::PresetType::Pattern || type == acid::PresetType::Arp
                || type == acid::PresetType::Template)
                fail (std::string (context) + ": bad preset reference '" + std::string (token) + "'");
            copyGroup (patch, type, value);
            continue;
        }

        const int index = acid::findParam (key);
        if (index < 0)
            fail (std::string (context) + ": unknown parameter '" + std::string (key) + "'");
        const acid::ParamInfo& info = acid::kParamTable[static_cast<std::size_t> (index)];
        float v = 0.0f;
        bool clamped = false;
        if (! acid::parseParamValue (info, value, v, clamped))
            fail (std::string (context) + ": '" + value + "' is not a number for " + info.id);
        if (clamped)
            fail (std::string (context) + ": " + info.id + "=" + value + " is outside the table range");
        if (info.kind != acid::ParamKind::Float && std::abs (std::strtod (value.c_str(), nullptr) - static_cast<double> (v)) > 1.0e-9)
            fail (std::string (context) + ": " + info.id + " takes whole numbers");
        patch.values[static_cast<std::size_t> (index)] = v;
    }
}

// ---- Tags -------------------------------------------------------------------------------

std::string distortionTag (const acid::ParamValues& v)
{
    // Effective preamp position of the block: the drive knob interpolates between the
    // voicing's preamp minimum and maximum. The Mods overdrive into the filter counts too.
    double amount = 0.0;
    if (param (v, acid::kDistOn) >= 0.5f)
    {
        const double lo = param (v, acid::kDistPreMin), hi = param (v, acid::kDistPreMax);
        amount = lo + (hi - lo) * static_cast<double> (param (v, acid::kDistDrive)) / 100.0;
    }
    if (param (v, acid::kModOn) >= 0.5f)
        amount = std::max (amount, 0.8 * static_cast<double> (param (v, acid::kModOverdrive)));
    if (amount <= 0.0) return "None";
    if (amount < 30.0) return "Light";
    if (amount < 55.0) return "Medium";
    return "Strong";
}

std::string waveformTag (const acid::ParamValues& v)
{
    return param (v, acid::kWave) >= 0.5f ? "Square" : "Saw";
}

std::string joinTags (const std::vector<std::string>& tags)
{
    std::string out;
    std::set<std::string> seen;
    for (const auto& t : tags)
    {
        if (t.empty() || ! seen.insert (t).second) continue;
        if (! out.empty()) out += ',';
        out += t;
    }
    return out;
}

const std::vector<std::pair<std::string, std::vector<std::string>>>& tagVocabulary()
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> vocabulary {
        { "Play Mode", { "Seq", "Arp" } },
        { "Style", { "Showcase", "Chicago", "Anthem", "Rolling", "Melodic", "Minimal", "Hard", "Trance", "Dub",
                     "Breaks", "Experimental", "Polymeter", "Teaching" } },
        { "Family", { "Stock", "Squelch", "Drive", "Deep Bass", "Scream", "Whistle", "Liquid Line", "Pluck", "Space",
                      "Overdrive", "Filter FM", "Tracking", "Sub", "Sustain", "Utility" } },
        { "Waveform", { "Saw", "Square" } },
        { "Distortion", { "None", "Light", "Medium", "Strong" } },
        { "Character", { "Squelchy", "Screaming", "Deep", "Whistling", "Plucky", "Liquid", "Dark", "Bright",
                         "Gritty", "Rubbery", "Talking", "Percussive", "Droning", "Chaotic", "Warm", "Hollow",
                         "Spacey", "Clean", "Noisy", "Vintage", "Aggressive", "Smooth" } },
        { "Mods", { "Mods" } },
        { "Pattern", { "Main", "Variation", "Generated", "One Note", "Two Note", "Melodic Line", "All Accent",
                       "Sparse Accent", "Slides", "Octave Jumps", "Ties", "Rests", "Shuffle", "Triplet",
                       "Odd Length", "Long", "Half Time", "Vibrato", "Ratchet" } },
        { "Arp Shape", { "Up", "Down", "Up Down", "Chord", "Pedal", "Random Walk", "Octave Spread" } },
        { "Unit", { "Stock Unit", "Unit Variation", "Worn", "Extended Range" } },
        { "Effect", { "Short", "Long", "Dotted", "Wide", "Filtered", "Tempo Sync", "Free Time", "Room", "Plate",
                      "Hall", "Subtle", "Heavy", "Glue", "Safety", "Scoop", "Boost", "Cut", "Custom Voicing",
                      "Pump", "Motion", "Clean Low" } },
        { "Template", { "Scale", "Pentatonic", "Chord", "Interval", "Acid", "Other" } },
    };
    return vocabulary;
}

void checkTags (std::string_view tags, std::string_view context)
{
    static const std::set<std::string> known = [] {
        std::set<std::string> s;
        for (const auto& [group, list] : tagVocabulary())
            for (const auto& t : list) s.insert (t);
        return s;
    }();
    std::stringstream ss { std::string (tags) };
    std::string t;
    while (std::getline (ss, t, ','))
        if (! known.contains (t))
            fail (std::string (context) + ": tag '" + t + "' is not in the vocabulary");
}

// ---- Names ------------------------------------------------------------------------------

std::string slugify (std::string_view name)
{
    std::string out;
    bool dash = false;
    for (const char c : name)
    {
        const auto u = static_cast<unsigned char> (c);
        if (std::isalnum (u))
        {
            if (dash && ! out.empty()) out += '-';
            out += static_cast<char> (std::tolower (u));
            dash = false;
        }
        else
            dash = true;
    }
    if (out.empty()) fail ("empty file name for '" + std::string (name) + "'");
    return out;
}

void setPatternName (acid::seq::Pattern& p, std::string_view name)
{
    if (name.size() >= static_cast<std::size_t> (acid::seq::kPatternNameLength))
        fail ("pattern name too long (31 bytes at most): '" + std::string (name) + "'");
    std::memset (p.name, 0, sizeof (p.name));
    std::memcpy (p.name, name.data(), name.size());
}

namespace
{
std::vector<std::string>& forbiddenNames()
{
    static std::vector<std::string> names;
    return names;
}

std::string lowerAscii (std::string_view s)
{
    std::string out (s);
    for (char& c : out) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return out;
}

bool isWordChar (char c)
{
    return std::isalnum (static_cast<unsigned char> (c)) != 0;
}
}

bool loadForbiddenNames (const std::string& path)
{
    std::ifstream f (path);
    if (! f) return false;
    forbiddenNames().clear();
    std::string line;
    while (std::getline (f, line))
    {
        if (const auto hash = line.find ('#'); hash != std::string::npos) line.erase (hash);
        while (! line.empty() && std::isspace (static_cast<unsigned char> (line.back()))) line.pop_back();
        std::size_t i = 0;
        while (i < line.size() && std::isspace (static_cast<unsigned char> (line[i]))) ++i;
        if (i < line.size()) forbiddenNames().push_back (lowerAscii (line.substr (i)));
    }
    return true;
}

std::string forbiddenNameIn (std::string_view text)
{
    const std::string t = lowerAscii (text);
    for (const std::string& name : forbiddenNames())
        for (std::size_t at = t.find (name); at != std::string::npos; at = t.find (name, at + 1))
        {
            const bool startOk = at == 0 || ! isWordChar (t[at - 1]);
            const bool endOk = at + name.size() == t.size() || ! isWordChar (t[at + name.size()]);
            if (startOk && endOk) return name;
        }
    return {};
}

std::string checkNames (const acid::PresetData& d)
{
    const auto hit = [] (std::string_view what, std::string_view text) -> std::string {
        if (const std::string n = forbiddenNameIn (text); ! n.empty())
            return std::string (what) + " '" + std::string (text) + "' uses the forbidden name '" + n + "'";
        return {};
    };
    for (const auto& [what, text] : { std::pair<const char*, std::string_view> { "name", d.name }, { "description", d.description },
                                      { "tags", d.tags } })
        if (auto why = hit (what, text); ! why.empty()) return why;
    for (const auto* slots : { &d.seqSlots, &d.arpSlots })
        for (const acid::SlotPattern& s : *slots)
            if (auto why = hit ("pattern name", s.pattern.name); ! why.empty()) return why;
    if (d.hasPattern)
        if (auto why = hit ("pattern name", d.pattern.name); ! why.empty()) return why;
    return {};
}

acid::seq::Pattern parseSteps (std::string_view steps, std::string_view context)
{
    acid::seq::Pattern p;
    std::string error;
    if (! acid::seq::fromStepText (steps, p, &error))
        fail (std::string (context) + ": " + error);
    return p;
}

// ---- Levels -----------------------------------------------------------------------------

bool Levels::load (const std::string& path)
{
    std::ifstream f (path);
    if (! f) return false;
    std::string line;
    while (std::getline (f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        const auto tab = line.find ('\t');
        if (tab == std::string::npos) fail (path + ": bad line '" + line + "'");
        trims[line.substr (0, tab)] = std::strtof (line.c_str() + tab + 1, nullptr);
    }
    return true;
}

float Levels::trim (const std::string& key) const
{
    const auto it = trims.find (key);
    return it == trims.end() ? 0.0f : it->second;
}

// ---- Library ----------------------------------------------------------------------------

std::string typeDirectory (acid::PresetType type)
{
    return acid::presetTypeName (type);
}

void Library::add (Entry entry)
{
    const std::string context = entry.path;
    checkTags (entry.data.tags, context);
    if (const std::string why = checkNames (entry.data); ! why.empty()) fail (context + ": " + why);
    if (entry.data.name.empty()) fail (context + ": empty name");
    if (entry.data.name.find ("--") != std::string::npos) fail (context + ": no double dashes in names");
    if (! paths.emplace (entry.path, 1).second) fail ("duplicate file " + entry.path);
    const std::string nameKey = std::string (acid::presetTypeName (entry.data.type)) + "/" + entry.data.name;
    if (entry.path != "default.mw12f" && ! names.emplace (nameKey, 1).second)
        fail ("duplicate " + std::string (acid::presetTypeName (entry.data.type)) + " name '" + entry.data.name + "'");
    all.push_back (std::move (entry));
}

int Library::count (acid::PresetType type) const
{
    return static_cast<int> (std::count_if (all.begin(), all.end(), [type] (const Entry& e) { return e.data.type == type; }));
}

acid::PresetData makeParamPreset (acid::PresetType type, const Patch& patch, std::string_view name,
                                  std::string_view tags, std::string_view description)
{
    acid::Settings settings;
    settings.fxOrder = patch.fxOrder;
    acid::PresetData d = acid::makePreset (type, patch.values, settings, nullptr);
    d.name = std::string (name);
    d.author = kAuthor;
    d.tags = std::string (tags);
    d.description = std::string (description);
    return d;
}

void applyLevelTrim (acid::ParamValues& values, float trimDb)
{
    const acid::ParamInfo& info = acid::paramInfo (acid::kMasterVolume);
    const float v = std::clamp (std::round ((param (values, acid::kMasterVolume) + trimDb) * 10.0f) / 10.0f, info.min, info.max);
    setParam (values, acid::kMasterVolume, v);
}
}
