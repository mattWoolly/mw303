#include "acid/engine/Xml.h"
#include "acid/common/Utf8.h"

#include <algorithm>
#include <cstring>

namespace acid::xml
{
namespace
{
bool isNameStart (char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
}

bool isNameChar (char c) noexcept
{
    return isNameStart (c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

bool isSpace (char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isAllowedChar (char c) noexcept
{
    const auto u = static_cast<unsigned char> (c);
    return u >= 0x20 || c == '\t' || c == '\n' || c == '\r';
}

class Parser
{
public:
    Parser (std::string_view text, const ParseLimits& limits) : src (text), lim (limits) {}

    ParseResult run (Element& root)
    {
        if (src.size() > lim.maxBytes)
            return fail ("input longer than the limit", 0);

        if (src.size() >= 3 && static_cast<unsigned char> (src[0]) == 0xEF
            && static_cast<unsigned char> (src[1]) == 0xBB && static_cast<unsigned char> (src[2]) == 0xBF)
            pos = 3;

        if (! skipMisc (true))
            return result;

        if (! startsWith ("<") || startsWith ("</"))
            return fail ("expected the root element", pos);

        if (! parseElement (root, 0))
            return result;

        if (! skipMisc (false))
            return result;

        if (pos != src.size())
            return fail ("content after the root element", pos);

        result.ok = true;
        return result;
    }

private:
    bool startsWith (std::string_view s) const noexcept
    {
        return src.size() - pos >= s.size() && src.compare (pos, s.size(), s) == 0;
    }

    ParseResult fail (const char* message, std::size_t at)
    {
        if (! failed)
        {
            failed = true;
            result.ok = false;
            result.error = message;
            result.offset = at;
        }
        return result;
    }

    /** Whitespace, comments and (only when allowDeclaration, at the very start) the XML
        declaration. Returns false on an error. */
    bool skipMisc (bool allowDeclaration)
    {
        bool declarationAllowed = allowDeclaration && pos <= 3;

        while (pos < src.size())
        {
            const char c = src[pos];

            if (isSpace (c))
            {
                ++pos;
                declarationAllowed = false;
                continue;
            }

            if (! isAllowedChar (c))
            {
                fail ("control character", pos);
                return false;
            }

            if (startsWith ("<!--"))
            {
                const auto end = src.find ("-->", pos + 4);
                if (end == std::string_view::npos)
                {
                    fail ("unterminated comment", pos);
                    return false;
                }
                for (std::size_t i = pos + 4; i < end; ++i)
                    if (! isAllowedChar (src[i]))
                    {
                        fail ("control character in comment", i);
                        return false;
                    }
                pos = end + 3;
                declarationAllowed = false;
                continue;
            }

            if (startsWith ("<?xml") && declarationAllowed)
            {
                const auto end = src.find ("?>", pos + 5);
                if (end == std::string_view::npos)
                {
                    fail ("unterminated XML declaration", pos);
                    return false;
                }
                for (std::size_t i = pos + 5; i < end; ++i)
                    if (! isAllowedChar (src[i]) || src[i] == '<')
                    {
                        fail ("bad XML declaration", i);
                        return false;
                    }
                pos = end + 2;
                declarationAllowed = false;
                continue;
            }

            if (startsWith ("<?"))
            {
                fail ("processing instructions are not supported", pos);
                return false;
            }

            if (startsWith ("<!"))
            {
                fail ("DOCTYPE and CDATA are not supported", pos);
                return false;
            }

            return true;    // an element, or stray text (the caller decides)
        }

        return true;
    }

    /** One more element or attribute; false when the document has too many. */
    bool countNode()
    {
        if (++nodes > lim.maxNodes)
        {
            fail ("too many elements and attributes", pos);
            return false;
        }
        return true;
    }

    bool parseName (std::string& out)
    {
        const std::size_t start = pos;
        if (pos >= src.size() || ! isNameStart (src[pos]))
        {
            fail ("expected a name", pos);
            return false;
        }
        while (pos < src.size() && isNameChar (src[pos]))
            ++pos;
        out.assign (src.substr (start, pos - start));
        return true;
    }

    void skipSpace() noexcept
    {
        while (pos < src.size() && isSpace (src[pos]))
            ++pos;
    }

    /** Decodes one entity starting at the '&' at pos; appends the character. */
    bool parseEntity (std::string& out)
    {
        static constexpr struct { const char* name; char value; } kEntities[] = {
            { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' }
        };

        for (const auto& e : kEntities)
        {
            if (startsWith (e.name))
            {
                out.push_back (e.value);
                pos += std::strlen (e.name);
                return true;
            }
        }

        fail ("unknown entity", pos);
        return false;
    }

    bool parseAttributeValue (std::string& out)
    {
        if (pos >= src.size() || (src[pos] != '"' && src[pos] != '\''))
        {
            fail ("expected a quoted attribute value", pos);
            return false;
        }

        const char quote = src[pos++];
        while (true)
        {
            if (pos >= src.size())
            {
                fail ("unterminated attribute value", pos);
                return false;
            }
            const char c = src[pos];
            if (c == quote)
            {
                ++pos;
                return true;
            }
            if (c == '<')
            {
                fail ("'<' in an attribute value", pos);
                return false;
            }
            if (c == '&')
            {
                if (! parseEntity (out)) return false;
                continue;
            }
            if (! isAllowedChar (c))
            {
                fail ("control character in an attribute value", pos);
                return false;
            }
            out.push_back (c);
            ++pos;
        }
    }

    /** pos is at '<' of a start tag. */
    bool parseElement (Element& el, int depth)
    {
        if (depth >= lim.maxDepth)
        {
            fail ("nested too deeply", pos);
            return false;
        }

        ++pos;    // '<'
        if (! countNode()) return false;
        if (! parseName (el.name)) return false;

        bool selfClosing = false;
        while (true)
        {
            const bool hadSpace = pos < src.size() && isSpace (src[pos]);
            skipSpace();
            if (pos >= src.size())
            {
                fail ("unterminated start tag", pos);
                return false;
            }

            if (src[pos] == '>')
            {
                ++pos;
                break;
            }
            if (startsWith ("/>"))
            {
                pos += 2;
                selfClosing = true;
                break;
            }

            if (! hadSpace)
            {
                fail ("expected whitespace before an attribute", pos);
                return false;
            }

            Attribute a;
            if (! countNode()) return false;
            if (! parseName (a.name)) return false;
            skipSpace();
            if (pos >= src.size() || src[pos] != '=')
            {
                fail ("expected '=' after an attribute name", pos);
                return false;
            }
            ++pos;
            skipSpace();
            if (! parseAttributeValue (a.value)) return false;

            for (const auto& existing : el.attributes)
                if (existing.name == a.name)
                {
                    fail ("duplicate attribute", pos);
                    return false;
                }

            el.attributes.push_back (std::move (a));
        }

        if (selfClosing)
            return true;

        // Content: text, comments and child elements, until the matching end tag.
        while (true)
        {
            if (pos >= src.size())
            {
                fail ("unterminated element", pos);
                return false;
            }

            const char c = src[pos];

            if (c == '<')
            {
                if (startsWith ("</"))
                {
                    pos += 2;
                    std::string closing;
                    if (! parseName (closing)) return false;
                    skipSpace();
                    if (pos >= src.size() || src[pos] != '>')
                    {
                        fail ("expected '>' in an end tag", pos);
                        return false;
                    }
                    ++pos;
                    if (closing != el.name)
                    {
                        fail ("mismatched end tag", pos);
                        return false;
                    }
                    break;
                }

                if (startsWith ("<!--"))
                {
                    const auto end = src.find ("-->", pos + 4);
                    if (end == std::string_view::npos)
                    {
                        fail ("unterminated comment", pos);
                        return false;
                    }
                    for (std::size_t i = pos + 4; i < end; ++i)
                        if (! isAllowedChar (src[i]))
                        {
                            fail ("control character in comment", i);
                            return false;
                        }
                    pos = end + 3;
                    continue;
                }

                if (startsWith ("<?") || startsWith ("<!"))
                {
                    fail ("unsupported markup inside an element", pos);
                    return false;
                }

                el.children.emplace_back();
                if (! parseElement (el.children.back(), depth + 1)) return false;
                continue;
            }

            if (c == '&')
            {
                if (! parseEntity (el.text)) return false;
                continue;
            }

            if (! isAllowedChar (c))
            {
                fail ("control character in text", pos);
                return false;
            }

            el.text.push_back (c);
            ++pos;
        }

        if (! el.children.empty()
            && std::all_of (el.text.begin(), el.text.end(), [] (char ch) { return isSpace (ch); }))
            el.text.clear();

        return true;
    }

    std::string_view src;
    ParseLimits lim;
    std::size_t pos = 0;
    std::size_t nodes = 0;
    bool failed = false;
    ParseResult result;
};

void escapeInto (std::string& out, std::string_view s, bool attribute)
{
    // Invalid UTF-8 is written as U+FFFD: the result is always well-formed text.
    std::string fixed;
    if (! utf8::isValid (s))
    {
        fixed = utf8::repaired (s);
        s = fixed;
    }
    for (const char c : s)
    {
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': if (attribute) out += "&quot;"; else out.push_back (c); break;
            default:
                if (isAllowedChar (c)) out.push_back (c);
                else out += "\xEF\xBF\xBD";    // U+FFFD: the parser rejects control characters
                break;
        }
    }
}

void indentTo (std::string& out, int indent)
{
    out.append (static_cast<std::size_t> (std::max (0, indent)) * 2u, ' ');
}

void writeStartTag (std::string& out, const Element& el, bool selfClose)
{
    out.push_back ('<');
    out += el.name;
    for (const auto& a : el.attributes)
    {
        out.push_back (' ');
        out += a.name;
        out += "=\"";
        escapeInto (out, a.value, true);
        out.push_back ('"');
    }
    out += selfClose ? "/>" : ">";
}
}

const Element* Element::child (std::string_view childName) const noexcept
{
    for (const auto& c : children)
        if (c.name == childName) return &c;
    return nullptr;
}

Element* Element::child (std::string_view childName) noexcept
{
    for (auto& c : children)
        if (c.name == childName) return &c;
    return nullptr;
}

const std::string* Element::attribute (std::string_view attributeName) const noexcept
{
    for (const auto& a : attributes)
        if (a.name == attributeName) return &a.value;
    return nullptr;
}

void Element::setAttribute (std::string_view attributeName, std::string_view value)
{
    for (auto& a : attributes)
        if (a.name == attributeName)
        {
            a.value.assign (value);
            return;
        }
    attributes.push_back ({ std::string (attributeName), std::string (value) });
}

void Element::removeAttribute (std::string_view attributeName)
{
    attributes.erase (std::remove_if (attributes.begin(), attributes.end(),
                                      [&] (const Attribute& a) { return a.name == attributeName; }),
                      attributes.end());
}

ParseResult parse (std::string_view text, Element& root, const ParseLimits& limits)
{
    root = Element {};
    Parser parser (text, limits);
    return parser.run (root);
}

std::string escapeText (std::string_view s)
{
    std::string out;
    out.reserve (s.size());
    escapeInto (out, s, false);
    return out;
}

std::string escapeAttribute (std::string_view s)
{
    std::string out;
    out.reserve (s.size());
    escapeInto (out, s, true);
    return out;
}

void write (std::string& out, const Element& el, int indent)
{
    indentTo (out, indent);

    if (el.children.empty() && el.text.empty())
    {
        writeStartTag (out, el, true);
        out.push_back ('\n');
        return;
    }

    writeStartTag (out, el, false);

    if (el.children.empty())
    {
        escapeInto (out, el.text, false);
    }
    else if (el.text.empty())
    {
        out.push_back ('\n');
        for (const auto& c : el.children)
            write (out, c, indent + 1);
        indentTo (out, indent);
    }
    else
    {
        // Mixed content: the text is kept whole and the children follow it inline.
        escapeInto (out, el.text, false);
        for (const auto& c : el.children)
        {
            std::string inlineChild;
            write (inlineChild, c, 0);
            if (! inlineChild.empty() && inlineChild.back() == '\n')
                inlineChild.pop_back();
            out += inlineChild;
        }
    }

    out += "</";
    out += el.name;
    out += ">\n";
}

std::string write (const Element& el)
{
    std::string out;
    write (out, el, 0);
    return out;
}

bool isValidName (std::string_view name) noexcept
{
    if (name.empty() || ! isNameStart (name[0])) return false;
    return std::all_of (name.begin(), name.end(), [] (char c) { return isNameChar (c); });
}
}
