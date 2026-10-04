#include "kit/TabBar.h"

namespace acid::ui
{
TabBar::TabBar()
{
    setWantsKeyboardFocus (false);
}

void TabBar::addTab (int id, const juce::String& name)
{
    auto* tab = tabs.add (new Tab());
    tab->id = id;
    tab->name = name;
    tab->led = std::make_unique<LedButton> (juce::String(), LedColour::Accent, LedButton::Style::Lens);
    tab->led->setSurface (Surface::Dark);
    tab->led->param.name = name + " On";
    addAndMakeVisible (*tab->led);

    if (selectedId < 0)
        selectedId = id;

    resized();
}

juce::Button& TabBar::getLedButton (int id)
{
    const auto i = indexOfId (id);
    jassert (i >= 0);
    return *tabs[i]->led;
}

int TabBar::indexOfId (int id) const noexcept
{
    for (int i = 0; i < tabs.size(); ++i)
        if (tabs[i]->id == id)
            return i;

    return -1;
}

void TabBar::setOrder (const juce::Array<int>& ids)
{
    int dest = 0;

    for (auto id : ids)
    {
        const auto i = indexOfId (id);

        if (i >= dest)
            tabs.move (i, dest++);
    }

    layoutLeds();
    repaint();
}

juce::Array<int> TabBar::getOrder() const
{
    juce::Array<int> order;

    for (auto* t : tabs)
        order.add (t->id);

    return order;
}

void TabBar::setSelectedTab (int id, juce::NotificationType notify)
{
    if (indexOfId (id) < 0 || selectedId == id)
        return;

    selectedId = id;
    repaint();

    if (notify != juce::dontSendNotification && onTabSelected != nullptr)
        onTabSelected (id);
}

float TabBar::tabWidth() const noexcept
{
    return tabs.isEmpty() ? 0.0f : (static_cast<float> (getWidth()) - 4.0f) / static_cast<float> (tabs.size());
}

juce::Rectangle<float> TabBar::tabBounds (int index) const noexcept
{
    const auto w = tabWidth();
    return { 2.0f + w * static_cast<float> (index), 2.0f, w, static_cast<float> (getHeight()) - 5.0f };
}

int TabBar::indexAt (float x) const noexcept
{
    const auto w = tabWidth();
    return w <= 0.0f ? -1 : juce::jlimit (0, tabs.size() - 1, static_cast<int> ((x - 2.0f) / w));
}

void TabBar::layoutLeds()
{
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto b = tabBounds (i);

        if (tabs[i]->id == dragId && dragMoved)
            b.setX (dragX);

        tabs[i]->led->setBounds (juce::roundToInt (b.getX()) + 3, juce::roundToInt (b.getY()), 22, juce::roundToInt (b.getHeight()));
    }
}

void TabBar::resized()
{
    layoutLeds();
}

void TabBar::setTabWarning (int id, bool warn)
{
    const auto i = indexOfId (id);

    if (i >= 0 && tabs[i]->warning != warn)
    {
        tabs[i]->warning = warn;
        repaint();
    }
}

void TabBar::paint (juce::Graphics& g)
{
    const auto& t = themeFor (*this);
    const auto trough = getLocalBounds().toFloat().withTrimmedBottom (1.0f);
    draw::well (g, trough, t, 4.0f);

    const auto font = fonts::legend (11.5f, 0.08f);

    const auto drawTab = [&] (const Tab& tab, juce::Rectangle<float> b, bool floating, bool hover)
    {
        const auto selected = tab.id == selectedId;

        if (floating)
            draw::dropShadow (g, b, t);

        if (selected || floating)
        {
            draw::buttonFace (g, b.reduced (0.5f, 0.0f), t, false, hover, 3.0f);
            draw::text (g, tab.name, b.withTrimmedLeft (24.0f).withTrimmedRight (6.0f), font, tab.warning ? t.ledAmber : t.buttonText,
                        juce::Justification::centredLeft);
        }
        else
        {
            if (hover)
            {
                g.setColour (t.screenText.withAlpha (0.08f));
                g.fillRoundedRectangle (b.reduced (0.5f, 0.0f), 3.0f);
            }

            draw::text (g, tab.name, b.withTrimmedLeft (24.0f).withTrimmedRight (6.0f), font, tab.warning ? t.ledAmber : t.screenText,
                        juce::Justification::centredLeft);
        }
    };

    const Tab* floatingTab = nullptr;

    for (int i = 0; i < tabs.size(); ++i)
    {
        const auto& tab = *tabs[i];
        const auto b = tabBounds (i);

        // Signal-flow chevron between neighbouring tabs.
        if (i > 0)
        {
            const auto leftSelected = tabs[i - 1]->id == selectedId;
            const auto thisSelected = tab.id == selectedId;

            if (! leftSelected && ! thisSelected && dragId < 0)
                draw::icon (g, draw::Icon::Next, juce::Rectangle<float> (b.getX() - 4.5f, b.getCentreY() - 4.5f, 9.0f, 9.0f),
                            t.screenTextDim);
        }

        if (tab.id == dragId && dragMoved)
        {
            floatingTab = &tab;
            continue;
        }

        drawTab (tab, b, false, i == hoverIndex && dragId < 0);
    }

    if (floatingTab != nullptr)
        drawTab (*floatingTab, tabBounds (0).withX (dragX), true, true);
}

void TabBar::mouseDown (const juce::MouseEvent& e)
{
    const auto i = indexAt (e.position.x);

    if (i < 0 || e.mods.isPopupMenu())
        return;

    dragId = tabs[i]->id;
    dragMoved = false;
    dragOffset = e.position.x - tabBounds (i).getX();
    dragX = tabBounds (i).getX();
    setSelectedTab (dragId);
}

void TabBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragId < 0 || ! reorderable)
        return;

    if (! dragMoved && std::abs (e.getDistanceFromDragStartX()) < 5)
        return;

    dragMoved = true;
    const auto w = tabWidth();
    dragX = juce::jlimit (2.0f, static_cast<float> (getWidth()) - 2.0f - w, e.position.x - dragOffset);

    // The dragged tab takes the slot its centre is over; the others close up around it.
    const auto from = indexOfId (dragId);
    const auto to = indexAt (dragX + w * 0.5f);

    if (to != from)
        tabs.move (from, to);

    layoutLeds();

    for (auto* tab : tabs)
        if (tab->id == dragId)
            tab->led->toFront (false);

    repaint();
}

void TabBar::setDragPreview (int id, float x)
{
    dragId = id;
    dragMoved = true;
    dragX = x;
    const auto from = indexOfId (id);
    const auto to = indexAt (x + tabWidth() * 0.5f);

    if (from >= 0 && to != from)
        tabs.move (from, to);

    layoutLeds();
    getLedButton (id).toFront (false);
    repaint();
}

void TabBar::mouseUp (const juce::MouseEvent&)
{
    const auto moved = dragMoved;
    dragId = -1;
    dragMoved = false;
    layoutLeds();
    repaint();

    if (moved && onOrderChanged != nullptr)
        onOrderChanged (getOrder());
}

void TabBar::mouseMove (const juce::MouseEvent& e)
{
    const auto i = indexAt (e.position.x);

    if (i != hoverIndex)
    {
        hoverIndex = i;
        repaint();
    }
}

void TabBar::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex != -1)
    {
        hoverIndex = -1;
        repaint();
    }
}
}
