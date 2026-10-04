// Reviewer's tests. Each one encodes a defect found in review and fails until it is fixed.
#include "SeqTestSupport.h"
#include "acid/seq/MidiFile.h"
#include "acid/seq/PatternCodec.h"
#include "acid/seq/PatternOps.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace acid::seq;

TEST_CASE ("review: copySteps clamps the requested range, not just its start", "[review][ops]")
{
    // PatternOps.h: "Copies steps [first, first + count) clamped to [0, length)".
    // [-5, -3) does not overlap the pattern; [-3, 2) overlaps it in steps 0 and 1. The code
    // clamps first to 0 and then adds count, so it returns steps 0..1 and 0..4.
    Pattern p;
    REQUIRE (fromStepText ("C D E F G A B C+", p));
    CHECK (copySteps (p, -5, 2).empty());
    CHECK (copySteps (p, -3, 5).size() == 2);
}

TEST_CASE ("review: shortening and restoring the length keeps the hidden steps", "[review][ops]")
{
    // Dragging the length control 16 -> 8 -> 16 must not wipe steps 8..15. setLength resets
    // their state, flags, probability, ratchet and gate, so the second half of the line is
    // gone (only the pitches survive). Steps beyond the length are otherwise preserved by
    // every edit (transpose even keeps them in key "when the length grows later").
    Pattern p;
    REQUIRE (fromStepText ("C:A D:S E F:P50 G:R2 A B C+:A C D:S E F:G90 G A B C+", p));
    const Pattern orig = p;
    setLength (p, 8);
    setLength (p, 16);
    CHECK (samePattern (p, orig));
}

TEST_CASE ("review: an embedded pattern that no longer matches the notes does not win", "[review][midi]")
{
    // Drag out, edit a note in the host, drag back in: the stale sidecar text event wins and
    // the user's edit is silently discarded.
    Pattern p;
    REQUIRE (fromStepText ("C E G C+ . C . . C . . . C . . .", p));
    std::vector<std::uint8_t> bytes = writeMidiFile (p);

    // Move the first E (MIDI 40) up to F (41) the way a host would: note-on and note-off.
    int edits = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i)
        if ((bytes[i] == 0x90 || bytes[i] == 0x80) && bytes[i + 1] == 40)
        {
            bytes[i + 1] = 41;
            ++edits;
        }
    REQUIRE (edits == 2);

    Pattern in;
    MidiImportReport rep;
    REQUIRE (readMidiFile (bytes, in, {}, &rep));
    CHECK (in.steps[1].note == 5);   // F, as the file's notes now say
}
