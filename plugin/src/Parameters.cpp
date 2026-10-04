#include "Parameters.h"

#include <cmath>
#include <cstring>

namespace acid::plugin
{
namespace
{
juce::StringArray splitChoices (const char* choices)
{
    juce::StringArray out;
    if (choices != nullptr && choices[0] != '\0')
        out.addTokens (juce::String (choices), "|", "");
    return out;
}

float roundHalfAway (float v) noexcept
{
    return v < 0.0f ? -std::floor (-v + 0.5f) : std::floor (v + 0.5f);
}

juce::String trimmedDecimal (float v, int decimals)
{
    juce::String s (static_cast<double> (v), decimals);
    if (s.containsChar ('.'))
    {
        s = s.trimCharactersAtEnd ("0");
        s = s.trimCharactersAtEnd (".");
    }
    if (s == "-0") s = "0";
    return s;
}
}

juce::NormalisableRange<float> HostParam::rangeFor (const ParamInfo& info)
{
    switch (info.kind)
    {
        case ParamKind::Bool:
            return juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f);
        case ParamKind::Int:
        case ParamKind::Choice:
            return juce::NormalisableRange<float> (info.min, info.max, 1.0f);
        case ParamKind::Float:
            break;
    }

    if (info.scale == ParamScale::Log && info.min > 0.0f && info.max > info.min)
    {
        return juce::NormalisableRange<float> (
            info.min, info.max,
            [] (float lo, float hi, float n) {
                const double ratio = static_cast<double> (hi) / static_cast<double> (lo);
                return static_cast<float> (static_cast<double> (lo) * std::pow (ratio, static_cast<double> (n)));
            },
            [] (float lo, float hi, float v) {
                const double ratio = static_cast<double> (hi) / static_cast<double> (lo);
                const double x = std::max (static_cast<double> (lo), std::min (static_cast<double> (hi), static_cast<double> (v)));
                return static_cast<float> (std::log (x / static_cast<double> (lo)) / std::log (ratio));
            });
    }
    return juce::NormalisableRange<float> (info.min, info.max);
}

juce::AudioProcessorParameterWithIDAttributes HostParam::attributesFor (const ParamInfo& info)
{
    auto a = juce::AudioProcessorParameterWithIDAttributes().withLabel (juce::String (info.unit)).withAutomatable (true);
    if (info.kind == ParamKind::Bool)
        a = a.withCategory (juce::AudioProcessorParameter::genericParameter);
    return a;
}

HostParam::HostParam (ParamId paramId)
    : juce::RangedAudioParameter (juce::ParameterID (paramInfo (paramId).id, kParameterVersion),
                                  juce::String (paramInfo (paramId).name), attributesFor (paramInfo (paramId))),
      id (paramId),
      row (paramInfo (paramId)),
      range (rangeFor (row)),
      choiceLabels (splitChoices (row.choices)),
      value (row.def),
      defaultValue (row.def)
{
}

float HostParam::legalValue (float natural) const noexcept
{
    if (! std::isfinite (natural))
        return row.def;
    const float clamped = std::min (row.max, std::max (row.min, natural));
    return row.kind == ParamKind::Float ? clamped : roundHalfAway (clamped);
}

float HostParam::fromHost (float normalised) const noexcept
{
    // A host's normalised number maps to a natural value with float noise in its last digit
    // (0.6 of 0..100 % is 60.000004), which the human-readable state would then carry. Float
    // rows keep six significant digits, a resolution far below anything audible or shown
    // (Int, Bool and Choice rows snap to whole numbers in legalValue anyway).
    const float v = legalValue (range.convertFrom0to1 (normalised));
    if (row.kind != ParamKind::Float || juce::exactlyEqual (v, 0.0f))
        return v;
    const double magnitude = std::floor (std::log10 (std::abs (static_cast<double> (v))));
    const double step = std::pow (10.0, magnitude - 5.0);
    const auto snapped = static_cast<float> (std::round (static_cast<double> (v) / step) * step);
    return std::isfinite (snapped) ? std::min (row.max, std::max (row.min, snapped)) : v;
}

void HostParam::setNatural (float newValue, bool hostRestore)
{
    const float v = legalValue (newValue);
    value.store (v, std::memory_order_relaxed);
    rawLatest.store (0, std::memory_order_relaxed);    // not a host number: the host's last poke is history
    if (! hostRestore)
        rawBaseline.store (0, std::memory_order_relaxed);   // the host is told the plain mapping: report that
    // setValue (below) sees that the stored value already maps to this normalised value and
    // keeps it, so the exact natural value survives the host round trip.
    notifyingNatural.store (true, std::memory_order_relaxed);
    setValueNotifyingHost (range.convertTo0to1 (v));
    notifyingNatural.store (false, std::memory_order_relaxed);
}

float HostParam::setNaturalFromAudioThread (float newValue) noexcept
{
    const float v = legalValue (newValue);
    value.store (v, std::memory_order_relaxed);
    rawLatest.store (0, std::memory_order_relaxed);
    rawBaseline.store (0, std::memory_order_relaxed);    // the echo tells the host the plain mapping
    return v;
}

float HostParam::naturalForController (int ccValue) const noexcept
{
    const float n = static_cast<float> (std::min (127, std::max (0, ccValue))) / 127.0f;
    return fromHost (n);
}

std::uint64_t HostParam::packRaw (float natural, float raw) noexcept
{
    std::uint32_t n = 0, r = 0;
    std::memcpy (&n, &natural, 4);
    std::memcpy (&r, &raw, 4);
    return (static_cast<std::uint64_t> (n) << 32) | static_cast<std::uint64_t> (r);
}

bool HostParam::rawFromSlot (const std::atomic<std::uint64_t>& slot, float natural, float& raw) const noexcept
{
    std::uint32_t n = 0;
    std::memcpy (&n, &natural, 4);
    const std::uint64_t packed = slot.load (std::memory_order_relaxed);
    if (packed == 0 || static_cast<std::uint32_t> (packed >> 32) != n)
        return false;
    const auto r = static_cast<std::uint32_t> (packed & 0xFFFFFFFFu);
    std::memcpy (&raw, &r, 4);
    return std::isfinite (raw) && raw >= 0.0f && raw <= 1.0f
        && juce::exactlyEqual (fromHost (raw), natural);
}

std::uint64_t HostParam::reportedRaw() const noexcept
{
    const float natural = value.load (std::memory_order_relaxed);
    float raw = 0.0f;
    if (rawFromSlot (rawLatest, natural, raw))
        return rawLatest.load (std::memory_order_relaxed);
    if (rawFromSlot (rawBaseline, natural, raw))
        return rawBaseline.load (std::memory_order_relaxed);
    return 0;
}

void HostParam::rememberRaw (float natural, float raw, std::uint64_t reportedBefore) noexcept
{
    if (notifyingNatural.load (std::memory_order_relaxed))
        return;
    const std::uint64_t packed = packRaw (natural, raw);
    if (rawLatest.load (std::memory_order_relaxed) == packed)
        return;
    rawLatest.store (packed, std::memory_order_relaxed);
    // The baseline is what getValue reported just before this poke (0: the plain mapping of
    // the natural value), never an older number: a stale one with the same natural value
    // would be read back after a state restore instead of what the host read before it poked.
    rawBaseline.store (reportedBefore, std::memory_order_relaxed);
}

float HostParam::getValue() const
{
    const float natural = value.load (std::memory_order_relaxed);
    float raw = 0.0f;
    if (rawFromSlot (rawLatest, natural, raw) || rawFromSlot (rawBaseline, natural, raw))
        return raw;
    return range.convertTo0to1 (natural);
}

void HostParam::setValue (float newNormalised)
{
    if (! (newNormalised >= 0.0f && newNormalised <= 1.0f))
        newNormalised = std::min (1.0f, std::max (0.0f, std::isfinite (newNormalised) ? newNormalised : 0.0f));
    const float current = value.load (std::memory_order_relaxed);
    const std::uint64_t reportedBefore = reportedRaw();
    if (juce::exactlyEqual (range.convertTo0to1 (current), newNormalised))
    {
        // The normalised value carries nothing the stored natural value does not: keep the
        // exact value (a file value survives the host round trip) and remember the host's number.
        rememberRaw (current, newNormalised, reportedBefore);
        return;
    }
    const float fresh = fromHost (newNormalised);
    if (juce::exactlyEqual (fresh, current))
    {
        rememberRaw (current, newNormalised, reportedBefore);    // snapped to the same value
        return;
    }
    value.store (fresh, std::memory_order_relaxed);
    rememberRaw (fresh, newNormalised, reportedBefore);
}

float HostParam::getDefaultValue() const
{
    return range.convertTo0to1 (defaultValue.load (std::memory_order_relaxed));
}

int HostParam::getNumSteps() const
{
    switch (row.kind)
    {
        case ParamKind::Bool: return 2;
        case ParamKind::Choice: return std::max (2, choiceLabels.size());
        case ParamKind::Int: return static_cast<int> (row.max - row.min) + 1;
        case ParamKind::Float: break;
    }
    return juce::AudioProcessor::getDefaultNumParameterSteps();
}

bool HostParam::isDiscrete() const
{
    return row.kind != ParamKind::Float;
}

bool HostParam::isBoolean() const
{
    return row.kind == ParamKind::Bool;
}

juce::String HostParam::getText (float normalised, int maximumLength) const
{
    const float v = legalValue (range.convertFrom0to1 (normalised));
    juce::String text;
    switch (row.kind)
    {
        case ParamKind::Bool:
            text = v >= 0.5f ? "On" : "Off";
            break;
        case ParamKind::Choice:
        {
            const int index = static_cast<int> (v);
            text = index >= 0 && index < choiceLabels.size() ? choiceLabels[index] : juce::String (index);
            break;
        }
        case ParamKind::Int:
            text = juce::String (static_cast<int> (v));
            break;
        case ParamKind::Float:
        {
            const float span = row.max - row.min;
            text = trimmedDecimal (v, span >= 1000.0f ? 1 : span >= 20.0f ? 2 : 3);
            break;
        }
    }
    if (maximumLength > 0 && text.length() > maximumLength)
        text = text.substring (0, maximumLength);
    return text;
}

float HostParam::getValueForText (const juce::String& text) const
{
    const juce::String t = text.trim();
    switch (row.kind)
    {
        case ParamKind::Bool:
        {
            const juce::String lower = t.toLowerCase();
            const bool on = lower == "on" || lower == "true" || lower == "yes" || lower.getFloatValue() >= 0.5f;
            return on ? 1.0f : 0.0f;
        }
        case ParamKind::Choice:
        {
            for (int i = 0; i < choiceLabels.size(); ++i)
                if (choiceLabels[i].equalsIgnoreCase (t))
                    return range.convertTo0to1 (static_cast<float> (i));
            break;
        }
        case ParamKind::Int:
        case ParamKind::Float:
            break;
    }
    return range.convertTo0to1 (legalValue (t.getFloatValue()));
}

juce::StringArray HostParam::getAllValueStrings() const
{
    if (row.kind == ParamKind::Choice)
        return choiceLabels;
    if (row.kind == ParamKind::Bool)
        return { "Off", "On" };
    return juce::AudioProcessorParameter::getAllValueStrings();
}

juce::String groupNameFor (const ParamInfo& info)
{
    return juce::String (info.group);
}

ParameterSet::ParameterSet()
{
    // One AudioProcessorParameterGroup per table group, in table order.
    std::unique_ptr<juce::AudioProcessorParameterGroup> group;
    juce::String currentGroup;

    const auto flush = [&] {
        if (group != nullptr)
            layout.add (std::move (group));
    };

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = static_cast<ParamId> (i);
        const ParamInfo& info = paramInfo (id);
        const juce::String groupName = groupNameFor (info);
        if (group == nullptr || groupName != currentGroup)
        {
            flush();
            currentGroup = groupName;
            group = std::make_unique<juce::AudioProcessorParameterGroup> (groupName.toLowerCase().replaceCharacter (' ', '_'),
                                                                          groupName, "/");
        }
        auto param = std::make_unique<HostParam> (id);
        byId[static_cast<std::size_t> (i)] = param.get();
        group->addChild (std::move (param));
    }
    flush();
}
}
