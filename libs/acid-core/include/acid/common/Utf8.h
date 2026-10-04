#pragma once
// UTF-8 checks and repairs for text that comes from outside (session and preset files,
// pattern text, MIDI files). Every name the model stores is valid UTF-8: a host's string
// class or a font shaper reading an invalid sequence can read past the end of the buffer.
//
// Validity is RFC 3629: no overlong forms, no UTF-16 surrogates (U+D800..U+DFFF), nothing
// above U+10FFFF, no stray continuation bytes, no truncated sequences. Repair replaces each
// maximal invalid subpart (the Unicode "substitution of maximal subparts" practice, as the
// WHATWG decoder does) with U+FFFD. Truncation to a byte budget never cuts a character.
// Header-only, allocation only where a std::string is returned, no exceptions otherwise.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace acid::utf8
{
inline constexpr std::string_view kReplacement = "\xEF\xBF\xBD";   // U+FFFD

/** Length of the valid sequence that starts at s[i], or 0 when the bytes there do not start
    one. When 0, `skip` receives the length of the maximal invalid subpart (at least 1). */
constexpr std::size_t sequenceAt (std::string_view s, std::size_t i, std::size_t& skip) noexcept
{
    const auto b = [&] (std::size_t k) { return static_cast<unsigned char> (s[k]); };
    const unsigned char c = b (i);
    skip = 1;
    if (c < 0x80) return 1;

    std::size_t need = 0;
    unsigned char lo = 0x80, hi = 0xBF;     // allowed range of the second byte
    if (c >= 0xC2 && c <= 0xDF) need = 1;
    else if (c == 0xE0) { need = 2; lo = 0xA0; }
    else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) need = 2;
    else if (c == 0xED) { need = 2; hi = 0x9F; }
    else if (c == 0xF0) { need = 3; lo = 0x90; }
    else if (c >= 0xF1 && c <= 0xF3) need = 3;
    else if (c == 0xF4) { need = 3; hi = 0x8F; }
    else return 0;      // continuation byte, C0, C1, F5..FF

    for (std::size_t k = 1; k <= need; ++k)
    {
        if (i + k >= s.size()) { skip = k; return 0; }
        const unsigned char x = b (i + k);
        const bool ok = k == 1 ? (x >= lo && x <= hi) : (x >= 0x80 && x <= 0xBF);
        if (! ok) { skip = k; return 0; }
    }
    return need + 1;
}

/** True when every byte belongs to a valid sequence. */
constexpr bool isValid (std::string_view s) noexcept
{
    for (std::size_t i = 0; i < s.size();)
    {
        std::size_t skip = 0;
        const std::size_t n = sequenceAt (s, i, skip);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

/** The text with each maximal invalid subpart replaced by U+FFFD. Valid text comes back
    unchanged. */
inline std::string repaired (std::string_view s)
{
    std::string out;
    out.reserve (s.size());
    for (std::size_t i = 0; i < s.size();)
    {
        std::size_t skip = 0;
        const std::size_t n = sequenceAt (s, i, skip);
        if (n == 0)
        {
            out += kReplacement;
            i += skip;
        }
        else
        {
            out.append (s.substr (i, n));
            i += n;
        }
    }
    return out;
}

/** Repairs in place; returns true when anything changed. */
inline bool repair (std::string& s)
{
    if (isValid (s)) return false;
    s = repaired (s);
    return true;
}

/** The longest prefix of valid text that fits in maxBytes without cutting a character. */
constexpr std::size_t prefixLength (std::string_view valid, std::size_t maxBytes) noexcept
{
    std::size_t i = 0;
    while (i < valid.size())
    {
        std::size_t skip = 0;
        std::size_t n = sequenceAt (valid, i, skip);
        if (n == 0) n = skip;      // not valid after all: count it as it stands
        if (i + n > maxBytes) break;
        i += n;
    }
    return i;
}

/** Latin-1 (ISO 8859-1) bytes to UTF-8: what an old MIDI file's text most likely is when
    it is not UTF-8. */
inline std::string fromLatin1 (std::string_view s)
{
    std::string out;
    out.reserve (s.size() * 2);
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c < 0x80) out += ch;
        else
        {
            out += static_cast<char> (0xC0 | (c >> 6));
            out += static_cast<char> (0x80 | (c & 0x3F));
        }
    }
    return out;
}

/** Writes text into a fixed char array of `capacity` bytes as a NUL-terminated, NUL-padded
    C string of valid UTF-8: invalid sequences repaired, cut to at most capacity - 1 bytes at
    a character boundary. Returns true when the text had to be repaired or cut. A NUL inside
    the text ends it. */
inline bool copyName (char* dst, std::size_t capacity, std::string_view src)
{
    if (dst == nullptr || capacity == 0) return ! src.empty();
    const std::size_t nul = src.find ('\0');
    if (nul != std::string_view::npos) src = src.substr (0, nul);

    const bool valid = isValid (src);
    const std::string fixed = valid ? std::string() : repaired (src);
    const std::string_view text = valid ? src : std::string_view (fixed);
    const std::size_t n = prefixLength (text, capacity - 1);
    for (std::size_t i = 0; i < capacity; ++i)
        dst[i] = i < n ? text[i] : '\0';
    return ! valid || n < text.size();
}

/** Repairs a fixed char array name in place (see copyName). Bytes after the terminator are
    cleared. Returns true when the text itself changed (it was not terminated, not valid, or
    had to be cut); clearing the padding alone does not count. */
inline bool repairName (char* name, std::size_t capacity)
{
    if (name == nullptr || capacity == 0) return false;
    std::size_t len = 0;
    while (len < capacity && name[len] != '\0') ++len;
    const std::string_view current (name, len);
    if (len < capacity && isValid (current))
    {
        for (std::size_t i = len; i < capacity; ++i) name[i] = '\0';
        return false;
    }
    const std::string copy (current);
    copyName (name, capacity, copy);
    return true;
}
}
