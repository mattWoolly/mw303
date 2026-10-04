// NOTICE
// The gate weights per position in the bar (0.6, 0.1, 0.3, 0.5, ...) are taken from an
// open-source browser acid generator, Copyright 2021 David Whiting (github.com/vitling),
// licensed under Creative Commons Attribution 4.0 (https://creativecommons.org/licenses/by/4.0/).
// We use them as relative weights inside a different, thresholded algorithm.
// The anti-clustering factors for consecutive accents (7/16) and slides (10/18) follow an
// MIT-licensed modular-synth firmware applet by Logarhythm. No code is copied from either.
#include "acid/seq/Generator.h"
#include "SeqInternal.h"
#include <algorithm>
#include <array>

namespace acid::seq
{
using detail::clampInt;

namespace
{
// ---- Streams ---------------------------------------------------------------------------

enum Stream { kStreamGate, kStreamAccent, kStreamSlide, kStreamVibrato, kStreamRepeat, kStreamPitch, kStreamOctave, kNumStreams };

constexpr std::uint64_t kStreamSalt[kNumStreams] {
    0x47415445A3B1C2D5ull,   // gate
    0x41434345D4E5F607ull,   // accent
    0x534C4944192A3B4Cull,   // slide
    0x56494252F1E2D3C4ull,   // vibrato
    0x52455045B5A69788ull,   // repeat
    0x50495443C7D8E9FAull,   // pitch
    0x4F435441E1D2C3B4ull    // octave
};

// ---- Weights ---------------------------------------------------------------------------

// Gate: per position in a 16-step bar, tenths (see NOTICE). Sum 58.
constexpr int kGateWeight16[16] { 6, 1, 3, 5, 6, 1, 5, 1, 6, 5, 3, 1, 6, 1, 3, 5 };
// Gate, triplet divisions: three steps per beat, 12-step bars (beat, weak, pushing). Sum 48.
constexpr int kGateWeight12[12] { 6, 2, 4, 6, 2, 4, 6, 2, 4, 6, 2, 4 };
constexpr int kGateScale = 2;            // overall gate eagerness: r = 2 * w / mean (w)

/** The bar the metric weights and gate guards work in. */
struct Meter
{
    const int* gateWeight;   // per position in the bar
    int bar;                 // steps per bar
    int weightSum;           // sum of gateWeight over one bar
    int beat;                // steps per beat
};

Meter meterFor (const Pattern& p) noexcept
{
    if (p.divMod == DivMod::Triplet) return Meter { kGateWeight12, 12, 48, 3 };
    return Meter { kGateWeight16, 16, 58, 4 };
}

// Accent: off-beat bias. On the beat 4, last sub-step before the next beat's off-eighth 6,
// others 5 (mean 5 for both four and three steps per beat).
constexpr int accentWeight (int i, int beat) noexcept
{
    const int pos = i % beat;
    return pos == 0 ? 4 : pos == 2 ? 6 : 5;
}
constexpr int kAccentWeightMean = 5;
constexpr int kAccentScale = 35;         // percent: r = 0.35 * w / mean
constexpr int kAccentClusterNum = 7;     // after a stronger accent candidate: * 7/16
constexpr int kAccentClusterDen = 16;

// Slide: leap bias. Octave or more 4, fifth or more 2, otherwise 1 (reference 2).
constexpr int slideWeight (int leap) noexcept { return leap >= 12 ? 4 : leap >= 7 ? 2 : 1; }
constexpr int kSlideWeightRef = 2;
constexpr int kSlideScale = 30;          // percent
constexpr int kSlideClusterNum = 10;     // after a stronger slide candidate: * 10/18
constexpr int kSlideClusterDen = 18;

constexpr int kVibratoScale = 30;        // percent, flat

// Gate guards.
constexpr int kMinNotesPerBar = 2;
constexpr int kMaxRestRun = 4;
constexpr int kRestRunGuardFrom = 30;    // amountNote at which the rest-run guard starts

constexpr std::uint8_t kNoMag = 255;
constexpr int kNever = 101;

/** Smallest amount A (1..100) with mag / 128 < p (A) for p (A) = A r / (100 + A (r - 1)),
    r = R / S; 101 when it never turns on. Exact integer solution of
    A (128 R - m (R - S)) > 100 m S. */
int solveTurnOn (int mag, long long R, long long S) noexcept
{
    const long long m = clampInt (mag, 0, 127);
    if (R <= 0) return kNever;
    const long long d = R * (128 - m) + m * S;
    const long long a = (100 * m * S) / d + 1;
    return clampInt (a, 1, kNever);
}

/** The lane rule applyDensities uses: on when the amount reaches the stored value. */
bool laneOn (int amount, std::uint8_t stored) noexcept
{
    return amount > 0 && amount >= stored;
}

bool hasMemory (const Step& s) noexcept
{
    return s.magGate != kNoMag || s.magAccent != kNoMag || s.magSlide != kNoMag || s.magVibrato != kNoMag;
}

// ---- Gate guards (evaluated by generate) -----------------------------------------------

struct GateCtx
{
    std::array<bool, kMaxSteps> free {};
    std::array<bool, kMaxSteps> fixedOn {};
    std::array<int, kMaxSteps> aStar {};
    std::array<int, kMaxSteps> mag {};
};

/** Guard ordering: the step a rising knob would reveal first comes first. */
bool before (const GateCtx& c, int a, int b) noexcept
{
    const auto ua = static_cast<std::size_t> (a);
    const auto ub = static_cast<std::size_t> (b);
    if (c.aStar[ua] != c.aStar[ub]) return c.aStar[ua] < c.aStar[ub];
    if (c.mag[ua] != c.mag[ub]) return c.mag[ua] < c.mag[ub];
    return a < b;
}

void applyGuards (const GateCtx& c, std::array<bool, kMaxSteps>& on, int amount, int bar) noexcept
{
    // Anchor the first step of every bar.
    for (int b = 0; b < kMaxSteps; b += bar)
        if (c.free[static_cast<std::size_t> (b)]) on[static_cast<std::size_t> (b)] = true;

    // At least kMinNotesPerBar sounding steps per bar.
    for (int b = 0; b < kMaxSteps; b += bar)
    {
        const int end = std::min (b + bar, kMaxSteps);
        int count = 0;
        for (int i = b; i < end; ++i)
            if (on[static_cast<std::size_t> (i)]) ++count;
        while (count < kMinNotesPerBar)
        {
            int best = -1;
            for (int i = b; i < end; ++i)
                if (c.free[static_cast<std::size_t> (i)] && ! on[static_cast<std::size_t> (i)] && (best < 0 || before (c, i, best)))
                    best = i;
            if (best < 0) break;
            on[static_cast<std::size_t> (best)] = true;
            ++count;
        }
    }

    // No run of more than kMaxRestRun rests (linear over the 64 steps, independent of length).
    if (amount < kRestRunGuardFrom) return;
    for (int guard = 0; guard < kMaxSteps; ++guard)
    {
        bool changed = false;
        int i = 0;
        while (i < kMaxSteps && ! changed)
        {
            if (on[static_cast<std::size_t> (i)]) { ++i; continue; }
            int j = i;
            while (j < kMaxSteps && ! on[static_cast<std::size_t> (j)]) ++j;
            if (j - i > kMaxRestRun)
            {
                int best = -1;
                for (int k = i; k < j; ++k)
                    if (c.free[static_cast<std::size_t> (k)] && (best < 0 || before (c, k, best))) best = k;
                if (best >= 0)
                {
                    on[static_cast<std::size_t> (best)] = true;
                    changed = true;
                }
            }
            i = j;
        }
        if (! changed) break;
    }
}

/** Gate turn-on amounts with the guards folded in. The sounding set at amount A is the union
    of the guarded natural sets at every amount 1..A, so it only grows with A, and a step's
    turn-on amount is the first A whose guarded set contains it. */
void bakeGate (Pattern& p, const std::array<std::uint8_t, kMaxSteps>& raw, const Meter& m) noexcept
{
    GateCtx c;
    bool anyFree = false;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto ui = static_cast<std::size_t> (i);
        const Step& s = p.steps[ui];
        c.free[ui] = (s.locks & kLaneGate) == 0;
        c.fixedOn[ui] = ! c.free[ui] && s.state != StepState::Rest;
        c.mag[ui] = raw[ui];
        const long long w = m.gateWeight[i % m.bar];
        c.aStar[ui] = c.free[ui] ? solveTurnOn (raw[ui], kGateScale * m.bar * w, m.weightSum) : kNever;
        anyFree = anyFree || c.free[ui];
    }
    if (! anyFree) return;

    std::array<int, kMaxSteps> turnOn {};
    turnOn.fill (kNever);
    for (int a = 1; a <= 100; ++a)
    {
        std::array<bool, kMaxSteps> on {};
        bool all = true;
        for (std::size_t i = 0; i < kMaxSteps; ++i)
            on[i] = c.fixedOn[i] || (c.free[i] && c.aStar[i] <= a);
        applyGuards (c, on, a, m.bar);
        for (std::size_t i = 0; i < kMaxSteps; ++i)
        {
            if (c.free[i] && on[i] && turnOn[i] > a) turnOn[i] = a;
            all = all && (! c.free[i] || turnOn[i] <= a);
        }
        if (all) break;
    }
    for (std::size_t i = 0; i < kMaxSteps; ++i)
        if (c.free[i]) p.steps[i].magGate = static_cast<std::uint8_t> (turnOn[i]);
}
}

// ---- Shared with the grid edits --------------------------------------------------------

namespace detail
{
int slideTarget (const Pattern& p, int step, int length) noexcept
{
    const int len = clampInt (length, 1, kMaxSteps);
    auto isTie = [&p] (int j) { return p.steps[static_cast<std::size_t> (j)].state == StepState::Tie; };
    if (step < 0 || step >= kMaxSteps) return -1;
    if (step < len)
    {
        for (int k = 1; k < len; ++k)
        {
            const int j = (step + k) % len;
            if (! isTie (j)) return j;
        }
        return step;   // every other step is a Tie: the loop comes back to this one
    }
    for (int j = step + 1; j < kMaxSteps; ++j)
        if (! isTie (j)) return j;
    return -1;
}

bool slideTargetIsNote (const Pattern& p, int step, int length) noexcept
{
    const int t = slideTarget (p, step, length);
    return t >= 0 && p.steps[static_cast<std::size_t> (t)].state == StepState::Note;
}

void rederiveSlide (Pattern& p, int step) noexcept
{
    if (step < 0 || step >= kMaxSteps || ! p.gen.useSlide) return;
    Step& s = p.steps[static_cast<std::size_t> (step)];
    if (s.magSlide == kNoMag || (s.locks & kLaneSlide) != 0) return;
    const int amount = clampInt (p.gen.amountSlide, 0, 100);
    s.slide = s.state == StepState::Note && laneOn (amount, s.magSlide) && slideTargetIsNote (p, step, p.length);
}
}

// ---- Public ----------------------------------------------------------------------------

int laneTurnOnAmount (const Step& s, LaneBit lane) noexcept
{
    std::uint8_t v = kNoMag;
    switch (lane)
    {
        case kLaneGate: v = s.magGate; break;
        case kLaneAccent: v = s.magAccent; break;
        case kLaneSlide: v = s.magSlide; break;
        case kLaneVibrato: v = s.magVibrato; break;
        case kLanePitch:
        case kLaneOctave: break;
    }
    return v == kNoMag ? -1 : v;
}

void generate (Pattern& p, std::uint64_t seed, const GeneratorOptions& opt) noexcept
{
    GenParams& g = p.gen;
    const std::uint32_t seed32 = static_cast<std::uint32_t> (seed);
    g.seed = seed32;

    Rng rng[kNumStreams] {
        detail::laneRng (seed32, kStreamSalt[0]), detail::laneRng (seed32, kStreamSalt[1]),
        detail::laneRng (seed32, kStreamSalt[2]), detail::laneRng (seed32, kStreamSalt[3]),
        detail::laneRng (seed32, kStreamSalt[4]), detail::laneRng (seed32, kStreamSalt[5]),
        detail::laneRng (seed32, kStreamSalt[6])
    };

    // Pitch weights by interval above the root.
    const int root = clampInt (g.root, 0, 11);
    const int mask = g.noteMask & 0x0FFF;
    int pitchW[12] {};
    for (int k = 0; k < 12; ++k)
        if (((mask >> k) & 1) != 0)
            pitchW[k] = std::max (0, k == 0 ? opt.rootWeight : k == 7 ? opt.fifthWeight : opt.otherWeight);
    {
        int total = 0;
        for (int w : pitchW) total += w;
        if (total == 0)
        {
            for (int& w : pitchW) w = 0;
            pitchW[0] = 1;
        }
    }

    // Octave weights by distance from the lower median of the allowed octaves.
    constexpr int kNumOct = kMaxOctave - kMinOctave + 1;
    int allowed[kNumOct] {};
    int numAllowed = 0;
    for (int o = 0; o < kNumOct; ++o)
        if (((g.octaveMask >> o) & 1) != 0) allowed[numAllowed++] = o;
    int octW[kNumOct] {};
    if (numAllowed == 0)
        octW[0 - kMinOctave] = 1;
    else
    {
        const int home = allowed[(numAllowed - 1) / 2];
        for (int k = 0; k < numAllowed; ++k)
        {
            const int o = allowed[k];
            const int d = o > home ? o - home : home - o;
            octW[o] = std::max (0, d == 0 ? opt.octaveHomeWeight : d == 1 ? opt.octaveNearWeight : opt.octaveFarWeight);
        }
        int total = 0;
        for (int w : octW) total += w;
        if (total == 0) octW[home] = 1;
    }

    const int repeatPct = clampInt (opt.repeatPercent, 0, 100);

    // Pass 1: every draw for every step, used or not, so nothing shifts between lanes.
    std::array<std::uint8_t, kMaxSteps> rawGate {}, rawAcc {}, rawSlide {}, rawVib {};
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto ui = static_cast<std::size_t> (i);
        Step& s = p.steps[ui];
        rawGate[ui] = detail::draw7 (rng[kStreamGate]);
        rawAcc[ui] = detail::draw7 (rng[kStreamAccent]);
        rawSlide[ui] = detail::draw7 (rng[kStreamSlide]);
        rawVib[ui] = detail::draw7 (rng[kStreamVibrato]);
        const bool repeat = detail::drawPercent (rng[kStreamRepeat], repeatPct);
        const int interval = detail::pickWeighted (rng[kStreamPitch], pitchW);
        const int octIdx = detail::pickWeighted (rng[kStreamOctave], octW);

        if ((s.locks & kLanePitch) == 0)
        {
            if (repeat && i > 0)
            {
                const Step& prev = p.steps[ui - 1];
                s.candNote = (prev.locks & kLanePitch) != 0 ? prev.note : prev.candNote;
            }
            else
                s.candNote = static_cast<std::uint8_t> ((root + interval) % 12);
        }
        if ((s.locks & kLaneOctave) == 0) s.candOctave = static_cast<std::int8_t> (octIdx + kMinOctave);
    }

    // Pass 2: fold every position-dependent term into the stored turn-on amounts.
    const Meter m = meterFor (p);
    const int len = clampInt (p.length, 1, kMaxSteps);
    auto pitchAt = [&p] (int i) {
        const Step& s = p.steps[static_cast<std::size_t> (i)];
        const int note = (s.locks & kLanePitch) != 0 ? s.note : s.candNote;
        const int oct = (s.locks & kLaneOctave) != 0 ? s.octave : s.candOctave;
        return 12 * clampInt (oct, kMinOctave, kMaxOctave) + clampInt (note, 0, 11);
    };

    bakeGate (p, rawGate, m);

    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto ui = static_cast<std::size_t> (i);
        Step& s = p.steps[ui];

        if (g.useAccent && (s.locks & kLaneAccent) == 0)
        {
            const bool prevStronger = i > 0 && rawAcc[ui - 1] < rawAcc[ui];
            const long long cl = prevStronger ? kAccentClusterNum : kAccentClusterDen;
            const long long R = static_cast<long long> (kAccentScale) * accentWeight (i, m.beat) * cl;
            const long long S = 100LL * kAccentWeightMean * kAccentClusterDen;
            s.magAccent = static_cast<std::uint8_t> (solveTurnOn (rawAcc[ui], R, S));
        }

        if (g.useSlide && (s.locks & kLaneSlide) == 0)
        {
            // The leap to the step that follows on the grid (the loop wrap for the last step).
            const int next = i < len ? (i + 1 < len ? i + 1 : 0) : (i + 1) % kMaxSteps;
            const int a = pitchAt (i), b = pitchAt (next);
            const int leap = a > b ? a - b : b - a;
            const bool prevStronger = i > 0 && rawSlide[ui - 1] < rawSlide[ui];
            const long long cl = prevStronger ? kSlideClusterNum : kSlideClusterDen;
            const long long R = static_cast<long long> (kSlideScale) * slideWeight (leap) * cl;
            const long long S = 100LL * kSlideWeightRef * kSlideClusterDen;
            s.magSlide = static_cast<std::uint8_t> (solveTurnOn (rawSlide[ui], R, S));
        }

        if (g.useVibrato && (s.locks & kLaneVibrato) == 0)
            s.magVibrato = static_cast<std::uint8_t> (solveTurnOn (rawVib[ui], kVibratoScale, 100));
    }
}

void generate (Pattern& p, const GeneratorOptions& opt) noexcept
{
    generate (p, p.gen.seed, opt);
}

void applyDensities (Pattern& p) noexcept
{
    const GenParams& g = p.gen;

    // Pitch: every generated step holds its candidate, sounding or not.
    for (Step& s : p.steps)
    {
        if (! hasMemory (s)) continue;
        if ((s.locks & kLanePitch) == 0) s.note = static_cast<std::uint8_t> (clampInt (s.candNote, 0, 11));
        if ((s.locks & kLaneOctave) == 0) s.octave = static_cast<std::int8_t> (clampInt (s.candOctave, kMinOctave, kMaxOctave));
    }

    // Gate.
    const int amountNote = clampInt (g.amountNote, 0, 100);
    for (Step& s : p.steps)
        if (s.magGate != kNoMag && (s.locks & kLaneGate) == 0)
            s.state = laneOn (amountNote, s.magGate) ? StepState::Note : StepState::Rest;

    // Accent and vibrato: the step's own state and memory only.
    const int amountAccent = clampInt (g.amountAccent, 0, 100);
    const int amountVibrato = clampInt (g.amountVibrato, 0, 100);
    for (Step& s : p.steps)
    {
        const bool note = s.state == StepState::Note;
        if (g.useAccent && s.magAccent != kNoMag && (s.locks & kLaneAccent) == 0)
            s.accent = note && laneOn (amountAccent, s.magAccent);
        if (g.useVibrato && s.magVibrato != kNoMag && (s.locks & kLaneVibrato) == 0)
            s.vibrato = note && laneOn (amountVibrato, s.magVibrato);
    }

    // Slide: from a Note into the next step that is not a Tie, which must also be a Note.
    for (int i = 0; i < kMaxSteps; ++i)
        detail::rederiveSlide (p, i);
}

void regenerate (Pattern& p, std::uint64_t seed, const GeneratorOptions& opt) noexcept
{
    generate (p, seed, opt);
    applyDensities (p);
}

void flatten (Pattern& p) noexcept
{
    for (Step& s : p.steps)
    {
        s.magGate = kNoMag;
        s.magAccent = kNoMag;
        s.magSlide = kNoMag;
        s.magVibrato = kNoMag;
    }
}

void keepEdits (Pattern& p) noexcept
{
    // Gate and pitch first: the gate is per step, so once every disagreeing gate is released
    // the derived states equal the real ones, and the flags can be compared against them.
    Pattern d = p;
    applyDensities (d);
    for (std::size_t i = 0; i < kMaxSteps; ++i)
    {
        Step& s = p.steps[i];
        const Step& t = d.steps[i];
        if (s.state != t.state) s.magGate = kNoMag;
        if (s.note != t.note) s.candNote = s.note;
        if (s.octave != t.octave) s.candOctave = s.octave;
    }

    d = p;
    applyDensities (d);
    for (std::size_t i = 0; i < kMaxSteps; ++i)
    {
        Step& s = p.steps[i];
        const Step& t = d.steps[i];
        if (s.accent != t.accent) s.magAccent = kNoMag;
        if (s.slide != t.slide) s.magSlide = kNoMag;
        if (s.vibrato != t.vibrato) s.magVibrato = kNoMag;
    }
}
}
