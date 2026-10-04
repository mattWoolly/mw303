#include "Fonts.h"
#include <mwAssets.h>

namespace acid::ui::fonts
{
namespace
{
juce::Typeface::Ptr load (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, static_cast<std::size_t> (size));
}

juce::Font make (const juce::Typeface::Ptr& face, float height, float tracking)
{
    return juce::Font (juce::FontOptions (face).withHeight (height).withKerningFactor (tracking));
}
}

juce::Typeface::Ptr textTypeface()
{
    static const auto face = load (mwAssets::BarlowSemiCondensedMedium_ttf, mwAssets::BarlowSemiCondensedMedium_ttfSize);
    return face;
}

juce::Typeface::Ptr legendTypeface()
{
    static const auto face = load (mwAssets::BarlowSemiCondensedSemiBold_ttf, mwAssets::BarlowSemiCondensedSemiBold_ttfSize);
    return face;
}

juce::Typeface::Ptr boldTypeface()
{
    static const auto face = load (mwAssets::BarlowSemiCondensedBold_ttf, mwAssets::BarlowSemiCondensedBold_ttfSize);
    return face;
}

juce::Typeface::Ptr readoutTypeface()
{
    static const auto face = load (mwAssets::ShareTechMonoRegular_ttf, mwAssets::ShareTechMonoRegular_ttfSize);
    return face;
}

juce::Font text (float height)                    { return make (textTypeface(), height, 0.0f); }
juce::Font legend (float height, float tracking)  { return make (legendTypeface(), height, tracking); }
juce::Font bold (float height, float tracking)    { return make (boldTypeface(), height, tracking); }
juce::Font readout (float height)                 { return make (readoutTypeface(), height, 0.0f); }
}
