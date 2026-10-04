#include "ParamControls.h"
#include <string_view>

namespace acid::ui
{
namespace
{
juce::StringArray choiceLabels (const ParamInfo& info)
{
    juce::StringArray labels;

    if (info.kind == ParamKind::Bool)
    {
        labels.add ("Off");
        labels.add ("On");
    }
    else
    {
        labels.addTokens (juce::String (info.choices), "|", {});
    }

    return labels;
}

juce::String withSign (double v, int decimals)
{
    return (v > 0.0 ? "+" : "") + juce::String (v, decimals);
}

bool unitIs (const ParamInfo& info, std::string_view unit) noexcept
{
    return std::string_view (info.unit) == unit;
}
}

juce::String formatParamValue (const ParamInfo& info, double v)
{
    if (info.kind == ParamKind::Choice || info.kind == ParamKind::Bool)
    {
        const auto labels = choiceLabels (info);
        return labels[juce::jlimit (0, labels.size() - 1, juce::roundToInt (v - static_cast<double> (info.min)))];
    }

    const auto bipolar = info.min < 0.0f;
    const auto isInt = info.kind == ParamKind::Int;

    if (unitIs (info, "%"))      return (bipolar ? withSign (v, 1) : juce::String (v, 1)) + " %";
    if (unitIs (info, "st"))     return withSign (v, isInt ? 0 : 2) + " st";
    if (unitIs (info, "ct"))     return withSign (std::round (v), 0) + " ct";
    if (unitIs (info, "mV"))     return (bipolar ? withSign (std::round (v), 0) : juce::String (juce::roundToInt (v))) + " mV";
    if (unitIs (info, "V"))      return juce::String (v, 2) + " V";
    if (unitIs (info, "uF"))     return juce::String (v, 2) + " uF";
    if (unitIs (info, "oct/V"))  return juce::String (v, 3);
    if (unitIs (info, "kOhm"))   return juce::String (v, 1) + " k";
    if (unitIs (info, "dB"))     return withSign (v, 1) + " dB";
    if (unitIs (info, "m"))      return juce::String (v, 1) + " m";
    if (unitIs (info, "oct"))    return juce::String (v, 2) + " oct";
    if (unitIs (info, ":1"))     return juce::String (v, v < 10.0 ? 1 : 0) + ":1";
    if (unitIs (info, "s"))      return juce::String (juce::roundToInt (v * 1000.0)) + " ms";
    if (unitIs (info, "ms"))     return juce::String (v, v < 10.0 ? 1 : 0) + " ms";

    if (unitIs (info, "nF"))
        return v >= 1000.0 ? juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " uF" : juce::String (v, v < 100.0 ? 1 : 0) + " nF";

    if (unitIs (info, "Hz"))
        return v >= 1000.0 ? juce::String (v / 1000.0, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";

    return isInt ? juce::String (juce::roundToInt (v)) : juce::String (v, 2);
}

double parseParamValue (const ParamInfo& info, const juce::String& text)
{
    const auto trimmed = text.trim();

    if (info.kind == ParamKind::Choice || info.kind == ParamKind::Bool)
    {
        const auto labels = choiceLabels (info);

        for (int i = 0; i < labels.size(); ++i)
            if (labels[i].equalsIgnoreCase (trimmed))
                return static_cast<double> (info.min) + i;
    }

    auto v = trimmed.retainCharacters ("+-0123456789.").getDoubleValue();
    const auto lower = trimmed.toLowerCase();

    if (unitIs (info, "Hz") && lower.containsChar ('k'))    v *= 1000.0;
    if (unitIs (info, "nF") && lower.contains ("u"))        v *= 1000.0;
    if (unitIs (info, "s") && ! lower.endsWith (" s"))      v /= 1000.0;   // shown, and so typed, in milliseconds

    return juce::jlimit (static_cast<double> (info.min), static_cast<double> (info.max), v);
}

juce::String shortCaption (ParamId id)
{
    auto name = juce::String (paramInfo (id).name);

    for (auto* prefix : { "Distortion ", "Delay ", "Reverb ", "Chorus ", "Limiter ", "EQ ", "Override " })
        if (name.startsWith (prefix))
        {
            name = name.fromFirstOccurrenceOf (prefix, false, false);
            break;
        }

    static const std::pair<const char*, const char*> kShort[] = {
        { "Numerator", "Num" }, { "Denominator", "Den" }, { "Diffusion", "Diff" }, { "Brightness", "Bright" },
        { "Threshold", "Thresh" }, { "Resonance", "Reso" }, { "High-pass", "HP" }, { "Stereo Spread", "Spread" },
        { "Filter Type", "Filter" }, { "Filter Cutoff", "Cutoff" }, { "Filter Reso", "Reso" }, { "Pre-delay", "Pre Delay" },
        { "Mid-Low", "Lo Mid" }, { "Mid-High", "Hi Mid" }, { "Amount", "Amt" }, { "Preamp", "Pre" }, { "Clean Low Band", "Low Keep" },
        { "Soft Clip", "Soft Clip" }, { "Early/Late", "Early/Late" }, { "Tail Mod", "Tail Mod" },
        { "Color", "Colour" }   // the panel knob says COLOUR; one spelling across the editor
    };

    for (const auto& [from, to] : kShort)
        name = name.replace (from, to);

    return name.toUpperCase();
}

void configureSlider (DragSlider& slider, ParamId id)
{
    const auto& info = paramInfo (id);
    const auto stepped = info.kind != ParamKind::Float;
    slider.setRange (static_cast<double> (info.min), static_cast<double> (info.max), stepped ? 1.0 : 0.0);

    if (info.scale == ParamScale::Log && info.min > 0.0f)
        slider.setSkewFactorFromMidPoint (std::sqrt (static_cast<double> (info.min) * static_cast<double> (info.max)));

    slider.setBipolar (info.kind == ParamKind::Float && info.min < 0.0f && std::abs (info.min + info.max) < 1.0e-6f);
    applyValueText (slider, id);

    if (! (info.kind == ParamKind::Bool || info.kind == ParamKind::Choice))
        slider.setDefaultValue (static_cast<double> (info.def));

    slider.param.name = info.name;
    slider.setValue (static_cast<double> (info.def), juce::dontSendNotification);
}

void applyValueText (DragSlider& slider, ParamId id)
{
    const auto& info = paramInfo (id);
    slider.textFromValueFunction = [&info] (double v) { return formatParamValue (info, v); };
    slider.valueFromTextFunction = [&info] (const juce::String& s) { return parseParamValue (info, s); };
}

//==============================================================================
Knob& ControlSet::knob (ParamId id, Knob::Size size, const juce::String& caption)
{
    auto& k = make<Knob> (size, caption.isEmpty() ? shortCaption (id) : caption);
    configureSlider (k, id);
    add (id, k);
    return k;
}

Trimmer& ControlSet::trimmer (ParamId id, const juce::String& caption)
{
    auto& t = make<Trimmer> (caption);
    configureSlider (t, id);
    add (id, t);
    return t;
}

NumericDisplay& ControlSet::numeric (ParamId id, const juce::String& caption, NumericDisplay::CaptionPlacement placement)
{
    auto& n = make<NumericDisplay> (caption, placement);
    configureSlider (n, id);
    add (id, n);
    return n;
}

ToggleSwitch& ControlSet::toggleSwitch (ParamId id, const juce::String& caption, juce::StringArray labels)
{
    if (labels.isEmpty())
        for (const auto& l : choiceLabels (paramInfo (id)))
            labels.add (l.toUpperCase());

    auto& s = make<ToggleSwitch> (labels, caption);
    configureSlider (s, id);
    add (id, s);
    return s;
}

SegmentedSelector& ControlSet::selector (ParamId id, juce::StringArray labels)
{
    if (labels.isEmpty())
        for (const auto& l : choiceLabels (paramInfo (id)))
            labels.add (l.toUpperCase());

    auto& s = make<SegmentedSelector> (labels);
    configureSlider (s, id);
    add (id, s);
    return s;
}

LedButton& ControlSet::ledButton (ParamId id, const juce::String& caption, LedColour colour, LedButton::Style style)
{
    auto& b = make<LedButton> (caption, colour, style);
    b.param.name = paramInfo (id).name;
    b.setToggleState (paramInfo (id).def >= 0.5f, juce::dontSendNotification);
    add (id, b);
    return b;
}

PushButton& ControlSet::pushButton (ParamId id, const juce::String& caption)
{
    auto& b = make<PushButton> (caption);
    b.param.name = paramInfo (id).name;
    add (id, b);
    return b;
}

Dropdown& ControlSet::dropdown (ParamId id)
{
    auto& d = make<Dropdown>();
    const auto& info = paramInfo (id);
    d.setItems (choiceLabels (info));
    d.setSelectedItemIndex (juce::roundToInt (info.def - info.min), juce::dontSendNotification);
    d.param.name = info.name;
    add (id, d);
    return d;
}

juce::Slider* ControlSet::findSlider (ParamId id) const noexcept
{
    for (auto& e : sliders)
        if (e.first == id)
            return e.second;

    return nullptr;
}

juce::Button* ControlSet::findButton (ParamId id) const noexcept
{
    for (auto& e : buttons)
        if (e.first == id)
            return e.second;

    return nullptr;
}

juce::ComboBox* ControlSet::findComboBox (ParamId id) const noexcept
{
    for (auto& e : combos)
        if (e.first == id)
            return e.second;

    return nullptr;
}

void ControlSet::setValue (ParamId id, double value)
{
    const juce::ScopedValueSetter<bool> guard (mirroring, true);
    const auto& info = paramInfo (id);

    for (auto& e : sliders)
        if (e.first == id)
            e.second->setValue (value, juce::dontSendNotification);

    for (auto& e : buttons)
        if (e.first == id)
            e.second->setToggleState (value >= 0.5, juce::dontSendNotification);

    for (auto& e : combos)
        if (e.first == id)
            e.second->setSelectedItemIndex (juce::roundToInt (value - static_cast<double> (info.min)), juce::dontSendNotification);
}

double ControlSet::getValue (ParamId id) const
{
    if (auto* s = findSlider (id))    return s->getValue();
    if (auto* b = findButton (id))    return b->getToggleState() ? 1.0 : 0.0;
    if (auto* c = findComboBox (id))  return static_cast<double> (paramInfo (id).min) + c->getSelectedItemIndex();

    return static_cast<double> (paramInfo (id).def);
}

void ControlSet::linkDuplicates (std::function<void (ParamId, double)> onChange)
{
    changeCallback = std::move (onChange);

    const auto changed = [this] (ParamId id, double value)
    {
        if (mirroring)
            return;

        setValue (id, value);

        if (changeCallback != nullptr)
            changeCallback (id, value);
    };

    for (auto& e : sliders)
    {
        const auto id = e.first;
        auto* s = e.second;
        s->onValueChange = [changed, id, s] { changed (id, s->getValue()); };
    }

    for (auto& e : buttons)
    {
        const auto id = e.first;
        auto* b = e.second;

        if (b->getClickingTogglesState())
            b->onClick = [changed, id, b] { changed (id, b->getToggleState() ? 1.0 : 0.0); };
    }

    for (auto& e : combos)
    {
        const auto id = e.first;
        auto* c = e.second;
        c->onChange = [changed, id, c] { changed (id, static_cast<double> (paramInfo (id).min) + c->getSelectedItemIndex()); };
    }
}
}
