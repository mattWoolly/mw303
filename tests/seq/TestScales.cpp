#include "acid/seq/Scales.h"
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>

using namespace acid::seq;

TEST_CASE ("template library: at least 50, unique names, valid masks", "[scales]")
{
    const auto all = scaleTemplates();
    REQUIRE (all.size() >= 50);
    REQUIRE (std::string (all[0].name) == "Everything");

    std::set<std::string> names;
    int counts[6] {};
    for (const ScaleTemplate& t : all)
    {
        std::string lower;
        for (const char* c = t.name; *c != '\0'; ++c) lower.push_back (static_cast<char> (*c >= 'A' && *c <= 'Z' ? *c - 'A' + 'a' : *c));
        INFO (t.name);
        REQUIRE (names.insert (lower).second);
        REQUIRE ((t.noteMask & 1) == 1);            // every template contains its root
        REQUIRE ((t.noteMask & ~0x0FFF) == 0);
        REQUIRE (t.octaveMask != 0);
        REQUIRE ((t.octaveMask & ~0x1F) == 0);
        counts[static_cast<int> (t.category)]++;
    }
    REQUIRE (counts[static_cast<int> (ScaleCategory::Scale)] >= 10);
    REQUIRE (counts[static_cast<int> (ScaleCategory::Pentatonic)] >= 3);
    REQUIRE (counts[static_cast<int> (ScaleCategory::Chord)] >= 10);
    REQUIRE (counts[static_cast<int> (ScaleCategory::Interval)] >= 6);
    REQUIRE (counts[static_cast<int> (ScaleCategory::Acid)] >= 8);
}

TEST_CASE ("template lookup by index and name", "[scales]")
{
    REQUIRE (findScaleTemplate ("Natural Minor") >= 0);
    REQUIRE (findScaleTemplate ("  natural MINOR ") == findScaleTemplate ("Natural Minor"));
    REQUIRE (findScaleTemplate ("No Such Scale") == -1);
    REQUIRE (findScaleTemplate ("") == -1);
    REQUIRE (scaleTemplate (-1) == nullptr);
    REQUIRE (scaleTemplate (static_cast<int> (scaleTemplates().size())) == nullptr);

    const int i = findScaleTemplate ("Minor Pentatonic");
    const ScaleTemplate* t = scaleTemplate (i);
    REQUIRE (t != nullptr);
    REQUIRE (t->noteMask == noteMaskOf ({ 0, 3, 5, 7, 10 }));

    const ScaleTemplate* major = scaleTemplate (findScaleTemplate ("Major"));
    REQUIRE (major->noteMask == 0b101010110101);
    REQUIRE (scaleTemplate (findScaleTemplate ("Chromatic"))->noteMask == 0x0FFF);
    REQUIRE (scaleTemplate (findScaleTemplate ("Major Triad"))->noteMask == noteMaskOf ({ 0, 4, 7 }));
    REQUIRE (scaleTemplate (findScaleTemplate ("Perfect Fifth"))->noteMask == noteMaskOf ({ 0, 7 }));
}

TEST_CASE ("applying and matching templates", "[scales]")
{
    GenParams g;
    g.root = 7;
    g.amountNote = 33;
    const ScaleTemplate& t = *scaleTemplate (findScaleTemplate ("Accent Hammer"));
    applyScaleTemplate (t, g);
    REQUIRE (g.noteMask == t.noteMask);
    REQUIRE (g.octaveMask == t.octaveMask);
    REQUIRE_FALSE (g.useSlide);
    REQUIRE (g.useAccent);
    REQUIRE (g.root == 7);            // kept
    REQUIRE (g.amountNote == 33);     // kept
    REQUIRE (matchScaleTemplate (g) == findScaleTemplate ("Accent Hammer"));

    for (int i = 0; i < static_cast<int> (scaleTemplates().size()); ++i)
    {
        GenParams h;
        applyScaleTemplate (*scaleTemplate (i), h);
        const int m = matchScaleTemplate (h);
        REQUIRE (m >= 0);
        REQUIRE (m <= i);   // the first identical template
    }

    GenParams odd;
    odd.noteMask = 0b100000000110;
    odd.octaveMask = 0b10001;
    REQUIRE (matchScaleTemplate (odd) == -1);
}
