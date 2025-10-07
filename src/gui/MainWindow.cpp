#include "gui/MainWindow.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "graph/Nodes/AudioIn.h"
#include "graph/Nodes/AudioOut.h"
#include "graph/Nodes/GainNode.h"
#include "graph/Nodes/Merge.h"
#include "graph/Nodes/Mix.h"
#include "graph/Nodes/Split.h"
#include "graph/Nodes/VstFx.h"
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
    options.content.setOwned(new host::gui::PreferencesComponent(deviceEngine, pluginScanner, deviceManager));
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

void MainWindow::rebuildGraphFromProject(const host::persist::Project& project)
{
    if (! graphEngine)
        return;

    const auto engineConfig = deviceEngine.getEngineConfig();
    graphEngine->clear();
    graphEngine->setEngineFormat(engineConfig.sampleRate, engineConfig.blockSize);

    std::unordered_map<juce::Uuid, host::graph::GraphEngine::NodeId> idMap;
    idMap.reserve(project.getNodes().size());

    std::vector<host::graph::GraphEngine::NodeId> orderedIds;
    orderedIds.reserve(project.getNodes().size());

    juce::StringArray missingPlugins;

    auto createNodeForDefinition = [&](const host::persist::Project::NodeDefinition& definition)
        -> std::unique_ptr<host::graph::Node>
    {
        const auto normalisedType = definition.type.isNotEmpty()
                                        ? definition.type.toLowerCase().removeCharacters(" ")
                                        : definition.name.toLowerCase().removeCharacters(" ");

        if (normalisedType == "audioin" || definition.name.equalsIgnoreCase("Audio In"))
            return std::make_unique<host::graph::nodes::AudioInNode>();
        if (normalisedType == "audioout" || definition.name.equalsIgnoreCase("Audio Out"))
            return std::make_unique<host::graph::nodes::AudioOutNode>();
        if (normalisedType == "gain")
            return std::make_unique<host::graph::nodes::GainNode>();
        if (normalisedType == "mix")
            return std::make_unique<host::graph::nodes::MixNode>();
        if (normalisedType == "split")
            return std::make_unique<host::graph::nodes::SplitNode>();
        if (normalisedType == "merge")
            return std::make_unique<host::graph::nodes::MergeNode>();

        const bool looksLikePlugin = normalisedType == "vstfx"
                                     || definition.pluginPath.isNotEmpty()
                                     || definition.pluginId.isNotEmpty();

        if (looksLikePlugin)
        {
            host::plugin::PluginInfo info;
            info.id = definition.pluginId.toStdString();
            info.name = definition.name.toStdString();
            info.latency = definition.latency;
            info.ins = definition.inputs > 0 ? definition.inputs : 2;
            info.outs = definition.outputs > 0 ? definition.outputs : 2;

            if (definition.pluginFormat.equalsIgnoreCase("VST2"))
                info.format = host::plugin::PluginFormat::VST2;
            else
                info.format = host::plugin::PluginFormat::VST3;

            if (definition.pluginPath.isNotEmpty())
                info.path = std::filesystem::path(definition.pluginPath.toStdString());

            const auto hydrateFromScanner = [&]()
            {
                if ((! info.path.empty()) && definition.pluginPath.isNotEmpty())
                    return;

                if (! pluginScanner)
                    return;

                const auto discovered = pluginScanner->getDiscoveredPlugins();
                const auto it = std::find_if(discovered.begin(), discovered.end(),
                                             [&](const host::plugin::PluginInfo& candidate)
                                             {
                                                 const bool idMatches = ! info.id.empty() && candidate.id == info.id;
                                                 const bool nameMatches = ! info.name.empty() && candidate.name == info.name;
                                                 return idMatches || nameMatches;
                                             });

                if (it != discovered.end())
                    info = *it;
            };

            hydrateFromScanner();

            const auto pluginFileExists = [&]() -> bool
            {
                if (info.path.empty())
                    return false;

                std::error_code ec;
                const bool exists = std::filesystem::exists(info.path, ec);
                return exists && ec.value() == 0;
            }();

            std::unique_ptr<host::plugin::PluginInstance> instance;

            if (pluginFileExists)
            {
                if (info.format == host::plugin::PluginFormat::VST2)
                    instance = host::plugin::loadVst2(info);
                else
                    instance = host::plugin::loadVst3(info);
            }

            if (! instance)
            {
                const auto descriptor = definition.name.isNotEmpty() ? definition.name : juce::String(info.id);
                missingPlugins.add("• " + descriptor);
            }

            std::optional<host::plugin::PluginInfo> storedInfo;
            if (! info.id.empty() || ! info.name.empty() || ! info.path.empty())
                storedInfo = info;

            return std::make_unique<host::graph::nodes::VstFxNode>(std::move(instance),
                                                                   definition.name.toStdString(),
                                                                   storedInfo);
        }

        return {};
    };

    for (const auto& nodeDef : project.getNodes())
    {
        auto node = createNodeForDefinition(nodeDef);
        if (node == nullptr)
        {
            if (nodeDef.type.isNotEmpty())
                juce::Logger::writeToLog("Unknown node type in project: " + nodeDef.type);
            continue;
        }

        host::graph::GraphEngine::NodeId assignedId;

        if (! nodeDef.id.isNull())
        {
            try
            {
                assignedId = graphEngine->addNodeWithId(nodeDef.id, std::move(node));
            }
            catch (const std::exception& e)
            {
                juce::Logger::writeToLog("Failed to reuse node id " + nodeDef.id.toString() + ": " + e.what());
                assignedId = graphEngine->addNode(std::move(node));
            }
        }
        else
        {
            assignedId = graphEngine->addNode(std::move(node));
        }

        if (! nodeDef.id.isNull())
            idMap[nodeDef.id] = assignedId;

        orderedIds.push_back(assignedId);
    }

    if (! project.getConnections().empty())
    {
        for (const auto& connection : project.getConnections())
        {
            const auto fromIt = idMap.find(connection.from);
            const auto toIt = idMap.find(connection.to);
            if (fromIt != idMap.end() && toIt != idMap.end())
            {
                try
                {
                    graphEngine->connect(fromIt->second, toIt->second);
                }
                catch (const std::exception& e)
                {
                    juce::Logger::writeToLog("Failed to connect nodes: " + juce::String(e.what()));
                }
            }
        }
    }
    else
    {
        for (size_t i = 1; i < orderedIds.size(); ++i)
        {
            try
            {
                graphEngine->connect(orderedIds[i - 1], orderedIds[i]);
            }
            catch (const std::exception& e)
            {
                juce::Logger::writeToLog("Failed to connect sequential nodes: " + juce::String(e.what()));
            }
        }
    }

    const auto resolveNodeId = [&](juce::Uuid desiredId, bool useFrontFallback)
    {
        if (! desiredId.isNull())
        {
            const auto it = idMap.find(desiredId);
            if (it != idMap.end())
                return it->second;
        }

        if (! orderedIds.empty())
            return useFrontFallback ? orderedIds.front() : orderedIds.back();

        return host::graph::GraphEngine::NodeId {};
    };

    const auto inputId = resolveNodeId(project.getInputNodeId(), true);
    const auto outputId = resolveNodeId(project.getOutputNodeId(), false);

    if (! inputId.isNull() && ! outputId.isNull())
    {
        try
        {
            graphEngine->setIO(inputId, outputId);
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("Failed to assign graph IO: " + juce::String(e.what()));
        }
    }

    graphEngine->prepare();
    graphView.repaint();

    if (missingPlugins.size() > 0)
    {
        const auto message = "Some plugins could not be loaded:\n" + missingPlugins.joinIntoString("\n");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Missing Plugins", message);
    }
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
        rebuildGraphFromProject(project);
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Load Failed",
                                               "Unable to load the selected project file.");
    }
}

void MainWindow::saveProject()
{
    juce::FileChooser chooser("Save project", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    if (! chooser.browseForFileToSave(true))
        return;

    host::persist::Project project;
    project.save(chooser.getResult(), *graphEngine);
}
