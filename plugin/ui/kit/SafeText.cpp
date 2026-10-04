#include "kit/SafeText.h"

#include <cstring>
#include <vector>

namespace acid::ui::text
{
namespace
{
constexpr juce::juce_wchar kReplacement = 0xfffd;

bool isControl (juce::juce_wchar c) noexcept
{
    return c < 0x20 || (c >= 0x7f && c < 0xa0);
}

/** Decodes one code point at `s[i]` (i < n), strictly. Returns the number of bytes consumed
    (at least 1) and sets `out` to the code point, or to U+FFFD for an invalid sequence. */
std::size_t decodeOne (const unsigned char* s, std::size_t i, std::size_t n, juce::juce_wchar& out) noexcept
{
    const auto b0 = s[i];

    if (b0 < 0x80)
    {
        out = static_cast<juce::juce_wchar> (b0);
        return 1;
    }

    std::size_t extra = 0;
    std::uint32_t cp = 0, minimum = 0;

    if (b0 >= 0xc2 && b0 <= 0xdf)      { extra = 1; cp = b0 & 0x1fu; minimum = 0x80; }
    else if (b0 >= 0xe0 && b0 <= 0xef) { extra = 2; cp = b0 & 0x0fu; minimum = 0x800; }
    else if (b0 >= 0xf0 && b0 <= 0xf4) { extra = 3; cp = b0 & 0x07u; minimum = 0x10000; }
    else
    {
        out = kReplacement;   // a continuation byte, an overlong lead (C0, C1) or F5..FF
        return 1;
    }

    std::size_t used = 1;

    for (; used <= extra; ++used)
    {
        if (i + used >= n || (s[i + used] & 0xc0u) != 0x80u)
        {
            out = kReplacement;   // truncated: resume at the byte that broke the sequence
            return used;
        }

        cp = (cp << 6) | (s[i + used] & 0x3fu);
    }

    out = (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) ? kReplacement : static_cast<juce::juce_wchar> (cp);
    return used;
}

std::size_t boundedLength (const char* bytes, std::size_t maxBytes) noexcept
{
    const auto* end = static_cast<const char*> (std::memchr (bytes, 0, maxBytes));
    return end != nullptr ? static_cast<std::size_t> (end - bytes) : maxBytes;
}

juce::String decode (const char* bytes, std::size_t n)
{
    const auto* s = reinterpret_cast<const unsigned char*> (bytes);
    std::vector<juce::juce_wchar> chars;
    chars.reserve (n + 1);

    for (std::size_t i = 0; i < n;)
    {
        juce::juce_wchar c = 0;
        i += decodeOne (s, i, n, c);
        chars.push_back (isControl (c) ? juce::juce_wchar (' ') : c);
    }

    chars.push_back (0);
    return juce::String (juce::CharPointer_UTF32 (chars.data()));
}
}

bool isClean (const char* bytes, std::size_t numBytes) noexcept
{
    if (bytes == nullptr)
        return true;

    const auto* s = reinterpret_cast<const unsigned char*> (bytes);

    for (std::size_t i = 0; i < numBytes;)
    {
        juce::juce_wchar c = 0;
        const auto used = decodeOne (s, i, numBytes, c);

        if (c == kReplacement && ! (used == 3 && s[i] == 0xef && s[i + 1] == 0xbf && s[i + 2] == 0xbd))
            return false;   // an invalid sequence (a literal U+FFFD in the input is fine)

        if (c == 0 || isControl (c))
            return false;

        i += used;
    }

    return true;
}

juce::String fromBytes (const char* bytes, std::size_t maxBytes)
{
    if (bytes == nullptr || maxBytes == 0)
        return {};

    const auto n = boundedLength (bytes, maxBytes);

    if (isClean (bytes, n))
        return juce::String (juce::CharPointer_UTF8 (bytes), juce::CharPointer_UTF8 (bytes + n));   // by bytes: the array need not end in a NUL

    return decode (bytes, n);
}

juce::String from (std::string_view bytes)
{
    return fromBytes (bytes.data(), bytes.size());
}

juce::String sanitise (const juce::String& s)
{
    // toRawUTF8 is the stored bytes (the storage is UTF-8); strlen, not getNumBytesAsUTF8,
    // which walks the string as UTF-8 and is exactly what invalid bytes break.
    const auto* raw = s.toRawUTF8();
    const auto n = std::strlen (raw);

    if (isClean (raw, n))
        return s;

    return decode (raw, n);
}
}
