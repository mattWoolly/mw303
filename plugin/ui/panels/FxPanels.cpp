#include "panels/Panels.h"

namespace acid::ui
{
namespace
{
void clearTitleRule (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour fill)
{
    g.setColour (fill);
    g.fillRect (area.expanded (5, 0).withTrimmedTop (2));
}

constexpr const char* kFxTabNames[kNumFxSlots] = { "DIST", "EQ", "DELAY", "REVERB", "CHORUS", "LIMIT" };
constexpr ParamId kFxOnParams[kNumFxSlots] = { kDistOn, kEqOn, kDelayOn, kRevOn, kChoOn, kLimOn };
}

//==============================================================================
DistortionPanel::DistortionPanel (ControlSet& c)
    : on (c.ledButton (kDistOn, "ON", LedColour::Accent, LedButton::Style::LensLabel)),
      dynamics (c.knob (kDistDynamics, Knob::Size::Small, "DYNAMICS")),
      drive (c.knob (kDistDrive, Knob::Size::Large, "DRIVE")),
      color (c.knob (kDistColor, Knob::Size::Small, "COLOUR")),
      order (c.toggleSwitch (kDistDynPost, "DYN", { "PRE", "POST" }))
{
    modelBox.param.name = paramInfo (kDistModel).name;
    prevModel.onClick = [this] { modelBox.step (-1); };
    nextModel.onClick = [this] { modelBox.step (1); };

    for (auto* comp : std::initializer_list<juce::Component*> { &on, &dynamics, &drive, &color, &order, &modelBox, &prevModel, &nextModel })
        addAndMakeVisible (comp);
}

void DistortionPanel::setModelNames (const juce::StringArray& names)
{
    const auto selected = modelBox.getSelectedItemIndex();
    modelBox.setItems (names);
    modelBox.setSelectedItemIndex (juce::jlimit (0, juce::jmax (0, names.size() - 1), selected), juce::dontSendNotification);
}

void DistortionPanel::resized()
{
    on.setBounds (getWidth() - 50, 2, 42, 18);

    prevModel.setBounds (10, 23, 22, 22);
    nextModel.setBounds (34, 23, 22, 22);
    modelBox.setBounds (60, 23, getWidth() - 70, 22);

    const auto largeY = 49;
    const auto smallY = largeY + metrics::kKnobLargeH - metrics::kKnobSmallH;
    dynamics.setBounds (10, smallY, metrics::kKnobSmallW, metrics::kKnobSmallH);
    order.setBounds (74, largeY + metrics::kKnobLargeH - order.preferredHeight(), 54, order.preferredHeight());
    drive.setBounds (132, largeY, metrics::kKnobLargeW, metrics::kKnobLargeH);
    color.setBounds (216, smallY, metrics::kKnobSmallW, metrics::kKnobSmallH);
}

void DistortionPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    draw::sectionFrame (g, getLocalBounds().toFloat().withTrimmedBottom (1.0f), "DISTORTION", t);
    clearTitleRule (g, on.getBounds(), t.sectionFill);
}

//==============================================================================
FxPage::FxPage (ControlSet& c, std::vector<ParamId> basicParams, std::vector<ParamId> fullParams)
{
    for (auto id : basicParams)
        basic.push_back (makeItem (c, id, false));

    for (auto id : fullParams)
        full.push_back (makeItem (c, id, true));

    for (auto& item : full)
        item.component->setVisible (false);
}

FxPage::Item FxPage::makeItem (ControlSet& c, ParamId id, bool isFull)
{
    Item item;
    const auto& info = paramInfo (id);
    const auto caption = shortCaption (id);

    switch (info.kind)
    {
        case ParamKind::Float:
        case ParamKind::Int:
            if (isFull)
                item.component = &c.numeric (id, caption);
            else
                item.component = &c.knob (id, Knob::Size::Small, caption);

            break;

        case ParamKind::Choice:
            item.component = &c.dropdown (id);
            item.caption = caption;
            item.captioned = true;
            break;

        case ParamKind::Bool:
            item.component = &c.ledButton (id, caption);
            break;
    }

    addAndMakeVisible (item.component);
    return item;
}

void FxPage::setFullView (bool shouldBeFull)
{
    fullView = shouldBeFull;

    for (auto& item : basic)
        item.component->setVisible (! fullView);

    for (auto& item : full)
        item.component->setVisible (fullView);

    if (basicExtra != nullptr)
        basicExtra->setVisible (! fullView);

    repaint();
}

void FxPage::setBasicExtra (juce::Component* extra, int width)
{
    basicExtra = extra;
    basicExtraWidth = width;

    if (extra != nullptr)
    {
        addAndMakeVisible (extra);
        extra->setVisible (! fullView);
    }

    resized();
}

void FxPage::resized()
{
    const auto b = getLocalBounds();

    // Basic: one centred row.
    {
        const auto widthOf = [] (const Item& item)
        {
            if (dynamic_cast<Knob*> (item.component) != nullptr)  return 76;
            if (item.captioned)                                    return 104;
            return 96;
        };

        auto total = basicExtra != nullptr ? basicExtraWidth + 16 : 0;

        for (auto& item : basic)
            total += widthOf (item);

        auto x = b.getCentreX() - total / 2;
        const auto knobY = b.getCentreY() - metrics::kKnobSmallH / 2;

        for (auto& item : basic)
        {
            const auto w = widthOf (item);

            if (dynamic_cast<Knob*> (item.component) != nullptr)
                item.component->setBounds (x + (w - metrics::kKnobSmallW) / 2, knobY, metrics::kKnobSmallW, metrics::kKnobSmallH);
            else if (item.captioned)
                item.component->setBounds (x + 4, b.getCentreY() - 4, w - 8, 22);
            else
                item.component->setBounds (x + 4, b.getCentreY() - 12, w - 8, 24);

            x += w;
        }

        if (basicExtra != nullptr)
            basicExtra->setBounds (x + 16, b.getCentreY() - 7, basicExtraWidth, 14);
    }

    // Full: a grid, two rows up to fourteen parameters, three above that.
    if (! full.empty())
    {
        const auto n = static_cast<int> (full.size());
        const auto rows = n > 14 ? 3 : 2;
        const auto cols = (n + rows - 1) / rows;
        const auto cellW = juce::jmin (104, b.getWidth() / cols);
        const auto cellH = juce::jmin (36, b.getHeight() / rows);
        const auto x0 = b.getCentreX() - cellW * cols / 2;
        const auto y0 = b.getCentreY() - cellH * rows / 2;

        for (int i = 0; i < n; ++i)
        {
            const juce::Rectangle<int> cell (x0 + cellW * (i % cols), y0 + cellH * (i / cols), cellW, cellH);
            auto& item = full[static_cast<std::size_t> (i)];

            if (item.captioned)
                item.component->setBounds (cell.reduced (4, 0).withTrimmedTop (13).withHeight (19));
            else if (dynamic_cast<NumericDisplay*> (item.component) != nullptr)
                item.component->setBounds (cell.reduced (4, 0).withHeight (32));
            else
                item.component->setBounds (cell.reduced (4, 0).withTrimmedTop (11).withHeight (21));
        }
    }
}

void FxPage::paint (juce::Graphics& g)
{
    Panel::paint (g);
    const auto& t = themeFor (*this);

    for (auto& item : fullView ? full : basic)
        if (item.captioned)
        {
            const auto cb = item.component->getBounds().toFloat();
            draw::text (g, item.caption, juce::Rectangle<float> (cb.getX() - 4.0f, cb.getY() - 14.0f, cb.getWidth() + 8.0f, 13.0f),
                        fonts::legend (10.5f), t.legend);
        }
}

//==============================================================================
FxRackPanel::FxRackPanel (ControlSet& c)
{
    for (int i = 0; i < kNumFxSlots; ++i)
    {
        tabs.addTab (i, kFxTabNames[i]);
        c.add (kFxOnParams[i], tabs.getLedButton (i));
    }

    const auto range = [] (ParamId first, ParamId last)
    {
        std::vector<ParamId> ids;

        for (int i = first; i <= last; ++i)
            ids.push_back (static_cast<ParamId> (i));

        return ids;
    };

    pages[0] = std::make_unique<FxPage> (c, std::vector<ParamId> { kDistClipper, kDistPreMin, kDistPreMax, kDistKnee, kDistBias },
                                         range (kDistClipper, kDistVolume));
    pages[1] = std::make_unique<FxPage> (c, std::vector<ParamId> { kEqLowGain, kEqLoMidGain, kEqHiMidGain, kEqHighGain },
                                         std::vector<ParamId> { kEqLowGain, kEqLoMidGain, kEqHiMidGain, kEqHighGain, kEqLowFreq, kEqLoMidFreq,
                                                                kEqHiMidFreq, kEqHighFreq, kEqLowWidth, kEqLoMidWidth, kEqHiMidWidth, kEqHighWidth });
    pages[2] = std::make_unique<FxPage> (c, std::vector<ParamId> { kDelayFeedback, kDelayFiltCutoff, kDelayFiltType, kDelaySpread, kDelayMix },
                                         range (kDelaySync, kDelayMix));
    pages[3] = std::make_unique<FxPage> (c, std::vector<ParamId> { kRevPreDelay, kRevEarlyLate, kRevFeedback, kRevMix },
                                         range (kRevPreDelay, kRevMix));
    pages[4] = std::make_unique<FxPage> (c, std::vector<ParamId> { kChoRate, kChoDepth, kChoSpatial, kChoMix }, range (kChoOffset, kChoMix));
    pages[5] = std::make_unique<FxPage> (c, std::vector<ParamId> { kLimAttack, kLimRelease, kLimThreshold, kLimGain, kLimSoftClip },
                                         range (kLimKnee, kLimSoftClip));
    pages[5]->setBasicExtra (&gainReduction, 124);

    for (auto& page : pages)
        addChildComponent (*page);

    tabs.onTabSelected = [this] (int id) { showEffect (static_cast<FxSlot> (id)); };
    tabs.onOrderChanged = [this] (const juce::Array<int>& order)
    {
        if (onOrderChanged == nullptr)
            return;

        std::array<std::uint8_t, kNumFxSlots> out {};

        for (int i = 0; i < kNumFxSlots; ++i)
            out[static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (order[i]);

        onOrderChanged (out);
    };

    presetBox.setTextWhenNothingSelected ("- Custom -");
    viewSelector.onValueChange = [this] { setFullView (viewSelector.getSelectedSegment() == 1); };

    addAndMakeVisible (tabs);
    addAndMakeVisible (presetBox);
    addAndMakeVisible (viewSelector);
    showEffect (FxSlot::Delay);
    tabs.setSelectedTab (static_cast<int> (FxSlot::Delay), juce::dontSendNotification);
}

void FxRackPanel::setFullView (bool isFull)
{
    fullView = isFull;
    viewSelector.setValue (isFull ? 1.0 : 0.0, juce::dontSendNotification);

    for (auto& page : pages)
        page->setFullView (isFull);
}

void FxRackPanel::showEffect (FxSlot slot)
{
    current = static_cast<int> (slot);

    for (int i = 0; i < kNumFxSlots; ++i)
        pages[i]->setVisible (i == current);

    repaint();
}

void FxRackPanel::setOrder (const std::array<std::uint8_t, kNumFxSlots>& order)
{
    juce::Array<int> ids;

    for (auto slot : order)
        ids.add (static_cast<int> (slot));

    tabs.setOrder (ids);
}

void FxRackPanel::resized()
{
    const auto w = getWidth();
    tabs.setBounds (8, 7, 464, 25);
    viewSelector.setBounds (w - 8 - 104, 7, 104, 25);
    presetBox.setBounds (tabs.getRight() + 8, 8, viewSelector.getX() - tabs.getRight() - 16, 23);

    for (auto& page : pages)
        page->setBounds (8, 36, w - 16, getHeight() - 36 - 5);
}

void FxRackPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    draw::sectionFrame (g, getLocalBounds().toFloat().withTrimmedBottom (1.0f), {}, t);
}

//==============================================================================
MasterPanel::MasterPanel (ControlSet& c)
    : volume (c.knob (kMasterVolume, Knob::Size::Large, "VOLUME")),
      bypass (c.ledButton (kFxBypass, "FX BYPASS", LedColour::Amber, LedButton::Style::LensLabel))
{
    addAndMakeVisible (volume);
    addAndMakeVisible (bypass);
    addAndMakeVisible (meter);
}

void MasterPanel::resized()
{
    meter.setBounds (getWidth() - 10 - 46, 25, 46, getHeight() - 25 - 9);
    volume.setBounds (8, 26, metrics::kKnobLargeW, metrics::kKnobLargeH);
    bypass.setBounds (10, getHeight() - 26, 82, 18);
}

void MasterPanel::paintStaticArt (juce::Graphics& g, const Theme& t)
{
    draw::sectionFrame (g, getLocalBounds().toFloat().withTrimmedBottom (1.0f), "MASTER", t);
}
}
