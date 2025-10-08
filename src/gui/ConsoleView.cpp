#include "gui/ConsoleView.h"

namespace host::gui
{
ConsoleView::ConsoleView()
{
    logOutput.setMultiLine(true, true);
    logOutput.setReadOnly(true);
    logOutput.setCaretVisible(false);
    logOutput.setScrollbarsShown(true);
    logOutput.setPopupMenuEnabled(false);
    const auto fontOptions = juce::FontOptions().withHeight(13.0f).withName(juce::Font::getDefaultMonospacedFontName());
    juce::Font consoleFont(fontOptions);
    logOutput.setFont(consoleFont);
    addAndMakeVisible(logOutput);

    scratchBuffer.ensureStorageAllocated(64);

    const bool truncated = host::util::ConsoleLogger::instance().copyMessagesSince(lastSequence, scratchBuffer);
    appendMessages(scratchBuffer, truncated);

    startTimer(250);
}

void ConsoleView::resized()
{
    logOutput.setBounds(getLocalBounds());
}

void ConsoleView::setAutoScroll(bool shouldAutoScroll)
{
    autoScroll = shouldAutoScroll;
}

void ConsoleView::timerCallback()
{
    const bool truncated = host::util::ConsoleLogger::instance().copyMessagesSince(lastSequence, scratchBuffer);

    if (scratchBuffer.isEmpty() && ! truncated)
        return;

    appendMessages(scratchBuffer, truncated);
}

void ConsoleView::appendMessages(const juce::StringArray& messages, bool replaceExisting)
{
    if (messages.isEmpty() && ! replaceExisting)
        return;

    juce::String combined;
    combined.preallocateBytes(static_cast<int>(messages.size() * 64));
    for (const auto& line : messages)
    {
        combined << line;
        combined << '\n';
    }

    if (replaceExisting)
    {
        logOutput.setText(combined, false);
    }
    else if (combined.isNotEmpty())
    {
        logOutput.moveCaretToEnd(false);
        logOutput.insertTextAtCaret(combined);
    }

    if (autoScroll)
    {
        logOutput.moveCaretToEnd(false);
        const auto caretRect = logOutput.getCaretRectangleForCharIndex(logOutput.getCaretPosition());
        logOutput.scrollEditorToPositionCaret(caretRect.getX(), caretRect.getY());
    }
}
} // namespace host::gui
