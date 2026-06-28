#include "gui/MainWindow.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "graph/Nodes/AudioIn.h"
#include "graph/Nodes/AudioOut.h"
#include "graph/Nodes/GainNode.h"
#include "graph/Nodes/Merge.h"
#include "graph/Nodes/Mix.h"
#include "graph/Nodes/Split.h"
#include "graph/Nodes/VstFx.h"
#include "persist/Project.h"
#include "util/Localization.h"

bool MainWindow::loadStartupGraph()
{
    const auto preset = config.getDefaultPreset();
    if (preset.getFullPathName().isNotEmpty())
    {
        if (preset.existsAsFile())
        {
            if (loadProjectFromFile(preset))
                return true;

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   host::i18n::tr("error.loadPreset.title"),
                                                   host::i18n::tr("error.loadPreset.message").replace("%1", preset.getFullPathName()));
        }
        else
        {
            juce::Logger::writeToLog("Default preset not found: " + preset.getFullPathName());
        }
    }

    if (lastSessionFile.existsAsFile())
        return loadProjectFromFile(lastSessionFile);

    return false;
}

bool MainWindow::loadProjectFromFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    host::persist::Project project;
    if (! project.load(file))
        return false;

    rebuildGraphFromProject(project);
    return true;
}

void MainWindow::saveLastSession()
{
    if (! graphEngine)
        return;

    if (lastSessionFile.getFullPathName().isEmpty())
        return;

    auto parent = lastSessionFile.getParentDirectory();
    if (! parent.isDirectory())
        parent.createDirectory();

    host::persist::Project project;
    project.save(lastSessionFile, *graphEngine);
}

void MainWindow::initialiseGraph()
{
    graphEngine->clear();
    const auto engineCfg = deviceEngine.getEngineConfig();
    graphEngine->setEngineFormat(engineCfg.sampleRate, engineCfg.blockSize);

    auto inputId = graphEngine->addNode(std::make_unique<host::graph::nodes::AudioInNode>());
    auto outputId = graphEngine->addNode(std::make_unique<host::graph::nodes::AudioOutNode>());

    graphEngine->setIO(inputId, outputId);
    graphEngine->connect(inputId, outputId);
    graphEngine->prepare();
    graphView.refreshGraph(false);
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
        // Prefer the persisted type field; fall back to the display name when
        // older project files omit it. Both are normalised for matching.
        const auto rawType = definition.type.isNotEmpty() ? definition.type : definition.name;
        const auto normalisedType = rawType.toLowerCase().removeCharacters(" ");

        if (normalisedType == "audioin" || normalisedType == "audioinnode")
            return std::make_unique<host::graph::nodes::AudioInNode>();
        if (normalisedType == "audioout" || normalisedType == "audiooutnode")
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

                if (instance)
                    instance->queryRuntimeInfo(info);

                if (instance && definition.pluginState.getSize() > 0)
                {
                    instance->setState(static_cast<const std::uint8_t*>(definition.pluginState.getData()),
                                       static_cast<std::size_t>(definition.pluginState.getSize()));
                }
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

    try
    {
        graphEngine->prepare();
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Failed to prepare graph after project rebuild: " + juce::String(e.what()));
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.graphPrepare.title"),
                                               host::i18n::tr("error.graphPrepare.message").replace("%1", juce::String(e.what())));
        initialiseGraph();
        return;
    }

    graphView.refreshGraph(false);

    if (missingPlugins.size() > 0)
    {
        const auto missingList = missingPlugins.joinIntoString("\n");
        const auto message = host::i18n::tr("error.missingPlugins.message").replace("%1", missingList);
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.missingPlugins.title"),
                                               message);
    }
}

void MainWindow::addPluginToGraph(const host::plugin::PluginInfo& info)
{
    if (! graphEngine)
        return;

    host::plugin::PluginInfo pluginInfo = info;

    std::unique_ptr<host::plugin::PluginInstance> instance;

    try
    {
        if (pluginInfo.format == host::plugin::PluginFormat::VST2)
            instance = host::plugin::loadVst2(pluginInfo);
        else
            instance = host::plugin::loadVst3(pluginInfo);

        if (instance)
            instance->queryRuntimeInfo(pluginInfo);
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.loadPlugin.title"),
                                               host::i18n::tr("error.loadPlugin.failed").replace("%1", juce::String(e.what())));
        return;
    }

    if (! instance)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.loadPlugin.title"),
                                               host::i18n::tr("error.loadPlugin.instantiate"));
        return;
    }

    auto node = std::make_unique<host::graph::nodes::VstFxNode>(std::move(instance), pluginInfo.name, pluginInfo);
    host::graph::GraphEngine::NodeId newNodeId;

    try
    {
        newNodeId = graphEngine->addNode(std::move(node));
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.graphUpdate.title"),
                                               host::i18n::tr("error.graphUpdate.message").replace("%1", juce::String(e.what())));
        return;
    }

    const auto inputId = graphEngine->getInputNode();
    const auto outputId = graphEngine->getOutputNode();

    std::vector<host::graph::GraphEngine::NodeId> previousSources;
    auto connections = graphEngine->getConnections();

    if (! outputId.isNull())
    {
        for (const auto& connection : connections)
        {
            if (connection.second == outputId)
            {
                previousSources.push_back(connection.first);
                graphEngine->disconnect(connection.first, connection.second);
            }
        }
    }

    if (previousSources.empty())
    {
        if (! inputId.isNull())
            graphEngine->connect(inputId, newNodeId);
    }
    else
    {
        for (const auto& source : previousSources)
        {
            if (source != newNodeId)
                graphEngine->connect(source, newNodeId);
        }
    }

    if (! outputId.isNull())
        graphEngine->connect(newNodeId, outputId);

    bool preparedOK = false;
    try
    {
        graphEngine->prepare();
        preparedOK = true;
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.graphPrepare.title"),
                                               host::i18n::tr("error.graphPrepare.message").replace("%1", juce::String(e.what())));
    }

    if (! preparedOK)
    {
        if (! outputId.isNull())
            graphEngine->disconnect(newNodeId, outputId);

        if (previousSources.empty())
        {
            if (! inputId.isNull())
                graphEngine->disconnect(inputId, newNodeId);
        }
        else
        {
            for (const auto& source : previousSources)
                graphEngine->disconnect(source, newNodeId);
        }

        if (! outputId.isNull())
        {
            for (const auto& source : previousSources)
                graphEngine->connect(source, outputId);

            if (previousSources.empty() && ! inputId.isNull())
                graphEngine->connect(inputId, outputId);
        }

        try
        {
            graphEngine->prepare();
        }
        catch (const std::exception&)
        {
            // If preparation still fails, let the user fix connections manually.
        }
    }

    graphView.refreshGraph(true);
    if (! newNodeId.isNull())
        graphView.focusOnNode(newNodeId);
}

void MainWindow::loadProject()
{
    juce::FileChooser chooser(host::i18n::tr("fileChooser.openProject"),
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.json");
    if (! chooser.browseForFileToOpen())
        return;

    auto file = chooser.getResult();
    if (! loadProjectFromFile(file))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.loadProject.title"),
                                               host::i18n::tr("error.loadProject.message"));
    }
}

void MainWindow::saveProject()
{
    juce::FileChooser chooser(host::i18n::tr("fileChooser.saveProject"),
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.json");
    if (! chooser.browseForFileToSave(true))
        return;

    host::persist::Project project;
    project.save(chooser.getResult(), *graphEngine);
}
