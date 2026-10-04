#include "acid/seq/PatternCodec.h"
#include "acid/common/Utf8.h"
#include "acid/seq/PatternOps.h"
#include "SeqInternal.h"
#include <array>
#include <cstdint>
#include <vector>

namespace acid::seq
{
using detail::clampInt;

namespace
{
constexpr std::string_view kHeader = "acidpat";
constexpr std::size_t kMaxInput = 64 * 1024;
constexpr std::size_t kMaxSpares = 256;

constexpr const char* kNoteNames[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr const char* kDivNames[7] { "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
constexpr const char* kModNames[3] { "full", "dot", "tri" };
constexpr const char* kDirNames[4] { "fwd", "rev", "pingpong", "random" };

// ---- Small parsing helpers -------------------------------------------------------------

std::string_view trim (std::string_view s) noexcept
{
    while (! s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix (1);
    while (! s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix (1);
    return s;
}

std::vector<std::string_view> split (std::string_view s, char sep)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == sep)
        {
            out.push_back (s.substr (start, i - start));
            start = i + 1;
        }
    return out;
}

std::vector<std::string_view> splitSpaces (std::string_view s)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        const std::size_t start = i;
        while (i < s.size() && ! (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        if (i > start) out.push_back (s.substr (start, i - start));
    }
    return out;
}

/** Signed decimal; saturates instead of overflowing. False on anything but [+-]digits. */
bool parseInt (std::string_view s, long long& out) noexcept
{
    if (s.empty()) return false;
    bool neg = false;
    std::size_t i = 0;
    if (s[0] == '-' || s[0] == '+')
    {
        neg = s[0] == '-';
        i = 1;
    }
    if (i >= s.size()) return false;
    long long v = 0;
    for (; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        if (v < 1000000000000000LL) v = v * 10 + (c - '0');
    }
    out = neg ? -v : v;
    return true;
}

int hexDigit (char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseHex (std::string_view s, long long& out) noexcept
{
    if (s.empty()) return false;
    long long v = 0;
    for (char c : s)
    {
        const int d = hexDigit (c);
        if (d < 0) return false;
        if (v < 1000000000000000LL) v = v * 16 + d;
    }
    out = v;
    return true;
}

int parseNoteName (std::string_view s) noexcept
{
    for (int i = 0; i < 12; ++i)
        if (s == kNoteNames[i]) return i;
    return -1;
}

template <std::size_t N>
int parseName (std::string_view s, const char* const (&names)[N]) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        if (s == names[i]) return static_cast<int> (i);
    return -1;
}

void appendHex (std::string& out, unsigned v, int digits)
{
    constexpr const char* kHex = "0123456789abcdef";
    for (int d = digits - 1; d >= 0; --d)
        out.push_back (kHex[(v >> (4 * d)) & 0xF]);
}

bool isPlainNameChar (unsigned char c) noexcept
{
    return c >= 0x20 && c < 0x7F && c != '%' && c != '|' && c != '=';
}

int nameLength (const Pattern& p) noexcept
{
    int n = 0;
    while (n < kPatternNameLength - 1 && p.name[n] != '\0') ++n;
    return n;
}

// ---- Step encoding ---------------------------------------------------------------------

std::string flagString (const Step& s)
{
    std::string f;
    if (s.accent) f.push_back ('A');
    if (s.slide) f.push_back ('S');
    if (s.vibrato) f.push_back ('V');
    return f.empty() ? std::string ("-") : f;
}

std::string magString (std::uint8_t m) { return m == 255 ? std::string ("-") : std::to_string (m); }

char stateChar (StepState s) noexcept
{
    switch (s)
    {
        case StepState::Note: return 'N';
        case StepState::Tie: return 'T';
        case StepState::Rest: break;
    }
    return 'R';
}

std::string encodeStep (const Step& s)
{
    std::array<std::string, 15> f {
        std::string (1, stateChar (s.state)),
        kNoteNames[s.note % 12],
        std::to_string (static_cast<int> (s.octave)),
        std::to_string (static_cast<int> (s.arpIndex)),
        flagString (s),
        std::to_string (static_cast<int> (s.probability)),
        std::to_string (static_cast<int> (s.ratchet)),
        std::to_string (static_cast<int> (s.gate)),
        std::to_string (static_cast<int> (s.locks)),
        magString (s.magGate),
        magString (s.magAccent),
        magString (s.magSlide),
        magString (s.magVibrato),
        kNoteNames[s.candNote % 12],
        std::to_string (static_cast<int> (s.candOctave))
    };
    const std::array<std::string, 15> def { "", "", "", "1", "-", "100", "1", "0", "0", "-", "-", "-", "-", "C", "0" };

    std::size_t n = f.size();
    while (n > 3 && f[n - 1] == def[n - 1]) --n;

    std::string out;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (i > 0) out.push_back (':');
        out += f[i];
    }
    return out;
}

// ---- Decoding --------------------------------------------------------------------------

struct Decoder
{
    DecodeResult& res;
    std::vector<PitchEntry>* spares = nullptr;   // where "spares=" goes; nullptr = ignore the key

    bool fail (std::string msg)
    {
        res.ok = false;
        res.error = std::move (msg);
        return false;
    }

    template <typename T>
    bool intField (std::string_view key, std::string_view v, T& dst, long long lo, long long hi)
    {
        long long x = 0;
        if (! parseInt (v, x)) return fail ("bad number for '" + std::string (key) + "': '" + std::string (v.substr (0, 32)) + "'");
        if (x < lo) { x = lo; ++res.adjusted; }
        if (x > hi) { x = hi; ++res.adjusted; }
        dst = static_cast<T> (x);
        return true;
    }

    bool boolField (std::string_view key, std::string_view v, bool& dst)
    {
        int x = 0;
        if (! intField (key, v, x, 0, 1)) return false;
        dst = x != 0;
        return true;
    }

    template <typename E, std::size_t N>
    void enumField (std::string_view v, E& dst, const char* const (&names)[N], E fallback)
    {
        const int i = parseName (v, names);
        if (i < 0) { dst = fallback; ++res.adjusted; }
        else dst = static_cast<E> (i);
    }

    bool hexField (std::string_view key, std::string_view v, long long& dst, long long mask)
    {
        if (! parseHex (v, dst)) return fail ("bad hex for '" + std::string (key) + "'");
        if ((dst & ~mask) != 0) { dst &= mask; ++res.adjusted; }
        return true;
    }

    bool name (std::string_view v, Pattern& p)
    {
        std::string bytes;
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            char c = v[i];
            if (c == '%')
            {
                if (i + 2 >= v.size()) return fail ("truncated escape in name");
                const int hi = hexDigit (v[i + 1]);
                const int lo = hexDigit (v[i + 2]);
                if (hi < 0 || lo < 0) return fail ("bad escape in name");
                c = static_cast<char> ((hi << 4) | lo);
                i += 2;
            }
            if (bytes.size() < 4 * static_cast<std::size_t> (kPatternNameLength)) bytes += c;
        }
        // An escape can carry any byte: the name is repaired to valid UTF-8 and cut at a
        // character boundary (both count as an adjustment).
        if (utf8::copyName (p.name, kPatternNameLength, bytes)) ++res.adjusted;
        return true;
    }

    bool note (std::string_view key, std::string_view v, std::uint8_t& dst)
    {
        const int n = parseNoteName (v);
        if (n < 0) return fail ("bad note name for '" + std::string (key) + "': '" + std::string (v.substr (0, 8)) + "'");
        dst = static_cast<std::uint8_t> (n);
        return true;
    }

    bool mag (std::string_view v, std::uint8_t& dst)
    {
        if (v == "-") { dst = 255; return true; }
        return intField ("magnitude", v, dst, 0, 127);
    }

    bool step (std::string_view tok, Step& s)
    {
        const auto f = split (tok, ':');
        s = Step {};
        if (f.empty() || f[0].size() != 1) return fail ("bad step '" + std::string (tok.substr (0, 32)) + "'");
        switch (f[0][0])
        {
            case 'N': s.state = StepState::Note; break;
            case 'R': s.state = StepState::Rest; break;
            case 'T': s.state = StepState::Tie; break;
            default: return fail ("bad step state '" + std::string (f[0]) + "'");
        }
        const std::size_t n = f.size();
        if (n > 1 && ! note ("note", f[1], s.note)) return false;
        if (n > 2 && ! intField ("octave", f[2], s.octave, kMinOctave, kMaxOctave)) return false;
        if (n > 3 && ! intField ("arp", f[3], s.arpIndex, 1, kMaxArpIndex)) return false;
        if (n > 4 && f[4] != "-")
            for (char c : f[4])
            {
                if (c == 'A') s.accent = true;
                else if (c == 'S') s.slide = true;
                else if (c == 'V') s.vibrato = true;
                else ++res.adjusted;   // a flag from a newer version
            }
        if (n > 5 && ! intField ("probability", f[5], s.probability, 0, 100)) return false;
        if (n > 6 && ! intField ("ratchet", f[6], s.ratchet, 1, 4)) return false;
        if (n > 7 && ! intField ("gate", f[7], s.gate, 0, 127)) return false;
        if (n > 8)
        {
            long long locks = 0;
            if (! parseInt (f[8], locks)) return fail ("bad locks");
            if (locks < 0 || locks > 63) { locks = clampInt (locks, 0, 1 << 30) & 63; ++res.adjusted; }
            s.locks = static_cast<std::uint8_t> (locks);
        }
        if (n > 9 && ! mag (f[9], s.magGate)) return false;
        if (n > 10 && ! mag (f[10], s.magAccent)) return false;
        if (n > 11 && ! mag (f[11], s.magSlide)) return false;
        if (n > 12 && ! mag (f[12], s.magVibrato)) return false;
        if (n > 13 && ! note ("cand", f[13], s.candNote)) return false;
        if (n > 14 && ! intField ("candoct", f[14], s.candOctave, kMinOctave, kMaxOctave)) return false;
        return true;
    }

    bool field (std::string_view key, std::string_view v, Pattern& p)
    {
        GenParams& g = p.gen;
        if (key == "name") return name (v, p);
        if (key == "len") return intField (key, v, p.length, 1, kMaxSteps);
        if (key == "div") { enumField (v, p.div, kDivNames, Pattern {}.div); return true; }
        if (key == "mod") { enumField (v, p.divMod, kModNames, Pattern {}.divMod); return true; }
        if (key == "shuffle") return intField (key, v, p.shuffle, 0, 100);
        if (key == "gate") return intField (key, v, p.gateLength, 0, 100);
        if (key == "dir") { enumField (v, p.direction, kDirNames, Pattern {}.direction); return true; }
        if (key == "transpose") return intField (key, v, p.transpose, -48, 48);
        if (key == "seed") return intField (key, v, g.seed, 0, 0xFFFFFFFFLL);
        if (key == "notemask")
        {
            long long m = 0;
            if (! hexField (key, v, m, 0x0FFF)) return false;
            g.noteMask = static_cast<std::uint16_t> (m);
            return true;
        }
        if (key == "root") return intField (key, v, g.root, 0, 11);
        if (key == "octmask")
        {
            long long m = 0;
            if (! hexField (key, v, m, 0x1F)) return false;
            g.octaveMask = static_cast<std::uint8_t> (m);
            return true;
        }
        if (key == "useslide") return boolField (key, v, g.useSlide);
        if (key == "useaccent") return boolField (key, v, g.useAccent);
        if (key == "usevib") return boolField (key, v, g.useVibrato);
        if (key == "amtnote") return intField (key, v, g.amountNote, 0, 100);
        if (key == "amtslide") return intField (key, v, g.amountSlide, 0, 100);
        if (key == "amtaccent") return intField (key, v, g.amountAccent, 0, 100);
        if (key == "amtvib") return intField (key, v, g.amountVibrato, 0, 100);
        if (key == "spares")
        {
            if (spares == nullptr) return true;
            spares->clear();
            for (std::string_view tok : splitSpaces (v))
            {
                if (spares->size() >= kMaxSpares) { ++res.adjusted; break; }
                const auto f = split (tok, ':');
                PitchEntry e;
                if (! note ("spare", f[0], e.note)) return false;
                if (f.size() > 1 && ! intField ("spare octave", f[1], e.octave, kMinOctave, kMaxOctave)) return false;
                if (f.size() > 2 && f[2] != "-")
                    for (char c : f[2])
                    {
                        if (c == 'A') e.accent = true;
                        else if (c == 'S') e.slide = true;
                        else if (c == 'V') e.vibrato = true;
                        else ++res.adjusted;
                    }
                spares->push_back (e);
            }
            return true;
        }
        if (key == "steps")
        {
            const auto toks = splitSpaces (v);
            for (std::size_t i = 0; i < toks.size(); ++i)
            {
                if (i >= static_cast<std::size_t> (kMaxSteps)) { ++res.adjusted; break; }
                if (! step (toks[i], p.steps[i])) return false;
            }
            for (std::size_t i = toks.size(); i < static_cast<std::size_t> (kMaxSteps); ++i) p.steps[i] = Step {};
            return true;
        }
        return true;   // unknown key: a newer writer's field
    }
};
}

// ---- Public: lossless codec ------------------------------------------------------------

std::string encodePattern (const Pattern& p)
{
    std::string out;
    out.reserve (1024);
    out += kHeader;
    out += ' ';
    out += std::to_string (kPatternCodecVersion);

    out += "|name=";
    const int nlen = nameLength (p);
    for (int i = 0; i < nlen; ++i)
    {
        const auto c = static_cast<unsigned char> (p.name[i]);
        if (isPlainNameChar (c)) out.push_back (static_cast<char> (c));
        else
        {
            out.push_back ('%');
            appendHex (out, c, 2);
        }
    }

    const GenParams& g = p.gen;
    auto kv = [&out] (const char* key, const std::string& v) {
        out.push_back ('|');
        out += key;
        out.push_back ('=');
        out += v;
    };
    auto num = [] (long long v) { return std::to_string (v); };
    auto hex = [] (unsigned v, int digits) { std::string s; appendHex (s, v, digits); return s; };

    kv ("len", num (p.length));
    kv ("div", kDivNames[clampInt (p.div, 0, 6)]);
    kv ("mod", kModNames[clampInt (p.divMod, 0, 2)]);
    kv ("shuffle", num (p.shuffle));
    kv ("gate", num (p.gateLength));
    kv ("dir", kDirNames[clampInt (p.direction, 0, 3)]);
    kv ("transpose", num (p.transpose));
    kv ("seed", num (g.seed));
    kv ("notemask", hex (g.noteMask & 0x0FFFu, 3));
    kv ("root", num (g.root));
    kv ("octmask", hex (g.octaveMask & 0x1Fu, 2));
    kv ("useslide", num (g.useSlide ? 1 : 0));
    kv ("useaccent", num (g.useAccent ? 1 : 0));
    kv ("usevib", num (g.useVibrato ? 1 : 0));
    kv ("amtnote", num (g.amountNote));
    kv ("amtslide", num (g.amountSlide));
    kv ("amtaccent", num (g.amountAccent));
    kv ("amtvib", num (g.amountVibrato));

    int last = -1;
    for (int i = 0; i < kMaxSteps; ++i)
        if (! sameStep (p.steps[static_cast<std::size_t> (i)], Step {})) last = i;

    out += "|steps=";
    for (int i = 0; i <= last; ++i)
    {
        if (i > 0) out.push_back (' ');
        Step s = p.steps[static_cast<std::size_t> (i)];
        out += encodeStep (s);
    }
    return out;
}

bool looksLikeEncodedPattern (std::string_view text) noexcept
{
    text = trim (text);
    return text.size() > kHeader.size() && text.substr (0, kHeader.size()) == kHeader
        && (text[kHeader.size()] == ' ' || text[kHeader.size()] == '\t');
}

std::string encodePattern (const Pattern& p, std::span<const PitchEntry> spares)
{
    std::string out = encodePattern (p);
    if (spares.empty()) return out;
    out += "|spares=";
    for (std::size_t i = 0; i < spares.size(); ++i)
    {
        const PitchEntry& e = spares[i];
        if (i > 0) out.push_back (' ');
        out += kNoteNames[e.note % 12];
        out.push_back (':');
        out += std::to_string (static_cast<int> (clampInt (e.octave, kMinOctave, kMaxOctave)));
        if (e.accent || e.slide || e.vibrato)
        {
            out.push_back (':');
            if (e.accent) out.push_back ('A');
            if (e.slide) out.push_back ('S');
            if (e.vibrato) out.push_back ('V');
        }
    }
    return out;
}

DecodeResult decodePattern (std::string_view text, Pattern& out)
{
    return decodePattern (text, out, nullptr);
}

DecodeResult decodePattern (std::string_view text, Pattern& out, std::vector<PitchEntry>* sparesOut)
{
    DecodeResult res;
    std::vector<PitchEntry> spares;
    Decoder d { res, sparesOut != nullptr ? &spares : nullptr };

    if (text.size() > kMaxInput) { d.fail ("input too long"); return res; }
    text = trim (text);
    if (! looksLikeEncodedPattern (text)) { d.fail ("missing 'acidpat' header"); return res; }

    const auto fields = split (text, '|');
    const std::string_view head = trim (fields[0].substr (kHeader.size()));
    long long version = 0;
    if (! parseInt (head, version) || version < 1) { d.fail ("bad version"); return res; }
    res.version = static_cast<int> (clampInt (version, 1, 1 << 30));
    res.newerVersion = res.version > kPatternCodecVersion;

    Pattern p {};
    for (std::size_t i = 1; i < fields.size(); ++i)
    {
        const std::string_view f = fields[i];
        const std::size_t eq = f.find ('=');
        if (eq == std::string_view::npos) continue;   // tolerate junk segments from newer writers
        if (! d.field (trim (f.substr (0, eq)), f.substr (eq + 1), p)) return res;
    }

    res.adjusted += sanitizePattern (p);
    out = p;
    if (sparesOut != nullptr) *sparesOut = std::move (spares);
    res.ok = true;
    return res;
}

// ---- Public: compact step notation -----------------------------------------------------

namespace
{
std::string pitchToken (int noteOrIndex, int octave, bool arp)
{
    std::string t = arp ? std::to_string (noteOrIndex) : std::string (kNoteNames[noteOrIndex % 12]);
    for (int o = 0; o < octave; ++o) t.push_back ('+');
    for (int o = 0; o > octave; --o) t.push_back ('-');
    return t;
}

/** Parses NOTE (or an arp index) with octave marks. Returns false on syntax or range errors. */
bool parsePitch (std::string_view s, bool& isArp, int& noteOrIndex, int& octave, std::string& err)
{
    std::size_t i = 0;
    int base = 0;
    if (! s.empty() && s[0] >= 'A' && s[0] <= 'G')
    {
        constexpr int kPc[7] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        base = kPc[s[0] - 'A'];
        i = 1;
        if (i < s.size() && s[i] == '#') { ++base; ++i; }
        else if (i < s.size() && s[i] == 'b') { --base; ++i; }
        isArp = false;
    }
    else if (! s.empty() && s[0] >= '0' && s[0] <= '9')
    {
        int v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i < 3) v = v * 10 + (s[i++] - '0');
        if (v < 1 || v > kMaxArpIndex) { err = "arp index out of range"; return false; }
        base = v;
        isArp = true;
    }
    else
    {
        err = "bad note '" + std::string (s.substr (0, 8)) + "'";
        return false;
    }

    int oct = 0;
    for (; i < s.size(); ++i)
    {
        if (s[i] == '+') ++oct;
        else if (s[i] == '-') --oct;
        else { err = "bad octave mark in '" + std::string (s.substr (0, 8)) + "'"; return false; }
        if (oct > 8 || oct < -8) { err = "octave out of range"; return false; }
    }

    if (! isArp)
    {
        const int pitch = 12 * oct + base;   // base may be -1 (Cb) or 12 (B#)
        oct = (pitch >= 0 ? pitch / 12 : (pitch - 11) / 12);
        base = pitch - 12 * oct;
    }
    if (oct < kMinOctave || oct > kMaxOctave) { err = "octave out of range"; return false; }
    noteOrIndex = base;
    octave = oct;
    return true;
}

bool parseStepToken (std::string_view tok, Step& s, std::string& err)
{
    s = Step {};
    if (tok == "~") { s.state = StepState::Tie; return true; }
    if (tok[0] == '.')
    {
        s.state = StepState::Rest;
        if (tok.size() > 1)
        {
            bool arp = false;
            int n = 0, o = 0;
            if (! parsePitch (tok.substr (1), arp, n, o, err)) return false;
            if (arp) s.arpIndex = static_cast<std::uint8_t> (n);
            else s.note = static_cast<std::uint8_t> (n);
            s.octave = static_cast<std::int8_t> (o);
        }
        return true;
    }

    s.state = StepState::Note;
    const std::size_t colon = tok.find (':');
    bool arp = false;
    int n = 0, o = 0;
    if (! parsePitch (tok.substr (0, colon), arp, n, o, err)) return false;
    if (arp) s.arpIndex = static_cast<std::uint8_t> (n);
    else s.note = static_cast<std::uint8_t> (n);
    s.octave = static_cast<std::int8_t> (o);
    if (colon == std::string_view::npos) return true;

    const std::string_view flags = tok.substr (colon + 1);
    std::size_t i = 0;
    while (i < flags.size())
    {
        const char c = flags[i++];
        if (c == 'A') { s.accent = true; continue; }
        if (c == 'S') { s.slide = true; continue; }
        if (c == 'V') { s.vibrato = true; continue; }
        if (c != 'P' && c != 'R' && c != 'G') { err = std::string ("bad flag '") + c + "'"; return false; }
        const std::size_t start = i;
        while (i < flags.size() && flags[i] >= '0' && flags[i] <= '9') ++i;
        long long v = 0;
        if (! parseInt (flags.substr (start, i - start), v)) { err = std::string ("missing value after '") + c + "'"; return false; }
        if (c == 'P')
        {
            if (v > 100) { err = "probability out of range"; return false; }
            s.probability = static_cast<std::uint8_t> (v);
        }
        else if (c == 'R')
        {
            if (v < 1 || v > 4) { err = "ratchet out of range"; return false; }
            s.ratchet = static_cast<std::uint8_t> (v);
        }
        else
        {
            if (v > 127) { err = "gate out of range"; return false; }
            s.gate = static_cast<std::uint8_t> (v);
        }
    }
    return true;
}

/** Whitespace tokens, except that name="..." is one token (with \" and \\ escapes). */
bool tokenizeStepText (std::string_view s, std::vector<std::string>& out, std::string& err)
{
    std::size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        if (i >= s.size()) break;
        std::string tok;
        if (s.substr (i, 6) == "name=\"")
        {
            tok = "name=";
            i += 6;
            bool closed = false;
            while (i < s.size())
            {
                const char c = s[i++];
                if (c == '\\' && i < s.size()) { tok.push_back (s[i++]); continue; }
                if (c == '"') { closed = true; break; }
                tok.push_back (c);
            }
            if (! closed) { err = "unterminated name"; return false; }
        }
        else
            while (i < s.size() && ! (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) tok.push_back (s[i++]);
        out.push_back (std::move (tok));
        if (out.size() > 4096) { err = "too many tokens"; return false; }
    }
    return true;
}
}

std::string toStepText (const Pattern& p, const StepTextOptions& opt)
{
    std::string out;
    const int len = clampInt (p.length, 1, kMaxSteps);
    if (opt.header)
    {
        out += "acidsteps v1 len=" + std::to_string (len);
        out += std::string (" div=") + kDivNames[clampInt (p.div, 0, 6)];
        out += std::string (" mod=") + kModNames[clampInt (p.divMod, 0, 2)];
        out += " shuffle=" + std::to_string (p.shuffle);
        out += " gate=" + (p.gateLength == 0 ? std::string ("auto") : std::to_string (p.gateLength));
        out += std::string (" dir=") + kDirNames[clampInt (p.direction, 0, 3)];
        out += " transpose=" + std::to_string (p.transpose);
        out += " name=\"";
        const int nlen = nameLength (p);
        for (int i = 0; i < nlen; ++i)
        {
            const char c = p.name[i];
            if (c == '"' || c == '\\') out.push_back ('\\');
            out.push_back (c);
        }
        out += "\"\n";
    }

    for (int i = 0; i < len; ++i)
    {
        if (i > 0) out.push_back ((opt.newlineEvery16 && i % 16 == 0) ? '\n' : ' ');
        const Step& s = p.steps[static_cast<std::size_t> (i)];
        const int pitch = opt.arpeggiator ? clampInt (s.arpIndex, 1, kMaxArpIndex) : clampInt (s.note, 0, 11);
        const int oct = clampInt (s.octave, kMinOctave, kMaxOctave);
        if (s.state == StepState::Tie) { out.push_back ('~'); continue; }
        if (s.state == StepState::Rest)
        {
            out.push_back ('.');
            if (opt.restPitches) out += pitchToken (pitch, oct, opt.arpeggiator);
            continue;
        }
        out += pitchToken (pitch, oct, opt.arpeggiator);
        std::string f;
        if (s.accent) f.push_back ('A');
        if (s.slide) f.push_back ('S');
        if (s.vibrato) f.push_back ('V');
        if (s.probability != 100) f += "P" + std::to_string (s.probability);
        if (s.ratchet != 1) f += "R" + std::to_string (s.ratchet);
        if (s.gate != 0) f += "G" + std::to_string (s.gate);
        if (! f.empty()) out += ":" + f;
    }
    return out;
}

bool fromStepText (std::string_view text, Pattern& out, std::string* error)
{
    std::string err;
    auto failWith = [&] (std::string msg) {
        if (error != nullptr) *error = std::move (msg);
        return false;
    };
    if (text.size() > kMaxInput) return failWith ("input too long");

    std::vector<std::string> toks;
    if (! tokenizeStepText (text, toks, err)) return failWith (err);

    Pattern p = out;
    std::size_t i = 0;
    int headerLen = -1;
    if (! toks.empty() && toks[0] == "acidsteps")
    {
        i = 1;
        if (i < toks.size() && toks[i].size() >= 2 && toks[i][0] == 'v') ++i;
    }

    std::array<Step, kMaxSteps> steps {};
    int count = 0;
    for (; i < toks.size(); ++i)
    {
        const std::string_view t = toks[i];
        const std::size_t eq = t.find ('=');
        if (eq != std::string_view::npos)
        {
            if (count > 0) return failWith ("header field after steps: '" + std::string (t.substr (0, 16)) + "'");
            const std::string_view key = t.substr (0, eq);
            const std::string_view v = t.substr (eq + 1);
            long long x = 0;
            if (key == "len")
            {
                if (! parseInt (v, x) || x < 1 || x > kMaxSteps) return failWith ("bad len");
                headerLen = static_cast<int> (x);
            }
            else if (key == "div")
            {
                const int k = parseName (v, kDivNames);
                if (k < 0) return failWith ("bad div");
                p.div = static_cast<StepDiv> (k);
            }
            else if (key == "mod")
            {
                const int k = parseName (v, kModNames);
                if (k < 0) return failWith ("bad mod");
                p.divMod = static_cast<DivMod> (k);
            }
            else if (key == "dir")
            {
                const int k = parseName (v, kDirNames);
                if (k < 0) return failWith ("bad dir");
                p.direction = static_cast<Direction> (k);
            }
            else if (key == "shuffle")
            {
                if (! parseInt (v, x) || x < 0 || x > 100) return failWith ("bad shuffle");
                p.shuffle = static_cast<std::uint8_t> (x);
            }
            else if (key == "gate")
            {
                if (v == "auto") p.gateLength = 0;
                else if (! parseInt (v, x) || x < 0 || x > 100) return failWith ("bad gate");
                else p.gateLength = static_cast<std::uint8_t> (x);
            }
            else if (key == "transpose")
            {
                if (! parseInt (v, x) || x < -48 || x > 48) return failWith ("bad transpose");
                p.transpose = static_cast<std::int8_t> (x);
            }
            else if (key == "name")
            {
                utf8::copyName (p.name, kPatternNameLength, v);
            }
            continue;   // unknown header keys are ignored
        }

        if (count >= kMaxSteps) return failWith ("more than 64 steps");
        if (! parseStepToken (t, steps[static_cast<std::size_t> (count)], err))
            return failWith ("step " + std::to_string (count + 1) + ": " + err);
        ++count;
    }

    if (count == 0 && headerLen < 0) return failWith ("no steps");
    p.steps = steps;
    p.length = static_cast<std::uint8_t> (headerLen > 0 ? headerLen : count);
    out = p;
    return true;
}
}
