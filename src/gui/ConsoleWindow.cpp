#include "gui/ConsoleWindow.h"

#include "util/Localization.h"

namespace host::gui
{
ConsoleWindow::ConsoleWindow()
    : juce::DocumentWindow(host::i18n::tr("console.title"),
                           juce::Colours::darkgrey,
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(480, 240, 1600, 900);
    setContentOwned(new ConsoleView(), true);
    setSize(900, 360);
    setVisible(false);
}

void ConsoleWindow::closeButtonPressed()
{
    setVisible(false);
    if (onHide)
        onHide();
}

void ConsoleWindow::setOnHide(std::function<void()> callback)
{
    onHide = std::move(callback);
}
} // namespace host::gui
