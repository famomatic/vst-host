#include "gui/MainWindow.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "graph/Nodes/AudioIn.h"
#include "graph/Nodes/AudioOut.h"
#include "persist/Project.h"

namespace
{
    enum MenuItem
    {
        menuOpen = 1,
        menuSave,
        menuPreferences,
        menuDeviceSelector,
        menuRescan
    };
}

MainWindow::MainWindow()
    : juce::DocumentWindow("VST Host Scaffold", juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
      graphEngine(std::make_shared<host::graph::GraphEngine>()),
      pluginScanner(std::make_shared<host::plugin::PluginScanner>()),
      deviceEngine()
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);

    deviceEngine.setGraph(graphEngine);
    deviceEngine.setEngineConfig({ 48000.0, 256 });

    deviceManager.initialise(0, 2, nullptr, true);
    deviceManager.addAudioCallback(&deviceEngine);

    pluginBrowser.setScanner(pluginScanner);
    graphView.setGraph(graphEngine);

    leftPanel.addAndMakeVisible(pluginBrowser);
    rightPanel.addAndMakeVisible(graphView);

    layoutManager.setItemLayout(0, 200, 400, 260);
    layoutManager.setItemLayout(1, 4, 8, 6);
    layoutManager.setItemLayout(2, 200, -0.9, -0.9);

    auto* content = new juce::Component();
    content->addAndMakeVisible(leftPanel);
    content->addAndMakeVisible(resizerBar);
    content->addAndMakeVisible(rightPanel);
    setContentOwned(content, true);

    setMenuBar(this);

    initialiseGraph();

    setCentrePosition(200, 200);
    setSize(1024, 768);
    setVisible(true);
}

MainWindow::~MainWindow()
{
    setMenuBar(nullptr);
    deviceManager.removeAudioCallback(&deviceEngine);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::resized()
{
    juce::DocumentWindow::resized();

    if (auto* content = getContentComponent())
    {
        auto area = content->getLocalBounds();
        juce::Component* comps[] = { &leftPanel, &resizerBar, &rightPanel };
        layoutManager.layOutComponents(comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);
        pluginBrowser.setBounds(leftPanel.getLocalBounds());
        graphView.setBounds(rightPanel.getLocalBounds());
    }
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return { "File", "Edit", "View" };
}

juce::PopupMenu MainWindow::getMenuForIndex(int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)
    {
        menu.addItem(menuOpen, "Open Project...");
        menu.addItem(menuSave, "Save Project");
        menu.addSeparator();
        menu.addItem(menuDeviceSelector, "Audio Device Setup...");
        menu.addItem(menuPreferences, "Preferences...");
    }
    else if (menuIndex == 1)
    {
        menu.addItem(menuRescan, "Rescan Plugins");
    }

    return menu;
}

void MainWindow::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
        case menuOpen:      loadProject(); break;
        case menuSave:      saveProject(); break;
        case menuPreferences: openPreferences(); break;
        case menuDeviceSelector: showDeviceSelector(); break;
        case menuRescan:
            if (pluginScanner)
                pluginScanner->scanAsync();
            break;
        default:
            break;
    }
}

void MainWindow::initialiseGraph()
{
    graphEngine->clear();
    graphEngine->setEngineFormat(48000.0, 256);

    auto inputId = graphEngine->addNode(std::make_unique<host::graph::nodes::AudioInNode>());
    auto outputId = graphEngine->addNode(std::make_unique<host::graph::nodes::AudioOutNode>());

    graphEngine->setIO(inputId, outputId);
    graphEngine->connect(inputId, outputId);
    graphEngine->prepare();
}

void MainWindow::openPreferences()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new host::gui::PreferencesComponent(deviceEngine, pluginScanner));
    options.dialogTitle = "Preferences";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.runModal();
}

void MainWindow::showDeviceSelector()
{
    juce::AudioDeviceSelectorComponent selector(deviceManager, 0, 2, 0, 2, true, true, true, false);
    selector.setSize(500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setNonOwned(&selector);
    options.dialogTitle = "Audio Device Settings";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.runModal();
}

void MainWindow::loadProject()
{
    juce::FileChooser chooser("Open project", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    if (! chooser.browseForFileToOpen())
        return;

    auto file = chooser.getResult();
    host::persist::Project project;
    if (project.load(file))
    {
        // TODO: reconstruct graph and plugins from project description.
    }
}

void MainWindow::saveProject()
{
    juce::FileChooser chooser("Save project", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    if (! chooser.browseForFileToSave(true))
        return;

    host::persist::Project project;
    project.save(chooser.getResult(), *graphEngine);
<<<<<<< HEAD
}
=======
}
>>>>>>> origin/main
