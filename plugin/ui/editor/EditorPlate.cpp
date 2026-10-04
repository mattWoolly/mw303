#include "editor/EditorPlate.h"

namespace acid::ui
{
namespace
{
constexpr int kMargin = 12, kGap = 6;
}

EditorPlate::EditorPlate (PatternEditModel& patternModel)
    : topBar (controls), synth (controls), mods (controls), calibration (controls), distortion (controls), rack (controls),
      master (controls), sequencer (controls, patternModel)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* panel : std::initializer_list<juce::Component*> { &topBar, &synth, &mods, &calibration, &distortion, &rack, &master, &sequencer })
        addAndMakeVisible (panel);

    addChildComponent (external);

    setSize (kDesignWidth, kDesignHeight);
}

EditorPlate::~EditorPlate()
{
    setLookAndFeel (nullptr);
}

void EditorPlate::setThemeId (ThemeId id)
{
    if (id == lookAndFeel.getThemeId())
        return;

    lookAndFeel.setTheme (id);
    sendLookAndFeelChange();
}

void EditorPlate::setPlayMode (seq::PlayMode mode)
{
    if (mode == playMode)
        return;

    playMode = mode;
    sequencer.setArpMode (mode == seq::PlayMode::Arpeggiator);

    // The two screens share one area and swap in place. No size change and no whole-plate
    // invalidation: each is an opaque panel with its own tile, and the sequencer relabels
    // itself for the arpeggiator (setArpMode repaints that panel).
    const auto isExternal = mode == seq::PlayMode::External;
    sequencer.setVisible (! isExternal);
    external.setVisible (isExternal);
}

void EditorPlate::setOverlay (juce::Component* newOverlay)
{
    if (overlay == newOverlay)
        return;

    if (overlay != nullptr)
        removeChildComponent (overlay);

    overlay = newOverlay;

    if (overlay != nullptr)
    {
        // The overlay shows and hides itself (the browser opens on demand).
        addChildComponent (overlay);
        overlay->toFront (false);
        resized();
    }
}

void EditorPlate::resized()
{
    Faceplate::resized();

    const auto w = getWidth();
    const auto inner = w - 2 * kMargin;
    topBar.setBounds (0, 0, w, TopBar::kHeight);

    auto y = TopBar::kHeight + kGap;
    synth.setBounds (kMargin, y, inner, SynthPanel::kHeight);
    y += SynthPanel::kHeight + kGap;

    mods.setBounds (kMargin, y, ModsPanel::kWidth, ModsPanel::kHeight);
    calibration.setBounds (kMargin + ModsPanel::kWidth + kGap, y, inner - ModsPanel::kWidth - kGap, CalibrationPanel::kHeight);
    y += ModsPanel::kHeight + kGap;

    distortion.setBounds (kMargin, y, DistortionPanel::kWidth, DistortionPanel::kHeight);
    master.setBounds (w - kMargin - MasterPanel::kWidth, y, MasterPanel::kWidth, MasterPanel::kHeight);
    rack.setBounds (distortion.getRight() + kGap, y, master.getX() - distortion.getRight() - 2 * kGap, FxRackPanel::kHeight);
    y += FxRackPanel::kHeight + kGap;

    sequencer.setBounds (kMargin, y, inner, SequencerPanel::kHeight);
    external.setBounds (sequencer.getBounds());

    if (overlay != nullptr)
        overlay->setBounds (0, TopBar::kHeight, w, getHeight() - TopBar::kHeight);
}

void EditorPlate::paintPlate (juce::Graphics& g, const Theme& t)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (t.plateTop, 0.0f, 0.0f, t.plateBottom, 0.0f, b.getHeight(), false));
    g.fillAll();

    // Fine horizontal grain, like a brushed and anodised sheet. Fixed seed: the same plate every time.
    juce::Random grain (0x12f);
    const auto strength = t.id == ThemeId::Silver ? 0.05f : 0.035f;

    for (int y = 0; y < getHeight(); ++y)
    {
        const auto v = grain.nextFloat();
        g.setColour ((v > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (std::abs (v - 0.5f) * 2.0f * strength));
        g.fillRect (0, y, getWidth(), 1);
    }

    // Top bar: a slightly deeper band with a machined edge under it.
    const auto bar = b.withHeight (static_cast<float> (TopBar::kHeight));
    g.setColour (juce::Colours::black.withAlpha (t.id == ThemeId::Silver ? 0.07f : 0.22f));
    g.fillRect (bar);
    g.setColour (t.plateEdgeDark.withAlpha (0.7f));
    g.fillRect (0.0f, bar.getBottom() - 1.0f, b.getWidth(), 1.0f);
    g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.9f : 0.25f));
    g.fillRect (0.0f, bar.getBottom(), b.getWidth(), 1.0f);

    // Plate edges.
    g.setColour (t.plateEdgeLight.withAlpha (t.id == ThemeId::Silver ? 0.9f : 0.3f));
    g.fillRect (0.0f, 0.0f, b.getWidth(), 1.0f);
    g.setColour (t.plateEdgeDark);
    g.fillRect (0.0f, b.getBottom() - 1.0f, b.getWidth(), 1.0f);
}
}
