#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "audio/DeviceEngine.h"
#include "graph/GraphEngine.h"
#include "gui/GraphView.h"
#include "gui/PluginBrowser.h"
#include "gui/Preferences.h"
#include "host/PluginScanner.h"
#include "persist/Project.h"

class MainWindow : public juce::DocumentWindow,
                   private juce::MenuBarModel
{
public:
    MainWindow();
    ~MainWindow() override;

    void closeButtonPressed() override;
    void resized() override;

private:
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    void initialiseGraph();
    void openPreferences();
    void showDeviceSelector();
    void loadProject();
    void saveProject();
    void rebuildGraphFromProject(const host::persist::Project& project);

    std::shared_ptr<host::graph::GraphEngine> graphEngine;
    std::shared_ptr<host::plugin::PluginScanner> pluginScanner;
    host::audio::DeviceEngine deviceEngine;
    juce::AudioDeviceManager deviceManager;

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    host::gui::GraphView graphView;
    host::gui::PluginBrowser pluginBrowser;
    juce::StretchableLayoutManager layoutManager;
    juce::StretchableLayoutResizerBar resizerBar { &layoutManager, 1, true };
    juce::Component leftPanel;
    juce::Component rightPanel;
};
