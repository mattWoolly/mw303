#include "kit/Faceplate.h"
#include "kit/SafeText.h"

namespace acid::ui
{
namespace
{
/** Draws the part of `image` (rendered at `scale` device pixels per unit, origin at the
    component's top-left) that lies under the clip. Only that part: JUCE builds an edge table
    as tall as the image it is handed, so blitting a whole plate for a knob-sized clip would
    cost far more than the copy. The transform is the exact inverse of the scale, so with the
    context's own scale it is a translation and the renderer takes its straight-copy path. */
void blitCached (juce::Graphics& g, const juce::Image& image, float scale)
{
    const auto device = (g.getClipBounds().toFloat() * scale).getSmallestIntegerContainer().getIntersection (image.getBounds());

    if (device.isEmpty())
        return;

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (image.getClippedImage (device),
                            juce::AffineTransform::translation (static_cast<float> (device.getX()), static_cast<float> (device.getY()))
                                .scaled (1.0f / scale));
}

int devicePixels (int designUnits, float scale) noexcept
{
    return juce::jmax (1, juce::roundToInt (std::ceil (static_cast<float> (designUnits) * scale)));
}

int bufferRenders = 0;

/** The buffer behind Faceplate::bufferPaint. Like JUCE's own StandardCachedComponentImage
    it keeps a list of the still-valid area and re-renders only what repaint() invalidated;
    unlike it, the image is sized by ceil (size * scale) and blitted with scale 1 / scale. */
class PaintCache final : public juce::CachedComponentImage
{
public:
    explicit PaintCache (juce::Component& c) : owner (c) {}

    void paint (juce::Graphics& g) override
    {
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto bounds = owner.getLocalBounds();
        const auto w = devicePixels (bounds.getWidth(), scale);
        const auto h = devicePixels (bounds.getHeight(), scale);
        const auto format = owner.isOpaque() ? juce::Image::RGB : juce::Image::ARGB;

        if (! image.isValid() || image.getWidth() != w || image.getHeight() != h || image.getFormat() != format
            || std::abs (scale - imageScale) > 1.0e-4f)
        {
            image = juce::Image (format, w, h, false);
            imageScale = scale;
            valid.clear();
        }

        if (! valid.containsRectangle (bounds))
        {
            juce::Graphics ig (image);
            auto& lg = ig.getInternalContext();
            lg.addTransform (juce::AffineTransform::scale (scale));

            for (const auto& r : valid)
                lg.excludeClipRectangle (r);

            if (! owner.isOpaque())
            {
                lg.setFill (juce::Colours::transparentBlack);
                lg.fillRect (bounds, true);
                lg.setFill (juce::Colours::black);
            }

            owner.paintEntireComponent (ig, true);
            valid.clear();
            valid.add (bounds);
            ++bufferRenders;
        }

        g.setColour (juce::Colours::black.withAlpha (owner.getAlpha()));
        blitCached (g, image, scale);
    }

    bool invalidateAll() override
    {
        valid.clear();
        return true;
    }

    bool invalidate (const juce::Rectangle<int>& area) override
    {
        valid.subtract (area);
        return true;
    }

    void releaseResources() override
    {
        image = {};
        valid.clear();
    }

private:
    juce::Component& owner;
    juce::Image image;
    juce::RectangleList<int> valid;
    float imageScale = 0.0f;
};
}

//==============================================================================
void StaticArt::paintStaticArtIfUncached (juce::Component& self, juce::Graphics& g)
{
    auto* plate = Faceplate::find (self);

    if (plate == nullptr || plate == &self)
        paintStaticArt (g, themeFor (self));
    else if (self.getParentComponent() == plate)
        plate->paintStaticTile (self, g);
}

//==============================================================================
ValueTooltip::ValueTooltip()
{
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
    setAlwaysOnTop (true);
    setVisible (false);
}

void ValueTooltip::showFor (juce::Component& target, const juce::String& name, const juce::String& value)
{
    auto* parent = getParentComponent();

    if (parent == nullptr)
        return;

    owner = &target;
    const auto cleanName = text::sanitise (name), cleanValue = text::sanitise (value);
    const auto changed = cleanName != nameText || cleanValue != valueText;
    nameText = cleanName;
    valueText = cleanValue;

    // Measured from the glyphs' own extents (the advance width alone can leave the last
    // glyph's overhang outside the box), with slack, and capped to the plate: anything longer
    // is drawn with an ellipsis rather than cut.
    const auto nameW = nameText.isEmpty() ? 0.0f : measure (nameFont(), nameText.toUpperCase()) + 10.0f;
    const auto valueW = measure (valueFont(), valueText);
    const auto w = juce::jmin (juce::roundToInt (nameW + valueW + 22.0f), juce::jmax (40, parent->getWidth() - 4));
    const auto h = 22;

    const auto area = parent->getLocalArea (&target, target.getLocalBounds());
    auto x = area.getCentreX() - w / 2;
    auto y = area.getY() - h - 4;

    if (y < 2)
        y = area.getBottom() + 4;

    x = juce::jlimit (2, juce::jmax (2, parent->getWidth() - w - 2), x);
    const juce::Rectangle<int> bounds (x, y, w, h);

    if (bounds != getBounds())
        setBounds (bounds);
    else if (changed)
        repaint();

    setVisible (true);
    toFront (false);
}

juce::Font ValueTooltip::nameFont() { return fonts::legend (11.0f); }
juce::Font ValueTooltip::valueFont() { return fonts::readout (13.0f); }

float ValueTooltip::measure (const juce::Font& font, const juce::String& s)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, s, 0.0f, 0.0f);
    const auto box = glyphs.getBoundingBox (0, -1, true);
    return std::ceil (juce::jmax (box.getRight(), juce::GlyphArrangement::getStringWidth (font, s))) + 2.0f;
}

void ValueTooltip::hideFor (const juce::Component* target)
{
    if (target == nullptr || owner == target)
    {
        owner = nullptr;
        setVisible (false);
    }
}

void ValueTooltip::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto r = getLocalBounds().toFloat();
    g.setColour (t.menuFill);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (t.menuOutline);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

    auto inner = r.reduced (9.0f, 0.0f);
    const auto nameW = nameText.isEmpty() ? 0.0f : juce::jmin (inner.getWidth() * 0.5f, measure (nameFont(), nameText.toUpperCase()) + 10.0f);

    if (nameText.isNotEmpty())
    {
        g.setColour (t.menuTextDim);
        g.setFont (nameFont());
        g.drawText (nameText.toUpperCase(), inner.removeFromLeft (nameW), juce::Justification::centredLeft, true);
    }

    g.setColour (t.wellText);
    g.setFont (valueFont());
    g.drawText (valueText, inner, juce::Justification::centredRight, true);   // ellipsis if it still does not fit
}

//==============================================================================
Faceplate::Faceplate()
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    addChildComponent (tooltip);
}

Faceplate::~Faceplate() = default;

Faceplate* Faceplate::find (juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (auto* f = dynamic_cast<Faceplate*> (p))
            return f;

    return nullptr;
}

int Faceplate::getBufferRenderCount() noexcept
{
    return bufferRenders;
}

void Faceplate::bufferPaint (juce::Component& c)
{
    if (dynamic_cast<PaintCache*> (c.getCachedComponentImage()) == nullptr)
        c.setCachedComponentImage (new PaintCache (c));
}

void Faceplate::invalidateStaticArt()
{
    background = {};
    backgroundScale = 0.0f;
    tiles.clear();
    repaint();

    // The panels' buffers hold the old artwork too.
    for (auto* child : getChildren())
        if (dynamic_cast<StaticArt*> (child) != nullptr)
            child->repaint();
}

void Faceplate::invalidateStaticArtFor (juce::Component& c)
{
    auto* f = find (c);

    if (f == nullptr)
        return;

    if (f == &c)
    {
        f->invalidateStaticArt();
        return;
    }

    auto* panel = &c;

    while (panel->getParentComponent() != f)
        panel = panel->getParentComponent();

    f->tiles.erase (panel);
    panel->repaint();
}

void Faceplate::childrenChanged()
{
    // Every panel (a direct child with static art) is opaque, since its tile fills its
    // bounds, and buffered. Tiles of children that have gone are dropped.
    for (auto* child : getChildren())
        if (dynamic_cast<StaticArt*> (child) != nullptr)
        {
            child->setOpaque (true);
            bufferPaint (*child);
        }

    for (auto it = tiles.begin(); it != tiles.end();)
        it = it->first->getParentComponent() != this ? tiles.erase (it) : std::next (it);
}

void Faceplate::showValueTip (juce::Component& target, const juce::String& name, const juce::String& value)
{
    if (entry == nullptr)
        tooltip.showFor (target, name, value);
}

void Faceplate::hideValueTip (const juce::Component* target)
{
    tooltip.hideFor (target);
}

void Faceplate::beginTextEntry (juce::Component& target, const juce::String& initialText,
                                std::function<void (const juce::String&)> onCommit)
{
    endTextEntry();
    tooltip.hideFor (nullptr);

    const auto& t = themeFor (*this);
    entry = std::make_unique<juce::TextEditor>();
    entryCommit = std::move (onCommit);

    auto area = getLocalArea (&target, target.getLocalBounds());
    auto box = area.withSizeKeepingCentre (juce::jmax (84, juce::jmin (area.getWidth(), 140)), 24);
    box.setX (juce::jlimit (2, juce::jmax (2, getWidth() - box.getWidth() - 2), box.getX()));
    box.setY (juce::jlimit (2, juce::jmax (2, getHeight() - box.getHeight() - 2), box.getY()));

    entry->setFont (fonts::readout (14.0f));
    entry->setJustification (juce::Justification::centred);
    entry->setColour (juce::TextEditor::backgroundColourId, t.wellFill);
    entry->setColour (juce::TextEditor::textColourId, t.wellText);
    entry->setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
    entry->setColour (juce::TextEditor::highlightedTextColourId, t.accentBright);
    entry->setColour (juce::TextEditor::outlineColourId, t.accent);
    entry->setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    entry->setColour (juce::CaretComponent::caretColourId, t.accentBright);
    entry->setText (text::sanitise (initialText), false);
    entry->setSelectAllWhenFocused (true);
    entry->setBounds (box);
    addAndMakeVisible (*entry);
    entry->setAlwaysOnTop (true);

    entry->onReturnKey = [this]
    {
        const auto text = entry != nullptr ? entry->getText() : juce::String();
        auto commit = std::move (entryCommit);
        endTextEntry();

        if (commit != nullptr)
            commit (text);
    };
    entry->onEscapeKey = [this] { endTextEntry(); };
    entry->onFocusLost = [this] { endTextEntry(); };

    entry->grabKeyboardFocus();
}

void Faceplate::endTextEntry()
{
    if (entry == nullptr)
        return;

    // Detach the callbacks first: removing a focused editor fires onFocusLost re-entrantly.
    auto dying = std::move (entry);
    dying->onFocusLost = nullptr;
    dying->onReturnKey = nullptr;
    dying->onEscapeKey = nullptr;
    entryCommit = nullptr;
    removeChildComponent (dying.get());
    juce::Component::unfocusAllComponents();
}

void Faceplate::resized()
{
    invalidateStaticArt();
}

void Faceplate::lookAndFeelChanged()
{
    invalidateStaticArt();
}

//==============================================================================
void Faceplate::paintStaticChildren (juce::Graphics& g, juce::Component& parent, const Theme& t)
{
    for (auto* child : parent.getChildren())
    {
        if (! child->isVisible() || child == &tooltip)
            continue;

        juce::Graphics::ScopedSaveState state (g);

        if (! g.reduceClipRegion (child->getBounds()))
            continue;

        g.setOrigin (child->getPosition());

        if (auto* art = dynamic_cast<StaticArt*> (child))
            art->paintStaticArt (g, t);

        paintStaticChildren (g, *child, t);
    }
}

void Faceplate::renderBackground (float scale)
{
    background = juce::Image (juce::Image::RGB, devicePixels (getWidth(), scale), devicePixels (getHeight(), scale), false);
    backgroundScale = scale;
    ++renderCount;

    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));
    paintPlate (g, themeFor (*this));
}

void Faceplate::renderTile (Tile& tile, juce::Component& panel, float scale)
{
    const auto bounds = panel.getBounds();
    tile.image = juce::Image (juce::Image::RGB, devicePixels (bounds.getWidth(), scale), devicePixels (bounds.getHeight(), scale), false);
    tile.bounds = bounds;
    tile.scale = scale;
    ++renderCount;

    // The plate under the panel (plate coordinates, clipped to the panel), then the panel's
    // own artwork and its descendants' in panel coordinates.
    juce::Graphics g (tile.image);
    g.addTransform (juce::AffineTransform::scale (scale));
    const auto& t = themeFor (*this);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setOrigin (-bounds.getPosition());
        g.reduceClipRegion (bounds);
        paintPlate (g, t);
    }

    if (auto* art = dynamic_cast<StaticArt*> (&panel))
        art->paintStaticArt (g, t);

    paintStaticChildren (g, panel, t);
}

void Faceplate::paintStaticTile (juce::Component& panel, juce::Graphics& g)
{
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    auto& tile = tiles[&panel];

    if (! tile.image.isValid() || std::abs (scale - tile.scale) > 1.0e-4f || tile.bounds != panel.getBounds())
        renderTile (tile, panel, scale);

    g.setOpacity (1.0f);
    blitCached (g, tile.image, scale);
}

void Faceplate::paint (juce::Graphics& g)
{
    // Only what no opaque panel covers is drawn: the gaps between panels and the margins.
    // JUCE narrows a parent's clip to the bounding box of what its opaque children leave,
    // which here is the whole plate, so the panels are cut out of the clip by hand (at 2x
    // that is the difference between a 0.8 ms and a 4.7 ms blit).
    for (auto* child : getChildren())
        if (child->isVisible() && child->isOpaque() && ! child->isTransformed())
            g.excludeClipRegion (child->getBounds());

    if (g.isClipEmpty())
        return;

    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    if (! background.isValid() || std::abs (scale - backgroundScale) > 1.0e-4f)
        renderBackground (scale);

    g.setOpacity (1.0f);
    blitCached (g, background, scale);
}
}
