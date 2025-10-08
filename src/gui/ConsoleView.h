#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "util/ConsoleLogger.h"

namespace host::gui
{
class ConsoleView : public juce::Component,
                    private juce::Timer
{
public:
    ConsoleView();
    ~ConsoleView() override = default;

    void resized() override;

    void setAutoScroll(bool shouldAutoScroll);

private:
    void timerCallback() override;
    void appendMessages(const juce::StringArray& messages, bool replaceExisting);

    juce::TextEditor logOutput;
    bool autoScroll { true };
    size_t lastSequence { 0 };
    juce::StringArray scratchBuffer;
};
} // namespace host::gui
