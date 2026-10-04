#pragma once
// Shared helpers for the acid_seq tests: seeded random patterns and an "audible view" that
// reduces a pattern to what a listener hears, for comparing lossy round trips.
#include "acid/common/Rng.h"
#include "acid/seq/Pattern.h"
#include "acid/seq/PatternOps.h"
#include <cstdint>
#include <string>

namespace acid::seq::test
{
/** Every field random and in range (steps beyond length too). */
inline Pattern randomPattern (std::uint64_t seed)
{
    Rng r (seed * 0x9E3779B97F4A7C15ull + 12345);
    Pattern p;
    // A name of valid UTF-8 (the model never stores anything else, see sanitizePattern) with
    // characters of every encoded length, cut at a character boundary. One draw per byte of
    // budget, so the rest of the pattern is drawn as it always was.
    const int nameLen = r.nextInt (kPatternNameLength);
    std::string name;
    for (int i = 0; i < nameLen; ++i)
    {
        const int v = 1 + r.nextInt (255);
        char32_t cp = static_cast<char32_t> (v);
        if (v >= 240) cp = 0x10000 + static_cast<char32_t> (v - 240) * 0x10000;     // 4 bytes
        else if (v >= 192) cp = 0x800 + static_cast<char32_t> (v - 192) * 0x400;    // 3 bytes, below the surrogates
        else if (v >= 128) cp = 0x80 + static_cast<char32_t> (v - 128) * 0x1E;      // 2 bytes
        std::string ch;
        if (cp < 0x80) ch += static_cast<char> (cp);
        else if (cp < 0x800) { ch += static_cast<char> (0xC0 | (cp >> 6)); ch += static_cast<char> (0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { ch += static_cast<char> (0xE0 | (cp >> 12)); ch += static_cast<char> (0x80 | ((cp >> 6) & 0x3F)); ch += static_cast<char> (0x80 | (cp & 0x3F)); }
        else { ch += static_cast<char> (0xF0 | (cp >> 18)); ch += static_cast<char> (0x80 | ((cp >> 12) & 0x3F)); ch += static_cast<char> (0x80 | ((cp >> 6) & 0x3F)); ch += static_cast<char> (0x80 | (cp & 0x3F)); }
        if (name.size() + ch.size() <= static_cast<std::size_t> (nameLen)) name += ch;
    }
    for (std::size_t i = 0; i < name.size(); ++i)
        p.name[i] = name[i];
    p.length = static_cast<std::uint8_t> (1 + r.nextInt (kMaxSteps));
    p.div = static_cast<StepDiv> (r.nextInt (7));
    p.divMod = static_cast<DivMod> (r.nextInt (3));
    p.shuffle = static_cast<std::uint8_t> (r.nextInt (101));
    p.gateLength = static_cast<std::uint8_t> (r.nextInt (101));
    p.direction = static_cast<Direction> (r.nextInt (4));
    p.transpose = static_cast<std::int8_t> (r.nextInt (97) - 48);
    for (Step& s : p.steps)
    {
        s.state = static_cast<StepState> (r.nextInt (3));
        s.note = static_cast<std::uint8_t> (r.nextInt (12));
        s.octave = static_cast<std::int8_t> (kMinOctave + r.nextInt (kMaxOctave - kMinOctave + 1));
        s.arpIndex = static_cast<std::uint8_t> (1 + r.nextInt (kMaxArpIndex));
        s.accent = r.chance (0.5);
        s.slide = r.chance (0.5);
        s.vibrato = r.chance (0.5);
        s.probability = static_cast<std::uint8_t> (r.nextInt (101));
        s.ratchet = static_cast<std::uint8_t> (1 + r.nextInt (4));
        s.gate = static_cast<std::uint8_t> (r.nextInt (128));
        s.locks = static_cast<std::uint8_t> (r.nextInt (64));
        auto mag = [&r] { return r.chance (0.2) ? std::uint8_t (255) : static_cast<std::uint8_t> (r.nextInt (128)); };
        s.magGate = mag();
        s.magAccent = mag();
        s.magSlide = mag();
        s.magVibrato = mag();
        s.candNote = static_cast<std::uint8_t> (r.nextInt (12));
        s.candOctave = static_cast<std::int8_t> (kMinOctave + r.nextInt (kMaxOctave - kMinOctave + 1));
    }
    GenParams& g = p.gen;
    g.seed = static_cast<std::uint32_t> (r.nextU64());
    g.noteMask = static_cast<std::uint16_t> (r.nextInt (0x1000));
    g.root = static_cast<std::uint8_t> (r.nextInt (12));
    g.octaveMask = static_cast<std::uint8_t> (r.nextInt (32));
    g.useSlide = r.chance (0.5);
    g.useAccent = r.chance (0.5);
    g.useVibrato = r.chance (0.5);
    g.amountNote = static_cast<std::uint8_t> (r.nextInt (101));
    g.amountSlide = static_cast<std::uint8_t> (r.nextInt (101));
    g.amountAccent = static_cast<std::uint8_t> (r.nextInt (101));
    g.amountVibrato = static_cast<std::uint8_t> (r.nextInt (101));
    return p;
}

/** A plain musical pattern for MIDI round trips: notes, ties, rests, flags, no extensions. */
inline Pattern randomPlayablePattern (std::uint64_t seed, int length)
{
    Rng r (seed * 0xD1B54A32D192ED03ull + 99);
    Pattern p;
    p.length = static_cast<std::uint8_t> (length);
    for (int i = 0; i < length; ++i)
    {
        Step& s = p.steps[static_cast<std::size_t> (i)];
        const double u = r.nextDouble();
        s.state = u < 0.6 ? StepState::Note : u < 0.8 ? StepState::Tie : StepState::Rest;
        s.note = static_cast<std::uint8_t> (r.nextInt (12));
        s.octave = static_cast<std::int8_t> (r.nextInt (3) - 1);
        s.accent = r.chance (0.3);
        s.slide = r.chance (0.25);
        s.vibrato = r.chance (0.1);
    }
    return p;
}

/** What a listener hears, as text: per step N<midi><flags>, T or R, with flags reduced to the
    ones that matter (accent and vibrato on Notes; slide only where the gate is actually held
    into a following Note under the Release policy; ties after silence become rests; a slide
    into the same pitch with the same accent is written as a tie, which is how it sounds). */
inline std::string audibleView (const Pattern& p, bool withVibrato = true)
{
    const int len = p.length;
    auto at = [&p] (int i) -> const Step& { return p.steps[static_cast<std::size_t> (i)]; };

    // Effective slide per Note step and whether it merges into its target.
    bool slide[kMaxSteps] {};
    bool mergesNext[kMaxSteps] {};
    for (int i = 0; i < len; ++i)
    {
        const Step& s = at (i);
        if (s.state != StepState::Note || ! s.slide) continue;
        int j = i + 1;
        while (j < len && at (j).state == StepState::Tie) ++j;
        if (j < len)
        {
            slide[i] = at (j).state == StepState::Note;
            const Step& n = at (j);
            if (slide[i] && n.note == s.note && n.octave == s.octave && n.accent == s.accent)
            {
                slide[i] = false;
                mergesNext[i] = true;
            }
        }
        else
            slide[i] = at (0).state == StepState::Note;
    }

    // Tokens per step; a slide at the end of a merged chain belongs to the chain's head note.
    std::string tok[kMaxSteps];
    bool sounding = false;
    bool merging = false;
    int head = -1;
    for (int i = 0; i < len; ++i)
    {
        const Step& s = at (i);
        if (s.state == StepState::Note && merging)
        {
            tok[i] = "T";
            merging = mergesNext[i];
            if (slide[i]) tok[head] += "s";
        }
        else if (s.state == StepState::Note)
        {
            tok[i] = "N" + std::to_string (stepMidiNote (s) + p.transpose);
            if (s.accent) tok[i] += "a";
            if (withVibrato && s.vibrato) tok[i] += "v";
            if (slide[i]) tok[i] += "s";
            sounding = true;
            merging = mergesNext[i];
            head = i;
        }
        else if (s.state == StepState::Tie && sounding)
            tok[i] = "T";
        else
        {
            tok[i] = "R";
            sounding = false;
            merging = false;
        }
    }
    std::string out = "len=" + std::to_string (len) + " ";
    for (int i = 0; i < len; ++i) out += tok[i] + " ";
    return out;
}
}
