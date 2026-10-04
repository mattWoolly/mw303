#pragma once
// One-line help for every parameter, shown as the control's tooltip: what the control does,
// in the plate's own words. The editor sets it on every control it attaches.
#include "acid/engine/ParamTable.h"
#include <juce_core/juce_core.h>

namespace acid::ui
{
/** "Name: what it does." for a parameter (the name alone when no help is written for it). */
juce::String paramTooltip (ParamId);
}
