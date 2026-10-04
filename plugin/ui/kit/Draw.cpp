#include "kit/Draw.h"
#include "kit/SafeText.h"

namespace acid::ui::draw
{
namespace
{
juce::Point<float> polar (juce::Point<float> c, float radius, float angle) noexcept
{
    return { c.x + radius * std::sin (angle), c.y - radius * std::cos (angle) };
}

float angleFor (float norm) noexcept
{
    return kSweepStart + juce::jlimit (0.0f, 1.0f, norm) * (kSweepEnd - kSweepStart);
}
}

void led (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool on, const Theme& t)
{
    const auto d = juce::jmin (area.getWidth(), area.getHeight());
    const auto bezel = area.withSizeKeepingCentre (d, d);
    const auto c = bezel.getCentre();

    if (on)
    {
        const auto glowR = d * 1.05f;
        juce::ColourGradient glow (colour.withAlpha (0.55f), c.x, c.y, colour.withAlpha (0.0f), c.x + glowR, c.y, true);
        glow.addColour (0.45, colour.withAlpha (0.28f));
        g.setGradientFill (glow);
        g.fillEllipse (bezel.withSizeKeepingCentre (glowR * 2.0f, glowR * 2.0f));
    }

    g.setColour (t.ledBezel);
    g.fillEllipse (bezel);

    const auto lens = bezel.reduced (juce::jmax (0.8f, d * 0.09f));

    if (on)
    {
        juce::ColourGradient fill (colour.interpolatedWith (juce::Colours::white, 0.65f), c.x, c.y - d * 0.08f,
                                   colour, c.x, c.y + lens.getHeight() * 0.55f, true);
        g.setGradientFill (fill);
        g.fillEllipse (lens);
    }
    else
    {
        g.setColour (colour.interpolatedWith (t.ledBezel, 0.72f));
        g.fillEllipse (lens);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillEllipse (lens.withSizeKeepingCentre (lens.getWidth() * 0.45f, lens.getHeight() * 0.35f)
                           .translated (0.0f, -lens.getHeight() * 0.18f));
    }
}

void well (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, float corner)
{
    g.setColour (t.wellFill);
    g.fillRoundedRectangle (r, corner);

    // Inner shade along the top edge sells the recess; a light lip below catches the panel light.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.withHeight (juce::jmin (2.0f, r.getHeight())), corner);
    g.setColour (t.wellOutline);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.8f : 0.18f));
    g.fillRect (r.getX() + corner, r.getBottom(), r.getWidth() - 2.0f * corner, 1.0f);
}

void buttonFace (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, bool down, bool hover, float corner)
{
    if (! down)
    {
        g.setColour (juce::Colours::black.withAlpha (t.id == ThemeId::Silver ? 0.16f : 0.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
    }

    auto top = down ? t.buttonDownTop : t.buttonTop;
    auto bottom = down ? t.buttonDownBottom : t.buttonBottom;

    if (hover && ! down)
    {
        top = top.brighter (0.12f);
        bottom = bottom.brighter (0.12f);
    }

    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (t.buttonOutline);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (t.id == ThemeId::Silver ? 0.7f : 0.10f));
        g.fillRect (r.getX() + corner, r.getY() + 1.0f, r.getWidth() - 2.0f * corner, 1.0f);
    }
}

namespace
{
/** A ring segment with round ends, built as one fill. Cheaper than stroking an arc path. */
juce::Path arcBand (juce::Point<float> c, float radius, float thickness, float from, float to)
{
    juce::Path p;
    const auto outer = radius + thickness * 0.5f, inner = radius - thickness * 0.5f;
    p.addCentredArc (c.x, c.y, outer, outer, 0.0f, from, to, true);
    const auto endCap = polar (c, radius, to);
    p.addCentredArc (endCap.x, endCap.y, thickness * 0.5f, thickness * 0.5f, to, 0.0f, juce::MathConstants<float>::pi, false);
    p.addCentredArc (c.x, c.y, inner, inner, 0.0f, to, from, false);
    const auto startCap = polar (c, radius, from);
    p.addCentredArc (startCap.x, startCap.y, thickness * 0.5f, thickness * 0.5f, from, juce::MathConstants<float>::pi,
                     juce::MathConstants<float>::twoPi, false);
    p.closeSubPath();
    return p;
}
}

void knobTrack (juce::Graphics& g, juce::Point<float> c, float arcRadius, float arcThickness, const Theme& t)
{
    g.setColour (t.arcTrack);
    g.fillPath (arcBand (c, arcRadius, arcThickness, kSweepStart, kSweepEnd));
}

void knob (juce::Graphics& g, juce::Point<float> c, float capRadius, float arcRadius, float arcThickness,
           float norm, float arcOrigin, const Theme& t, bool enabled, bool hover)
{
    knobTrack (g, c, arcRadius, arcThickness, t);
    knobValue (g, c, capRadius, arcRadius, arcThickness, norm, arcOrigin, t, enabled, hover);
}

void knobValue (juce::Graphics& g, juce::Point<float> c, float capRadius, float arcRadius, float arcThickness,
                float norm, float arcOrigin, const Theme& t, bool enabled, bool hover)
{
    const auto angle = angleFor (norm);

    if (enabled)
    {
        const auto from = angleFor (arcOrigin);

        if (std::abs (angle - from) > 0.01f)
        {
            g.setColour (t.arcFill);
            g.fillPath (arcBand (c, arcRadius, arcThickness, juce::jmin (from, angle), juce::jmax (from, angle)));
        }
        else if (arcOrigin > 0.0f)
        {
            // A bipolar control at rest still shows where its centre is.
            const auto p = polar (c, arcRadius, from);
            g.setColour (t.arcFill);
            g.fillEllipse (p.x - arcThickness * 0.5f, p.y - arcThickness * 0.5f, arcThickness, arcThickness);
        }
    }

    // Contact shadow, then skirt, cap and pointer.
    g.setColour (juce::Colours::black.withAlpha (t.id == ThemeId::Silver ? 0.20f : 0.40f));
    g.fillEllipse (c.x - capRadius - 1.0f, c.y - capRadius + 1.5f, (capRadius + 1.0f) * 2.0f, (capRadius + 1.0f) * 2.0f);

    g.setGradientFill (juce::ColourGradient (t.knobSkirtTop, 0.0f, c.y - capRadius, t.knobSkirtBottom, 0.0f, c.y + capRadius, false));
    g.fillEllipse (c.x - capRadius, c.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);

    const auto top = capRadius - juce::jmax (1.5f, capRadius * 0.11f);
    auto capTop = t.knobCapTop;
    auto capBottom = t.knobCapBottom;

    if (hover)
    {
        capTop = capTop.brighter (0.10f);
        capBottom = capBottom.brighter (0.10f);
    }

    g.setGradientFill (juce::ColourGradient (capTop, 0.0f, c.y - top, capBottom, 0.0f, c.y + top, false));
    g.fillEllipse (c.x - top, c.y - top, top * 2.0f, top * 2.0f);
    g.setColour (t.knobRim.withAlpha (0.55f));
    g.drawEllipse (c.x - top + 0.5f, c.y - top + 0.5f, top * 2.0f - 1.0f, top * 2.0f - 1.0f, 1.0f);

    const auto p0 = polar (c, capRadius * 0.30f, angle);
    const auto p1 = polar (c, capRadius * 0.86f, angle);
    g.setColour (enabled ? t.knobPointer : t.knobPointer.withAlpha (0.35f));
    g.drawLine ({ p0, p1 }, juce::jmax (2.0f, capRadius * 0.14f));
    const auto tip = juce::jmax (2.0f, capRadius * 0.14f);
    g.fillEllipse (p1.x - tip * 0.5f, p1.y - tip * 0.5f, tip, tip);
    g.fillEllipse (p0.x - tip * 0.5f, p0.y - tip * 0.5f, tip, tip);
}

void trimmer (juce::Graphics& g, juce::Rectangle<float> body, float norm, const Theme& t, bool hover)
{
    const auto c = body.getCentre();
    const auto r = juce::jmin (body.getWidth(), body.getHeight()) * 0.5f;

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (body.translated (0.0f, 1.0f), 3.0f);
    g.setColour (hover ? t.trimBody.brighter (0.15f) : t.trimBody);
    g.fillRoundedRectangle (body, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (body.reduced (0.5f), 3.0f, 1.0f);

    const auto rotor = r * 0.74f;
    g.setGradientFill (juce::ColourGradient (t.trimRotor, 0.0f, c.y - rotor, t.trimRotor.darker (0.25f), 0.0f, c.y + rotor, false));
    g.fillEllipse (c.x - rotor, c.y - rotor, rotor * 2.0f, rotor * 2.0f);

    const auto angle = angleFor (norm);
    g.setColour (t.trimSlot);
    g.drawLine ({ polar (c, rotor * 0.82f, angle), polar (c, rotor * 0.82f, angle + juce::MathConstants<float>::pi) }, 2.4f);

    // Arrow head on the pointing end of the slot.
    juce::Path head;
    head.addTriangle (polar (c, rotor * 0.98f, angle), polar (c, rotor * 0.45f, angle - 0.62f), polar (c, rotor * 0.45f, angle + 0.62f));
    g.fillPath (head);
}

void screw (juce::Graphics& g, juce::Point<float> c, float radius, const Theme& t)
{
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (c.x - radius, c.y - radius + 0.8f, radius * 2.0f, radius * 2.0f);
    g.setGradientFill (juce::ColourGradient (t.plateEdgeLight, 0.0f, c.y - radius, t.plateEdgeDark, 0.0f, c.y + radius, false));
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (t.plateEdgeDark.darker (0.5f));
    g.drawLine ({ polar (c, radius * 0.72f, 0.9f), polar (c, radius * 0.72f, 0.9f + juce::MathConstants<float>::pi) }, juce::jmax (1.0f, radius * 0.28f));
}

void icon (juce::Graphics& g, Icon which, juce::Rectangle<float> area, juce::Colour colour)
{
    // Waveform glyphs use the whole (wide) area; everything else is drawn in a centred square.
    const auto wide = which == Icon::Saw || which == Icon::Square;
    const auto s = juce::jmin (area.getWidth(), area.getHeight());
    const auto r = wide ? area : area.withSizeKeepingCentre (s, s);
    const auto x = r.getX(), y = r.getY();
    juce::Path p;
    g.setColour (colour);

    const auto at = [&] (float u, float v) { return juce::Point<float> (x + u * r.getWidth(), y + v * r.getHeight()); };
    const juce::PathStrokeType line (juce::jmax (1.4f, s * 0.13f), juce::PathStrokeType::mitered, juce::PathStrokeType::rounded);

    switch (which)
    {
        case Icon::None:
            return;

        case Icon::Prev:  p.addTriangle (at (0.70f, 0.18f), at (0.70f, 0.82f), at (0.24f, 0.50f)); g.fillPath (p); return;
        case Icon::Next:  p.addTriangle (at (0.30f, 0.18f), at (0.30f, 0.82f), at (0.76f, 0.50f)); g.fillPath (p); return;
        case Icon::Up:    p.addTriangle (at (0.18f, 0.70f), at (0.82f, 0.70f), at (0.50f, 0.24f)); g.fillPath (p); return;
        case Icon::Down:  p.addTriangle (at (0.18f, 0.30f), at (0.82f, 0.30f), at (0.50f, 0.76f)); g.fillPath (p); return;
        case Icon::Play:  p.addTriangle (at (0.26f, 0.14f), at (0.26f, 0.86f), at (0.86f, 0.50f)); g.fillPath (p); return;
        case Icon::Stop:  g.fillRect (r.reduced (s * 0.2f)); return;

        case Icon::ChevronDown:
            p.startNewSubPath (at (0.22f, 0.36f));
            p.lineTo (at (0.50f, 0.66f));
            p.lineTo (at (0.78f, 0.36f));
            g.strokePath (p, line);
            return;

        case Icon::Saw:
            p.startNewSubPath (at (0.04f, 0.92f));
            p.lineTo (at (0.50f, 0.08f));
            p.lineTo (at (0.50f, 0.92f));
            p.lineTo (at (0.96f, 0.08f));
            p.lineTo (at (0.96f, 0.92f));
            g.strokePath (p, line);
            return;

        case Icon::Square:
            p.startNewSubPath (at (0.04f, 0.92f));
            p.lineTo (at (0.04f, 0.08f));
            p.lineTo (at (0.50f, 0.08f));
            p.lineTo (at (0.50f, 0.92f));
            p.lineTo (at (0.96f, 0.92f));
            p.lineTo (at (0.96f, 0.08f));
            g.strokePath (p, line);
            return;

        case Icon::Cog:
        {
            // Eight flat-topped teeth around a round bore, as one even-odd outline.
            const auto c = r.getCentre();
            const auto outer = s * 0.50f, root = s * 0.37f;
            const auto pitch = juce::MathConstants<float>::twoPi / 8.0f;
            bool first = true;

            for (int i = 0; i < 8; ++i)
            {
                const auto a = pitch * static_cast<float> (i);

                for (const auto& [radius, offset] : { std::pair (root, -0.27f), std::pair (outer, -0.17f), std::pair (outer, 0.17f), std::pair (root, 0.27f) })
                {
                    const auto pt = polar (c, radius, a + offset);

                    if (first)
                        p.startNewSubPath (pt);
                    else
                        p.lineTo (pt);

                    first = false;
                }
            }

            p.closeSubPath();
            p.addEllipse (c.x - s * 0.17f, c.y - s * 0.17f, s * 0.34f, s * 0.34f);
            p.setUsingNonZeroWinding (false);
            g.fillPath (p);
            return;
        }

        case Icon::Dice:
        {
            g.drawRoundedRectangle (r.reduced (s * 0.12f), s * 0.16f, juce::jmax (1.2f, s * 0.11f));
            const auto dot = s * 0.15f;

            for (auto pt : { at (0.34f, 0.34f), at (0.66f, 0.66f), at (0.66f, 0.34f), at (0.34f, 0.66f) })
                g.fillEllipse (pt.x - dot * 0.5f, pt.y - dot * 0.5f, dot, dot);

            return;
        }

        case Icon::Grip:
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 2; ++j)
                {
                    const auto pt = at (0.36f + 0.28f * static_cast<float> (j), 0.22f + 0.28f * static_cast<float> (i));
                    g.fillEllipse (pt.x - s * 0.07f, pt.y - s * 0.07f, s * 0.14f, s * 0.14f);
                }

            return;

        case Icon::Export:
            p.startNewSubPath (at (0.50f, 0.66f));
            p.lineTo (at (0.50f, 0.12f));
            p.startNewSubPath (at (0.28f, 0.32f));
            p.lineTo (at (0.50f, 0.10f));
            p.lineTo (at (0.72f, 0.32f));
            p.startNewSubPath (at (0.14f, 0.56f));
            p.lineTo (at (0.14f, 0.88f));
            p.lineTo (at (0.86f, 0.88f));
            p.lineTo (at (0.86f, 0.56f));
            g.strokePath (p, line);
            return;

        case Icon::Note:
            g.fillEllipse (x + s * 0.16f, y + s * 0.60f, s * 0.36f, s * 0.28f);
            g.fillRect (x + s * 0.45f, y + s * 0.12f, s * 0.09f, s * 0.62f);
            p.addTriangle (at (0.52f, 0.12f), at (0.86f, 0.30f), at (0.52f, 0.36f));
            g.fillPath (p);
            return;

        case Icon::Save:
            p.startNewSubPath (at (0.50f, 0.10f));
            p.lineTo (at (0.50f, 0.62f));
            p.startNewSubPath (at (0.28f, 0.42f));
            p.lineTo (at (0.50f, 0.64f));
            p.lineTo (at (0.72f, 0.42f));
            p.startNewSubPath (at (0.14f, 0.86f));
            p.lineTo (at (0.86f, 0.86f));
            g.strokePath (p, line);
            return;

        case Icon::Link:
            g.drawRoundedRectangle (x + s * 0.06f, y + s * 0.34f, s * 0.50f, s * 0.32f, s * 0.16f, juce::jmax (1.2f, s * 0.12f));
            g.drawRoundedRectangle (x + s * 0.44f, y + s * 0.34f, s * 0.50f, s * 0.32f, s * 0.16f, juce::jmax (1.2f, s * 0.12f));
            return;
    }
}

void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const juce::Font& f, juce::Colour c,
           juce::Justification j)
{
    g.setColour (c);
    g.setFont (f);
    g.drawText (acid::ui::text::sanitise (s), r, j, false);
}

void sectionFrame (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const Theme& t)
{
    const auto corner = metrics::kSectionCorner;

    // A shallow machined pocket: light lip below, shade above.
    g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.9f : 0.20f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
    g.setColour (t.sectionFill);
    g.fillRoundedRectangle (r, corner);
    g.setColour (t.sectionOutline);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

    if (title.isNotEmpty())
    {
        const auto font = fonts::bold (metrics::kTitleHeight, 0.14f);
        const auto titleW = juce::GlyphArrangement::getStringWidth (font, title);
        const auto row = r.withHeight (20.0f).reduced (10.0f, 0.0f);
        text (g, title, row.withTrimmedTop (2.0f), font, t.legend, juce::Justification::centredLeft);

        g.setColour (t.sectionRule);
        g.fillRect (row.getX() + titleW + 8.0f, row.getCentreY() + 0.5f, row.getWidth() - titleW - 8.0f, 1.0f);
    }
}

void screen (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t)
{
    const auto corner = metrics::kSectionCorner;
    g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.9f : 0.16f));
    g.fillRoundedRectangle (r.translated (0.0f, 1.0f), corner);
    g.setColour (t.screenFill);
    g.fillRoundedRectangle (r, corner);
    g.setColour (t.wellOutline);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void dropShadow (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, float corner)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (t.shadow.withMultipliedAlpha (0.28f));
        g.fillRoundedRectangle (r.expanded (static_cast<float> (i)).translated (0.0f, 2.0f), corner + static_cast<float> (i));
    }
}
}
