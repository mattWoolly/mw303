#include "acid/engine/StateCodec.h"
#include "acid/common/Utf8.h"

#include "acid/common/Math.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace acid
{
namespace
{
constexpr const char* kTypeNames[kNumPresetTypes] = {
    "state", "scene", "synth", "calibration", "distortion", "eq", "delay", "reverb", "chorus", "limiter",
    "pattern", "arp", "template"
};

struct EnumName { int value; const char* name; };

constexpr EnumName kTriggerModes[] = { { 0, "live" }, { 1, "host_note" } };
constexpr EnumName kLaunchQuants[] = { { 0, "pattern_end" }, { 1, "next_bar" }, { 2, "immediate" } };
constexpr EnumName kSlideIntoRests[] = { { 0, "release" }, { 1, "hold" }, { 2, "sustain" } };
constexpr EnumName kNotePriorities[] = { { 0, "last" }, { 1, "low" }, { 2, "high" } };
constexpr EnumName kQualities[] = { { 0, "draft" }, { 1, "normal" }, { 2, "high" }, { 3, "ultra" } };

struct SettingsAttr
{
    SettingsField field;
    const char* name;
};

/** Written in this order. */
constexpr SettingsAttr kSettingsAttrs[kNumSettingsFields] = {
    { kSetTriggerMode, "trigger_mode" },
    { kSetLaunchQuant, "launch_quant" },
    { kSetSlideIntoRest, "slide_into_rest" },
    { kSetNotePriority, "note_priority" },
    { kSetFollowHost, "follow_host" },
    { kSetHostLink, "host_link" },
    { kSetRestartOnTrigger, "restart_on_trigger" },
    { kSetInternalBpm, "internal_bpm" },
    { kSetHostNoteBase, "host_note_base" },
    { kSetAccentThreshold, "accent_threshold" },
    { kSetBendRange, "bend_range" },
    { kSetMidiOut, "midi_out" },
    { kSetMidiThru, "midi_thru" },
    { kSetOutAccentVelocity, "out_accent_velocity" },
    { kSetOutNormalVelocity, "out_normal_velocity" },
    { kSetRealtimeQuality, "realtime_quality" },
    { kSetOfflineQuality, "offline_quality" },
    { kSetFxOrder, "fx_order" },
};

template <std::size_t N>
const char* enumName (const EnumName (&table)[N], int value) noexcept
{
    for (const auto& e : table)
        if (e.value == value) return e.name;
    return table[0].name;
}

template <std::size_t N>
bool enumValue (const EnumName (&table)[N], std::string_view name, int& value) noexcept
{
    for (const auto& e : table)
        if (name == e.name)
        {
            value = e.value;
            return true;
        }
    return false;
}

bool sameGroup (const ParamInfo& info, const char* group) noexcept
{
    return std::strcmp (info.group, group) == 0;
}

/** Momentary performance controls that a saved scene must not replay. */
bool isMomentary (ParamId id) noexcept
{
    switch (id)
    {
        case kSeqRun:
        case kOvrNoteSet: case kOvrNoteClr: case kOvrSlideSet: case kOvrSlideClr:
        case kOvrAccentSet: case kOvrAccentClr: case kOvrVibSet: case kOvrVibClr:
            return true;
        default:
            return false;
    }
}

std::string intText (long long v)
{
    char buf[32];
    const auto r = std::to_chars (buf, buf + sizeof (buf), v);
    return std::string (buf, r.ptr);
}

bool parseInt (std::string_view text, long long& out) noexcept
{
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    if (*begin == '+') ++begin;
    const auto r = std::from_chars (begin, end, out);
    return r.ec == std::errc {} && r.ptr == end;
}

/** True when a number too small or too large for a double is on the small side (its exponent
    is negative, or it has no exponent and no non-zero digit before the point). */
bool isUnderflow (std::string_view text) noexcept
{
    const auto e = text.find_first_of ("eE");
    if (e != std::string_view::npos)
        return e + 1 < text.size() && text[e + 1] == '-';
    const auto point = text.find ('.');
    const std::string_view integer = text.substr (0, point);
    return integer.find_first_of ("123456789") == std::string_view::npos;
}

/** Reads a decimal number. Values written with more magnitude than a double holds ("1e999")
    come back as the float limit of the right sign (callers clamp to the row range anyway);
    tiny ones as 0. The words "inf", "infinity" and "nan" in any case, and anything else that
    is not a plain decimal number, are rejected. */
bool parseFloat (std::string_view text, float& out) noexcept
{
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    if (*begin == '+') ++begin;
    if (begin == end) return false;

    double d = 0.0;
    const auto r = std::from_chars (begin, end, d);
    if (r.ptr != end) return false;

    if (r.ec == std::errc::result_out_of_range)
        d = isUnderflow ({ begin, static_cast<std::size_t> (end - begin) }) ? 0.0
          : (*begin == '-' ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity());
    else if (r.ec != std::errc {})
        return false;
    else if (! std::isfinite (d))
        return false;    // the words inf, infinity and nan

    out = static_cast<float> (clamp (d, -static_cast<double> (std::numeric_limits<float>::max()),
                                     static_cast<double> (std::numeric_limits<float>::max())));
    return true;
}

/** Reads an integer attribute into a clamped range; counts repairs. Returns false when absent. */
bool readIntAttr (const xml::Element& el, const char* name, long long lo, long long hi, long long def,
                  long long& out, int& adjusted) noexcept
{
    const std::string* v = el.attribute (name);
    if (v == nullptr) return false;

    long long parsed = 0;
    if (! parseInt (*v, parsed))
    {
        ++adjusted;
        out = def;
        return true;
    }

    if (parsed < lo || parsed > hi)
    {
        ++adjusted;
        parsed = clamp (parsed, lo, hi);
    }
    out = parsed;
    return true;
}

/** Shortest text that reads back to the same float; "0" for zero of either sign. */
std::string floatText (float value)
{
    if (value == 0.0f) return "0";
    char buf[64];
    const auto r = std::to_chars (buf, buf + sizeof (buf), value);
    return std::string (buf, r.ptr);
}

/** Reads a decimal attribute into a clamped range; counts repairs. Returns false when absent. */
bool readFloatAttr (const xml::Element& el, const char* name, float lo, float hi, float def,
                    float& out, int& adjusted) noexcept
{
    const std::string* v = el.attribute (name);
    if (v == nullptr) return false;

    float parsed = 0.0f;
    if (! parseFloat (*v, parsed))
    {
        ++adjusted;
        out = def;
        return true;
    }

    if (parsed < lo || parsed > hi)
    {
        ++adjusted;
        parsed = clamp (parsed, lo, hi);
    }
    out = parsed;
    return true;
}

constexpr float kMinInternalBpm = 20.0f;
constexpr float kMaxInternalBpm = 300.0f;

/** The internal tempo as it may be written or applied: finite and within 20..300. */
float internalBpmSanitised (float bpm) noexcept
{
    if (! std::isfinite (bpm)) return Settings {}.internalBpm;
    return clamp (bpm, kMinInternalBpm, kMaxInternalBpm);
}

template <std::size_t N>
bool readEnumAttr (const xml::Element& el, const char* name, const EnumName (&table)[N], int def, int& out, int& adjusted) noexcept
{
    const std::string* v = el.attribute (name);
    if (v == nullptr) return false;

    if (! enumValue (table, *v, out))
    {
        ++adjusted;
        out = def;
    }
    return true;
}

std::string fxOrderText (const std::array<std::uint8_t, kNumFxSlots>& order)
{
    std::string s;
    for (int i = 0; i < kNumFxSlots; ++i)
    {
        if (i > 0) s.push_back (',');
        s += intText (order[static_cast<std::size_t> (i)]);
    }
    return s;
}

bool parseFxOrder (std::string_view text, std::array<std::uint8_t, kNumFxSlots>& order) noexcept
{
    std::array<std::uint8_t, kNumFxSlots> parsed {};
    unsigned seen = 0;
    int count = 0;
    std::size_t pos = 0;

    while (count < kNumFxSlots)
    {
        const auto comma = text.find (',', pos);
        const std::string_view token = text.substr (pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        long long v = 0;
        if (! parseInt (token, v) || v < 0 || v >= kNumFxSlots || (seen & (1u << v)) != 0)
            return false;
        parsed[static_cast<std::size_t> (count++)] = static_cast<std::uint8_t> (v);
        seen |= 1u << v;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }

    if (count != kNumFxSlots) return false;
    order = parsed;
    return true;
}

/** The order itself when it is a permutation of the six slots, otherwise the default. */
std::array<std::uint8_t, kNumFxSlots> fxOrderSanitised (const std::array<std::uint8_t, kNumFxSlots>& order) noexcept
{
    unsigned seen = 0;
    for (const auto s : order)
    {
        if (s >= kNumFxSlots) return Settings {}.fxOrder;
        seen |= 1u << s;
    }
    return seen == (1u << kNumFxSlots) - 1u ? order : Settings {}.fxOrder;
}

bool isEmptyPattern (const seq::Pattern& p) noexcept
{
    static const seq::Pattern empty {};
    return seq::samePattern (p, empty);
}

void writeAttr (std::string& out, const char* name, std::string_view value)
{
    out.push_back (' ');
    out += name;
    out += "=\"";
    out += xml::escapeAttribute (value);
    out.push_back ('"');
}

using KnownNameFn = bool (*) (std::string_view) noexcept;

/** The kept unknown attributes of an element, in their order, skipping any that would clash
    with a name this build writes itself or that is not a name at all. */
void writeExtraAttrs (std::string& out, const std::vector<xml::Attribute>& attrs, KnownNameFn known)
{
    for (const auto& a : attrs)
        if (xml::isValidName (a.name) && ! known (a.name))
            writeAttr (out, a.name.c_str(), a.value);
}

/** Every attribute of an element but the named known ones, in document order. */
template <std::size_t N>
std::vector<xml::Attribute> extraAttrsOf (const xml::Element& el, const char* const (&known)[N])
{
    std::vector<xml::Attribute> extras;
    for (const auto& a : el.attributes)
    {
        bool isKnown = false;
        for (const char* k : known)
            if (a.name == k) isKnown = true;
        if (! isKnown) extras.push_back (a);
    }
    return extras;
}

constexpr const char* kTemplateAttrs[] = { "note_mask", "root", "octave_mask", "slide", "accent", "vibrato",
                                           "amount_note", "amount_slide", "amount_accent", "amount_vibrato" };

bool isKnownElement (std::string_view name) noexcept
{
    return name == "params" || name == "settings" || name == "bank" || name == "pattern" || name == "template";
}

bool isKnownRootAttribute (std::string_view name) noexcept
{
    return name == "schema" || name == "type" || name == "name" || name == "author" || name == "tags" || name == "description";
}

bool isKnownSettingsAttribute (std::string_view name) noexcept
{
    for (const auto& a : kSettingsAttrs)
        if (name == a.name) return true;
    return false;
}

bool isKnownParamAttribute (std::string_view name) noexcept { return name == "id" || name == "v"; }
bool isKnownSlotAttribute (std::string_view name) noexcept { return name == "slot"; }
bool isKnownBankChild (std::string_view name) noexcept { return name == "seq" || name == "arp"; }
bool noKnownAttribute (std::string_view) noexcept { return false; }

bool isKnownTemplateAttribute (std::string_view name) noexcept
{
    for (const char* k : kTemplateAttrs)
        if (name == k) return true;
    return false;
}

void writeSlots (std::string& out, const char* tag, std::vector<SlotPattern> slots)
{
    std::sort (slots.begin(), slots.end(), [] (const SlotPattern& a, const SlotPattern& b) { return a.slot < b.slot; });
    int lastSlot = -1;
    for (const auto& s : slots)
    {
        if (s.slot < 0 || s.slot >= seq::kNumSlots || s.slot == lastSlot) continue;
        lastSlot = s.slot;
        out += "    <";
        out += tag;
        out += " slot=\"";
        out += intText (s.slot);
        out.push_back ('"');
        writeExtraAttrs (out, s.extraAttributes, &isKnownSlotAttribute);
        out.push_back ('>');
        out += xml::escapeText (seq::encodePattern (s.pattern));
        out += "</";
        out += tag;
        out += ">\n";
    }
}

void readSlots (const xml::Element& bank, const char* tag, std::vector<SlotPattern>& slots, PresetParseResult& r)
{
    std::array<bool, seq::kNumSlots> seen {};
    for (const auto& c : bank.children)
    {
        if (c.name != tag) continue;

        long long slot = -1;
        const std::string* slotText = c.attribute ("slot");
        if (slotText == nullptr || ! parseInt (*slotText, slot) || slot < 0 || slot >= seq::kNumSlots
            || seen[static_cast<std::size_t> (slot)])
        {
            ++r.ignored;
            continue;
        }

        SlotPattern sp;
        sp.slot = static_cast<int> (slot);
        const seq::DecodeResult d = seq::decodePattern (c.text, sp.pattern);
        if (! d.ok)
        {
            ++r.adjusted;
            continue;
        }
        r.adjusted += d.adjusted;
        r.adjusted += seq::sanitizePattern (sp.pattern);
        sp.extraAttributes = extraAttrsOf (c, { "slot" });
        seen[static_cast<std::size_t> (slot)] = true;
        slots.push_back (std::move (sp));
    }
}

/** How many bits of a settings mask are set. */
int countBits (std::uint32_t m) noexcept
{
    int n = 0;
    for (; m != 0; m &= m - 1) ++n;
    return n;
}
}

// ---- Names --------------------------------------------------------------------------------

const char* presetTypeName (PresetType t) noexcept
{
    const auto i = static_cast<std::size_t> (t);
    return i < static_cast<std::size_t> (kNumPresetTypes) ? kTypeNames[i] : kTypeNames[1];
}

bool parsePresetType (std::string_view name, PresetType& out) noexcept
{
    for (int i = 0; i < kNumPresetTypes; ++i)
        if (name == kTypeNames[i])
        {
            out = static_cast<PresetType> (i);
            return true;
        }
    return false;
}

// ---- PresetData ---------------------------------------------------------------------------

const xml::Element* PresetData::extra (std::string_view elementName) const noexcept
{
    for (const auto& e : extraElements)
        if (e.name == elementName) return &e;
    return nullptr;
}

xml::Element* PresetData::extra (std::string_view elementName) noexcept
{
    for (auto& e : extraElements)
        if (e.name == elementName) return &e;
    return nullptr;
}

void PresetData::setExtra (xml::Element element)
{
    for (auto& e : extraElements)
        if (e.name == element.name)
        {
            e = std::move (element);
            return;
        }
    extraElements.push_back (std::move (element));
}

void PresetData::setParam (ParamId id, float value) noexcept
{
    const auto i = static_cast<std::size_t> (id);
    if (i >= static_cast<std::size_t> (kNumParams)) return;
    const ParamInfo& info = kParamTable[i];
    hasParam[i] = true;
    params[i] = std::isfinite (value) ? clamp (value, info.min, info.max) : info.def;
}

bool PresetData::anyParam() const noexcept
{
    return std::any_of (hasParam.begin(), hasParam.end(), [] (bool b) { return b; });
}

// ---- Type rules ---------------------------------------------------------------------------

bool paramInType (PresetType type, ParamId id) noexcept
{
    const ParamInfo& info = paramInfo (id);
    switch (type)
    {
        case PresetType::State: return true;
        case PresetType::Scene: return ! isMomentary (id);
        case PresetType::Synth:
            return sameGroup (info, "Synth") || sameGroup (info, "Calibration") || sameGroup (info, "Mods")
                || sameGroup (info, "Distortion") || sameGroup (info, "Distortion Voicing") || sameGroup (info, "EQ")
                || sameGroup (info, "Delay") || sameGroup (info, "Reverb") || sameGroup (info, "Chorus")
                || sameGroup (info, "Limiter") || sameGroup (info, "Master");
        case PresetType::Calibration: return sameGroup (info, "Calibration");
        case PresetType::Distortion: return sameGroup (info, "Distortion") || sameGroup (info, "Distortion Voicing");
        case PresetType::Eq: return sameGroup (info, "EQ");
        case PresetType::Delay: return sameGroup (info, "Delay");
        case PresetType::Reverb: return sameGroup (info, "Reverb");
        case PresetType::Chorus: return sameGroup (info, "Chorus");
        case PresetType::Limiter: return sameGroup (info, "Limiter");
        case PresetType::Pattern:
        case PresetType::Arp:
        case PresetType::Template:
            return false;
    }
    return false;
}

std::uint32_t settingsFieldsInType (PresetType type) noexcept
{
    switch (type)
    {
        case PresetType::State:
        case PresetType::Scene: return kSetAll;
        case PresetType::Synth: return kSetFxOrder;
        default: return 0;
    }
}

bool bankInType (PresetType type) noexcept
{
    return type == PresetType::State || type == PresetType::Scene;
}

void restrictToType (PresetData& d) noexcept
{
    bool anyParamInType = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        if (! paramInType (d.type, static_cast<ParamId> (i)))
            d.hasParam[static_cast<std::size_t> (i)] = false;
        else
            anyParamInType = true;
    }

    // Unknown attributes travel with a row that is carried.
    std::erase_if (d.extraParamAttributes, [&d] (const ParamAttributes& pa) {
        return pa.param < 0 || pa.param >= kNumParams || ! d.hasParam[static_cast<std::size_t> (pa.param)];
    });

    if (! anyParamInType)
    {
        d.extraParamsAttributes.clear();
        d.extraParamElements.clear();
    }

    d.settingsMask &= settingsFieldsInType (d.type);
    if (settingsFieldsInType (d.type) == 0)
        d.extraSettingsAttributes.clear();

    if (! bankInType (d.type))
    {
        d.hasBank = false;
        d.seqSlots.clear();
        d.arpSlots.clear();
    }

    if (d.type != PresetType::Pattern && d.type != PresetType::Arp)
        d.hasPattern = false;

    if (d.type != PresetType::Template)
        d.hasTemplate = false;

    // Unknown attributes and children of an element that is not written have nowhere to go.
    if (! d.hasBank)
    {
        d.extraBankAttributes.clear();
        d.extraBankElements.clear();
    }
    if (! d.hasPattern) d.extraPatternAttributes.clear();
    if (! d.hasTemplate) d.extraTemplateAttributes.clear();
}

PresetData makePreset (PresetType type, const ParamValues& values, const Settings& settings, const seq::PatternBank* bank)
{
    PresetData d;
    d.type = type;

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<ParamId> (i);
        if (paramInType (type, id))
            d.setParam (id, values[static_cast<std::size_t> (i)]);
    }

    d.settingsMask = settingsFieldsInType (type);
    d.settings = settings;

    if (bankInType (type) && bank != nullptr)
    {
        d.hasBank = true;
        for (int s = 0; s < seq::kNumSlots; ++s)
        {
            const auto i = static_cast<std::size_t> (s);
            if (! isEmptyPattern (bank->seq[i])) d.seqSlots.push_back ({ s, bank->seq[i] });
            if (! isEmptyPattern (bank->arp[i])) d.arpSlots.push_back ({ s, bank->arp[i] });
        }
    }

    restrictToType (d);
    return d;
}

PresetData makePatternPreset (PresetType patternOrArp, const seq::Pattern& p)
{
    PresetData d;
    d.type = patternOrArp == PresetType::Arp ? PresetType::Arp : PresetType::Pattern;
    d.hasPattern = true;
    d.pattern = p;
    d.name.assign (p.name, strnlen (p.name, seq::kPatternNameLength));
    return d;
}

PresetData makeTemplatePreset (const seq::GenParams& g)
{
    PresetData d;
    d.type = PresetType::Template;
    d.hasTemplate = true;
    d.genParams = g;
    return d;
}

// ---- Value text ---------------------------------------------------------------------------

std::string formatParamValue (const ParamInfo& info, float value)
{
    if (! std::isfinite (value)) value = info.def;
    value = clamp (value, info.min, info.max);

    if (info.kind != ParamKind::Float)
        return intText (std::lround (value));

    if (value == 0.0f) return "0";   // no "-0"

    char buf[64];
    const auto r = std::to_chars (buf, buf + sizeof (buf), value);
    return std::string (buf, r.ptr);
}

bool parseParamValue (const ParamInfo& info, std::string_view text, float& value, bool& clamped) noexcept
{
    clamped = false;
    float v = 0.0f;
    if (! parseFloat (text, v)) return false;

    if (info.kind != ParamKind::Float)
        v = std::round (v);

    const float c = clamp (v, info.min, info.max);
    clamped = c != v;
    value = c;
    return true;
}

// ---- Writing ------------------------------------------------------------------------------

std::string writePreset (const PresetData& d)
{
    std::string out;
    out.reserve (4096);

    // The type rules, applied while writing (restrictToType without the copy).
    bool anyParam = false, anyParamInType = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        if (! paramInType (d.type, static_cast<ParamId> (i))) continue;
        anyParamInType = true;
        if (d.hasParam[static_cast<std::size_t> (i)]) anyParam = true;
    }
    const std::uint32_t settingsMask = d.settingsMask & settingsFieldsInType (d.type);
    const bool settingsInType = settingsFieldsInType (d.type) != 0;
    const bool bank = d.hasBank && bankInType (d.type);
    const bool pattern = d.hasPattern && (d.type == PresetType::Pattern || d.type == PresetType::Arp);
    const bool tpl = d.hasTemplate && d.type == PresetType::Template;

    out += "<";
    out += kPresetRootName;
    writeAttr (out, "schema", intText (std::max (d.schema, kPresetSchema)));
    writeAttr (out, "type", presetTypeName (d.type));
    if (! d.name.empty()) writeAttr (out, "name", d.name);
    if (! d.author.empty()) writeAttr (out, "author", d.author);
    if (! d.tags.empty()) writeAttr (out, "tags", d.tags);
    if (! d.description.empty()) writeAttr (out, "description", d.description);
    writeExtraAttrs (out, d.extraRootAttributes, &isKnownRootAttribute);
    out += ">\n";

    if (anyParam || (anyParamInType && (! d.extraParamElements.empty() || ! d.extraParamsAttributes.empty())))
    {
        out += "  <params";
        writeExtraAttrs (out, d.extraParamsAttributes, &noKnownAttribute);
        out += ">\n";
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto idx = static_cast<std::size_t> (i);
            if (! d.hasParam[idx] || ! paramInType (d.type, static_cast<ParamId> (i))) continue;
            const ParamInfo& info = kParamTable[idx];
            out += "    <p id=\"";
            out += info.id;
            out += "\" v=\"";
            out += formatParamValue (info, d.params[idx]);
            out.push_back ('"');
            for (const auto& pa : d.extraParamAttributes)
                if (pa.param == i)
                    writeExtraAttrs (out, pa.attributes, &isKnownParamAttribute);
            out += "/>\n";
        }
        for (const auto& e : d.extraParamElements)
            if (xml::isValidName (e.name))
                xml::write (out, e, 2);
        out += "  </params>\n";
    }

    if (settingsMask != 0 || (settingsInType && ! d.extraSettingsAttributes.empty()))
    {
        const Settings& s = d.settings;
        out += "  <settings";
        for (const auto& a : kSettingsAttrs)
        {
            if ((settingsMask & a.field) == 0) continue;
            switch (a.field)
            {
                case kSetTriggerMode: writeAttr (out, a.name, enumName (kTriggerModes, static_cast<int> (s.triggerMode))); break;
                case kSetLaunchQuant: writeAttr (out, a.name, enumName (kLaunchQuants, static_cast<int> (s.launchQuant))); break;
                case kSetSlideIntoRest: writeAttr (out, a.name, enumName (kSlideIntoRests, static_cast<int> (s.slideIntoRest))); break;
                case kSetNotePriority: writeAttr (out, a.name, enumName (kNotePriorities, static_cast<int> (s.notePriority))); break;
                case kSetFollowHost: writeAttr (out, a.name, s.followHostTransport ? "1" : "0"); break;
                case kSetHostLink: writeAttr (out, a.name, s.hostLink ? "1" : "0"); break;
                case kSetRestartOnTrigger: writeAttr (out, a.name, s.restartOnTrigger ? "1" : "0"); break;
                case kSetInternalBpm: writeAttr (out, a.name, floatText (internalBpmSanitised (s.internalBpm))); break;
                case kSetHostNoteBase: writeAttr (out, a.name, intText (std::min<int> (s.hostNoteBase, 127))); break;
                case kSetAccentThreshold: writeAttr (out, a.name, intText (std::min<int> (s.accentThreshold, 127))); break;
                case kSetBendRange: writeAttr (out, a.name, intText (std::min<int> (s.bendRange, 12))); break;
                case kSetMidiOut: writeAttr (out, a.name, s.midiOut ? "1" : "0"); break;
                case kSetMidiThru: writeAttr (out, a.name, s.midiThru ? "1" : "0"); break;
                case kSetOutAccentVelocity: writeAttr (out, a.name, intText (std::min<int> (s.outAccentVelocity, 127))); break;
                case kSetOutNormalVelocity: writeAttr (out, a.name, intText (std::min<int> (s.outNormalVelocity, 127))); break;
                case kSetRealtimeQuality: writeAttr (out, a.name, enumName (kQualities, static_cast<int> (s.realtimeQuality))); break;
                case kSetOfflineQuality: writeAttr (out, a.name, enumName (kQualities, static_cast<int> (s.offlineQuality))); break;
                case kSetFxOrder: writeAttr (out, a.name, fxOrderText (fxOrderSanitised (s.fxOrder))); break;
                default: break;
            }
        }
        writeExtraAttrs (out, d.extraSettingsAttributes, &isKnownSettingsAttribute);
        out += "/>\n";
    }

    if (bank)
    {
        out += "  <bank";
        writeExtraAttrs (out, d.extraBankAttributes, &noKnownAttribute);
        out += ">\n";
        writeSlots (out, "seq", d.seqSlots);
        writeSlots (out, "arp", d.arpSlots);
        for (const auto& e : d.extraBankElements)
            if (xml::isValidName (e.name) && ! isKnownBankChild (e.name))
                xml::write (out, e, 2);
        out += "  </bank>\n";
    }

    if (pattern)
    {
        out += "  <pattern";
        writeExtraAttrs (out, d.extraPatternAttributes, &noKnownAttribute);
        out.push_back ('>');
        out += xml::escapeText (seq::encodePattern (d.pattern));
        out += "</pattern>\n";
    }

    if (tpl)
    {
        const seq::GenParams& g = d.genParams;
        out += "  <template";
        writeAttr (out, "note_mask", intText (g.noteMask & 0x0FFF));
        writeAttr (out, "root", intText (std::min<int> (g.root, 11)));
        writeAttr (out, "octave_mask", intText (g.octaveMask & 0x1F));
        writeAttr (out, "slide", g.useSlide ? "1" : "0");
        writeAttr (out, "accent", g.useAccent ? "1" : "0");
        writeAttr (out, "vibrato", g.useVibrato ? "1" : "0");
        writeAttr (out, "amount_note", intText (std::min<int> (g.amountNote, 100)));
        writeAttr (out, "amount_slide", intText (std::min<int> (g.amountSlide, 100)));
        writeAttr (out, "amount_accent", intText (std::min<int> (g.amountAccent, 100)));
        writeAttr (out, "amount_vibrato", intText (std::min<int> (g.amountVibrato, 100)));
        writeExtraAttrs (out, d.extraTemplateAttributes, &isKnownTemplateAttribute);
        out += "/>\n";
    }

    for (const auto& e : d.extraElements)
        if (xml::isValidName (e.name) && ! isKnownElement (e.name))
            xml::write (out, e, 1);

    out += "</";
    out += kPresetRootName;
    out += ">\n";
    return out;
}

// ---- Reading ------------------------------------------------------------------------------

void migratePresetSchema (xml::Element&, int)
{
    // One schema so far. Each future step edits the tree from schema n to n + 1 here.
}

namespace
{
/** The conversion itself. With steal set the unknown subtrees are moved out of root, which
    is then left with their husks; otherwise root is only read. */
PresetParseResult presetFromXmlImpl (xml::Element& root, PresetData& out, bool steal)
{
    PresetParseResult r;
    PresetData d;
    const auto take = [steal] (xml::Element& e) -> xml::Element {
        if (steal) return std::move (e);
        return e;
    };

    if (root.name != kPresetRootName)
    {
        r.error = "not a preset document (root element is not " + std::string (kPresetRootName) + ")";
        return r;
    }

    if (const std::string* schema = root.attribute ("schema"))
    {
        long long s = 0;
        if (! parseInt (*schema, s) || s < 1 || s > std::numeric_limits<int>::max())
        {
            r.error = "bad schema attribute";
            return r;
        }
        r.schema = static_cast<int> (s);
    }
    else
    {
        r.error = "missing schema attribute";
        return r;
    }
    d.schema = r.schema;

    if (const std::string* type = root.attribute ("type"))
    {
        if (! parsePresetType (*type, d.type))
        {
            r.error = "unknown preset type: " + *type;
            return r;
        }
    }
    else
    {
        r.error = "missing type attribute";
        return r;
    }

    // Free text: repaired to valid UTF-8 (an invalid sequence becomes U+FFFD and counts as
    // an adjustment), so nothing downstream ever sees broken text.
    const auto text = [&] (const char* attr, std::string& dst) {
        if (const std::string* v = root.attribute (attr))
        {
            dst = *v;
            if (utf8::repair (dst)) ++r.adjusted;
        }
    };
    text ("name", d.name);
    text ("author", d.author);
    text ("tags", d.tags);
    text ("description", d.description);
    for (const auto& a : root.attributes)
        if (! isKnownRootAttribute (a.name))
            d.extraRootAttributes.push_back (a);

    if (xml::Element* params = root.child ("params"))
    {
        d.extraParamsAttributes = params->attributes;
        for (auto& p : params->children)
        {
            // A <p> with an unknown id (or no id, or no value) and any other child are kept
            // for the re-save, not understood.
            const std::string* id = p.name == "p" ? p.attribute ("id") : nullptr;
            const std::string* v = p.name == "p" ? p.attribute ("v") : nullptr;
            const int index = id != nullptr ? findParam (*id) : -1;
            if (index < 0 || v == nullptr)
            {
                if (p.name == "p") ++r.ignored;
                d.extraParamElements.push_back (take (p));
                continue;
            }
            float value = 0.0f;
            bool clamped = false;
            if (! parseParamValue (kParamTable[static_cast<std::size_t> (index)], *v, value, clamped))
            {
                ++r.adjusted;
                continue;
            }
            if (clamped) ++r.adjusted;
            const auto idx = static_cast<std::size_t> (index);
            d.hasParam[idx] = true;
            d.params[idx] = value;

            std::vector<xml::Attribute> extras = extraAttrsOf (p, { "id", "v" });
            std::erase_if (d.extraParamAttributes, [index] (const ParamAttributes& pa) { return pa.param == index; });
            if (! extras.empty())
                d.extraParamAttributes.push_back ({ index, std::move (extras) });
        }
    }

    if (const xml::Element* se = root.child ("settings"))
    {
        Settings& s = d.settings;
        long long i = 0;
        int e = 0;

        if (readEnumAttr (*se, "trigger_mode", kTriggerModes, 0, e, r.adjusted)) { s.triggerMode = static_cast<seq::TriggerMode> (e); d.settingsMask |= kSetTriggerMode; }
        if (readEnumAttr (*se, "launch_quant", kLaunchQuants, 0, e, r.adjusted)) { s.launchQuant = static_cast<seq::LaunchQuant> (e); d.settingsMask |= kSetLaunchQuant; }
        if (readEnumAttr (*se, "slide_into_rest", kSlideIntoRests, 0, e, r.adjusted)) { s.slideIntoRest = static_cast<seq::SlideIntoRest> (e); d.settingsMask |= kSetSlideIntoRest; }
        if (readEnumAttr (*se, "note_priority", kNotePriorities, 0, e, r.adjusted)) { s.notePriority = static_cast<seq::NotePriority> (e); d.settingsMask |= kSetNotePriority; }
        if (readIntAttr (*se, "follow_host", 0, 1, 1, i, r.adjusted)) { s.followHostTransport = i != 0; d.settingsMask |= kSetFollowHost; }
        if (readIntAttr (*se, "host_link", 0, 1, 1, i, r.adjusted)) { s.hostLink = i != 0; d.settingsMask |= kSetHostLink; }
        if (readIntAttr (*se, "restart_on_trigger", 0, 1, 1, i, r.adjusted)) { s.restartOnTrigger = i != 0; d.settingsMask |= kSetRestartOnTrigger; }
        float f = 0.0f;
        if (readFloatAttr (*se, "internal_bpm", kMinInternalBpm, kMaxInternalBpm, Settings {}.internalBpm, f, r.adjusted)) { s.internalBpm = f; d.settingsMask |= kSetInternalBpm; }
        if (readIntAttr (*se, "host_note_base", 0, 127, 24, i, r.adjusted)) { s.hostNoteBase = static_cast<std::uint8_t> (i); d.settingsMask |= kSetHostNoteBase; }
        if (readIntAttr (*se, "accent_threshold", 0, 127, 64, i, r.adjusted)) { s.accentThreshold = static_cast<std::uint8_t> (i); d.settingsMask |= kSetAccentThreshold; }
        if (readIntAttr (*se, "bend_range", 0, 12, 2, i, r.adjusted)) { s.bendRange = static_cast<std::uint8_t> (i); d.settingsMask |= kSetBendRange; }
        if (readIntAttr (*se, "midi_out", 0, 1, 1, i, r.adjusted)) { s.midiOut = i != 0; d.settingsMask |= kSetMidiOut; }
        if (readIntAttr (*se, "midi_thru", 0, 1, 0, i, r.adjusted)) { s.midiThru = i != 0; d.settingsMask |= kSetMidiThru; }
        if (readIntAttr (*se, "out_accent_velocity", 0, 127, 96, i, r.adjusted)) { s.outAccentVelocity = static_cast<std::uint8_t> (i); d.settingsMask |= kSetOutAccentVelocity; }
        if (readIntAttr (*se, "out_normal_velocity", 0, 127, 32, i, r.adjusted)) { s.outNormalVelocity = static_cast<std::uint8_t> (i); d.settingsMask |= kSetOutNormalVelocity; }
        if (readEnumAttr (*se, "realtime_quality", kQualities, 1, e, r.adjusted)) { s.realtimeQuality = static_cast<Quality> (e); d.settingsMask |= kSetRealtimeQuality; }
        if (readEnumAttr (*se, "offline_quality", kQualities, 2, e, r.adjusted)) { s.offlineQuality = static_cast<Quality> (e); d.settingsMask |= kSetOfflineQuality; }
        if (const std::string* v = se->attribute ("fx_order"))
        {
            if (! parseFxOrder (*v, s.fxOrder))
            {
                ++r.adjusted;
                s.fxOrder = Settings {}.fxOrder;
            }
            d.settingsMask |= kSetFxOrder;
        }

        for (const auto& a : se->attributes)
            if (! isKnownSettingsAttribute (a.name))
                d.extraSettingsAttributes.push_back (a);
    }

    if (xml::Element* bank = root.child ("bank"))
    {
        d.hasBank = true;
        d.extraBankAttributes = bank->attributes;
        readSlots (*bank, "seq", d.seqSlots, r);
        readSlots (*bank, "arp", d.arpSlots, r);
        for (auto& c : bank->children)
            if (! isKnownBankChild (c.name))
                d.extraBankElements.push_back (take (c));
    }

    if (const xml::Element* pat = root.child ("pattern"))
    {
        seq::Pattern p;
        const seq::DecodeResult dr = seq::decodePattern (pat->text, p);
        if (dr.ok)
        {
            r.adjusted += dr.adjusted;
            r.adjusted += seq::sanitizePattern (p);
            d.hasPattern = true;
            d.pattern = p;
            d.extraPatternAttributes = pat->attributes;
        }
        else
        {
            ++r.adjusted;
        }
    }

    if (const xml::Element* t = root.child ("template"))
    {
        seq::GenParams& g = d.genParams;
        d.extraTemplateAttributes = extraAttrsOf (*t, kTemplateAttrs);
        long long i = 0;
        if (readIntAttr (*t, "note_mask", 0, 0x0FFF, 0x0FFF, i, r.adjusted)) g.noteMask = static_cast<std::uint16_t> (i);
        if (readIntAttr (*t, "root", 0, 11, 0, i, r.adjusted)) g.root = static_cast<std::uint8_t> (i);
        if (readIntAttr (*t, "octave_mask", 0, 0x1F, 0b00110, i, r.adjusted)) g.octaveMask = static_cast<std::uint8_t> (i);
        if (readIntAttr (*t, "slide", 0, 1, 1, i, r.adjusted)) g.useSlide = i != 0;
        if (readIntAttr (*t, "accent", 0, 1, 1, i, r.adjusted)) g.useAccent = i != 0;
        if (readIntAttr (*t, "vibrato", 0, 1, 0, i, r.adjusted)) g.useVibrato = i != 0;
        if (readIntAttr (*t, "amount_note", 0, 100, 50, i, r.adjusted)) g.amountNote = static_cast<std::uint8_t> (i);
        if (readIntAttr (*t, "amount_slide", 0, 100, 50, i, r.adjusted)) g.amountSlide = static_cast<std::uint8_t> (i);
        if (readIntAttr (*t, "amount_accent", 0, 100, 50, i, r.adjusted)) g.amountAccent = static_cast<std::uint8_t> (i);
        if (readIntAttr (*t, "amount_vibrato", 0, 100, 50, i, r.adjusted)) g.amountVibrato = static_cast<std::uint8_t> (i);
        d.hasTemplate = true;
    }

    for (auto& c : root.children)
        if (! isKnownElement (c.name))
            d.extraElements.push_back (take (c));

    // The type rules: whatever the document carries that its type does not is counted and dropped.
    for (int i = 0; i < kNumParams; ++i)
        if (d.hasParam[static_cast<std::size_t> (i)] && ! paramInType (d.type, static_cast<ParamId> (i)))
            ++r.ignored;
    r.ignored += countBits (d.settingsMask & ~settingsFieldsInType (d.type));
    if (d.hasBank && ! bankInType (d.type))
        r.ignored += 1 + static_cast<int> (d.seqSlots.size() + d.arpSlots.size());
    if (d.hasPattern && d.type != PresetType::Pattern && d.type != PresetType::Arp)
        ++r.ignored;
    if (d.hasTemplate && d.type != PresetType::Template)
        ++r.ignored;
    restrictToType (d);

    out = std::move (d);
    r.ok = true;
    return r;
}
}

PresetParseResult presetFromXml (const xml::Element& root, PresetData& out)
{
    // Read only: the const_cast is never written through while steal is false.
    return presetFromXmlImpl (const_cast<xml::Element&> (root), out, false);
}

PresetParseResult presetFromXml (xml::Element&& root, PresetData& out)
{
    return presetFromXmlImpl (root, out, true);
}

PresetParseResult parsePreset (std::string_view text, PresetData& out, const PresetParseOptions& options)
{
    PresetParseResult r;
    xml::Element root;
    const xml::ParseResult x = xml::parse (text, root, options.limits);
    if (! x.ok)
    {
        r.error = "XML: " + x.error;
        r.offset = x.offset;
        return r;
    }

    if (root.name != kPresetRootName)
    {
        r.error = "not a preset document";
        return r;
    }

    const std::string* schemaText = root.attribute ("schema");
    long long schema = 0;
    if (schemaText == nullptr || ! parseInt (*schemaText, schema) || schema < 1 || schema > std::numeric_limits<int>::max())
    {
        r.error = "bad or missing schema attribute";
        return r;
    }

    if (schema > static_cast<long long> (kPresetSchema) + options.maxForwardSkew)
    {
        r.error = "schema " + intText (schema) + " is newer than this build supports";
        r.schema = static_cast<int> (schema);
        return r;
    }

    for (int s = static_cast<int> (schema); s < kPresetSchema; ++s)
    {
        if (options.migrate != nullptr)
            options.migrate (root, s);
        root.setAttribute ("schema", intText (s + 1));
    }

    r = presetFromXml (std::move (root), out);
    r.schema = static_cast<int> (schema);
    if (r.ok)
        out.schema = static_cast<int> (schema);
    return r;
}

// ---- Applying -----------------------------------------------------------------------------

void applyParams (const PresetData& d, ParamValues& values) noexcept
{
    const bool defaults = d.type == PresetType::State;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto idx = static_cast<std::size_t> (i);
        const ParamInfo& info = kParamTable[idx];
        if (d.hasParam[idx] && paramInType (d.type, static_cast<ParamId> (i)))
            values[idx] = std::isfinite (d.params[idx]) ? clamp (d.params[idx], info.min, info.max) : info.def;
        else if (defaults)
            values[idx] = info.def;
    }
}

void applySettings (const PresetData& d, Settings& s) noexcept
{
    if (d.type == PresetType::State)
        s = Settings {};

    const Settings& in = d.settings;
    const std::uint32_t m = d.settingsMask & settingsFieldsInType (d.type);

    if (m & kSetTriggerMode) s.triggerMode = static_cast<std::uint8_t> (in.triggerMode) <= 1 ? in.triggerMode : seq::TriggerMode::Live;
    if (m & kSetLaunchQuant) s.launchQuant = static_cast<std::uint8_t> (in.launchQuant) <= 2 ? in.launchQuant : seq::LaunchQuant::EndOfPattern;
    if (m & kSetSlideIntoRest) s.slideIntoRest = static_cast<std::uint8_t> (in.slideIntoRest) <= 2 ? in.slideIntoRest : seq::SlideIntoRest::Release;
    if (m & kSetNotePriority) s.notePriority = static_cast<std::uint8_t> (in.notePriority) <= 2 ? in.notePriority : seq::NotePriority::Last;
    if (m & kSetFollowHost) s.followHostTransport = in.followHostTransport;
    if (m & kSetHostLink) s.hostLink = in.hostLink;
    if (m & kSetRestartOnTrigger) s.restartOnTrigger = in.restartOnTrigger;
    if (m & kSetInternalBpm) s.internalBpm = internalBpmSanitised (in.internalBpm);
    if (m & kSetHostNoteBase) s.hostNoteBase = std::min<std::uint8_t> (in.hostNoteBase, 127);
    if (m & kSetAccentThreshold) s.accentThreshold = std::min<std::uint8_t> (in.accentThreshold, 127);
    if (m & kSetBendRange) s.bendRange = std::min<std::uint8_t> (in.bendRange, 12);
    if (m & kSetMidiOut) s.midiOut = in.midiOut;
    if (m & kSetMidiThru) s.midiThru = in.midiThru;
    if (m & kSetOutAccentVelocity) s.outAccentVelocity = std::min<std::uint8_t> (in.outAccentVelocity, 127);
    if (m & kSetOutNormalVelocity) s.outNormalVelocity = std::min<std::uint8_t> (in.outNormalVelocity, 127);
    if (m & kSetRealtimeQuality) s.realtimeQuality = static_cast<std::uint8_t> (in.realtimeQuality) <= 3 ? in.realtimeQuality : Quality::Normal;
    if (m & kSetOfflineQuality) s.offlineQuality = static_cast<std::uint8_t> (in.offlineQuality) <= 3 ? in.offlineQuality : Quality::High;
    if (m & kSetFxOrder) s.fxOrder = fxOrderSanitised (in.fxOrder);
}

void applyBank (const PresetData& d, seq::PatternBank& bank) noexcept
{
    if (! bankInType (d.type))
        return;
    if (! d.hasBank && d.type != PresetType::State)
        return;

    bank = seq::PatternBank {};
    for (const auto& s : d.seqSlots)
        if (s.slot >= 0 && s.slot < seq::kNumSlots)
        {
            bank.seq[static_cast<std::size_t> (s.slot)] = s.pattern;
            seq::sanitizePattern (bank.seq[static_cast<std::size_t> (s.slot)]);
        }
    for (const auto& s : d.arpSlots)
        if (s.slot >= 0 && s.slot < seq::kNumSlots)
        {
            bank.arp[static_cast<std::size_t> (s.slot)] = s.pattern;
            seq::sanitizePattern (bank.arp[static_cast<std::size_t> (s.slot)]);
        }
}

void applyPreset (const PresetData& d, ParamValues& values, Settings& settings, seq::PatternBank& bank) noexcept
{
    applyParams (d, values);
    applySettings (d, settings);
    applyBank (d, bank);
}

bool applyPattern (const PresetData& d, seq::Pattern& p) noexcept
{
    if (! d.hasPattern) return false;
    p = d.pattern;
    seq::sanitizePattern (p);
    return true;
}

bool applyTemplate (const PresetData& d, seq::GenParams& g) noexcept
{
    if (! d.hasTemplate) return false;
    const seq::GenParams& in = d.genParams;
    g.noteMask = in.noteMask & 0x0FFF;
    g.root = std::min<std::uint8_t> (in.root, 11);
    g.octaveMask = in.octaveMask & 0x1F;
    g.useSlide = in.useSlide;
    g.useAccent = in.useAccent;
    g.useVibrato = in.useVibrato;
    g.amountNote = std::min<std::uint8_t> (in.amountNote, 100);
    g.amountSlide = std::min<std::uint8_t> (in.amountSlide, 100);
    g.amountAccent = std::min<std::uint8_t> (in.amountAccent, 100);
    g.amountVibrato = std::min<std::uint8_t> (in.amountVibrato, 100);
    return true;
}
}
