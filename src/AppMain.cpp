#include "gui/MainWindow.h"
#include "util/ConsoleLogger.h"

class VSTHostApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "VST Host Scaffold"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        juce::Logger::setCurrentLogger(&host::util::ConsoleLogger::instance());
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::Logger::setCurrentLogger(nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VSTHostApplication)
