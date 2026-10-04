#pragma once
// factory-build: shared pieces. A recipe is plain data in the other source files; this file
// turns recipes into PresetData, names files, derives tags and keeps the library in order.
//
// Build-time strictness: every parameter id, value, voicing name, step string and name is
// checked here, and anything wrong stops the build with a message (fail). The library never
// relies on the reader's repairs, because shipped files must load with zero adjustments.
#include "acid/engine/StateCodec.h"
#include "acid/seq/Pattern.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace factory
{
[[noreturn]] void fail (const std::string& message);

inline constexpr const char* kAuthor = "mwAudio";

// ---- Parameters -------------------------------------------------------------------------

/** A sound as the synth type stores it: every parameter plus the effects order. */
struct Patch
{
    acid::ParamValues values = acid::defaultParamValues();
    std::array<std::uint8_t, acid::kNumFxSlots> fxOrder { 0, 1, 2, 3, 4, 5 };
};

/** Applies a recipe of space separated "id=value" pairs, in natural units, to a patch.
    Pseudo keys:
      voicing=Name     copies the built-in distortion voicing called Name ('_' for spaces)
                       into the Distortion Voicing rows and sets dist_model to its index
                       (a preset that uses distortion carries the voicing values and the
                       model index), and switches the distortion on
      order=a,b,c,d,e,f  the effects order (FxSlot numbers, a permutation of 0..5)
      +cal=slug, +dist=slug, +eq=slug, +delay=slug, +reverb=slug, +chorus=slug, +limiter=slug
                       copies every parameter of that factory calibration or effect preset
    Unknown ids, unparsable values and values outside the table range stop the build. */
void applySpec (Patch& patch, std::string_view spec, std::string_view context);

/** Index of a built-in voicing by its name. Stops the build when unknown. */
int voicingIndex (std::string_view name);

/** Writes voicing index into values (dist_model and every voicing row). */
void setVoicing (acid::ParamValues& values, int index);

/** The stock starting point every sound recipe is applied to: the table defaults, with the
    voicing rows set to the model the default dist_model names, so a file is self-consistent
    even when its distortion is off. */
Patch baseSound();

float param (const acid::ParamValues& v, acid::ParamId id);
void setParam (acid::ParamValues& v, acid::ParamId id, float value);

// ---- Tags -------------------------------------------------------------------------------

/** "None", "Light", "Medium" or "Strong", from the distortion block's settings (and the
    Mods overdrive, which saturates the filter input). */
std::string distortionTag (const acid::ParamValues&);

/** "Saw" or "Square". */
std::string waveformTag (const acid::ParamValues&);

/** Joins tags with commas, dropping empty ones and repeats, keeping the order given. */
std::string joinTags (const std::vector<std::string>& tags);

/** Every tag a file may carry, by group (the README lists them; the build rejects others). */
const std::vector<std::pair<std::string, std::vector<std::string>>>& tagVocabulary();
void checkTags (std::string_view tags, std::string_view context);

// ---- Names ------------------------------------------------------------------------------

/** File name stem: lowercase ASCII letters and digits, words joined by '-'. */
std::string slugify (std::string_view name);

/** Copies name into Pattern::name (checked to fit). */
void setPatternName (acid::seq::Pattern&, std::string_view name);

/** Reads the list of names factory content must not use (forbidden_names.txt: records,
    artists, labels, shops, product and game trademarks; one phrase per line, '#' comments).
    Returns false when the file cannot be read. */
bool loadForbiddenNames (const std::string& path);

/** The first forbidden phrase in text (case-insensitive, whole words), or an empty string. */
std::string forbiddenNameIn (std::string_view text);

/** Checks a document's name, description and tags, and every pattern name it carries,
    against the forbidden names. Returns an empty string when it passes. */
std::string checkNames (const acid::PresetData&);

// ---- Patterns ---------------------------------------------------------------------------

/** Parses the compact step notation (PatternCodec.h) into a default Pattern. The optional
    header sets length and timing; without one the length is the token count. Stops the build
    on a parse error. */
acid::seq::Pattern parseSteps (std::string_view steps, std::string_view context);

// ---- Library ----------------------------------------------------------------------------

/** One file of the library. */
struct Entry
{
    acid::PresetData data;
    std::string path;          // relative to the output root, e.g. "synth/rubber-pluck.mw12f"
    std::string group;         // style or family key, for the manifest and the README counts
    double bpm = 125.0;        // tempo the QA renders scenes and patterns at
    std::string levelKey;      // key into levels.tsv for presets whose level is trimmed
    int levelParam = -1;       // the ParamId that trim moves (master_volume, or an effect's output trim)
};

/** Measured level trims (tools/factory/levels.tsv, written by qa.py --calibrate): dB added
    to master_volume of synth presets and scenes. A missing key means 0 dB. */
class Levels
{
public:
    bool load (const std::string& path);
    float trim (const std::string& key) const;
    std::size_t size() const noexcept { return trims.size(); }

private:
    std::map<std::string, float> trims;
};

class Library
{
public:
    explicit Library (const Levels& l) : levels (l) {}

    /** Adds an entry, checking that its path and its name are unique within its type and
        that its tags come from the vocabulary. */
    void add (Entry entry);

    const std::vector<Entry>& entries() const noexcept { return all; }
    const Levels& levelTrims() const noexcept { return levels; }

    /** Number of entries of a type. */
    int count (acid::PresetType) const;

private:
    const Levels& levels;
    std::vector<Entry> all;
    std::map<std::string, int> paths;
    std::map<std::string, int> names;
};

/** Directory name of a type inside assets/factory/ (the type's file name, as in SPEC 6.7). */
std::string typeDirectory (acid::PresetType);

/** Builds the PresetData of a parameter-carrying preset with name, tags and description.
    Synth presets carry the effects order; other types carry no settings. */
acid::PresetData makeParamPreset (acid::PresetType, const Patch&, std::string_view name,
                                  std::string_view tags, std::string_view description);

/** Rounds a level trim to 0.1 dB and applies it to master_volume (clamped to the table). */
void applyLevelTrim (acid::ParamValues& values, float trimDb);

// ---- The content builders (one source file each) ----------------------------------------

/** A sound recipe as the scenes refer to it. */
struct SoundRef
{
    std::string name;
    std::string family;
    Patch patch;                       // untrimmed: the scene measures its own level
    std::vector<std::string> character;
};

void addEffects (Library&);        // Effects.cpp: calibration, distortion, eq, delay, reverb, chorus, limiter
void addSounds (Library&);         // Sounds.cpp: synth presets (after addEffects)
const SoundRef& findSound (std::string_view name);   // valid after addSounds
const Patch& findEffect (acid::PresetType, std::string_view slug);   // valid after addEffects
void addPatterns (Library&);       // Patterns.cpp: pattern and arp presets
void addScenes (Library&);         // Scenes.cpp: scenes and the first-load default
void addTemplates (Library&);      // Templates.cpp: generator templates
}
