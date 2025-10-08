#pragma once

#include <deque>

#include <juce_core/juce_core.h>

namespace host::util
{
class ConsoleLogger : public juce::Logger
{
public:
    static ConsoleLogger& instance();

    void logMessage(const juce::String& message) override;

    bool copyMessagesSince(size_t& lastSequence, juce::StringArray& dest) const;

private:
    ConsoleLogger() = default;
    ConsoleLogger(const ConsoleLogger&) = delete;
    ConsoleLogger& operator=(const ConsoleLogger&) = delete;

    static constexpr size_t kMaxMessages = 2000;

    mutable juce::SpinLock lock;
    std::deque<juce::String> messages;
    size_t nextSequence { 0 };
    size_t firstSequence { 0 };
};
} // namespace host::util
