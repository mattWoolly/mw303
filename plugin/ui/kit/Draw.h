#pragma once
// Shared vector primitives. Every kit component paints through these so the two themes
// stay consistent. No bitmaps: everything is juce::Graphics paths and fills.
#include "Theme.h"
#include "Fonts.h"

namespace acid::ui::draw
{
enum class Icon
{
    None, Prev, Next, Up, Down, Cog, Play, Stop, Dice, Saw, Square, ChevronDown, Grip, Export, Note, Save, Link
};

/** Rotary sweep shared by every knob: 270 degrees, symmetric about 12 o'clock. */
inline constexpr float kSweepStart = -2.35619449f;
inline constexpr float kSweepEnd = 2.35619449f;

/** An LED lens in its bezel. `area` is the bezel's bounding square; the glow spills outside it when on. */
void led (juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool on, const Theme&);

/** A recessed dark window: numeric readouts, switch slots, selector troughs. */
void well (juce::Graphics&, juce::Rectangle<float>, const Theme&, float corner = metrics::kCorner);

/** A raised key cap. */
void buttonFace (juce::Graphics&, juce::Rectangle<float>, const Theme&, bool down, bool hover,
                 float corner = metrics::kCorner);

/** The unlit groove a knob's value arc runs in. It never changes, so Knob keeps it in the
    Faceplate's static layer. */
void knobTrack (juce::Graphics&, juce::Point<float> centre, float arcRadius, float arcThickness, const Theme&);

/** Knob cap, pointer and value arc. `norm` is 0..1 along the sweep, `arcOrigin` is where the
    arc fill starts (0 for unipolar, 0.5 for bipolar controls). */
void knobValue (juce::Graphics&, juce::Point<float> centre, float capRadius, float arcRadius, float arcThickness,
                float norm, float arcOrigin, const Theme&, bool enabled, bool hover);

/** Track and value together, for knobs drawn outside the kit (stock sliders). */
void knob (juce::Graphics&, juce::Point<float> centre, float capRadius, float arcRadius, float arcThickness,
           float norm, float arcOrigin, const Theme&, bool enabled, bool hover);

/** Screwdriver trimmer: square body, slotted rotor. */
void trimmer (juce::Graphics&, juce::Rectangle<float> body, float norm, const Theme&, bool hover);

void screw (juce::Graphics&, juce::Point<float> centre, float radius, const Theme&);

void icon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);

void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, const juce::Font&, juce::Colour,
           juce::Justification = juce::Justification::centred);

/** Section body: fill, outline and a title sitting on a hairline rule. */
void sectionFrame (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, const Theme&);

/** The sequencer screen: a dark recessed panel. */
void screen (juce::Graphics&, juce::Rectangle<float>, const Theme&);

/** Soft drop shadow under a floating rectangle (tooltips, dragged tabs). */
void dropShadow (juce::Graphics&, juce::Rectangle<float>, const Theme&, float corner = metrics::kCorner);
}
