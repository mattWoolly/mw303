#pragma once
// Types that cross the boundary between the sequencer module and the rest of the engine:
// what goes in (transport, MIDI, mode settings) and what comes out (voice and MIDI events).
#include <cstdint>

namespace acid::seq
{
enum class PlayMode : std::uint8_t
{
    External = 0,     // played from MIDI notes: overlap = slide, velocity = accent
    Sequencer = 1,
    Arpeggiator = 2
};

/** What starts and selects patterns in Sequencer mode. */
enum class TriggerMode : std::uint8_t
{
    Live = 0,         // Run button or host transport; pattern chosen by the bank/slot parameters
    HostNote = 1      // a MIDI note selects and starts a pattern; releasing it stops playback
};

/** When a newly selected pattern takes over from the playing one. */
enum class LaunchQuant : std::uint8_t { EndOfPattern = 0, NextBar = 1, Immediate = 2 };

/** What a slide flag does when the next step is a rest. Undocumented on the hardware. */
enum class SlideIntoRest : std::uint8_t
{
    Release = 0,          // the slide is ignored: normal gate length
    HoldThroughRest = 1,  // the gate is held for the rest step's gate time
    SustainToNextNote = 2 // the gate is held, across any rests, until the next Note step
};

enum class NotePriority : std::uint8_t { Last = 0, Low = 1, High = 2 };

/** Host transport as seen at the start of a block. Fields the host did not supply are flagged. */
struct TransportInfo
{
    bool valid = false;           // false: the host gave no position this block (not the same as stopped)
    bool playing = false;
    bool ppqValid = false;
    double ppq = 0.0;             // quarter notes since the host's origin, at the first sample of the block
    bool bpmValid = false;
    double bpm = 120.0;
    bool looping = false;
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
    int timeSigNum = 4;
    int timeSigDen = 4;
};

/** A short MIDI message with its sample offset inside the current block. */
struct MidiMsg
{
    std::int32_t offset = 0;
    std::uint8_t size = 0;        // 1..3
    std::uint8_t data[3] = {};
};

/** What the sequencer, arpeggiator or external-note handler tells the voice to do.
    Events are ordered by offset; at equal offsets a NoteOff comes before a NoteOn. */
struct VoiceEvent
{
    enum class Type : std::uint8_t { NoteOn, NoteOff, AllOff, PitchBend, VibratoDepth };

    Type type = Type::NoteOff;
    std::int32_t offset = 0;      // samples from the start of the block
    std::uint8_t note = 0;        // NoteOn: MIDI note after all transposition
    bool accent = false;          // NoteOn
    bool legato = false;          // NoteOn: slide into this note. No envelope retrigger; pitch lag engaged.
    bool vibrato = false;         // NoteOn: the step's vibrato flag
    float value = 0.0f;           // PitchBend: semitones. VibratoDepth: 0..1 (mod wheel).
};
}
