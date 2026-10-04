#include "Theme.h"
#include "mw12FLookAndFeel.h"

namespace acid::ui
{
namespace
{
juce::Colour rgb (std::uint32_t hex) noexcept { return juce::Colour (0xff000000u | hex); }
juce::Colour argb (std::uint32_t hex) noexcept { return juce::Colour (hex); }

Theme makeSilver()
{
    Theme t {};
    t.id = ThemeId::Silver;
    t.name = "Silver";

    t.plateTop = rgb (0xd9dbde);
    t.plateBottom = rgb (0xc6c9cd);
    t.plateEdgeLight = rgb (0xf4f5f6);
    t.plateEdgeDark = rgb (0x8f949a);
    t.sectionFill = rgb (0xe3e5e7);
    t.sectionOutline = rgb (0xa3a8ae);
    t.sectionRule = rgb (0x9a9fa6);
    t.legend = rgb (0x1c2025);
    t.legendDim = rgb (0x4f555d);
    t.legendFaint = rgb (0x7d848c);

    t.knobSkirtTop = rgb (0x4a4f56);
    t.knobSkirtBottom = rgb (0x15171a);
    t.knobCapTop = rgb (0x3b3f45);
    t.knobCapBottom = rgb (0x25282c);
    t.knobRim = rgb (0x6b727a);
    t.knobPointer = rgb (0xf2f4f5);
    t.arcTrack = rgb (0xb0b4ba);
    t.arcFill = rgb (0x23272c);
    t.trimBody = rgb (0x2c3035);
    t.trimRotor = rgb (0xd9dcdf);
    t.trimSlot = rgb (0x1a1d20);

    t.buttonTop = rgb (0xf3f4f5);
    t.buttonBottom = rgb (0xd6d9dc);
    t.buttonOutline = rgb (0x858b92);
    t.buttonText = rgb (0x1c2025);
    t.buttonDownTop = rgb (0xc3c7cb);
    t.buttonDownBottom = rgb (0xd2d5d8);
    t.keyWhite = rgb (0xf1f2f3);
    t.keyBlack = rgb (0x25282c);
    t.keyOutline = rgb (0x7c8289);

    t.wellFill = rgb (0x1b1e22);
    t.wellOutline = rgb (0x0c0d0f);
    t.wellText = rgb (0xc8ee4a);
    t.wellTextDim = rgb (0x7f8c5a);

    t.screenFill = rgb (0x1d2024);
    t.screenRow = rgb (0x2a2e33);
    t.screenRowDark = rgb (0x22262a);
    t.screenGrid = rgb (0x1d2024);
    t.screenGridBar = rgb (0x596069);
    t.screenText = rgb (0xc9ced3);
    t.screenTextDim = rgb (0x7e868f);
    t.screenDisabled = argb (0xb01d2024);
    t.cursor = rgb (0xff4a3d);

    t.accent = rgb (0xb7e23a);
    t.accentBright = rgb (0xe2ff8a);
    t.accentDim = rgb (0x5d7222);
    t.accentInk = rgb (0x151a05);
    t.accentOnPlate = rgb (0x4d6a0c);

    t.ledRed = rgb (0xff3d2e);
    t.ledBlue = rgb (0x2f8fff);
    t.ledAmber = rgb (0xffa51f);
    t.ledTurquoise = rgb (0x1ad9c5);
    t.ledBezel = rgb (0x15171a);

    t.bayFill = rgb (0x20242a);
    t.bayOutline = rgb (0x0c0d0f);
    t.bayText = rgb (0xc9ced3);
    t.bayTextDim = rgb (0x858d96);
    t.coverTop = rgb (0xe9ebed);
    t.coverBottom = rgb (0xcfd2d6);

    t.menuFill = rgb (0x24282d);
    t.menuOutline = rgb (0x0c0d0f);
    t.menuText = rgb (0xe4e7ea);
    t.menuTextDim = rgb (0x8d949c);
    t.menuHighlight = rgb (0xb7e23a);
    t.menuHighlightText = rgb (0x151a05);
    t.shadow = argb (0x55000000);
    return t;
}

Theme makeNight()
{
    Theme t {};
    t.id = ThemeId::Night;
    t.name = "Night";

    t.plateTop = rgb (0x202327);
    t.plateBottom = rgb (0x17191c);
    t.plateEdgeLight = rgb (0x3a3f45);
    t.plateEdgeDark = rgb (0x08090a);
    t.sectionFill = rgb (0x25292e);
    t.sectionOutline = rgb (0x3a3f46);
    t.sectionRule = rgb (0x444a52);
    t.legend = rgb (0xdfe2e5);
    t.legendDim = rgb (0xa2a9b1);
    t.legendFaint = rgb (0x6b727a);

    t.knobSkirtTop = rgb (0x767d86);
    t.knobSkirtBottom = rgb (0x2b2f34);
    t.knobCapTop = rgb (0x565c64);
    t.knobCapBottom = rgb (0x383c42);
    t.knobRim = rgb (0x8b929a);
    t.knobPointer = rgb (0xf2f4f5);
    t.arcTrack = rgb (0x0f1012);
    t.arcFill = rgb (0xb7e23a);
    t.trimBody = rgb (0x33383e);
    t.trimRotor = rgb (0xc3c7cc);
    t.trimSlot = rgb (0x1a1d20);

    t.buttonTop = rgb (0x444a52);
    t.buttonBottom = rgb (0x33373d);
    t.buttonOutline = rgb (0x0b0c0d);
    t.buttonText = rgb (0xe4e7ea);
    t.buttonDownTop = rgb (0x2a2e33);
    t.buttonDownBottom = rgb (0x33373d);
    t.keyWhite = rgb (0xc9cdd2);
    t.keyBlack = rgb (0x1a1c1f);
    t.keyOutline = rgb (0x0b0c0d);

    t.wellFill = rgb (0x0d0e10);
    t.wellOutline = rgb (0x000000);
    t.wellText = rgb (0xc8ee4a);
    t.wellTextDim = rgb (0x76834f);

    t.screenFill = rgb (0x0d0e10);
    t.screenRow = rgb (0x1c1f23);
    t.screenRowDark = rgb (0x15171a);
    t.screenGrid = rgb (0x0d0e10);
    t.screenGridBar = rgb (0x4a5059);
    t.screenText = rgb (0xc3c8ce);
    t.screenTextDim = rgb (0x727981);
    t.screenDisabled = argb (0xb00d0e10);
    t.cursor = rgb (0xff4a3d);

    t.accent = rgb (0xb7e23a);
    t.accentBright = rgb (0xe2ff8a);
    t.accentDim = rgb (0x55691f);
    t.accentInk = rgb (0x151a05);
    t.accentOnPlate = rgb (0xb7e23a);

    t.ledRed = rgb (0xff3d2e);
    t.ledBlue = rgb (0x3c98ff);
    t.ledAmber = rgb (0xffa51f);
    t.ledTurquoise = rgb (0x1ad9c5);
    t.ledBezel = rgb (0x050506);

    t.bayFill = rgb (0x101214);
    t.bayOutline = rgb (0x000000);
    t.bayText = rgb (0xc3c8ce);
    t.bayTextDim = rgb (0x7b838c);
    t.coverTop = rgb (0x33373d);
    t.coverBottom = rgb (0x25282d);

    t.menuFill = rgb (0x2a2e33);
    t.menuOutline = rgb (0x050506);
    t.menuText = rgb (0xe4e7ea);
    t.menuTextDim = rgb (0x8d949c);
    t.menuHighlight = rgb (0xb7e23a);
    t.menuHighlightText = rgb (0x151a05);
    t.shadow = argb (0x77000000);
    return t;
}
}

juce::Colour Theme::led (LedColour c) const noexcept
{
    switch (c)
    {
        case LedColour::Accent:    return accent;
        case LedColour::Red:       return ledRed;
        case LedColour::Blue:      return ledBlue;
        case LedColour::Amber:     return ledAmber;
        case LedColour::Turquoise: return ledTurquoise;
    }
    return accent;
}

const Theme& theme (ThemeId id) noexcept
{
    static const Theme silver = makeSilver();
    static const Theme night = makeNight();
    return id == ThemeId::Night ? night : silver;
}

const Theme& themeFor (const juce::Component& c) noexcept
{
    if (auto* lf = dynamic_cast<mw12FLookAndFeel*> (&c.getLookAndFeel()))
        return lf->getTheme();

    return theme (ThemeId::Silver);
}
}
