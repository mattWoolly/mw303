#pragma once
// The one XML envelope for plugin state, every preset type and the factory content
// (docs/SPEC.md section 6.7). JUCE-free; the plugin wraps it.
//
//   <mw12F schema="1" type="scene" name="..." author="..." tags="..." description="...">
//     <params>
//       <p id="cutoff" v="35"/>                natural units, table order, shortest round-trip text
//     </params>
//     <settings trigger_mode="live" ... fx_order="0,1,2,3,4,5"/>
//     <bank>
//       <seq slot="0">ENCODED</seq>           PatternCodec::encodePattern; empty slots omitted
//       <arp slot="0">ENCODED</arp>
//     </bank>
//     <pattern>ENCODED</pattern>              pattern and arp types
//     <template note_mask="4095" .../>        template type: the generator fields of GenParams
//     <ui .../> <midi_map>...</midi_map>      plugin state only; carried through untouched
//   </mw12F>
//
// Writing is byte-deterministic: the same PresetData gives the same bytes, and whatever is
// written reads back (control characters in a name or any other text are written as U+FFFD).
// Reading is strict about the XML (see Xml.h) and forgiving about values: every number is
// range-checked against the table, out-of-range values are clamped, unparsable values and
// unknown names fall back to the default or are skipped, and each such repair is counted in
// ParseResult::adjusted. Unknown content is kept and written back at every level: unknown
// elements, unknown attributes on the root, on <params>, <settings>, <bank>, <pattern> and
// <template>, unknown attributes on a known <p>, <seq> or <arp>, and unknown children of
// <params> and <bank> (a <p> with an id this build does not know among them). So a newer
// file of the same schema survives a round trip through an older build.
//
// Type rules (SPEC 6.7): a document carries only what its type carries. The writer leaves
// out everything else, the reader counts it in ParseResult::ignored and drops it, and the
// apply functions check again, so a calibration preset that somehow carries a bank, the Run
// switch or the trigger mode cannot wipe the patterns or start the sequencer.
//
// Applying: a `state` document restores a session, so what it does not carry takes its
// default; every other type is a preset, so what it does not carry keeps its current value.
// A bank is an exception in both cases: a slot the document omits is an empty slot.
//
// Schema: a lower schema is upgraded step by step through the migration hook before it is
// read; a higher one is rejected unless the caller allows a forward skew. A document read
// under a forward skew keeps its unknown content and is written back under its own, newer
// schema number (PresetData::schema), so that a build which knows that schema still sees
// schema-consistent content and does not run a migration over it twice.
//
// Message thread, or any non-audio thread. Everything here allocates; nothing throws.
#include "acid/engine/ParamTable.h"
#include "acid/engine/Settings.h"
#include "acid/engine/Xml.h"
#include "acid/seq/Pattern.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace acid
{
inline constexpr int kPresetSchema = 1;
inline constexpr const char* kPresetRootName = "mw12F";
inline constexpr const char* kPresetFileExtension = ".mw12f";

enum class PresetType : std::uint8_t
{
    State = 0, Scene, Synth, Calibration, Distortion, Eq, Delay, Reverb, Chorus, Limiter,
    Pattern, Arp, Template
};
inline constexpr int kNumPresetTypes = 13;

/** The type's name as written in the file ("scene", "synth", ...). */
const char* presetTypeName (PresetType) noexcept;

/** Looks a type up by name. Returns false when unknown. */
bool parsePresetType (std::string_view, PresetType&) noexcept;

/** Bits of PresetData::settingsMask: which Settings fields a document carries. */
enum SettingsField : std::uint32_t
{
    kSetTriggerMode = 1u << 0,
    kSetLaunchQuant = 1u << 1,
    kSetSlideIntoRest = 1u << 2,
    kSetNotePriority = 1u << 3,
    kSetFollowHost = 1u << 4,
    kSetHostLink = 1u << 5,
    kSetRestartOnTrigger = 1u << 6,
    kSetHostNoteBase = 1u << 7,
    kSetAccentThreshold = 1u << 8,
    kSetBendRange = 1u << 9,
    kSetMidiOut = 1u << 10,
    kSetMidiThru = 1u << 11,
    kSetOutAccentVelocity = 1u << 12,
    kSetOutNormalVelocity = 1u << 13,
    kSetRealtimeQuality = 1u << 14,
    kSetOfflineQuality = 1u << 15,
    kSetFxOrder = 1u << 16,
    kSetInternalBpm = 1u << 17,   // attribute internal_bpm, 20..300
    kSetAll = (1u << 18) - 1u
};
inline constexpr int kNumSettingsFields = 18;

/** One occupied slot of a bank. */
struct SlotPattern
{
    int slot = 0;                 // 0..seq::kNumSlots-1
    seq::Pattern pattern {};
    std::vector<xml::Attribute> extraAttributes {};   // unknown attributes of the <seq> or <arp> element, kept
};

/** Unknown attributes found on a known <p> row, kept for a re-save. */
struct ParamAttributes
{
    int param = 0;                // ParamId
    std::vector<xml::Attribute> attributes;
};

/** Everything a document can carry. Presence is explicit: a parameter is carried when its
    bit in hasParam is set, a Settings field when its bit in settingsMask is set, and so on. */
struct PresetData
{
    /** The schema the document was read with (kPresetSchema for one built in code). Written
        back as max (schema, kPresetSchema): only a forward-skewed document raises it. */
    int schema = kPresetSchema;
    PresetType type = PresetType::Scene;
    std::string name;
    std::string author;
    std::string tags;             // comma separated, as written
    std::string description;

    std::array<bool, kNumParams> hasParam {};
    ParamValues params = defaultParamValues();

    std::uint32_t settingsMask = 0;
    Settings settings {};

    bool hasBank = false;
    std::vector<SlotPattern> seqSlots;    // sorted by slot when written
    std::vector<SlotPattern> arpSlots;

    bool hasPattern = false;      // Pattern and Arp types
    seq::Pattern pattern {};

    bool hasTemplate = false;     // Template type
    seq::GenParams genParams {};  // only the generator fields are written (not the seed)

    /** Carried through untouched: ui, midi_map and any element this build does not know,
        in document order, written after the known elements. */
    std::vector<xml::Element> extraElements;
    std::vector<xml::Attribute> extraRootAttributes;
    std::vector<xml::Attribute> extraSettingsAttributes;

    /** Unknown content inside the known elements, also carried through: attributes on the
        element itself, children this build does not know (for <params>: any child that is
        not a <p> with a known id and a value), and unknown attributes on known <p> rows.
        Children are written after the known ones, in document order. */
    std::vector<xml::Attribute> extraParamsAttributes;
    std::vector<xml::Element> extraParamElements;
    std::vector<ParamAttributes> extraParamAttributes;
    std::vector<xml::Attribute> extraBankAttributes;
    std::vector<xml::Element> extraBankElements;
    std::vector<xml::Attribute> extraPatternAttributes;
    std::vector<xml::Attribute> extraTemplateAttributes;

    /** Looks an extra element up by name ("ui", "midi_map"), or nullptr. */
    const xml::Element* extra (std::string_view elementName) const noexcept;
    xml::Element* extra (std::string_view elementName) noexcept;

    /** Replaces (or adds) an extra element of that name. */
    void setExtra (xml::Element element);

    void setParam (ParamId id, float value) noexcept;
    void setSettingsField (SettingsField field) noexcept { settingsMask |= field; }
    bool anyParam() const noexcept;
};

// ---- Type rules -------------------------------------------------------------------------

/** True when documents of that type carry that parameter. */
bool paramInType (PresetType, ParamId) noexcept;

/** The Settings fields documents of that type carry (0 for most preset types). */
std::uint32_t settingsFieldsInType (PresetType) noexcept;

bool bankInType (PresetType) noexcept;

/** Drops from data everything its type does not carry, the unknown content kept inside an
    element the type does not carry included (and inside a bank, pattern or template element
    the data does not carry: it would have nowhere to be written). The reader applies it to
    every document and the writer and the apply functions obey the same rules. */
void restrictToType (PresetData&) noexcept;

/** Builds a document of a parameter-carrying type (everything but Pattern, Arp, Template)
    from the live model. bank may be null for types that do not carry one. Patterns equal
    to a default Pattern are left out as empty slots. */
PresetData makePreset (PresetType, const ParamValues&, const Settings&, const seq::PatternBank* bank = nullptr);

PresetData makePatternPreset (PresetType patternOrArp, const seq::Pattern&);
PresetData makeTemplatePreset (const seq::GenParams&);

// ---- Writing and reading ----------------------------------------------------------------

/** The document as text. Deterministic: same data, same bytes. Writes only what the type
    carries (restrictToType), so a PresetData built by hand cannot produce a file its own
    reader would have to repair. */
std::string writePreset (const PresetData&);

/** Called for each schema step below kPresetSchema, in order: migrate (root, n) must turn a
    schema-n document into a schema n+1 one, editing the element tree in place. */
using MigrationFn = void (*) (xml::Element& root, int fromSchema);

/** The built-in migrations (none yet: there is one schema). */
void migratePresetSchema (xml::Element& root, int fromSchema);

struct PresetParseOptions
{
    MigrationFn migrate = &migratePresetSchema;
    int maxForwardSkew = 0;         // schemas above kPresetSchema + this are rejected
    xml::ParseLimits limits {};
};

struct PresetParseResult
{
    bool ok = false;
    std::string error;            // when ok is false
    std::size_t offset = 0;       // byte offset of an XML error
    int schema = 0;               // the document's schema before migration
    int adjusted = 0;             // values clamped, defaulted or skipped
    int ignored = 0;              // unknown parameter ids and slots, and content outside the type (dropped)
};

/** Parses text into out. On failure out is unchanged. The default limits (xml::ParseLimits)
    bound the input size and the number of nodes, so a hostile document costs a bounded
    amount of memory. */
PresetParseResult parsePreset (std::string_view text, PresetData& out, const PresetParseOptions& = {});

/** Converts an already parsed (and migrated) element tree. The rvalue overload moves the
    unknown content out of the tree instead of copying it. */
PresetParseResult presetFromXml (const xml::Element& root, PresetData& out);
PresetParseResult presetFromXml (xml::Element&& root, PresetData& out);

// ---- Applying ---------------------------------------------------------------------------

/** Parameters: a carried one that the type carries is written (range-checked); a missing one
    keeps its value for a preset and takes its default for a `state` document. */
void applyParams (const PresetData&, ParamValues&) noexcept;

/** Settings fields, with the same rule and the same type check. A `state` document with no
    settings element restores the defaults. */
void applySettings (const PresetData&, Settings&) noexcept;

/** The bank, when carried by a type that carries one (or for a `state` document always):
    carried slots are written, the rest become empty slots. Patterns are sanitised. Any other
    type leaves the bank alone. */
void applyBank (const PresetData&, seq::PatternBank&) noexcept;

/** All three. */
void applyPreset (const PresetData&, ParamValues&, Settings&, seq::PatternBank&) noexcept;

/** The single pattern of a Pattern or Arp document. Returns false when there is none. */
bool applyPattern (const PresetData&, seq::Pattern&) noexcept;

/** The generator fields of a Template document (noteMask, root, octaveMask, the use switches
    and the four amounts; the seed is kept). Returns false when there is none. */
bool applyTemplate (const PresetData&, seq::GenParams&) noexcept;

// ---- Value text -------------------------------------------------------------------------

/** The text written for a parameter value: shortest text that reads back to the same float
    for Float rows, an integer for the other kinds. */
std::string formatParamValue (const ParamInfo&, float value);

/** Reads a parameter value in natural units: false when the text is not a number; the value
    is clamped to the row's range, rounded for non-Float kinds, and clamped is set when the
    text was out of range. */
bool parseParamValue (const ParamInfo&, std::string_view text, float& value, bool& clamped) noexcept;
}
