#pragma once
// Host parameters built from acid::kParamTable.
//
// One HostParam per table row. The parameter's value of record is the natural-unit value
// (percent, Hz, dB...) exactly as the table and the state files define it: the normalised
// 0..1 value the host sees is derived from it, never the other way round, so a value that
// was loaded from a file is written back byte for byte (clap-validator requires save, load,
// save to be byte-identical, which a float mapped through a log range and back would break).
//
// Bool rows are two-step discrete parameters (Off/On); they snap, which avoids the pluginval
// "state restoration" flake of juce::AudioParameterBool whose getValue() returns the raw
// value the host poked in (docs/research/07 section 1.2 lesson 2). Int and Choice rows snap
// to whole numbers. Float rows with ParamScale::Log get a logarithmic 0..1 mapping (every
// Log row has a positive minimum).
//
// getValue() returns the normalised value the host last set for the current natural value
// when there is one (hosts compare what they read back with what they set, and a snapped
// 0.77 would read as 1.0 otherwise), else the natural value mapped through the range. Either
// way the value maps to the same natural value, which is the only value of record.
//
// Audio thread: natural() is a relaxed atomic load; the processor keeps a HostParam* per
// ParamId and fills a ParamValues from them once per block without allocation.
#include "acid/engine/ParamTable.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace acid::plugin
{
/** Parameter id version hint written into every ParameterID (house style). */
inline constexpr int kParameterVersion = 1;

class HostParam final : public juce::RangedAudioParameter
{
public:
    explicit HostParam (ParamId id);

    ParamId paramId() const noexcept { return id; }
    const ParamInfo& info() const noexcept { return row; }

    /** Any thread. The value in natural units. */
    float natural() const noexcept { return value.load (std::memory_order_relaxed); }

    /** Non-audio threads: sets the natural value exactly (range-checked, snapped for
        discrete kinds) and tells the host and every listener. The value survives the
        normalise/denormalise round trip the notification goes through.

        From then on getValue reports the plain mapping, which is what the host was told,
        except for a host's own state restore (hostRestore): the wrappers do not pass that
        notification on and read getValue afterwards instead (VST3 setComponentState), so
        what the host read before its last poke is reported when it maps to the restored
        value (pluginval's state restoration test reads a value, pokes, restores, compares). */
    void setNatural (float newValue, bool hostRestore = false);

    /** Audio thread: the value a MIDI controller asks for, stored directly (no host call;
        the processor echoes it to the host from the message thread). Returns the value
        stored. */
    float setNaturalFromAudioThread (float newValue) noexcept;

    /** The natural value a 0..127 controller position maps to (snapped for discrete kinds). */
    float naturalForController (int ccValue) const noexcept;

    /** Clamps and, for Int, Bool and Choice rows, rounds to the nearest legal value. */
    float legalValue (float natural) const noexcept;

    /** The natural value a host's normalised number stands for: legalValue of the range's
        mapping, and for Float rows rounded to six significant digits, so a value a host set
        is stored (and saved) without float noise. */
    float fromHost (float normalised) const noexcept;

    /** The value hosts reset to and compare against after construction. Starts as the table
        default; the processor moves it to the factory default scene's value when one is
        embedded, because that is what a new instance holds. */
    void setDefaultNatural (float natural) noexcept { defaultValue.store (legalValue (natural), std::memory_order_relaxed); }
    float defaultNatural() const noexcept { return defaultValue.load (std::memory_order_relaxed); }

    // RangedAudioParameter
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range; }

    // AudioProcessorParameter
    float getValue() const override;
    void setValue (float newNormalised) override;
    float getDefaultValue() const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;
    juce::String getText (float normalised, int maximumLength) const override;
    float getValueForText (const juce::String& text) const override;
    juce::StringArray getAllValueStrings() const override;

private:
    static juce::NormalisableRange<float> rangeFor (const ParamInfo&);
    static juce::AudioProcessorParameterWithIDAttributes attributesFor (const ParamInfo&);

    const ParamId id;
    const ParamInfo& row;
    const juce::NormalisableRange<float> range;
    /** The host's own normalised numbers, packed (natural bits high, raw bits low) so a reader
        never sees a torn pair. `latest` is the last value the host set; `baseline` what
        getValue reported just before that (0 when it was the plain mapping). A write from
        anywhere else (a state or preset load, the editor, a controller) drops `latest`, so a
        host that read a value, poked another and restored a state reads back what it read
        first (pluginval's state restoration test). */
    static std::uint64_t packRaw (float natural, float raw) noexcept;
    bool rawFromSlot (const std::atomic<std::uint64_t>& slot, float natural, float& raw) const noexcept;
    void rememberRaw (float natural, float raw, std::uint64_t reportedBefore) noexcept;
    /** The packed pair getValue reports from memory right now, or 0 for the plain mapping. */
    std::uint64_t reportedRaw() const noexcept;

    const juce::StringArray choiceLabels;
    std::atomic<float> value;
    std::atomic<float> defaultValue;
    std::atomic<std::uint64_t> rawLatest { 0 };
    std::atomic<std::uint64_t> rawBaseline { 0 };
    std::atomic<bool> notifyingNatural { false };   // setNatural's own host notification is not a host number
};

static_assert (std::atomic<float>::is_always_lock_free);
static_assert (std::atomic<std::uint64_t>::is_always_lock_free);

/** The table's parameters, in table order, with a pointer per ParamId for the audio thread.
    The unique_ptrs are handed to the AudioProcessorValueTreeState layout, which hands them
    to the processor; the raw pointers stay valid for the processor's lifetime. */
struct ParameterSet
{
    std::array<HostParam*, kNumParams> byId {};
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    ParameterSet();

    /** Audio thread. Every parameter's natural value. */
    void read (ParamValues& out) const noexcept
    {
        for (int i = 0; i < kNumParams; ++i)
            out[static_cast<std::size_t> (i)] = byId[static_cast<std::size_t> (i)]->natural();
    }
};

/** Table order groups, for hosts that show them. Empty when the group has no rows. */
juce::String groupNameFor (const ParamInfo&);
}
