#pragma once
// Pattern data model shared by the sequencer, arpeggiator, generator, engine, plugin
// state and editor. Everything here is trivially copyable so whole patterns and banks
// can be handed between threads by value.
//
// One canonical form: a step grid where every step carries its own pitch. The
// pitch-list/time-list view of the original hardware is derived from it
// (see docs/research/06-sequencer-arp-randomizer.md, section 8).
#include <array>
#include <cstdint>
#include <type_traits>

namespace acid::seq
{
inline constexpr int kMaxSteps = 64;
inline constexpr int kNumBanks = 4;
inline constexpr int kSlotsPerBank = 12;
inline constexpr int kNumSlots = kNumBanks * kSlotsPerBank;   // 48 sequencer + 48 arpeggiator
inline constexpr int kMaxArpIndex = 10;                       // held notes addressable by an arp step
inline constexpr int kMinOctave = -1;
inline constexpr int kMaxOctave = 3;
inline constexpr int kPatternNameLength = 32;

/** MIDI note of pitch class 0 (C) at octave 0. Step pitch = kBaseNote + 12 * octave + note. */
inline constexpr int kBaseNote = 36;

enum class StepState : std::uint8_t
{
    Rest = 0,
    Note = 1,
    Tie = 2    // holds the previous note's gate with no new pitch, accent or envelope
};

/** Bits for Step::locks: lanes the generator must leave alone. */
enum LaneBit : std::uint8_t
{
    kLanePitch = 1,
    kLaneOctave = 2,
    kLaneGate = 4,
    kLaneAccent = 8,
    kLaneSlide = 16,
    kLaneVibrato = 32
};

struct Step
{
    StepState state = StepState::Rest;

    // Pitch is kept on rests and ties so toggling a step back on restores it.
    std::uint8_t note = 0;        // pitch class 0..11 (C..B). Unused in arpeggiator patterns.
    std::int8_t octave = 0;       // kMinOctave..kMaxOctave. In arp patterns: octave offset.
    std::uint8_t arpIndex = 1;    // arpeggiator patterns only: 1..kMaxArpIndex, held notes sorted low to high

    bool accent = false;
    bool slide = false;           // on the note slid FROM: its gate is held into the next Note step
    bool vibrato = false;

    std::uint8_t probability = 100;   // 0..100 %. A failed roll plays as a Rest.
    std::uint8_t ratchet = 1;         // 1..4 evenly spaced hits inside the step
    std::uint8_t gate = 0;            // 0 = use the pattern's gate length; 1..127 = this fraction of the step

    std::uint8_t locks = 0;           // LaneBit mask

    // Generator memory. Each lane keeps a 0..127 magnitude per step; a lane's density knob
    // is a threshold over these, so turning it reveals or hides steps without re-rolling.
    // 255 = not generated.
    std::uint8_t magGate = 255;
    std::uint8_t magAccent = 255;
    std::uint8_t magSlide = 255;
    std::uint8_t magVibrato = 255;
    std::uint8_t candNote = 0;        // pitch the generator chose for this step (shown when its gate is revealed)
    std::int8_t candOctave = 0;
};

enum class StepDiv : std::uint8_t { d1_64, d1_32, d1_16, d1_8, d1_4, d1_2, d1_1 };
enum class DivMod : std::uint8_t { Full, Dotted, Triplet };
enum class Direction : std::uint8_t { Forward, Reverse, PingPong, Random };

/** The generator ("randomizer") template and its live density knobs. */
struct GenParams
{
    std::uint32_t seed = 1;
    std::uint16_t noteMask = 0x0FFF;      // bit n = pitch class n allowed, relative to root
    std::uint8_t root = 0;                // 0..11, transposes noteMask
    std::uint8_t octaveMask = 0b00110;    // bit (octave - kMinOctave) allowed; default octaves 0 and +1
    bool useSlide = true;
    bool useAccent = true;
    bool useVibrato = false;
    std::uint8_t amountNote = 50;         // 0..100 density thresholds
    std::uint8_t amountSlide = 50;
    std::uint8_t amountAccent = 50;
    std::uint8_t amountVibrato = 50;
};

struct Pattern
{
    char name[kPatternNameLength] = {};
    std::uint8_t length = 16;             // 1..kMaxSteps
    StepDiv div = StepDiv::d1_16;
    DivMod divMod = DivMod::Full;
    std::uint8_t shuffle = 0;             // 0..100 %
    std::uint8_t gateLength = 0;          // 0 = authentic gate (half a step plus 1.35 ms); 1..100 = % of the step
    Direction direction = Direction::Forward;
    std::int8_t transpose = 0;            // semitones, applied at playback
    std::array<Step, kMaxSteps> steps {};
    GenParams gen {};
};

/** Everything a scene stores: 48 sequencer patterns and 48 arpeggiator patterns. */
struct PatternBank
{
    std::array<Pattern, kNumSlots> seq {};
    std::array<Pattern, kNumSlots> arp {};
};

constexpr int slotIndex (int bank, int slotInBank) noexcept { return bank * kSlotsPerBank + slotInBank; }

/** MIDI note a Note step plays, before pattern and live transpose. */
constexpr int stepMidiNote (const Step& s) noexcept { return kBaseNote + 12 * s.octave + s.note; }

static_assert (std::is_trivially_copyable_v<Pattern>);
static_assert (std::is_trivially_copyable_v<PatternBank>);
}
