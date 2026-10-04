#include "seq/GeneratorPanel.h"
#include "kit/Faceplate.h"

namespace acid::ui
{
namespace
{
constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr int kWhitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
constexpr int kBlackPc[5] = { 1, 3, 6, 8, 10 };
constexpr float kBlackOffset[5] = { 1.0f, 2.0f, 4.0f, 5.0f, 6.0f };
constexpr const char* kOctaveNames[5] = { "-1", "0", "+1", "+2", "+3" };
constexpr const char* kAttrNames[3] = { "SLIDE", "ACCENT", "VIBRATO" };
constexpr float kCaptionW = 34.0f;
}

struct GeneratorPanel::GestureListener final : juce::Slider::Listener
{
    explicit GestureListener (PatternEditModel& m) : model (m) {}
    void sliderValueChanged (juce::Slider*) override {}
    void sliderDragStarted (juce::Slider*) override { model.beginEditGesture(); }
    void sliderDragEnded (juce::Slider*) override { model.endEditGesture(); }
    PatternEditModel& model;
};

GeneratorPanel::GeneratorPanel (PatternEditModel& m) : model (m)
{
    setWantsKeyboardFocus (false);
    gestures = std::make_unique<GestureListener> (model);

    root.setRange (0.0, 11.0, 1.0);
    root.setCaptionWidth (static_cast<int> (kCaptionW));
    root.setDefaultValue (0.0);
    root.textFromValueFunction = [] (double v) { return juce::String (kNoteNames[juce::jlimit (0, 11, juce::roundToInt (v))]); };
    root.onValueChange = [this]
    {
        if (! refreshing)
            edit ([this] (seq::GenParams& gp) { gp.root = static_cast<std::uint8_t> (juce::roundToInt (root.getValue())); });
    };
    root.addListener (gestures.get());
    addAndMakeVisible (root);

    for (int i = 0; i < 4; ++i)
    {
        auto& k = amounts[i];
        k.setRange (0.0, 100.0, 1.0);
        k.setDefaultValue (50.0);
        k.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
        k.param.name = juce::String (i == 0 ? "Note" : (i == 1 ? "Slide" : (i == 2 ? "Accent" : "Vibrato"))) + " Amount";
        k.onValueChange = [this, i]
        {
            if (refreshing)
                return;

            edit ([this, i] (seq::GenParams& gp)
            {
                const auto v = static_cast<std::uint8_t> (juce::roundToInt (amounts[i].getValue()));

                if (i == 0)       gp.amountNote = v;
                else if (i == 1)  gp.amountSlide = v;
                else if (i == 2)  gp.amountAccent = v;
                else              gp.amountVibrato = v;
            });
        };
        k.addListener (gestures.get());
        addAndMakeVisible (k);
    }

    drawButton.setProminent (true);
    drawButton.onClick = [this] { if (onDraw != nullptr) onDraw(); };
    addAndMakeVisible (drawButton);

    templateBox.setTextWhenNothingSelected ("- Custom -");
    templateBox.onChange = [this]
    {
        if (! refreshing && onTemplateChosen != nullptr && templateBox.getSelectedId() > 0)
            onTemplateChosen (templateBox.getSelectedId() - 1);   // ids are template index + 1
    };
    prevTemplate.onClick = [this] { templateBox.step (-1); };
    nextTemplate.onClick = [this] { templateBox.step (1); };
    keepButton.onClick = [this] { if (onFlatten != nullptr) onFlatten(); };

    for (auto* c : std::initializer_list<juce::Component*> { &templateBox, &prevTemplate, &nextTemplate, &keepButton })
        addAndMakeVisible (c);

    root.setTooltip ("Root: the key the note pool is built on. The keyboard above shows the pool in absolute pitch.");
    drawButton.setTooltip ("Draw a new line from the pool, octaves and amounts (a new seed).");
    keepButton.setTooltip ("Keep: turn the generated line into plain steps (the amount knobs stop changing it).");
    templateBox.setTooltip ("Template: a ready-made note pool, octaves and attributes.");
    prevTemplate.setTooltip ("Previous template");
    nextTemplate.setTooltip ("Next template");
    const char* amountTips[4] = { "Note amount: how many steps play a note.", "Slide amount: how many notes slide.",
                                  "Accent amount: how many notes are accented.", "Vibrato amount: how many notes get vibrato." };

    for (int i = 0; i < 4; ++i)
        amounts[i].setTooltip (amountTips[i]);

    model.addListener (this);
    refresh();
}

GeneratorPanel::~GeneratorPanel()
{
    root.removeListener (gestures.get());

    for (auto& k : amounts)
        k.removeListener (gestures.get());

    model.removeListener (this);
}

void GeneratorPanel::setTemplateNames (const juce::StringArray& names)
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    templateBox.setItems (names);
}

void GeneratorPanel::setTemplates (const juce::StringArray& names, const juce::StringArray& categories)
{
    // One submenu per category, in the order the categories first appear, so the 76 templates
    // open as a short list of groups instead of a long scrolling column. Ids stay index + 1.
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    templateBox.clear (juce::dontSendNotification);
    juce::StringArray order;

    for (const auto& c : categories)
        order.addIfNotAlreadyThere (c);

    auto* rootMenu = templateBox.getRootMenu();

    for (const auto& category : order)
    {
        juce::PopupMenu sub;

        for (int i = 0; i < names.size(); ++i)
            if (categories[i] == category)
                sub.addItem (i + 1, names[i]);

        rootMenu->addSubMenu (category, sub);
    }
}

void GeneratorPanel::setTemplateIndex (int index)
{
    const juce::ScopedValueSetter<bool> guard (refreshing, true);

    if (index < 0 || templateBox.indexOfItemId (index + 1) < 0)
        templateBox.setSelectedId (0, juce::dontSendNotification);
    else
        templateBox.setSelectedId (index + 1, juce::dontSendNotification);
}

void GeneratorPanel::setSurface (Surface s)
{
    surface = s;
    root.setSurface (s);

    for (auto& k : amounts)
        k.setSurface (s);

    repaint();
}

std::uint16_t GeneratorPanel::getAbsoluteNoteMask() const
{
    const auto& gp = model.getPattern().gen;
    std::uint16_t out = 0;

    for (int i = 0; i < 12; ++i)
        if ((gp.noteMask & (1 << i)) != 0)
            out = static_cast<std::uint16_t> (out | (1 << ((i + gp.root) % 12)));

    return out;
}

void GeneratorPanel::resized()
{
    // Compact enough to leave the live override block visible under it in the tools column.
    auto b = getLocalBounds().reduced (0, 2);
    keysArea = b.removeFromTop (40).toFloat();
    b.removeFromTop (6);
    auto rootRow = b.removeFromTop (22);
    root.setBounds (rootRow.removeFromLeft (juce::jmin (rootRow.getWidth(), 96)));
    rootRow.removeFromLeft (6);
    keepButton.setBounds (rootRow);
    b.removeFromTop (6);
    octaveRow = b.removeFromTop (20).toFloat();
    b.removeFromTop (4);
    attrRow = b.removeFromTop (20).toFloat();
    b.removeFromTop (5);

    auto knobs = b.removeFromTop (metrics::kKnobMiniH);
    const auto pitch = knobs.getWidth() / 4;

    for (auto& k : amounts)
        k.setBounds (knobs.removeFromLeft (pitch).withSizeKeepingCentre (juce::jmin (pitch, metrics::kKnobMiniW), metrics::kKnobMiniH));

    b.removeFromTop (5);
    auto bottom = b.removeFromTop (24);
    drawButton.setBounds (bottom.removeFromRight (68));
    bottom.removeFromRight (6);
    seedArea = bottom.toFloat();

    // Template row: library of masks, stepped with the arrows.
    b.removeFromTop (6);
    templateCaption = b.removeFromTop (13).toFloat();
    auto row = b.removeFromTop (22);
    prevTemplate.setBounds (row.removeFromLeft (22));
    row.removeFromLeft (3);
    nextTemplate.setBounds (row.removeFromLeft (22));
    row.removeFromLeft (4);
    templateBox.setBounds (row);
}

juce::Rectangle<float> GeneratorPanel::keyBounds (int pitchClass, bool& isBlack) const noexcept
{
    const auto whiteW = keysArea.getWidth() / 7.0f;

    for (int i = 0; i < 7; ++i)
        if (kWhitePc[i] == pitchClass)
        {
            isBlack = false;
            return juce::Rectangle<float> (keysArea.getX() + whiteW * static_cast<float> (i), keysArea.getY(), whiteW - 2.0f, keysArea.getHeight());
        }

    for (int i = 0; i < 5; ++i)
        if (kBlackPc[i] == pitchClass)
        {
            isBlack = true;
            const auto bw = whiteW * 0.64f;
            return juce::Rectangle<float> (keysArea.getX() + whiteW * kBlackOffset[i] - bw * 0.5f - 1.0f, keysArea.getY(), bw, keysArea.getHeight() * 0.58f);
        }

    isBlack = false;
    return {};
}

juce::Rectangle<float> GeneratorPanel::chip (const juce::Rectangle<float>& row, int index, int count) const noexcept
{
    const auto area = row.withTrimmedLeft (kCaptionW);
    const auto w = (area.getWidth() + 3.0f) / static_cast<float> (count);
    return { area.getX() + w * static_cast<float> (index), area.getY(), w - 3.0f, area.getHeight() - 1.0f };
}

void GeneratorPanel::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto& gp = model.getPattern().gen;
    const auto captionFont = fonts::legend (10.5f);
    const auto captionInk = t.caption (surface);
    bool black = false;

    // Pitch-class keys in absolute pitch: key pc is lit when its interval above the root is in
    // the pool (noteMask is relative to the root), and the root key carries a dot.
    const auto rootPc = static_cast<int> (gp.root % 12);
    const auto drawKey = [&] (int pc)
    {
        const auto b = keyBounds (pc, black);
        const auto inPool = (gp.noteMask & (1 << ((pc - rootPc + 12) % 12))) != 0;
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 2.5f);
        g.setColour (inPool ? t.accent : (black ? t.keyBlack : t.keyWhite));
        g.fillRoundedRectangle (b, 2.5f);
        g.setColour (inPool ? t.accentDim : t.keyOutline);
        g.drawRoundedRectangle (b.reduced (0.5f), 2.5f, 1.0f);

        if (pc == rootPc)
        {
            g.setColour (inPool ? t.accentInk : (black ? t.keyWhite : t.keyBlack));
            g.fillEllipse (b.getCentreX() - 2.0f, b.getBottom() - 8.0f, 4.0f, 4.0f);
        }
    };

    for (auto pc : kWhitePc)  drawKey (pc);
    for (auto pc : kBlackPc)  drawKey (pc);

    const auto drawChips = [&] (const juce::Rectangle<float>& row, const juce::String& caption, const char* const* names, int count,
                                const std::function<bool (int)>& isOn)
    {
        draw::text (g, caption, row.withWidth (kCaptionW), captionFont, captionInk, juce::Justification::centredLeft);

        for (int i = 0; i < count; ++i)
        {
            const auto c = chip (row, i, count);
            const auto on = isOn (i);

            if (on)
            {
                g.setColour (t.accent);
                g.fillRoundedRectangle (c, 2.5f);
                draw::text (g, names[i], c, fonts::legend (10.0f), t.accentInk);
            }
            else
            {
                draw::well (g, c, t, 2.5f);
                draw::text (g, names[i], c, fonts::legend (10.0f), t.screenTextDim);
            }
        }
    };

    drawChips (octaveRow, "OCT", kOctaveNames, 5, [&] (int i) { return (gp.octaveMask & (1 << i)) != 0; });
    drawChips (attrRow, "USE", kAttrNames, 3, [&] (int i) { return i == 0 ? gp.useSlide : (i == 1 ? gp.useAccent : gp.useVibrato); });

    draw::well (g, seedArea.withTrimmedBottom (1.0f), t);
    auto seedText = seedArea.reduced (6.0f, 0.0f);
    draw::text (g, "SEED", seedText.removeFromLeft (30.0f), fonts::legend (9.5f, 0.06f), t.wellTextDim, juce::Justification::centredLeft);
    draw::text (g, juce::String::toHexString (static_cast<juce::uint32> (gp.seed)).paddedLeft ('0', 8).toUpperCase(), seedText,
                fonts::readout (12.0f), t.wellText, juce::Justification::centredRight);

    draw::text (g, "TEMPLATE", templateCaption, captionFont, captionInk, juce::Justification::centredLeft);
}

void GeneratorPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    bool black = false;

    if (keysArea.contains (e.position))
    {
        int hit = -1;

        for (auto pc : kBlackPc)
            if (hit < 0 && keyBounds (pc, black).contains (e.position))
                hit = pc;

        for (auto pc : kWhitePc)
            if (hit < 0 && keyBounds (pc, black).contains (e.position))
                hit = pc;

        if (hit >= 0)
            edit ([hit] (seq::GenParams& gp) { gp.noteMask = static_cast<std::uint16_t> (gp.noteMask ^ (1 << ((hit - gp.root % 12 + 12) % 12))); });

        return;
    }

    if (seedArea.contains (e.position))
    {
        enterSeed();
        return;
    }

    for (int i = 0; i < 5; ++i)
        if (chip (octaveRow, i, 5).contains (e.position))
        {
            edit ([i] (seq::GenParams& gp) { gp.octaveMask = static_cast<std::uint8_t> (gp.octaveMask ^ (1 << i)); });
            return;
        }

    for (int i = 0; i < 3; ++i)
        if (chip (attrRow, i, 3).contains (e.position))
        {
            edit ([i] (seq::GenParams& gp)
            {
                if (i == 0)       gp.useSlide = ! gp.useSlide;
                else if (i == 1)  gp.useAccent = ! gp.useAccent;
                else              gp.useVibrato = ! gp.useVibrato;
            });
            return;
        }
}

void GeneratorPanel::enterSeed()
{
    auto* plate = Faceplate::find (*this);

    if (plate == nullptr || onSeedEntered == nullptr)
        return;

    juce::Component::SafePointer<GeneratorPanel> safe (this);
    const auto current = juce::String::toHexString (static_cast<juce::uint32> (model.getPattern().gen.seed)).paddedLeft ('0', 8).toUpperCase();
    plate->beginTextEntry (*this, current, [safe] (const juce::String& text)
    {
        const auto hex = text.trim().retainCharacters ("0123456789abcdefABCDEF");

        if (safe == nullptr || hex.isEmpty() || safe->onSeedEntered == nullptr)
            return;

        const auto seed = static_cast<std::uint32_t> (hex.getLastCharacters (8).getHexValue64());
        safe->onSeedEntered (seed == 0 ? 1u : seed);
    });
}

juce::String GeneratorPanel::getTooltip()
{
    const auto pos = getMouseXYRelative().toFloat();

    if (keysArea.contains (pos))
        return "Note pool, in absolute pitch: click a key to add or remove that note. The dotted key is the root.";

    if (octaveRow.contains (pos))
        return "Octaves the line may use.";

    if (attrRow.contains (pos))
        return "Attributes the generator may place (each has an amount knob below).";

    if (seedArea.contains (pos))
        return "Seed of the line shown: click to type one and draw that line again.";

    return {};
}

void GeneratorPanel::edit (const std::function<void (seq::GenParams&)>& fn)
{
    auto edited = model.getPattern();
    fn (edited.gen);

    // With an owner, the owner applies the parameters (acid::seq::applyDensities in the
    // plugin) and the model comes back changed; without one the panel edits the model itself.
    if (onParamsChanged != nullptr)
        onParamsChanged (edited.gen);
    else
        model.applyPattern (edited);
}

void GeneratorPanel::refresh()
{
    const auto& gp = model.getPattern().gen;
    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    root.setValue (gp.root, juce::dontSendNotification);
    amounts[0].setValue (gp.amountNote, juce::dontSendNotification);
    amounts[1].setValue (gp.amountSlide, juce::dontSendNotification);
    amounts[2].setValue (gp.amountAccent, juce::dontSendNotification);
    amounts[3].setValue (gp.amountVibrato, juce::dontSendNotification);
    amounts[1].setEnabled (gp.useSlide);
    amounts[2].setEnabled (gp.useAccent);
    amounts[3].setEnabled (gp.useVibrato);
}

void GeneratorPanel::patternChanged()
{
    refresh();
    repaint();
}
}
