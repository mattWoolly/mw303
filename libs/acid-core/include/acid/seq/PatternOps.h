#pragma once
// Edit operations on one Pattern, and the pitch-list/time-list view of the original unit.
//
// Everything here runs on the message thread. Functions marked noexcept never allocate,
// lock or throw, so they are also safe on a copy owned by any other thread; the few that
// return containers allocate. All operations are deterministic.
//
// "Step data" below always means the whole Step: state, pitch, flags, probability,
// ratchet, gate, locks and the generator memory (magnitudes and candidates) move together.
//
// Generated patterns. The generator memory is position-independent (see Generator.h), so
// edits that only move steps (rotate, reverse, duplicate, setLength) keep a generated line
// exactly as it is through later density knob moves. Edits that write step content, or that
// pair a slide with a different target (reverseSteps, pasteSteps, muteSteps, resetTiming,
// randomiseLane, transpose without the generator, the authentic edits) call keepEdits on the
// result: lanes they changed on generated steps stop following the knobs, so the next
// applyDensities never undoes them. Every function in this header leaves the pattern so that
// applyDensities with unchanged amounts is a no-op, if it was before. An editor that writes
// Step fields directly must call keepEdits itself (Generator.h).
#include "acid/seq/Pattern.h"
#include <cstdint>
#include <span>
#include <vector>

namespace acid::seq
{
// ---- Comparison and validation ---------------------------------------------------------

/** Field-by-field equality (Pattern has padding, so never compare with memcmp).
    The name compares as a C string of at most kPatternNameLength bytes. */
bool sameStep (const Step& a, const Step& b) noexcept;
bool samePattern (const Pattern& a, const Pattern& b) noexcept;

/** Clamps every field into its legal range (length 1..64, note 0..11, octave -1..3,
    arpIndex 1..10, probability 0..100, ratchet 1..4, gate 0..127, locks 6 bits,
    magnitudes 0..127 or 255, enums to their last value, amounts 0..100, root 0..11,
    noteMask 12 bits, octaveMask 5 bits, shuffle and gateLength 0..100, transpose -48..48)
    and makes the name a NUL-terminated, NUL-padded string of valid UTF-8 (invalid sequences
    become U+FFFD, and a name too long for the field is cut at a character boundary).
    Returns the number of fields it changed.
    Call it on anything that came from outside (state files, clipboard, other software). */
int sanitizePattern (Pattern&) noexcept;

// ---- Pitch helpers ---------------------------------------------------------------------

/** Semitones above (pitch class 0, kMinOctave): 0..59 for legal steps. */
constexpr int pitchIndex (int note, int octave) noexcept { return 12 * (octave - kMinOctave) + note; }
inline constexpr int kNumPitches = 12 * (kMaxOctave - kMinOctave + 1);   // 60

// ---- Grid edits ------------------------------------------------------------------------

/** Rotates steps [0, length) by n positions with wrap; positive n moves steps later
    (right), negative earlier (left). Steps beyond length are untouched. Any n is legal. */
void rotate (Pattern&, int n) noexcept;
inline void shiftRight (Pattern& p, int n = 1) noexcept { rotate (p, n); }
inline void shiftLeft (Pattern& p, int n = 1) noexcept { rotate (p, -n); }

/** Octave window that transpose wraps inside. Defaults to the full range, -1..+3. */
struct TransposeRange
{
    int lowOctave = kMinOctave;
    int highOctave = kMaxOctave;
};

/** Transposes by semitones with wrap inside the window (B at the top wraps to C at the
    bottom and back), carrying note and octave together. Applies to all 64 steps, so
    hidden steps stay in key when the length grows later. Rests and ties keep a stored
    pitch and are transposed with everything else, so toggling them on stays in key.
    With includeGenerator the generator candidates and GenParams::root move too, so
    steps revealed later by a density knob are also in the new key. Without it, root stays
    (the next Draw is in the old key) and keepEdits pins the transposed pitches of generated
    steps. A pitch outside the window is first folded into it by whole octaves. */
void transpose (Pattern&, int semitones, bool includeGenerator = true, TransposeRange range = {}) noexcept;

/** Arpeggiator inversion: rotates every step's arpIndex by n inside 1..kMaxArpIndex
    (10 + 1 wraps to 1). All 64 steps. Out-of-range indices are clamped first. */
void invertArp (Pattern&, int n) noexcept;

/** Musical reverse of steps [0, length): the order of steps is mirrored, then each
    note-plus-ties group is re-formed so the Note comes first (N T T stays N T T, it does
    not become T T N), and slide flags are moved so every glide joins the same two notes
    in the new direction (including the glide that wraps around the loop); a slide's generator
    memory and slide lock move with its flag. Applying it twice restores the pattern exactly,
    unless it has orphan ties (a Tie right after a Rest, or on step 0), which play as rests
    and may attach to a note once mirrored. */
void reverse (Pattern&) noexcept;

/** Plain mirror of the step array [0, length) with no musical fix-ups, then keepEdits
    (a mirrored slide flag now leads into a different step). Self-inverse on a pattern with
    no generator memory. */
void reverseSteps (Pattern&) noexcept;

/** Every one of the 64 steps back to a default Step (rest, C, octave 0, no flags, no
    locks, no generator memory). Pattern settings, name and GenParams are kept. */
void clearSteps (Pattern&) noexcept;

/** Every step becomes a Rest with accent, slide and vibrato off; pitch, probability,
    ratchet, gate and locks are kept ("clear gates, keep the notes"). Then keepEdits: on
    generated steps that were sounding, the gate memory is released, so no density knob
    setting brings those notes back; steps that were hidden keep their memory and can still
    be revealed by raising the Note knob. */
void muteSteps (Pattern&) noexcept;

/** Copies the part of [first, first + count) that lies inside [0, length), so it may
    return fewer steps, or none (count <= 0 or no overlap). */
std::vector<Step> copySteps (const Pattern&, int first, int count);

/** Writes the clip starting at step first. Without wrap, steps that would land at or
    beyond length are dropped; with wrap they continue at step 0 (still within length,
    and never more than length steps). Then keepEdits. Returns the number of steps written. */
int pasteSteps (Pattern&, int first, std::span<const Step> clip, bool wrap = false) noexcept;

/** Copies the first half into the second: steps [0, length) are repeated after the end
    and length doubles, up to 64 (only the part that fits is copied when length > 32).
    Generated slides whose landing step changed (the last copied step wraps to step 0) are
    re-derived from their memory. Returns false and changes nothing when length is already 64. */
bool duplicate (Pattern&) noexcept;

/** Sets length (clamped to 1..64). No step data changes: steps beyond the length are
    kept as they are, so dragging the length down and back up restores the line. The one
    exception is a generated slide whose landing step changes with the loop point (the last
    Note before the old or new length): it is re-derived from its memory, as the next
    applyDensities would. For the original unit's behaviour (steps a longer length reveals
    come up as rests) call resetTiming on them first. */
void setLength (Pattern&, int length) noexcept;

/** Steps [first, first + count) (clamped to 0..63) become Rests with accent, slide and
    vibrato off and probability, ratchet and gate back to default; pitch, locks and the
    rest of the generator memory are kept. Then keepEdits. */
void resetTiming (Pattern&, int first, int count) noexcept;

/** Re-randomises a single lane over steps [0, length) and leaves every other lane alone.
    lane is one LaneBit: kLanePitch draws pitch classes from GenParams::noteMask rotated
    by root; kLaneOctave draws from GenParams::octaveMask; kLaneGate makes each step a
    Note with probability amount % (otherwise a Rest; ties are replaced); kLaneAccent,
    kLaneSlide and kLaneVibrato set the flag with probability amount %. Steps whose
    Step::locks has that lane are skipped. Deterministic in (seed, lane, step). Then
    keepEdits, so on a generated pattern the new values stay through knob moves. */
void randomiseLane (Pattern&, LaneBit lane, std::uint64_t seed, int amount = 50) noexcept;

// ---- The original unit's pitch-list/time-list view (document 06 sections 1.2 and 8) ----
//
// The hardware stores an ordered pitch list and a time list (Note, Tie, Rest per step).
// At playback only a Note consumes the next pitch entry; ties and rests consume nothing.
// Our canonical form is the step grid; this view is derived losslessly:
//
//   pitches = the Note steps' pitch entries in step order (steps < length only),
//             followed by the spare entries the grid does not use.
//   time    = every step (all 64) with all of its data. For a step that consumes an entry
//             the pitch fields duplicate the entry; for a Rest, a Tie or any step beyond
//             length they are that step's own stored ("shadow") pitch, kept so the grid
//             view gets back exactly what it had.
//
// Pattern has no field for spare pitch entries, so the editor keeps them next to the
// pattern (a std::vector<PitchEntry>) while it edits in authentic mode, and must keep them
// per slot in its own undo and state model; encodePattern and decodePattern have overloads
// that store them under a "spares=" key. The authentic edits below end with keepEdits.

/** One entry of the pitch list. Accent, slide and vibrato belong to the entry. */
struct PitchEntry
{
    std::uint8_t note = 0;     // 0..11
    std::int8_t octave = 0;    // kMinOctave..kMaxOctave
    bool accent = false;
    bool slide = false;
    bool vibrato = false;
};

bool samePitchEntry (const PitchEntry& a, const PitchEntry& b) noexcept;

struct PitchTimeView
{
    int length = 16;
    std::vector<PitchEntry> pitches;          // consumed entries first, then spares
    std::array<Step, kMaxSteps> time {};      // per time position (see above)

    /** Number of Note steps inside the length, i.e. entries consumed per cycle. */
    int consumedCount() const noexcept;
};

/** Grid to pitch/time. spare is appended after the consumed entries. */
PitchTimeView toPitchTime (const Pattern&, std::span<const PitchEntry> spare = {});

/** Pitch/time to grid, the hardware zip: the k-th Note step inside the length takes
    pitches[k]; when the list runs out a Note keeps its own shadow pitch. Writes length
    and all 64 steps of out and nothing else. Entries left unconsumed go to spareOut
    (when given). toPitchTime followed by fromPitchTime reproduces the pattern and the
    spares exactly. */
void fromPitchTime (const PitchTimeView&, Pattern& out, std::vector<PitchEntry>* spareOut = nullptr);

/** Index of the pitch entry step consumes, or -1 for a Rest, a Tie or a step beyond length. */
int pitchEntryIndexForStep (const Pattern&, int step) noexcept;

// Authentic edit mode. Each call converts to the view, edits it the way the hardware
// would, and zips back, so later pitches re-flow exactly as on the original unit.

/** Changes the time event at step. Turning a Rest into a Note makes every later Note
    play the entry after the one it played (the last one picks up the first spare);
    turning a Note into a Rest or Tie does the reverse and pushes one entry into the
    spares. The step keeps the pitch it was playing as its shadow pitch. */
void authenticSetState (Pattern&, std::vector<PitchEntry>& spare, int step, StepState state);

/** Overwrites pitch entry index (consumed or spare; index == size appends a spare) with
    a new note and octave. As on the hardware, re-entering a pitch clears the entry's
    accent and slide (vibrato too). Returns false when index is out of range. */
bool authenticWritePitch (Pattern&, std::vector<PitchEntry>& spare, int index, int note, int octave);

/** Sets the flags of pitch entry index without touching its pitch. */
bool authenticSetFlags (Pattern&, std::vector<PitchEntry>& spare, int index, bool accent, bool slide, bool vibrato);

/** Changes the length in authentic mode: entries consumed by Notes that fall outside the
    new length become spares (at the front of the spare list, in order). Steps newly
    inside the length become Rests (resetTiming), so they consume nothing. */
void authenticSetLength (Pattern&, std::vector<PitchEntry>& spare, int length);
}
