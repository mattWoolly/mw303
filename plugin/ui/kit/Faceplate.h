#pragma once
// The root of an editor built from the kit.
//
// A Faceplate does three jobs for everything inside it:
//   1. Static artwork cache, one tile per panel. Every direct child that implements StaticArt
//      gets a tile: the plate background under it plus its own and its descendants' fixed
//      artwork, rendered once per (zoom, theme, layout) into an RGB image. The child is made
//      opaque and its paint is buffered (bufferPaint), so a full repaint of the editor is a
//      handful of blits, a knob repaint re-renders only the knob's area of its panel's buffer
//      over the tile, and a change to one panel's artwork rebuilds that tile alone. The
//      plate's own paint() fills what no panel covers from a background image.
//   2. The value tooltip, drawn inside the editor (no extra X11 window).
//   3. Typed value entry: the only place the editor ever takes keyboard focus.
#include "kit/Draw.h"
#include <functional>
#include <map>

namespace acid::ui
{
class Faceplate;

/** Implemented by components whose fixed artwork should live in the Faceplate's cache. */
class StaticArt
{
public:
    virtual ~StaticArt() = default;

    /** Paints the unchanging artwork. The origin is the component's top-left. */
    virtual void paintStaticArt (juce::Graphics&, const Theme&) = 0;

    /** Call from paint(). Outside a Faceplate it draws the static art directly, so the
        component also works on its own. Inside one, a direct child of the plate blits its
        tile and a nested component draws nothing (its art is in the enclosing tile). */
    void paintStaticArtIfUncached (juce::Component& self, juce::Graphics&);
};

/** Floating readout shown while a control is hovered or dragged. */
class ValueTooltip final : public juce::Component
{
public:
    ValueTooltip();
    void showFor (juce::Component& target, const juce::String& name, const juce::String& value);
    void hideFor (const juce::Component* target);
    void paint (juce::Graphics&) override;
    const juce::String& getValueText() const noexcept { return valueText; }

private:
    static juce::Font nameFont();
    static juce::Font valueFont();
    static float measure (const juce::Font&, const juce::String&);

    juce::Component::SafePointer<juce::Component> owner;
    juce::String nameText, valueText;
};

class Faceplate : public juce::Component
{
public:
    Faceplate();
    ~Faceplate() override;

    /** Nearest Faceplate at or above a component, or nullptr. */
    static Faceplate* find (juce::Component&);

    /** Drops every tile and the background. Call after a layout or theme change. */
    void invalidateStaticArt();

    /** Drops the tile of the panel that holds `c` and repaints that panel only. Call after
        changing the fixed artwork of one panel. For the plate itself it drops everything. */
    static void invalidateStaticArtFor (juce::Component&);

    /** Paints `panel`'s tile (rendering it first when needed) in the panel's own coordinates.
        StaticArt::paintStaticArtIfUncached calls this for a direct child of the plate. */
    void paintStaticTile (juce::Component& panel, juce::Graphics&);

    /** Keeps a component's rendered paint (and its children's) in an image at the physical
        pixel scale, so repainting it costs a blit until something inside it repaints. Unlike
        juce::Component::setBufferedToImage the image is blitted back with the exact inverse
        of the scale, so it is a straight copy, never resampled, at every zoom step. */
    static void bufferPaint (juce::Component&);

    void showValueTip (juce::Component& target, const juce::String& name, const juce::String& value);
    void hideValueTip (const juce::Component* target);
    ValueTooltip& getValueTooltip() noexcept { return tooltip; }

    /** Opens a one-line editor over `target`. Return commits, Escape or loss of focus cancels.
        This is the only code path in the kit that takes keyboard focus. */
    void beginTextEntry (juce::Component& target, const juce::String& initialText,
                         std::function<void (const juce::String&)> onCommit);
    void endTextEntry();
    bool isTextEntryActive() const noexcept { return entry != nullptr; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void childrenChanged() override;

    /** Number of tile and background renders so far (for tests and the gallery report). A
        parameter change, a tab switch or a hover must not raise it for unrelated panels. */
    int getStaticRenderCount() const noexcept { return renderCount; }

    /** Process-wide count of buffer (bufferPaint) renders, whole or partial, for tests: a
        repaint that nothing invalidated must not raise it. */
    static int getBufferRenderCount() noexcept;

protected:
    /** The plate itself: background, bevels, anything not owned by a child. */
    virtual void paintPlate (juce::Graphics&, const Theme&) = 0;

private:
    struct Tile
    {
        juce::Image image;
        juce::Rectangle<int> bounds;
        float scale = 0.0f;
    };

    void renderBackground (float scale);
    void renderTile (Tile&, juce::Component& panel, float scale);
    void paintStaticChildren (juce::Graphics&, juce::Component& parent, const Theme&);

    juce::Image background;
    float backgroundScale = 0.0f;
    std::map<juce::Component*, Tile> tiles;
    int renderCount = 0;
    ValueTooltip tooltip;
    std::unique_ptr<juce::TextEditor> entry;
    std::function<void (const juce::String&)> entryCommit;
};
}
