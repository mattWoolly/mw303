#pragma once
// A small, strict XML reader and writer for the state and preset codec. No JUCE, no
// dependencies, no surprises: it reads exactly the subset the codec writes and rejects the
// rest with a message and an offset.
//
// Accepted: an optional UTF-8 byte order mark, an optional XML declaration at the very
// start, comments anywhere, one root element, elements with attributes (quoted with " or '),
// character data, and the five predefined entities (amp, lt, gt, quot, apos). Names are
// ASCII letters, digits, '_', '-', '.', ':', starting with a letter, '_' or ':'.
//
// Rejected: DOCTYPE, CDATA, processing instructions, numeric character references, unknown
// entities, control characters other than tab, newline and return, duplicate attributes,
// mismatched or unterminated tags, more than one root, nesting deeper than the limit, input
// longer than the limit, and more elements and attributes than the limit. Everything is
// bounds-checked, so hostile input cannot crash or run away; the cost is linear in the input
// size and the memory is bounded by the node limit.
//
// The tree keeps every attribute in document order and the concatenated character data of
// each element. Whitespace-only text next to child elements is dropped (it is layout), so
// writing a parsed tree back gives the same tree, and writing that again gives the same
// bytes. Writing never produces what parsing rejects: a control character other than tab,
// newline and return in text or an attribute value is written as U+FFFD.
//
// Message thread. Everything here allocates.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace acid::xml
{
struct Attribute
{
    std::string name;
    std::string value;
};

struct Element
{
    std::string name;
    std::vector<Attribute> attributes;
    std::string text;                 // character data, entities decoded
    std::vector<Element> children;

    /** First child with that name, or nullptr. */
    const Element* child (std::string_view childName) const noexcept;
    Element* child (std::string_view childName) noexcept;

    /** Value of that attribute, or nullptr. */
    const std::string* attribute (std::string_view attributeName) const noexcept;

    /** Sets or adds an attribute, keeping the order of the ones already there. */
    void setAttribute (std::string_view attributeName, std::string_view value);
    void removeAttribute (std::string_view attributeName);
};

/** Bounds on what parse() accepts. The defaults suit state and preset documents, which are
    a few kilobytes and a few thousand nodes: a 4 MiB document of empty elements would
    otherwise cost hundreds of megabytes of tree. */
struct ParseLimits
{
    std::size_t maxBytes = 4u * 1024u * 1024u;    // input longer than this is rejected
    int maxDepth = 64;                            // elements nested deeper than this are rejected
    std::size_t maxNodes = 100000;                // elements plus attributes beyond this are rejected
};

struct ParseResult
{
    bool ok = false;
    std::string error;        // when ok is false: what was wrong
    std::size_t offset = 0;   // byte offset the error was found at
};

/** Parses one document into root. On failure root is left in an unspecified state. */
ParseResult parse (std::string_view text, Element& root, const ParseLimits& limits = {});

/** Escapes & < > for character data, and additionally " for attribute values. Control
    characters other than tab, newline and return, and every invalid UTF-8 sequence, become
    U+FFFD, so the result always parses and is always valid UTF-8. */
std::string escapeText (std::string_view);
std::string escapeAttribute (std::string_view);

/** Appends the element as indented text, two spaces per level, deterministic. An element
    with children and no text puts each child on its own line; one with text and no children
    is written on one line; one with both is written inline. */
void write (std::string& out, const Element&, int indent = 0);
std::string write (const Element&);

/** True for a name parse() would accept. */
bool isValidName (std::string_view) noexcept;
}
