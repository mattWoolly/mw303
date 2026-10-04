#pragma once
// The pattern generator: layered, thresholded and seeded (document 06 section 5.8).
//
// Two stages, so the four density knobs work in real time without re-rolling:
//
//   generate (pattern, seed)   draws, for every step (all 64) and every unlocked lane, a
//                              candidate pitch class and octave and a random magnitude for
//                              gate, accent, slide and vibrato, and stores per step and lane
//                              the density amount at which that lane turns on (see "Stored
//                              memory"). It changes only Step::candNote, candOctave and the
//                              mag* fields, and GenParams::seed. Nothing audible changes until:
//   applyDensities (pattern)   derives step state, pitch and the accent, slide and vibrato
//                              flags from the stored memory and the four amounts in
//                              GenParams. No randomness. Call it after generate and again on
//                              every density knob move.
//
// Stored memory. Step::magGate, magAccent, magSlide and magVibrato hold a turn-on amount:
// the lane is on for that step when its density amount is at least the stored value
// (1..100; 101 to 127 = never; 255 = not generated, applyDensities leaves the lane alone;
// at amount 0 a lane is always empty). Everything that depends on the step's position (the
// metric weight, the bar anchors and other gate guards, the accent off-beat bias, the
// anti-clustering against the previous step and the slide's leap) is folded into that value
// when generate runs, so the memory travels with the step: rotating, reversing or
// duplicating a generated line keeps it exactly as it is through later knob moves. The
// only neighbour test applyDensities still makes is that a slide needs a Note to land on.
//
// Guarantees (each is a unit test):
//   - Same seed, same GenParams, same pattern in, bit-identical pattern out, everywhere
//     (integer arithmetic only, no library distributions, no floating point).
//   - Raising amountNote only turns Rests into Notes; lowering it only turns Notes into Rests.
//     Raising amountAccent, amountSlide or amountVibrato only adds that flag, lowering only
//     removes it. No knob ever moves a pitch: a revealed step plays its stored candidate.
//   - A density knob never changes another lane, except that a slide needs both its own step
//     and the step it leads into (the next step that is not a Tie) to be Notes, so hiding a
//     note can hide the slide before it.
//   - After any PatternOps edit (shift, reverse, paste, duplicate, length, mute, lane
//     randomise, transpose) and after keepEdits, applyDensities with unchanged amounts is a
//     no-op: an edit is never undone or moved by the next knob move.
//   - Changing the pattern length never changes steps below min (old, new), with one exception:
//     the slide flag of the last Note before min (old, new), whose target is the loop wrap to
//     step 0 on one side of the change and a real step on the other. setLength re-derives it.
//   - A lane locked in Step::locks is bit-identical after generate and applyDensities, for that
//     step: kLanePitch keeps note and candNote, kLaneOctave keeps octave and candOctave,
//     kLaneGate keeps state and magGate, kLaneAccent/Slide/Vibrato keep the flag and magnitude.
//   - A GenParams lane that is switched off (useAccent, useSlide, useVibrato false) is left
//     untouched by both stages, as the parity target does with unselected attributes.
//
// Hand edits. The parity target lets the user edit the grid while the generator is active.
// After writing any step field of a generated pattern directly (a grid click, a pitch drag,
// a flag toggle, a pasted value), call keepEdits: otherwise the next applyDensities writes the
// generator's state, pitch and flags back over the edit. The PatternOps functions do this
// themselves where they need to.
//
// Musical shaping (defaults are idiomatic acid lines; see the statistical test):
//   - Gate: metric weights per position in the bar (strong downbeats, weak off-sixteenths;
//     triplet patterns use a three-per-beat table and 12-step bars), an anchored first step
//     of every bar, at least two notes per bar, and (from amountNote 30 up) no run of more
//     than four rests. The guards are evaluated by generate over every amount 1..100 against
//     the steps' states at that time (locked notes count), and stored in the turn-on amounts.
//   - Pitch: from GenParams::noteMask rotated by root, weighted towards the root and the fifth,
//     with a chance to repeat the previous step's pitch class.
//   - Octave: from GenParams::octaveMask, weighted towards the middle of the allowed range.
//   - Accent: off-beat bias and anti-clustering (an accent right after a stronger candidate is
//     less likely). Slide: biased towards octave and fifth leaps, anti-clustered.
//   - No ties are generated (Pattern has no tie amount); a slide into the same pitch plays as
//     a legato tie.
//
// A turn-on amount is the smallest A at which mag / 128 < p (A) for the monotone curve
// p (A) = A r / (100 + A (r - 1)), where mag is the random draw and r the step's weight
// relative to the average, so p (0) = 0 and p (100) = 1 for every step.
//
// Message thread. Every function here is noexcept and allocation-free.
#include "acid/seq/Pattern.h"
#include <cstdint>

namespace acid::seq
{
/** Pitch-shaping options for generate. They are not stored in the pattern and do not affect
    applyDensities, so changing them needs a new generate. Defaults are the shipped sound. */
struct GeneratorOptions
{
    int rootWeight = 4;          // relative pick weight of the root when it is in the mask
    int fifthWeight = 2;         // relative weight of the fifth (root + 7) when in the mask
    int otherWeight = 1;         // every other allowed pitch class
    int repeatPercent = 30;      // chance a step copies the previous step's pitch class
    int octaveHomeWeight = 4;    // weight of the middle allowed octave (lower median)
    int octaveNearWeight = 2;    // octaves one away from it
    int octaveFarWeight = 1;     // anything further
};

/** Draws candidates and turn-on amounts for every unlocked lane of all 64 steps. Only the
    low 32 bits of seed are used, and they are stored in GenParams::seed, so generate (p) later
    reproduces the same line. Uses GenParams noteMask, root, octaveMask and the use* switches,
    Pattern::divMod (metric weights), Pattern::length (the slide leap of the last step wraps to
    step 0) and the current state of steps whose gate is locked. Each lane has its own stream
    derived from the seed, and every draw is made for every step whether used or not, so
    locking a step or a lane never changes the random draws of any other step or lane (only
    the bar guards, which count locked notes, can move another step's gate turn-on amount). */
void generate (Pattern&, std::uint64_t seed, const GeneratorOptions& = {}) noexcept;

/** Same, seeded from GenParams::seed. */
void generate (Pattern&, const GeneratorOptions& = {}) noexcept;

/** Derives states and flags from the stored memory and GenParams amounts (see above).
    Steps with no generator memory (all magnitudes 255) are left alone, so this is a no-op on
    a pattern that was never generated. Writes the candidate pitch into every generated step,
    Rests included, so a step revealed later already holds its pitch. */
void applyDensities (Pattern&) noexcept;

/** generate followed by applyDensities: what a "Draw" button does. */
void regenerate (Pattern&, std::uint64_t seed, const GeneratorOptions& = {}) noexcept;

/** Commit: keeps the pattern exactly as it sounds and drops the generator memory (every
    magnitude back to 255), so later applyDensities calls leave it alone. */
void flatten (Pattern&) noexcept;

/** Makes the generator memory agree with the pattern as it is now, after a direct edit:
    for every step and lane where applyDensities (with the current amounts) would write
    something different, that lane's memory is released (its magnitude set to 255, so the
    knob no longer drives it on that step) and, for pitch and octave, the candidate is set to
    the step's own pitch. Locks are not touched, so the next Draw regenerates those lanes as
    usual. Lanes that already agree keep their memory and keep following the knobs.
    Afterwards applyDensities is a no-op until an amount changes. Call it with the amounts
    the pattern was last derived with. */
void keepEdits (Pattern&) noexcept;

/** The density amount at which a lane of this step turns on (0..127, see "Stored memory"),
    or -1 when the lane has no generator memory on this step. lane is one of kLaneGate,
    kLaneAccent, kLaneSlide, kLaneVibrato; pitch and octave lanes have none (-1). Exposed for
    the editor, which can draw the thresholds, and for tests. */
int laneTurnOnAmount (const Step&, LaneBit lane) noexcept;
}
