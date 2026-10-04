#include "panels/Panels.h"

namespace acid::ui
{
namespace
{
/** Clears the title rule behind a control that sits in a section's title row. */
void clearTitleRule (juce::Graphics& g, const Theme& t, juce::Rectangle<int> area, juce::Colour fill)
{
    juce::ignoreUnused (t);
    g.setColour (fill);
    g.fillRect (area.expanded (5, 0).withTrimmedTop (2));
}
}

//==============================================================================
TopBar::TopBar (ControlSet& controls) : playMode (controls.selector (kPlayMode))
{
    presetBox.setTextWhenNothingSelected ("Init");

    zoomBox.setItems ({ "75 %", "100 %", "125 %", "150 %", "175 %", "200 %" });
    zoomBox.setSelectedItemIndex (1, juce::dontSendNotification);
    zoomBox.onChange = [this]
    {
        if (onZoomChanged != nullptr)
            onZoomChanged (0.75f + 0.25f * static_cast<float> (zoomBox.getSelectedItemIndex()));
    };

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &zoomBox, &prevPreset, &nextPreset, &savePreset, &settingsButton, &playMode })
        addAndMakeVisible (c);
}

void TopBar::setZoom (float zoom)
{
    zoomBox.setSelectedItemIndex (juce::jlimit (0, 5, juce::roundToInt ((zoom - 0.75f) / 0.25f)), juce::dontSendNotification);
}

void TopBar::resized()
{
    const auto w = getWidth();
    const auto y = 12, h = 24;
    namePlate = { 12, 8, 124, 32 };
    prevPreset.setBounds (318, y, 24, h);
    nextPreset.setBounds (344, y, 24, h);
    presetBox.setBounds (372, y, 280, h);
    savePreset.setBounds (656, y, 50, h);
    playMode.setBounds (780, y, 168, h);
    settingsButton.setBounds (w - 12 - 28, y, 28, h);
    zoomBox.setBounds (w - 12 - 28 - 6 - 78, y, 78, h);
}

void TopBar::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    // Name plate: a dark inlay that looks the same on both themes, so the mark is constant.
    const auto plate = namePlate.toFloat();
    g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.9f : 0.18f));
    g.fillRoundedRectangle (plate.translated (0.0f, 1.0f), 4.0f);
    g.setColour (juce::Colour (0xff17191c));
    g.fillRoundedRectangle (plate, 4.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (plate.reduced (0.5f), 4.0f, 1.0f);

    const auto mark = fonts::bold (25.0f, 0.01f);
    const auto mwW = juce::GlyphArrangement::getStringWidth (mark, "mw");
    const auto numW = juce::GlyphArrangement::getStringWidth (mark, "12F");
    auto text = plate.withSizeKeepingCentre (mwW + numW + 1.0f, plate.getHeight()).translated (0.0f, -1.0f);
    draw::text (g, "mw", text.removeFromLeft (mwW + 1.0f), mark, juce::Colour (0xffeef0f2), juce::Justification::centredLeft);
    draw::text (g, "12F", text, mark, t.accent, juce::Justification::centredLeft);

    const auto tag = fonts::legend (9.5f, 0.14f);
    draw::text (g, "CIRCUIT-MODELLED", juce::Rectangle<float> (plate.getRight() + 12.0f, plate.getY() + 3.0f, 130.0f, 12.0f), tag, t.legendDim,
                juce::Justification::centredLeft);
    draw::text (g, "ACID SYNTHESIZER", juce::Rectangle<float> (plate.getRight() + 12.0f, plate.getY() + 16.0f, 130.0f, 12.0f), tag, t.legendDim,
                juce::Justification::centredLeft);

    const auto caption = fonts::legend (10.5f, 0.10f);
    const auto row = [&] (const juce::Component& c, const juce::String& label, float width)
    {
        draw::text (g, label, juce::Rectangle<float> (static_cast<float> (c.getX()) - width - 8.0f, static_cast<float> (c.getY()), width,
                                                      static_cast<float> (c.getHeight())),
                    caption, t.legendDim, juce::Justification::centredRight);
    };

    row (prevPreset, "PRESET", 50.0f);
    row (playMode, "MODE", 40.0f);
    row (zoomBox, "ZOOM", 40.0f);
}

//==============================================================================
SynthPanel::SynthPanel (ControlSet& c)
    : wave (c.toggleSwitch (kWave, "WAVE")),
      tuneExt (c.ledButton (kTuneExt, "EXT", LedColour::Accent, LedButton::Style::LensLabel)),
      tune (c.knob (kTune, Knob::Size::Large, "TUNING")),
      tuneFine (c.knob (kTuneFine, Knob::Size::Mini, "FINE")),
      cutoff (c.knob (kCutoff, Knob::Size::Large, "CUTOFF")),
      resonance (c.knob (kResonance, Knob::Size::Large, "RESONANCE")),
      envMod (c.knob (kEnvMod, Knob::Size::Large, "ENV MOD")),
      decay (c.knob (kDecay, Knob::Size::Large, "DECAY")),
      envAttack (c.knob (kEnvAttack, Knob::Size::Small, "ATTACK")),
      accent (c.knob (kAccent, Knob::Size::Large, "ACCENT")),
      accDecay (c.knob (kAccDecay, Knob::Size::Small, "DECAY")),
      accVol (c.knob (kAccVol, Knob::Size::Small, "VOLUME")),
      sweepAmt (c.knob (kSweepAmt, Knob::Size::Small, "SWEEP")),
      slideTime (c.knob (kSlideTime, Knob::Size::Small, "SLIDE TIME")),
      vibSpeed (c.knob (kVibSpeed, Knob::Size::Small, "VIB SPEED")),
      vibDepth (c.knob (kVibDepth, Knob::Size::Small, "VIB DEPTH")),
      tuneCoarse (c.numeric (kTuneCoarse, "TRANS")),
      sweepCoupleA (c.ledButton (kSweepCouple, {}, LedColour::Amber, LedButton::Style::Lens)),
      sweepCoupleB (c.ledButton (kSweepCouple, {}, LedColour::Amber, LedButton::Style::Lens)),
      accCoupleA (c.ledButton (kAccVolCouple, {}, LedColour::Turquoise, LedButton::Style::Lens)),
      accCoupleB (c.ledButton (kAccVolCouple, {}, LedColour::Turquoise, LedButton::Style::Lens))
{
    wave.setPositionIcon (0, draw::Icon::Saw);
    wave.setPositionIcon (1, draw::Icon::Square);

    for (auto* comp : std::initializer_list<juce::Component*> { &wave, &tuneExt, &tune, &cutoff, &resonance, &envMod, &decay, &envAttack,
                                                                &accent, &accDecay, &accVol, &sweepAmt, &slideTime, &vibSpeed, &vibDepth,
                                                                &sweepCoupleA, &sweepCoupleB, &accCoupleA, &accCoupleB })
        addAndMakeVisible (comp);

    addChildComponent (tuneFine);
    addChildComponent (tuneCoarse);

    for (auto* lamp : { &gateLamp, &accentLamp })
    {
        lamp->setInterceptsMouseClicks (false, false);
        lamp->setLit (false);
        addAndMakeVisible (lamp);
    }

    addAndMakeVisible (chargeMeter);
    setCouples (true, true);
}

void SynthPanel::setExtendedTuning (bool extended)
{
    tune.setVisible (! extended);
    tuneFine.setVisible (extended);
    tuneCoarse.setVisible (extended);
}

void SynthPanel::setCouples (bool sweepCoupled, bool accentVolumeCoupled)
{
    sweepAmt.setEnabled (! sweepCoupled);
    accVol.setEnabled (! accentVolumeCoupled);
}

void SynthPanel::resized()
{
    const auto w = getWidth();
    frames[0] = { 0, 0, 172, kHeight };
    frames[1] = { 178, 0, 434, kHeight };
    frames[2] = { 618, 0, 310, kHeight };
    frames[3] = { 934, 0, w - 934, kHeight };

    const auto largeY = 34;
    const auto smallY = largeY + metrics::kKnobLargeH - metrics::kKnobSmallH;
    const auto large = [&] (Knob& k, int x) { k.setBounds (x, largeY, metrics::kKnobLargeW, metrics::kKnobLargeH); };
    const auto small = [&] (Knob& k, int x) { k.setBounds (x, smallY, metrics::kKnobSmallW, metrics::kKnobSmallH); };
    const auto couple = [] (LedButton& b, const Knob& k) { b.setBounds (k.getRight() - 18, k.getY() - 8, 22, 22); };

    // Oscillator.
    wave.setBounds (10, largeY + metrics::kKnobLargeH - wave.preferredHeight(), 62, wave.preferredHeight());
    tuneExt.setBounds (frames[0].getRight() - 56, 2, 48, 18);
    large (tune, 82);
    tuneCoarse.setBounds (78, largeY + 10, 44, 34);
    tuneFine.setBounds (124, largeY + 4, metrics::kKnobMiniW, metrics::kKnobMiniH);

    // Filter.
    auto x = frames[1].getX() + 12;
    large (cutoff, x);
    large (resonance, x + 84);
    large (envMod, x + 168);
    large (decay, x + 252);
    small (envAttack, x + 346);
    couple (sweepCoupleA, resonance);
    accentLamp.setBounds (frames[1].getRight() - 56, 2, 48, 18);
    gateLamp.setBounds (accentLamp.getX() - 58, 2, 54, 18);

    // Accent.
    x = frames[2].getX() + 12;
    large (accent, x);
    small (accDecay, x + 90);
    small (accVol, x + 156);
    small (sweepAmt, x + 222);
    couple (accCoupleA, accent);
    couple (accCoupleB, accVol);
    couple (sweepCoupleB, sweepAmt);
    chargeMeter.setBounds (frames[2].getRight() - 10 - 64, 5, 64, 12);

    // Slide and vibrato.
    x = frames[3].getX() + 13;
    small (slideTime, x);
    small (vibSpeed, x + 66);
    small (vibDepth, x + 132);
}

void SynthPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    static constexpr const char* kTitles[4] = { "OSCILLATOR", "FILTER", "ACCENT", "SLIDE + VIBRATO" };

    for (int i = 0; i < 4; ++i)
        draw::sectionFrame (g, frames[i].toFloat().withTrimmedBottom (1.0f), kTitles[i], t);

    clearTitleRule (g, t, tuneExt.getBounds(), t.sectionFill);
    clearTitleRule (g, t, gateLamp.getBounds().getUnion (accentLamp.getBounds()), t.sectionFill);

    const juce::Rectangle<int> c13 (chargeMeter.getX() - 34, 2, 30 + chargeMeter.getWidth() + 4, 18);
    clearTitleRule (g, t, c13, t.sectionFill);
    draw::text (g, "C13", juce::Rectangle<float> (static_cast<float> (c13.getX()), 2.0f, 28.0f, 18.0f), fonts::legend (10.5f, 0.08f),
                t.legendDim, juce::Justification::centredRight);
}

//==============================================================================
ModsPanel::ModsPanel (ControlSet& c)
    : enable (c.ledButton (kModOn, "ENABLE", LedColour::Accent, LedButton::Style::LensLabel))
{
    static constexpr ParamId kKnobIds[7] = { kModOverdrive, kModFilterFm, kModTracking, kModAmpDecay, kModAmpSustain, kModSoftAttack, kModSubLevel };
    static constexpr const char* kCaptions[7] = { "OVERDRIVE", "FILTER FM", "TRACKING", "AMP DECAY", "SUSTAIN", "SOFT ATK", "SUB LEVEL" };

    addAndMakeVisible (enable);

    for (int i = 0; i < 7; ++i)
    {
        knobs[i] = &c.knob (kKnobIds[i], Knob::Size::Small, kCaptions[i]);
        addAndMakeVisible (knobs[i]);
        gated.add (knobs[i]);
    }

    sweepSpeed = &c.toggleSwitch (kModSweepSpeed, "SWEEP", { "NORM", "FAST", "SLOW" });
    muffler = &c.toggleSwitch (kModMuffler, "MUFFLER");
    subOct = &c.toggleSwitch (kModSubOct, "SUB OCT", { "-1", "-2" });
    hiRes = &c.ledButton (kModHiRes, "HI RESO", LedColour::Accent, LedButton::Style::LensLabel);
    extRange = &c.ledButton (kModExtRange, "EXT RANGE", LedColour::Accent, LedButton::Style::LensLabel);

    for (auto* comp : std::initializer_list<juce::Component*> { sweepSpeed, muffler, subOct, hiRes, extRange })
    {
        addAndMakeVisible (comp);
        gated.add (comp);
    }

    setModsEnabled (false);
}

void ModsPanel::setModsEnabled (bool enabled)
{
    for (auto* comp : gated)
        comp->setEnabled (enabled);
}

void ModsPanel::resized()
{
    enable.setBounds (getWidth() - 78, 2, 70, 18);

    const auto knobY = 28;
    auto x = 8;

    for (auto* k : knobs)
    {
        k->setBounds (x, knobY, metrics::kKnobSmallW, metrics::kKnobSmallH);
        x += 58;
    }

    x += 6;
    const auto bottom = knobY + metrics::kKnobSmallH;

    for (auto* s : { sweepSpeed, muffler, subOct })
    {
        s->setBounds (x, bottom - s->preferredHeight(), 54, s->preferredHeight());
        x += 54;
    }

    hiRes->setBounds (x + 2, knobY + 12, 84, 18);
    extRange->setBounds (x + 2, knobY + 38, 84, 18);
}

void ModsPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    draw::sectionFrame (g, getLocalBounds().toFloat().withTrimmedBottom (1.0f), "MODS", t);
    clearTitleRule (g, t, enable.getBounds(), t.sectionFill);
}

//==============================================================================
CalibrationPanel::CalibrationPanel (ControlSet& c)
{
    struct Cell { ParamId id; const char* caption; };
    static constexpr Cell kCells[15] = {
        { kCalTm3, "TM3 CUTOFF" },      { kCalTm5, "TM5 WIDTH" },    { kCalCvScale, "CV SCALE" },   { kCalRb, "LADDER LOAD" },   { kCalKmax, "LOOP GAIN" },
        { kCalAccEnv, "ACC FILTER" },   { kCalAccAmp, "ACC AMP" },   { kCalC13, "C13 ACCENT" },     { kCalC21, "C21 COUPLE" },   { kCalPw, "SQR WIDTH" },
        { kCalGateDelay, "GATE DELAY" },{ kCalDrift, "DRIFT" },      { kCalVcaNoise, "VCA NOISE" }, { kCalVcaClicks, "VCA CLICKS" }, { kCalCpuJitter, "CPU JITTER" }
    };

    for (const auto& cell : kCells)
    {
        juce::Component* comp = nullptr;

        if (paramInfo (cell.id).kind == ParamKind::Bool)
        {
            auto& b = c.ledButton (cell.id, cell.caption, LedColour::Accent, LedButton::Style::LensLabel);
            b.setSurface (Surface::Dark);
            comp = &b;
        }
        else
        {
            comp = &c.trimmer (cell.id, cell.caption);
        }

        addAndMakeVisible (comp);
        cells.add (comp);
    }

    presetBox.setTextWhenNothingSelected ("- Custom -");
    addAndMakeVisible (presetBox);
    addAndMakeVisible (cover);
}

void CalibrationPanel::resized()
{
    const auto usable = getWidth() - 10 - CoverPanel::kGripWidth - 4;
    const auto cellW = usable / 5;
    const auto rowH = 27;

    for (int i = 0; i < cells.size(); ++i)
    {
        const juce::Rectangle<int> cell (10 + cellW * (i % 5), 25 + rowH * (i / 5), cellW - 4, rowH - 1);
        cells[i]->setBounds (dynamic_cast<Trimmer*> (cells[i]) != nullptr ? cell : cell.withTrimmedLeft (4).withSizeKeepingCentre (cell.getWidth() - 4, 18));
    }

    presetBox.setBounds (10 + usable - 4 - 150, 3, 150, 19);
    cover.setClosedBounds (getLocalBounds().withTrimmedBottom (1));
}

void CalibrationPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    const auto b = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
    const auto corner = metrics::kSectionCorner;
    g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.9f : 0.16f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), corner);
    g.setColour (t.bayFill);
    g.fillRoundedRectangle (b, corner);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (b.withHeight (3.0f), corner);
    g.setColour (t.bayOutline);
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

    draw::text (g, "CALIBRATION", juce::Rectangle<float> (12.0f, 3.0f, 140.0f, 19.0f), fonts::bold (metrics::kTitleHeight, 0.14f), t.bayText,
                juce::Justification::centredLeft);
    draw::text (g, "PRESET", juce::Rectangle<float> (static_cast<float> (presetBox.getX()) - 58.0f, 3.0f, 50.0f, 19.0f),
                fonts::legend (10.0f, 0.10f), t.bayTextDim, juce::Justification::centredRight);
}
}
