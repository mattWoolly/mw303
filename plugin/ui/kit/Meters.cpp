#include "kit/Meters.h"

namespace acid::ui
{
namespace
{
constexpr float kLevelDb[LevelMeter::kNumSegments] = { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -15.0f, -18.0f, -24.0f, -30.0f, -42.0f };
constexpr float kReductionDb[GainReductionMeter::kNumSegments] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 14.0f, 20.0f };
}

LevelMeter::LevelMeter()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    setSize (44, 96);
}

float LevelMeter::segmentDb (int index) noexcept
{
    return kLevelDb[juce::jlimit (0, kNumSegments - 1, index)];
}

int LevelMeter::litCount (float db) noexcept
{
    int n = 0;

    for (int i = 0; i < kNumSegments; ++i)
        if (db >= kLevelDb[i])
            ++n;

    return n;
}

void LevelMeter::setLevels (float leftDb, float rightDb)
{
    const auto l = litCount (leftDb), r = litCount (rightDb);

    if (l != litLeft || r != litRight)
    {
        litLeft = l;
        litRight = r;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat();
    g.fillAll (t.wellFill);
    g.setColour (t.wellOutline);
    g.drawRect (b, 1.0f);

    const auto inner = b.reduced (4.0f, 4.0f);
    const auto colW = juce::jmin (9.0f, (inner.getWidth() - 18.0f) * 0.5f);
    const auto segH = inner.getHeight() / static_cast<float> (kNumSegments);
    const auto leftX = inner.getX();
    const auto rightX = inner.getRight() - colW;
    const auto font = fonts::readout (10.0f);   // the dB scale stays legible at 75 % zoom

    for (int i = 0; i < kNumSegments; ++i)
    {
        const auto y = inner.getY() + segH * static_cast<float> (i);
        const auto db = kLevelDb[i];
        const auto colour = db >= 3.0f ? t.ledRed : (db >= 0.0f ? t.ledAmber : t.accent);
        const auto fromBottom = kNumSegments - i;

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto on = fromBottom <= (ch == 0 ? litLeft : litRight);
            g.setColour (on ? colour : colour.interpolatedWith (t.wellFill, 0.82f));
            g.fillRect (juce::Rectangle<float> (ch == 0 ? leftX : rightX, y + 0.75f, colW, segH - 1.5f));
        }

        if (i % 2 == 0)
        {
            const auto label = db > 0.0f ? "+" + juce::String (juce::roundToInt (db)) : juce::String (juce::roundToInt (std::abs (db)));
            draw::text (g, label, juce::Rectangle<float> (leftX + colW, y, rightX - leftX - colW, segH), font,
                        db >= 0.0f ? t.screenText : t.screenTextDim);
        }
    }
}

//==============================================================================
GainReductionMeter::GainReductionMeter()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    setSize (120, 14);
}

float GainReductionMeter::segmentDb (int index) noexcept
{
    return kReductionDb[juce::jlimit (0, kNumSegments - 1, index)];
}

void GainReductionMeter::setReductionDb (float db)
{
    int n = 0;

    for (int i = 0; i < kNumSegments; ++i)
        if (db >= kReductionDb[i])
            ++n;

    if (n != lit)
    {
        lit = n;
        repaint();
    }
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat();
    g.fillAll (t.wellFill);
    g.setColour (t.wellOutline);
    g.drawRect (b, 1.0f);

    auto inner = b.reduced (4.0f, 3.0f);
    draw::text (g, "GR", inner.removeFromLeft (16.0f), fonts::readout (10.0f), t.screenTextDim, juce::Justification::centredLeft);
    const auto segW = inner.getWidth() / static_cast<float> (kNumSegments);

    for (int i = 0; i < kNumSegments; ++i)
    {
        g.setColour (i < lit ? t.ledAmber : t.ledAmber.interpolatedWith (t.wellFill, 0.82f));
        g.fillRect (juce::Rectangle<float> (inner.getX() + segW * static_cast<float> (i) + 0.75f, inner.getY(), segW - 1.5f, inner.getHeight()));
    }
}

//==============================================================================
ChargeMeter::ChargeMeter()
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    setSize (64, 12);
}

void ChargeMeter::setCharge (float charge01)
{
    const auto n = juce::jlimit (0, kNumSegments, static_cast<int> (std::floor (juce::jlimit (0.0f, 1.0f, charge01) * kNumSegments + 0.5f)));

    if (n != lit)
    {
        lit = n;
        repaint();
    }
}

void ChargeMeter::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto b = getLocalBounds().toFloat();
    g.fillAll (t.wellFill);
    g.setColour (t.wellOutline);
    g.drawRect (b, 1.0f);

    const auto inner = b.reduced (3.0f, 2.5f);
    const auto segW = inner.getWidth() / static_cast<float> (kNumSegments);
    const auto on = t.led (colour);

    for (int i = 0; i < kNumSegments; ++i)
    {
        g.setColour (i < lit ? on : on.interpolatedWith (t.wellFill, 0.84f));
        g.fillRect (juce::Rectangle<float> (inner.getX() + segW * static_cast<float> (i) + 0.75f, inner.getY(), segW - 1.5f, inner.getHeight()));
    }
}
}
