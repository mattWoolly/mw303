#include "acid/seq/PatternOps.h"
#include "acid/common/Utf8.h"
#include "acid/seq/Generator.h"
#include "SeqInternal.h"
#include <algorithm>
#include <array>
#include <type_traits>

namespace acid::seq
{
using detail::clampInt;

namespace
{
int lengthOf (const Pattern& p) noexcept { return clampInt (p.length, 1, kMaxSteps); }

/** Whether each step's slide currently lands on a Note, for re-deriving the generator's
    slides where a length change or a copy changed that (see rederiveMovedSlides). */
std::array<bool, kMaxSteps> slideLandings (const Pattern& p, int len) noexcept
{
    std::array<bool, kMaxSteps> out {};
    for (int i = 0; i < kMaxSteps; ++i)
        out[static_cast<std::size_t> (i)] = detail::slideTargetIsNote (p, i, len);
    return out;
}

/** Re-applies the generator's slide rule to steps [0, kMaxSteps) whose slide now lands on a
    Note where its source step's did not, or the other way round. source (i) names the step
    whose landing step i inherited. Steps without slide memory are never touched. */
template <typename Source>
void rederiveMovedSlides (Pattern& p, const std::array<bool, kMaxSteps>& before, Source source) noexcept
{
    const int len = lengthOf (p);
    for (int i = 0; i < kMaxSteps; ++i)
        if (detail::slideTargetIsNote (p, i, len) != before[static_cast<std::size_t> (source (i))])
            detail::rederiveSlide (p, i);
}
}

// ---- Comparison and validation ---------------------------------------------------------

bool sameStep (const Step& a, const Step& b) noexcept
{
    return a.state == b.state && a.note == b.note && a.octave == b.octave && a.arpIndex == b.arpIndex
        && a.accent == b.accent && a.slide == b.slide && a.vibrato == b.vibrato
        && a.probability == b.probability && a.ratchet == b.ratchet && a.gate == b.gate
        && a.locks == b.locks && a.magGate == b.magGate && a.magAccent == b.magAccent
        && a.magSlide == b.magSlide && a.magVibrato == b.magVibrato
        && a.candNote == b.candNote && a.candOctave == b.candOctave;
}

bool samePattern (const Pattern& a, const Pattern& b) noexcept
{
    for (int i = 0; i < kPatternNameLength; ++i)
    {
        if (a.name[i] != b.name[i]) return false;
        if (a.name[i] == '\0') break;
    }

    if (a.length != b.length || a.div != b.div || a.divMod != b.divMod || a.shuffle != b.shuffle
        || a.gateLength != b.gateLength || a.direction != b.direction || a.transpose != b.transpose)
        return false;

    for (int i = 0; i < kMaxSteps; ++i)
        if (! sameStep (a.steps[static_cast<std::size_t> (i)], b.steps[static_cast<std::size_t> (i)]))
            return false;

    const GenParams& g = a.gen;
    const GenParams& h = b.gen;
    return g.seed == h.seed && g.noteMask == h.noteMask && g.root == h.root && g.octaveMask == h.octaveMask
        && g.useSlide == h.useSlide && g.useAccent == h.useAccent && g.useVibrato == h.useVibrato
        && g.amountNote == h.amountNote && g.amountSlide == h.amountSlide
        && g.amountAccent == h.amountAccent && g.amountVibrato == h.amountVibrato;
}

int sanitizePattern (Pattern& p) noexcept
{
    int changes = 0;

    auto fixU8 = [&changes] (std::uint8_t& v, int lo, int hi) {
        const int c = clampInt (v, lo, hi);
        if (c != v) { v = static_cast<std::uint8_t> (c); ++changes; }
    };
    auto fixS8 = [&changes] (std::int8_t& v, int lo, int hi) {
        const int c = clampInt (v, lo, hi);
        if (c != v) { v = static_cast<std::int8_t> (c); ++changes; }
    };
    auto fixMag = [&changes] (std::uint8_t& v) {
        if (v > 127 && v != 255) { v = 127; ++changes; }
    };
    auto fixEnum = [&changes] (auto& e, int last) {
        using E = std::remove_reference_t<decltype (e)>;
        if (static_cast<int> (e) > last) { e = static_cast<E> (last); ++changes; }
    };

    // The name: NUL terminated and padded, valid UTF-8, never a character cut in half.
    if (utf8::repairName (p.name, kPatternNameLength)) ++changes;

    fixU8 (p.length, 1, kMaxSteps);
    fixEnum (p.div, static_cast<int> (StepDiv::d1_1));
    fixEnum (p.divMod, static_cast<int> (DivMod::Triplet));
    fixU8 (p.shuffle, 0, 100);
    fixU8 (p.gateLength, 0, 100);
    fixEnum (p.direction, static_cast<int> (Direction::Random));
    fixS8 (p.transpose, -48, 48);

    for (Step& s : p.steps)
    {
        fixEnum (s.state, static_cast<int> (StepState::Tie));
        fixU8 (s.note, 0, 11);
        fixS8 (s.octave, kMinOctave, kMaxOctave);
        fixU8 (s.arpIndex, 1, kMaxArpIndex);
        fixU8 (s.probability, 0, 100);
        fixU8 (s.ratchet, 1, 4);
        fixU8 (s.gate, 0, 127);
        fixU8 (s.locks, 0, 63);
        fixMag (s.magGate);
        fixMag (s.magAccent);
        fixMag (s.magSlide);
        fixMag (s.magVibrato);
        fixU8 (s.candNote, 0, 11);
        fixS8 (s.candOctave, kMinOctave, kMaxOctave);
    }

    GenParams& g = p.gen;
    if (g.noteMask > 0x0FFF) { g.noteMask = static_cast<std::uint16_t> (g.noteMask & 0x0FFF); ++changes; }
    fixU8 (g.root, 0, 11);
    if (g.octaveMask > 0x1F) { g.octaveMask = static_cast<std::uint8_t> (g.octaveMask & 0x1F); ++changes; }
    fixU8 (g.amountNote, 0, 100);
    fixU8 (g.amountSlide, 0, 100);
    fixU8 (g.amountAccent, 0, 100);
    fixU8 (g.amountVibrato, 0, 100);
    return changes;
}

// ---- Grid edits ------------------------------------------------------------------------

void rotate (Pattern& p, int n) noexcept
{
    const int len = lengthOf (p);
    const int r = detail::positiveMod (n, len);
    if (r == 0) return;
    auto first = p.steps.begin();
    std::rotate (first, first + (len - r), first + len);
}

namespace
{
void foldPitch (int& note, int& octave, int semitones, int lo, int size) noexcept
{
    const long long idx = pitchIndex (note, octave);
    const int folded = lo + detail::positiveMod (idx - lo + semitones, size);
    note = folded % 12;
    octave = folded / 12 + kMinOctave;
}
}

void transpose (Pattern& p, int semitones, bool includeGenerator, TransposeRange range) noexcept
{
    const int lowOct = clampInt (range.lowOctave, kMinOctave, kMaxOctave);
    const int highOct = clampInt (range.highOctave, lowOct, kMaxOctave);
    const int lo = pitchIndex (0, lowOct);
    const int size = 12 * (highOct - lowOct + 1);

    for (Step& s : p.steps)
    {
        int note = s.note, octave = s.octave;
        foldPitch (note, octave, semitones, lo, size);
        s.note = static_cast<std::uint8_t> (note);
        s.octave = static_cast<std::int8_t> (octave);

        if (includeGenerator)
        {
            int cn = s.candNote, co = s.candOctave;
            foldPitch (cn, co, semitones, lo, size);
            s.candNote = static_cast<std::uint8_t> (cn);
            s.candOctave = static_cast<std::int8_t> (co);
        }
    }

    if (includeGenerator)
        p.gen.root = static_cast<std::uint8_t> (detail::positiveMod (static_cast<long long> (p.gen.root) + semitones, 12));
    else
        keepEdits (p);
}

void invertArp (Pattern& p, int n) noexcept
{
    for (Step& s : p.steps)
    {
        const int idx = clampInt (s.arpIndex, 1, kMaxArpIndex) - 1;
        s.arpIndex = static_cast<std::uint8_t> (detail::positiveMod (static_cast<long long> (idx) + n, kMaxArpIndex) + 1);
    }
}

void reverseSteps (Pattern& p) noexcept
{
    const int len = lengthOf (p);
    std::reverse (p.steps.begin(), p.steps.begin() + len);
    keepEdits (p);
}

void reverse (Pattern& p) noexcept
{
    const int len = lengthOf (p);
    const std::array<Step, kMaxSteps> src = p.steps;

    // Mirror, keeping each Note and the ties attached to it as one block in forward order.
    int i = 0;
    while (i < len)
    {
        const auto si = static_cast<std::size_t> (i);
        if (src[si].state == StepState::Note)
        {
            int g = 1;
            while (i + g < len && src[static_cast<std::size_t> (i + g)].state == StepState::Tie) ++g;
            const int dest = len - i - g;
            for (int k = 0; k < g; ++k)
                p.steps[static_cast<std::size_t> (dest + k)] = src[static_cast<std::size_t> (i + k)];
            i += g;
        }
        else
        {
            p.steps[static_cast<std::size_t> (len - 1 - i)] = src[si];
            ++i;
        }
    }

    // A glide joins a note and the next note. Reversed, the same two notes are joined in the
    // other direction, so the flag moves to the note that now comes first. Cyclic. The slide's
    // generator memory and lock belong to the glide, so they move with the flag.
    std::array<int, kMaxSteps> pos {};
    int count = 0;
    for (int k = 0; k < len; ++k)
        if (p.steps[static_cast<std::size_t> (k)].state == StepState::Note) pos[static_cast<std::size_t> (count++)] = k;
    if (count == 0) return;

    struct Glide
    {
        bool flag;
        std::uint8_t mag;
        std::uint8_t lock;
    };
    std::array<Glide, kMaxSteps> old {};
    for (int j = 0; j < count; ++j)
    {
        const Step& s = p.steps[static_cast<std::size_t> (pos[static_cast<std::size_t> (j)])];
        old[static_cast<std::size_t> (j)] = Glide { s.slide, s.magSlide, static_cast<std::uint8_t> (s.locks & kLaneSlide) };
    }
    for (int j = 0; j < count; ++j)
    {
        Step& s = p.steps[static_cast<std::size_t> (pos[static_cast<std::size_t> (j)])];
        const Glide& g = old[static_cast<std::size_t> ((j + 1) % count)];
        s.slide = g.flag;
        s.magSlide = g.mag;
        s.locks = static_cast<std::uint8_t> ((s.locks & ~kLaneSlide) | g.lock);
    }
}

void clearSteps (Pattern& p) noexcept
{
    p.steps.fill (Step {});
}

void muteSteps (Pattern& p) noexcept
{
    for (Step& s : p.steps)
    {
        s.state = StepState::Rest;
        s.accent = false;
        s.slide = false;
        s.vibrato = false;
    }
    keepEdits (p);
}

std::vector<Step> copySteps (const Pattern& p, int first, int count)
{
    const int len = lengthOf (p);
    const long long lo = first;
    const long long hi = lo + std::max (count, 0);
    const int a = clampInt (lo, 0, len);
    const int b = clampInt (hi, a, len);
    return std::vector<Step> (p.steps.begin() + a, p.steps.begin() + b);
}

int pasteSteps (Pattern& p, int first, std::span<const Step> clip, bool wrap) noexcept
{
    const int len = lengthOf (p);
    int written = 0;
    if (! wrap && (first < 0 || first >= len)) return 0;
    const int start = wrap ? detail::positiveMod (first, len) : first;

    for (std::size_t k = 0; k < clip.size(); ++k)
    {
        if (written >= len) break;
        int pos = start + static_cast<int> (k);
        if (wrap) pos %= len;
        else if (pos >= len) break;
        p.steps[static_cast<std::size_t> (pos)] = clip[k];
        ++written;
    }
    if (written > 0) keepEdits (p);
    return written;
}

bool duplicate (Pattern& p) noexcept
{
    const int len = lengthOf (p);
    if (len >= kMaxSteps) return false;
    const int newLen = std::min (2 * len, kMaxSteps);
    const auto before = slideLandings (p, len);
    for (int i = len; i < newLen; ++i)
        p.steps[static_cast<std::size_t> (i)] = p.steps[static_cast<std::size_t> (i - len)];
    p.length = static_cast<std::uint8_t> (newLen);
    rederiveMovedSlides (p, before, [len, newLen] (int i) { return i >= len && i < newLen ? i - len : i; });
    return true;
}

void setLength (Pattern& p, int length) noexcept
{
    const int oldLen = lengthOf (p);
    const int newLen = clampInt (length, 1, kMaxSteps);
    const auto before = slideLandings (p, oldLen);
    p.length = static_cast<std::uint8_t> (newLen);
    if (newLen != oldLen) rederiveMovedSlides (p, before, [] (int i) { return i; });
}

void resetTiming (Pattern& p, int first, int count) noexcept
{
    const Step def {};
    const long long lo = first;
    const int a = clampInt (lo, 0, kMaxSteps);
    const int b = clampInt (lo + std::max (count, 0), a, kMaxSteps);
    for (int i = a; i < b; ++i)
    {
        Step& s = p.steps[static_cast<std::size_t> (i)];
        s.state = StepState::Rest;
        s.accent = false;
        s.slide = false;
        s.vibrato = false;
        s.probability = def.probability;
        s.ratchet = def.ratchet;
        s.gate = def.gate;
    }
    keepEdits (p);
}

void randomiseLane (Pattern& p, LaneBit lane, std::uint64_t seed, int amount) noexcept
{
    const int len = lengthOf (p);
    const int pct = clampInt (amount, 0, 100);
    Rng rng = detail::laneRng (seed, 0x6C616E6552616E64ull ^ (static_cast<std::uint64_t> (lane) << 56));

    // Allowed pitch classes and octaves, as lists, for uniform picks.
    int notes[12] {};
    int numNotes = 0;
    const std::uint16_t abs = detail::absoluteNoteMask (static_cast<std::uint16_t> (p.gen.noteMask & 0x0FFF), p.gen.root % 12);
    for (int pc = 0; pc < 12; ++pc)
        if ((abs >> pc) & 1u) notes[numNotes++] = pc;
    if (numNotes == 0) notes[numNotes++] = p.gen.root % 12;

    int octaves[kMaxOctave - kMinOctave + 1] {};
    int numOct = 0;
    for (int o = kMinOctave; o <= kMaxOctave; ++o)
        if ((p.gen.octaveMask >> (o - kMinOctave)) & 1u) octaves[numOct++] = o;
    if (numOct == 0) octaves[numOct++] = 0;

    // One draw per step for all 64 steps, so the result for a step never depends on length.
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const std::uint64_t u = rng.nextU64();
        if (i >= len) continue;
        Step& s = p.steps[static_cast<std::size_t> (i)];
        if (s.locks & lane) continue;

        const auto top = u >> 32;
        auto below = [top] (int n) { return static_cast<int> ((top * static_cast<std::uint64_t> (n)) >> 32); };
        const bool hit = below (100) < pct;

        switch (lane)
        {
            case kLanePitch:   s.note = static_cast<std::uint8_t> (notes[below (numNotes)]); break;
            case kLaneOctave:  s.octave = static_cast<std::int8_t> (octaves[below (numOct)]); break;
            case kLaneGate:    s.state = hit ? StepState::Note : StepState::Rest; break;
            case kLaneAccent:  s.accent = hit; break;
            case kLaneSlide:   s.slide = hit; break;
            case kLaneVibrato: s.vibrato = hit; break;
            default: break;
        }
    }
    keepEdits (p);
}

// ---- Pitch/time view -------------------------------------------------------------------

namespace
{
PitchEntry entryOf (const Step& s) noexcept
{
    return PitchEntry { s.note, s.octave, s.accent, s.slide, s.vibrato };
}

void applyEntry (Step& s, const PitchEntry& e) noexcept
{
    s.note = e.note;
    s.octave = e.octave;
    s.accent = e.accent;
    s.slide = e.slide;
    s.vibrato = e.vibrato;
}
}

bool samePitchEntry (const PitchEntry& a, const PitchEntry& b) noexcept
{
    return a.note == b.note && a.octave == b.octave && a.accent == b.accent && a.slide == b.slide
        && a.vibrato == b.vibrato;
}

int PitchTimeView::consumedCount() const noexcept
{
    const int len = clampInt (length, 1, kMaxSteps);
    int n = 0;
    for (int i = 0; i < len; ++i)
        if (time[static_cast<std::size_t> (i)].state == StepState::Note) ++n;
    return n;
}

PitchTimeView toPitchTime (const Pattern& p, std::span<const PitchEntry> spare)
{
    PitchTimeView v;
    v.length = lengthOf (p);
    v.time = p.steps;
    v.pitches.reserve (static_cast<std::size_t> (v.length) + spare.size());
    for (int i = 0; i < v.length; ++i)
    {
        const Step& s = p.steps[static_cast<std::size_t> (i)];
        if (s.state == StepState::Note) v.pitches.push_back (entryOf (s));
    }
    v.pitches.insert (v.pitches.end(), spare.begin(), spare.end());
    return v;
}

void fromPitchTime (const PitchTimeView& v, Pattern& out, std::vector<PitchEntry>* spareOut)
{
    const int len = clampInt (v.length, 1, kMaxSteps);
    std::size_t k = 0;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        Step s = v.time[static_cast<std::size_t> (i)];
        if (i < len && s.state == StepState::Note)
        {
            if (k < v.pitches.size()) applyEntry (s, v.pitches[k]);
            ++k;
        }
        out.steps[static_cast<std::size_t> (i)] = s;
    }
    out.length = static_cast<std::uint8_t> (len);

    if (spareOut != nullptr)
    {
        spareOut->clear();
        if (k < v.pitches.size())
            spareOut->assign (v.pitches.begin() + static_cast<std::ptrdiff_t> (k), v.pitches.end());
    }
}

int pitchEntryIndexForStep (const Pattern& p, int step) noexcept
{
    const int len = lengthOf (p);
    if (step < 0 || step >= len || p.steps[static_cast<std::size_t> (step)].state != StepState::Note) return -1;
    int k = 0;
    for (int i = 0; i < step; ++i)
        if (p.steps[static_cast<std::size_t> (i)].state == StepState::Note) ++k;
    return k;
}

void authenticSetState (Pattern& p, std::vector<PitchEntry>& spare, int step, StepState state)
{
    if (step < 0 || step >= kMaxSteps) return;
    PitchTimeView v = toPitchTime (p, spare);
    // The time slot already holds the pitch it plays as its shadow, so a Note that becomes a
    // Rest or Tie keeps it for the grid view; the pitch list itself is never edited here.
    v.time[static_cast<std::size_t> (step)].state = state;
    fromPitchTime (v, p, &spare);
    keepEdits (p);
}

bool authenticWritePitch (Pattern& p, std::vector<PitchEntry>& spare, int index, int note, int octave)
{
    PitchTimeView v = toPitchTime (p, spare);
    if (index < 0 || index > static_cast<int> (v.pitches.size())) return false;
    const PitchEntry e { static_cast<std::uint8_t> (clampInt (note, 0, 11)),
                         static_cast<std::int8_t> (clampInt (octave, kMinOctave, kMaxOctave)), false, false, false };
    if (index == static_cast<int> (v.pitches.size())) v.pitches.push_back (e);
    else v.pitches[static_cast<std::size_t> (index)] = e;
    fromPitchTime (v, p, &spare);
    keepEdits (p);
    return true;
}

bool authenticSetFlags (Pattern& p, std::vector<PitchEntry>& spare, int index, bool accent, bool slide, bool vibrato)
{
    PitchTimeView v = toPitchTime (p, spare);
    if (index < 0 || index >= static_cast<int> (v.pitches.size())) return false;
    PitchEntry& e = v.pitches[static_cast<std::size_t> (index)];
    e.accent = accent;
    e.slide = slide;
    e.vibrato = vibrato;
    fromPitchTime (v, p, &spare);
    keepEdits (p);
    return true;
}

void authenticSetLength (Pattern& p, std::vector<PitchEntry>& spare, int length)
{
    PitchTimeView v = toPitchTime (p, spare);
    Pattern tmp = p;
    tmp.steps = v.time;
    const int oldLen = lengthOf (tmp);
    const int newLen = clampInt (length, 1, kMaxSteps);
    if (newLen > oldLen) resetTiming (tmp, oldLen, newLen - oldLen);
    setLength (tmp, newLen);
    v.time = tmp.steps;
    v.length = tmp.length;
    fromPitchTime (v, p, &spare);
    keepEdits (p);
}
}
