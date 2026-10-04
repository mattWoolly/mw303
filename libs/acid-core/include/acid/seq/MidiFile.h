#pragma once
// Standard MIDI File export and import for patterns (document 06 section 7.1).
//
// Export writes a type-0 file in memory: one track with the track name (pattern name), tempo,
// 4/4 time signature, an optional lossless sidecar (a text meta event holding the
// encodePattern string, which other software ignores), then the notes:
//   - Note step: Note On at the step onset (shuffle applied to odd steps of Full divisions),
//     velocity accentVelocity or normalVelocity.
//   - Note end: the step's gate (Step::gate as gate/127 of the step, else Pattern::gateLength
//     percent, else the authentic half step plus 1.35 ms), never reaching the next step's
//     onset: a non-legato note always ends at least one tick before the next onset.
//   - Tie: the note is extended to the gate end of the last tied step (one merged note).
//     A tie after a rest (or on step 0) is silent.
//   - Slide to a different pitch: the source's Note Off comes max (1 tick, 1/64 step) after the
//     target's Note On (overlap). Slides hold through ties. A slide on the last sounding note
//     holds it to the end of the clip when step 0 is a Note (the loop glide).
//   - Slide to the same pitch: one merged note; if the accent changes, CC64 = 127 before a same
//     pitch Note Off / Note On with the new velocity, and CC64 = 0 at the end of that chain.
//   - Slide into a rest follows SlideIntoRest: Release ignores it, HoldThroughRest holds for the
//     rest's gate, SustainToNextNote holds until the next Note (legato into it).
//   - Vibrato: CC1 = 127 before a vibrato note's Note On, CC1 = 0 when it ends.
//   - Ratchet r: r evenly spaced hits inside the step, each with 1/r of the step's gate.
//   - Probability is not rolled: every Note step with probability > 0 is written.
//   - Clip length = length steps (optionally padded to whole 4/4 bars); End of Track sits there.
//     The player delays odd steps of the absolute grid, so an odd-length pattern with shuffle
//     swaps its parity every pass; by default such a pattern is written as two passes
//     (2 x length steps) so a host looping the clip plays it the way the player does.
//   - Pattern::transpose is applied to the written notes. Arpeggiator patterns cannot be
//     exported this way (they need held notes): record the take and use the MidiNote overload.
//
// Import (readMidiFile) turns a monophonic clip into a Pattern:
//   1. A sidecar written by us wins (exact round trip), as long as it still describes the notes:
//      the decoded pattern is rendered again and its notes (onset, key, end within one tick,
//      and the accent class of the velocity) must match the file's first track with notes.
//      Every export option that changes the notes is tried, at the file's resolution and at
//      the common writer resolutions (96, 192, 384, 480, 960, 1920 ppq) scaled to it, so a host
//      that re-saves the clip at its own resolution keeps the sidecar.
//      Hosts keep text events when a clip is edited, so a sidecar that no longer matches is
//      stale: the notes are imported with the heuristics below and the report says so.
//   2. Otherwise the first track with notes is used. Grid: 1/16, or 1/8 triplet when more than
//      20 % of onsets miss the 1/16 grid and the triplet grid fits better (or the grid you set).
//      Shuffle is detected when at least two off-beat notes are late by the same amount. A 2:1
//      shuffle on 1/16 lands exactly on 1/8 triplets; the clip end then picks the grid it fits.
//      Onsets snap to the nearest step.
//   3. Several notes in one step: the latest starting wins, then the highest velocity.
//   4. Accent = velocity above accentThreshold. Vibrato = CC1 >= 64 at the onset.
//   5. Slide on A when the next note starts before A ends, or when CC64 is held across a same
//      pitch retrigger; a note that reaches the clip end slides into step 0 when that is a Note.
//   6. Tie: a note without slide that covers the following steps far enough (half the
//      estimated gate into a step) marks them as Tie. Steps a slid note holds through are Ties.
//   7. Length from End of Track when it is on the grid and covers the notes, otherwise from the
//      last note end, clamped to 1..64. Pattern gate from the median plain-note length
//      (authentic when within 1.5 ticks of it). Notes outside the octave range are moved by
//      whole octaves; with keepPitch the pattern transpose compensates.
//
// Message thread. Everything allocates; nothing throws. Hostile files never crash: every
// read is bounds-checked and malformed files are rejected with a message.
#include "acid/seq/Pattern.h"
#include "acid/seq/PlayerTypes.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace acid::seq
{
struct MidiExportOptions
{
    double bpm = 120.0;                      // tempo meta event; clamped to 20..999
    int ppq = 480;                           // ticks per quarter; rounded to a multiple of 96 (96..9600)
    std::uint8_t accentVelocity = 96;        // defaults match Settings::outAccentVelocity
    std::uint8_t normalVelocity = 32;        // and Settings::outNormalVelocity
    int channel = 0;                         // 0..15
    bool applyShuffle = true;
    bool padToBar = false;                   // round the clip length up to whole 4/4 bars
    bool vibratoCC = true;                   // CC1 for vibrato notes
    bool embedPattern = true;                // lossless sidecar (Pattern overload only)
    bool applyTranspose = true;              // add Pattern::transpose to the notes
    SlideIntoRest slideIntoRest = SlideIntoRest::Release;   // as Settings::slideIntoRest
    bool twoPassesForOddShuffle = true;      // odd length with shuffle: write two passes (see above)
};

/** A timed note for exporting a recorded take (for example the arpeggiator's output).
    Times are in quarter notes from the clip start. Overlaps are written as given, so a slide
    is a note that ends after the next one starts. */
struct MidiNote
{
    double startQ = 0.0;
    double endQ = 0.25;
    std::uint8_t note = 60;
    bool accent = false;
    bool vibrato = false;
    std::uint8_t velocity = 0;               // 0 = from accent and the options' velocities
};

std::vector<std::uint8_t> writeMidiFile (const Pattern&, const MidiExportOptions& = {});

std::vector<std::uint8_t> writeMidiFile (std::span<const MidiNote> notes, double clipLengthQ,
                                         const MidiExportOptions& = {}, std::string_view trackName = {});

struct MidiImportOptions
{
    int accentThreshold = 64;                // velocity above this is an accent (Settings::accentThreshold)
    bool autoDivision = true;                // choose 1/16 or 1/8 triplet from the onsets
    StepDiv div = StepDiv::d1_16;            // used when autoDivision is false
    DivMod divMod = DivMod::Full;
    bool detectShuffle = true;
    double snapTolerance = 0.25;             // of a step: onsets further off count as off-grid
    bool keepPitch = true;                   // compensate octave moves with Pattern::transpose
    bool useEmbeddedPattern = true;          // a sidecar written by writeMidiFile wins while it matches the notes
};

struct MidiImportReport
{
    bool ok = false;
    std::string error;
    bool usedEmbedded = false;
    bool sidecarStale = false;               // a sidecar was found but the notes were edited since
    int format = 0;
    int ppq = 0;
    double bpm = 120.0;
    int notesRead = 0;                       // notes in the chosen track
    int notesDropped = 0;                    // polyphony, beyond 64 steps
    double offGridFraction = 0.0;            // on the chosen grid
    bool suggestTriplet = false;             // more than 20 % off the 1/16 grid
    int octaveShift = 0;                     // semitones added to fit the range
};

/** Parses bytes into out. On failure out is unchanged and the report says why. */
bool readMidiFile (std::span<const std::uint8_t> bytes, Pattern& out, const MidiImportOptions& = {},
                   MidiImportReport* report = nullptr);

// ---- Low-level SMF access (used by the importer, exposed for tests and tools) ----------

struct SmfEvent
{
    std::int64_t tick = 0;                   // absolute
    std::uint8_t status = 0;                 // channel status with channel, 0xFF meta, 0xF0 or 0xF7 sysex
    std::uint8_t data1 = 0;                  // meta: type
    std::uint8_t data2 = 0;
    std::vector<std::uint8_t> payload;       // meta and sysex data
};

struct SmfTrack
{
    std::vector<SmfEvent> events;
    bool hasEndOfTrack = false;
    std::int64_t endTick = 0;                // tick of End of Track (or of the last event)
};

struct SmfFile
{
    int format = 0;
    int ppq = 480;
    int declaredTracks = 0;
    std::vector<SmfTrack> tracks;
};

/** Strict structural parse: MThd first, MTrk chunks (unknown chunks skipped), delta times,
    running status, meta and sysex lengths all bounds-checked. SMPTE time division is rejected.
    Returns false with a message on any malformation. */
bool parseSmf (std::span<const std::uint8_t> bytes, SmfFile& out, std::string* error = nullptr);
}
