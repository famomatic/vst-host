#include "util/ConsoleLogger.h"

namespace host::util
{
ConsoleLogger& ConsoleLogger::instance()
{
    static ConsoleLogger logger;
    return logger;
}

void ConsoleLogger::logMessage(const juce::String& message)
{
    const auto timestamp = juce::Time::getCurrentTime().toISO8601(true);
    const juce::String formatted = timestamp + " " + message;

    {
        const juce::SpinLock::ScopedLockType guard(lock);
        messages.push_back(formatted);
        ++nextSequence;
        if (messages.size() > kMaxMessages)
        {
            messages.pop_front();
            ++firstSequence;
        }
    }

    juce::Logger::outputDebugString(formatted);
}

bool ConsoleLogger::copyMessagesSince(size_t& lastSequence, juce::StringArray& dest) const
{
    dest.clearQuick();

    const juce::SpinLock::ScopedLockType guard(lock);

    const size_t availableStart = firstSequence;
    const bool truncated = lastSequence < availableStart;
    const size_t startSequence = truncated ? availableStart : lastSequence;

    const size_t offset = startSequence - availableStart;
    const size_t total = messages.size();
    for (size_t i = offset; i < total; ++i)
        dest.add(messages[i]);

    lastSequence = nextSequence;
    return truncated;
}
} // namespace host::util
