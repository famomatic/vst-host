#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace Steinberg
{
class IPlugView;
}

namespace host::plugin::vst3editor
{
std::unique_ptr<juce::Component> createEditorComponent(
    const std::function<Steinberg::IPlugView*()>& createView,
    const std::function<void(Steinberg::IPlugView*)>& releaseView);
}

