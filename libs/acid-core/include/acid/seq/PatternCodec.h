#pragma once
// Text encodings of one Pattern.
//
// 1. encodePattern / decodePattern: the lossless, versioned, single-line form used inside
//    preset and state files. Every field of Pattern is stored, generator memory and
//    GenParams included. Example (wrapped here; the real string is one line):
//
//      acidpat 1|name=Night%20Line|len=16|div=1/16|mod=full|shuffle=0|gate=0|dir=fwd|
//      transpose=0|seed=1|notemask=fff|root=0|octmask=06|useslide=1|useaccent=1|usevib=0|
//      amtnote=50|amtslide=50|amtaccent=50|amtvib=50|steps=N:C:0 R:D#:1 N:G:-1:1:AS ...
//
//    Fields are key=value separated by '|'. Readers ignore unknown keys (anywhere, so newer
//    writers can add fields) and keep defaults for missing ones (so older strings load).
//
//    Compatibility rule: a new version only ever adds keys, trailing step subfields and flag
//    letters. It never changes the meaning, order or range of anything that exists. A change
//    that would has to use a new header word instead of "acidpat", so an older reader refuses
//    it ("missing 'acidpat' header") rather than misreading it. That is why any version
//    number >= 1 is accepted; DecodeResult::newerVersion tells the caller something was
//    skipped.
//
//    The optional key "spares" holds the authentic-mode spare pitch entries (PatternOps.h),
//    which Pattern has no field for: space separated NOTE:octave[:flags] tokens, for example
//    "spares=G:1:AS C#:0". Written and read only by the overloads that take spares.
//    Steps are space separated, after the last default step they may be omitted. Each step
//    is positional, ':' separated, and trailing subfields equal to the default may be
//    omitted (extra trailing subfields are ignored):
//
//      state:note:octave:arpIndex:flags:probability:ratchet:gate:locks:magGate:magAccent:
//      magSlide:magVibrato:candNote:candOctave
//
//    state N, R or T; note and candNote C, C#, D ... B; flags a subset of "ASV" or "-";
//    magnitudes 0..127 or "-" for not generated; locks decimal. Name bytes outside printable
//    ASCII, and '%', '|', '=', are percent-encoded. The name round-trips up to its first NUL
//    and at most 31 bytes (decoders truncate longer names and always NUL-terminate).
//
// 2. toStepText / fromStepText: the compact human-readable notation of document 06
//    section 7.4, for tests, the clipboard and docs. Lossy by design (no generator data,
//    no locks, and rests and ties carry no pitch unless asked for):
//
//      acidsteps v1 len=16 div=1/16 mod=full shuffle=0 gate=auto dir=fwd transpose=0 name="example"
//      C  C+:S  D#:A  ~  .  G-:AS  C  .  C:S  C+  .  A#  ~  ~  .  C:A
//
//    Token := '.' rest | '~' tie | NOTE[':'FLAGS]. NOTE := [A-G][#b]? followed by one '+' or
//    '-' per octave above or below octave 0, or (arpeggiator patterns) an index 1..10 with
//    the same octave marks. FLAGS := any of A (accent), S (slide), V (vibrato), P<0..100>
//    (probability), R<1..4> (ratchet), G<0..127> (step gate). A rest may carry its stored
//    pitch as '.' + NOTE. The header line is optional; without it the length is the number
//    of tokens and other fields keep their defaults.
//
// Message thread. Decoders never trust input: every value is range-checked, out-of-range
// numbers are clamped (and counted), malformed syntax is rejected, nothing is written out of
// bounds, and on rejection the output pattern is left unchanged.
#include "acid/seq/Pattern.h"
#include "acid/seq/PatternOps.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace acid::seq
{
inline constexpr int kPatternCodecVersion = 1;

/** Lossless single-line encoding of every field. */
std::string encodePattern (const Pattern&);

/** Same, followed by the authentic-mode spare pitch entries under "spares=" (nothing is
    added when spares is empty, so the string equals encodePattern (p)). */
std::string encodePattern (const Pattern&, std::span<const PitchEntry> spares);

struct DecodeResult
{
    bool ok = false;
    int version = 0;          // version found in the header
    bool newerVersion = false;   // version > kPatternCodecVersion: keys or subfields this
                                 // reader does not know were skipped
    int adjusted = 0;         // values that were clamped, unknown enum names replaced by the
                              // default, or extra steps dropped
    std::string error;        // when ok is false: what was wrong and where
};

/** Decodes into out. On failure out is unchanged. Accepts any version >= 1: older strings
    simply lack fields, newer ones may add fields that are ignored. Input over 64 KiB is
    rejected. */
DecodeResult decodePattern (std::string_view text, Pattern& out);

/** Same, and also reads the "spares=" key into spares (cleared when the string has none, at
    most 256 entries). On failure neither out nor spares is changed. */
DecodeResult decodePattern (std::string_view text, Pattern& out, std::vector<PitchEntry>* spares);

/** True when text starts with the encodePattern header (cheap sniffing for clipboard or
    MIDI sidecar text). */
bool looksLikeEncodedPattern (std::string_view text) noexcept;

struct StepTextOptions
{
    bool header = true;          // write the "acidsteps v1 ..." header line
    bool arpeggiator = false;    // write arpIndex instead of note names
    bool restPitches = false;    // write rests as '.' + their stored pitch
    bool newlineEvery16 = false; // break the step line every 16 steps
};

/** Compact notation, steps [0, length). */
std::string toStepText (const Pattern&, const StepTextOptions& = {});

/** Parses the compact notation into out, starting from out's current fields (so a
    headerless string only replaces steps and length). Steps beyond the parsed tokens are
    reset to default Steps. Arpeggiator index tokens set arpIndex and leave note alone.
    On failure out is unchanged and error (when given) says why. */
bool fromStepText (std::string_view text, Pattern& out, std::string* error = nullptr);
}
