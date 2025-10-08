#pragma once

#include <functional>

#include <juce_gui_extra/juce_gui_extra.h>

#include "gui/ConsoleView.h"

namespace host::gui
{
class ConsoleWindow : public juce::DocumentWindow
{
public:
    ConsoleWindow();
    ~ConsoleWindow() override = default;

    void closeButtonPressed() override;

    void setOnHide(std::function<void()> callback);

private:
    std::function<void()> onHide;
};
} // namespace host::gui

