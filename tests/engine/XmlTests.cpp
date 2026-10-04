// The small XML reader and writer: what it accepts, what it rejects, and that writing a
// parsed tree is stable.
#include "acid/engine/Xml.h"
#include "acid/common/Rng.h"
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace acid;

TEST_CASE ("xml: parses elements, attributes, text, entities, comments and the declaration", "[xml]")
{
    const std::string doc = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                            "<!-- a comment -->\n"
                            "<root a=\"1\" b='two &amp; &lt;three&gt;'>\n"
                            "  <child x=\"&quot;q&quot;\"/>\n"
                            "  <text>hello &apos;world&apos; &amp; more</text>\n"
                            "  <!-- inside -->\n"
                            "  <nested><deeper k=\"v\">t</deeper></nested>\n"
                            "</root>\n<!-- trailing -->\n";
    xml::Element root;
    const xml::ParseResult r = xml::parse (doc, root);
    REQUIRE (r.ok);
    CHECK (root.name == "root");
    REQUIRE (root.attributes.size() == 2);
    CHECK (*root.attribute ("a") == "1");
    CHECK (*root.attribute ("b") == "two & <three>");
    CHECK (root.text.empty());
    REQUIRE (root.children.size() == 3);
    CHECK (*root.child ("child")->attribute ("x") == "\"q\"");
    CHECK (root.child ("text")->text == "hello 'world' & more");
    CHECK (root.child ("nested")->child ("deeper")->text == "t");
    CHECK (root.child ("missing") == nullptr);
    CHECK (root.attribute ("missing") == nullptr);
}

TEST_CASE ("xml: rejects what it does not support", "[xml]")
{
    const char* bad[] = {
        "",
        "   ",
        "<a>",
        "<a></b>",
        "<a></a><b/>",
        "<a><b></a>",
        "<a x=1/>",
        "<a x=\"1\" x=\"2\"/>",
        "<a x=\"1\"y=\"2\"/>",
        "<a>&nbsp;</a>",
        "<a>&#65;</a>",
        "<a>&amp</a>",
        "<a x=\"<\"/>",
        "<!DOCTYPE a><a/>",
        "<a><![CDATA[x]]></a>",
        "<?pi x?><a/>",
        "<a/><?xml version=\"1.0\"?>",
        "<a><?xml version=\"1.0\"?></a>",
        "<a>\x01</a>",
        "<a x=\"\x02\"/>",
        "<1a/>",
        "<a b.c=\"1\"/>x",
        "text<a/>",
        "<a/><!-- unterminated",
        "<a><!-- x --",
        "<a/",
        "<a",
        "<",
        "<a x=\"1\"",
        "<a x",
        "<a x=",
        "<a x=\"",
        "</a>",
    };
    for (const char* text : bad)
    {
        xml::Element root;
        const xml::ParseResult r = xml::parse (text, root);
        INFO ("input: " << text);
        CHECK_FALSE (r.ok);
        CHECK_FALSE (r.error.empty());
    }
}

TEST_CASE ("xml: depth and size limits", "[xml]")
{
    std::string deep;
    for (int i = 0; i < 70; ++i) deep += "<a>";
    for (int i = 0; i < 70; ++i) deep += "</a>";
    xml::Element root;
    CHECK_FALSE (xml::parse (deep, root).ok);
    xml::ParseLimits generous;
    generous.maxDepth = 100;
    CHECK (xml::parse (deep, root, generous).ok);

    std::string big = "<a x=\"" + std::string (300000, 'y') + "\"/>";
    CHECK (xml::parse (big, root).ok);
    xml::ParseLimits tight;
    tight.maxBytes = 1000;
    CHECK_FALSE (xml::parse (big, root, tight).ok);

    // Elements and attributes are counted against maxNodes: a small document of many nodes
    // is refused, so the tree it would build is bounded in memory.
    std::string many = "<r>";
    for (int i = 0; i < 1000; ++i) many += "<a b=\"1\" c=\"2\"/>";   // 3001 nodes with the root
    many += "</r>";
    CHECK (xml::parse (many, root).ok);
    xml::ParseLimits fewNodes;
    fewNodes.maxNodes = 3000;
    const xml::ParseResult r = xml::parse (many, root, fewNodes);
    CHECK_FALSE (r.ok);
    CHECK (r.error == "too many elements and attributes");
    fewNodes.maxNodes = 3001;
    CHECK (xml::parse (many, root, fewNodes).ok);

    // Writing replaces what parsing would reject, so a written tree always reads back.
    xml::Element bad;
    bad.name = "n";
    bad.setAttribute ("v", std::string ("a\x01" "b"));
    bad.text = std::string ("x\x07y\tz");
    const std::string written = xml::write (bad);
    CHECK (written == "<n v=\"a\xEF\xBF\xBD" "b\">x\xEF\xBF\xBD" "y\tz</n>\n");
    CHECK (xml::parse (written, root).ok);
    CHECK (xml::write (root) == written);
}

TEST_CASE ("xml: writing a parsed tree is stable and escapes correctly", "[xml]")
{
    xml::Element e;
    e.name = "doc";
    e.setAttribute ("q", "a \"quoted\" & <tagged> 'value'");
    xml::Element t;
    t.name = "t";
    t.text = "x < y && y > z \"q\" 'a'";
    e.children.push_back (t);
    xml::Element empty;
    empty.name = "e";
    e.children.push_back (empty);
    xml::Element mixed;
    mixed.name = "m";
    mixed.text = " some text ";
    mixed.children.push_back (empty);
    e.children.push_back (mixed);

    const std::string once = xml::write (e);
    xml::Element back;
    REQUIRE (xml::parse (once, back).ok);
    CHECK (*back.attribute ("q") == "a \"quoted\" & <tagged> 'value'");
    CHECK (back.child ("t")->text == t.text);
    CHECK (back.child ("m")->text == " some text ");
    REQUIRE (back.child ("m")->children.size() == 1);
    const std::string twice = xml::write (back);
    CHECK (once == twice);
    CHECK (once == "<doc q=\"a &quot;quoted&quot; &amp; &lt;tagged&gt; 'value'\">\n"
                   "  <t>x &lt; y &amp;&amp; y &gt; z \"q\" 'a'</t>\n"
                   "  <e/>\n"
                   "  <m> some text <e/></m>\n"
                   "</doc>\n");

    e.removeAttribute ("q");
    CHECK (e.attribute ("q") == nullptr);
    CHECK (xml::isValidName ("a-b.c:d_1"));
    CHECK_FALSE (xml::isValidName ("1a"));
    CHECK_FALSE (xml::isValidName ("a b"));
    CHECK_FALSE (xml::isValidName (""));
}

TEST_CASE ("xml: random garbage and mutations never crash", "[xml][fuzz]")
{
    const std::string seed = "<mw12F schema=\"1\" type=\"scene\" name=\"n &amp; m\"><params><p id=\"cutoff\" v=\"35\"/></params>"
                             "<settings fx_order=\"0,1,2,3,4,5\"/><bank><seq slot=\"0\">acidpat 1|len=16</seq></bank><ui zoom=\"1\"/></mw12F>";
    Rng rng (2024);
    int ok = 0;
    for (int i = 0; i < 20000; ++i)
    {
        std::string s = seed;
        const int mutations = 1 + rng.nextInt (6);
        for (int m = 0; m < mutations; ++m)
        {
            const int kind = rng.nextInt (5);
            const auto at = static_cast<std::size_t> (rng.nextInt (static_cast<int> (s.size()) + 1));
            if (kind == 0 && ! s.empty()) s.erase (std::min (at, s.size() - 1), 1);
            else if (kind == 1) s.insert (at, 1, static_cast<char> (rng.nextInt (256)));
            else if (kind == 2 && at < s.size()) s[at] = "<>&\"'/= \n"[rng.nextInt (9)];
            else if (kind == 3) s = s.substr (0, at);
            else if (kind == 4) s.insert (at, s.substr (0, std::min<std::size_t> (s.size(), static_cast<std::size_t> (rng.nextInt (40)))));
        }
        xml::Element root;
        const xml::ParseResult r = xml::parse (s, root);
        if (r.ok)
        {
            ++ok;
            const std::string w = xml::write (root);
            xml::Element again;
            REQUIRE (xml::parse (w, again).ok);
            CHECK (xml::write (again) == w);
        }
    }
    CHECK (ok > 0);
}
