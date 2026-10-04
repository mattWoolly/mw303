#pragma once
// Instrument settings that are saved with the plugin state but are not host parameters:
// they are chosen in a settings menu, not automated.
#include "acid/seq/PlayerTypes.h"
#include <array>
#include <cstdint>
#include <type_traits>

namespace acid
{
/** Oversampling tier. Each tier sets the voice and distortion oversampling factors. */
enum class Quality : std::uint8_t { Draft = 0, Normal = 1, High = 2, Ultra = 3 };

/** Positions in the effects chain. Every slot can be moved, the distortion included. */
enum class FxSlot : std::uint8_t { Distortion = 0, Eq = 1, Delay = 2, Reverb = 3, Chorus = 4, Limiter = 5 };
inline constexpr int kNumFxSlots = 6;

struct Settings
{
    seq::TriggerMode triggerMode = seq::TriggerMode::Live;
    seq::LaunchQuant launchQuant = seq::LaunchQuant::EndOfPattern;
    seq::SlideIntoRest slideIntoRest = seq::SlideIntoRest::Release;
    seq::NotePriority notePriority = seq::NotePriority::Last;

    bool followHostTransport = true;   // start and stop with the host
    bool hostLink = true;              // keep the step position locked to the host's song position while playing
    bool restartOnTrigger = true;      // HostNote: restart the pattern on each trigger note
    float internalBpm = 120.0f;        // tempo of the internal clock (standalone, or no host tempo); 20..300

    std::uint8_t hostNoteBase = 24;    // HostNote: the MIDI note that triggers bank A, slot 1
    std::uint8_t accentThreshold = 64; // External mode: velocity above this plays accented
    std::uint8_t bendRange = 2;        // semitones, 0..12

    bool midiOut = true;               // send sequencer and arpeggiator notes to the MIDI output
    bool midiThru = false;             // External mode: pass incoming notes to the MIDI output
    std::uint8_t outAccentVelocity = 96;
    std::uint8_t outNormalVelocity = 32;

    Quality realtimeQuality = Quality::Normal;
    Quality offlineQuality = Quality::High;

    /** Processing order: fxOrder[0] runs first. A permutation of the FxSlot values. */
    std::array<std::uint8_t, kNumFxSlots> fxOrder { 0, 1, 2, 3, 4, 5 };
};

static_assert (std::is_trivially_copyable_v<Settings>);
}
