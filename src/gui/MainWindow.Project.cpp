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
#include "graph/Nodes/VstFx.h"
#include "graph/NodeFactory.h"
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
        // Built-in nodes (Gain, EQ, Compressor, Reverb, Delay, routing) go
        // through the factory. Only VST plugins need the bespoke loader below.
        const auto rawType = definition.type.isNotEmpty() ? definition.type : definition.name;

        if (auto node = host::graph::NodeFactory::createFromPersistedName(rawType.toStdString()))
        {
            // Restore saved parameters for effect nodes.
            if (definition.parameters.isNotEmpty())
            {
                // Parameters are stored as JSON in the project; parsed and
                // applied here so presets and projects share the format.
                juce::var paramsVar;
                if (juce::JSON::parse(definition.parameters, paramsVar).wasOk())
                {
                    std::vector<host::graph::NodeParameter> params;
                    if (auto* arr = paramsVar.getArray())
                    {
                        for (const auto& item : *arr)
                        {
                            if (auto* obj = item.getDynamicObject())
                            {
                                host::graph::NodeParameter p;
                                p.id = obj->getProperty("id").toString().toStdString();
                                p.value = obj->getProperty("value");
                                params.push_back(p);
                            }
                        }
                        node->setParameters(params);
                    }
                }
            }
            return node;
        }

        const bool looksLikePlugin = rawType.toLowerCase().removeCharacters(" ") == "vstfx"
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
                if (pluginScanner)
                    instance = pluginScanner->loader().load(info);

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
        instance = pluginScanner->loader().load(pluginInfo);

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

    // Decide where to splice the new node. Preference order:
    //   1. Tail nodes that have no outgoing connection (the user left the
    //      output unconnected) -> chain the new node after them, then connect
    //      the new node to the output. This gives the requested
    //      "input -> new -> output" structure when nothing was wired to the
    //      output yet.
    //   2. Otherwise splice the new node just before the output (the classic
    //      insert-at-end behaviour).
    std::vector<host::graph::GraphEngine::NodeId> sourcesToConnect;
    std::vector<host::graph::GraphEngine::NodeId> nodesBeforeOutput;
    bool spliceBeforeOutput = false;

    auto connections = graphEngine->getConnections();

    if (! outputId.isNull())
    {
        for (const auto& connection : connections)
        {
            if (connection.second == outputId)
                nodesBeforeOutput.push_back(connection.first);
        }
    }

    // Find tail nodes: registered nodes (excluding input/output/io) that have
    // no outgoing edge at all. These are plugins the user added but never
    // wired onward. Chaining the new node after them is the most useful
    // behaviour because it actually routes their audio somewhere.
    const auto allNodeIds = graphEngine->getNodeIds();
    std::vector<host::graph::GraphEngine::NodeId> tailNodes;
    for (const auto& id : allNodeIds)
    {
        if (id == inputId || id == outputId || id == newNodeId)
            continue;

        bool hasOutgoing = false;
        for (const auto& connection : connections)
        {
            if (connection.first == id)
            {
                hasOutgoing = true;
                break;
            }
        }

        if (! hasOutgoing)
            tailNodes.push_back(id);
    }

    if (! tailNodes.empty())
    {
        // Chain after the tail nodes; do not touch the output edge yet.
        sourcesToConnect = tailNodes;
    }
    else if (! nodesBeforeOutput.empty())
    {
        // Classic insert-at-end: disconnect the nodes feeding the output and
        // rewire them through the new node.
        spliceBeforeOutput = true;
        sourcesToConnect = nodesBeforeOutput;
        for (const auto& source : sourcesToConnect)
            graphEngine->disconnect(source, outputId);
    }
    else
    {
        // No tail nodes and nothing wired to the output: feed from the input
        // so the chain is input -> new -> output.
        if (! inputId.isNull())
            sourcesToConnect.push_back(inputId);
    }

    for (const auto& source : sourcesToConnect)
    {
        if (source != newNodeId)
            graphEngine->connect(source, newNodeId);
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
        // Roll back the connections we added so the graph is left in the same
        // shape it was before the failed insert: disconnect everything we
        // wired to/from the new node, then restore the original output edges.
        if (! outputId.isNull())
            graphEngine->disconnect(newNodeId, outputId);

        for (const auto& source : sourcesToConnect)
        {
            if (source != newNodeId)
                graphEngine->disconnect(source, newNodeId);
        }

        if (! outputId.isNull())
        {
            // Restore the original output edges. Only the splice-before-output
            // path actually disconnected them; in the tail path there was
            // nothing wired to the output, so restoring amounts to reconnecting
            // input -> output when no tail/sources existed.
            if (spliceBeforeOutput)
            {
                for (const auto& source : nodesBeforeOutput)
                    graphEngine->connect(source, outputId);
            }
            else if (nodesBeforeOutput.empty() && ! inputId.isNull())
            {
                graphEngine->connect(inputId, outputId);
            }
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

void MainWindow::addBuiltinNodeToGraph(const std::string& typeId)
{
    auto node = host::graph::NodeFactory::create(typeId);
    if (! node)
        return;

    // Audio In / Audio Out are structural and must not be inserted mid-chain.
    if (typeId == "AudioIn" || typeId == "AudioOut")
    {
        try
        {
            const auto id = graphEngine->addNode(std::move(node));
            graphEngine->prepare();
            graphView.refreshGraph(true);
            graphView.focusOnNode(id);
        }
        catch (const std::exception&)
        {
        }
        return;
    }

    host::graph::GraphEngine::NodeId newNodeId;
    try
    {
        newNodeId = graphEngine->addNode(std::move(node));
    }
    catch (const std::exception&)
    {
        return;
    }

    // Insert between the current chain tail and the output, mirroring how
    // plugins are added so the effect sits in the signal path immediately.
    const auto inputId = graphEngine->getInputNode();
    const auto outputId = graphEngine->getOutputNode();

    auto connections = graphEngine->getConnections();
    std::vector<host::graph::GraphEngine::NodeId> nodesBeforeOutput;
    for (const auto& connection : connections)
    {
        if (connection.second == outputId && ! outputId.isNull())
            nodesBeforeOutput.push_back(connection.first);
    }

    // Prefer chaining after tail nodes (no outgoing edge) so a half-wired
    // graph still routes audio through the new effect. Falls back to the
    // classic insert-before-output splice, and finally to input -> new ->
    // output when nothing is wired yet. Mirrors addPluginToGraph().
    std::vector<host::graph::GraphEngine::NodeId> sourcesToConnect;
    bool spliceBeforeOutput = false;

    const auto allNodeIds = graphEngine->getNodeIds();
    std::vector<host::graph::GraphEngine::NodeId> tailNodes;
    for (const auto& id : allNodeIds)
    {
        if (id == inputId || id == outputId || id == newNodeId)
            continue;

        bool hasOutgoing = false;
        for (const auto& connection : connections)
        {
            if (connection.first == id)
            {
                hasOutgoing = true;
                break;
            }
        }

        if (! hasOutgoing)
            tailNodes.push_back(id);
    }

    if (! tailNodes.empty())
    {
        sourcesToConnect = tailNodes;
    }
    else if (! nodesBeforeOutput.empty())
    {
        spliceBeforeOutput = true;
        sourcesToConnect = nodesBeforeOutput;
        for (const auto& source : sourcesToConnect)
            graphEngine->disconnect(source, outputId);
    }
    else if (! inputId.isNull())
    {
        sourcesToConnect.push_back(inputId);
    }

    for (const auto& source : sourcesToConnect)
        if (source != newNodeId)
            graphEngine->connect(source, newNodeId);

    if (! outputId.isNull())
        graphEngine->connect(newNodeId, outputId);

    try
    {
        graphEngine->prepare();
    }
    catch (const std::exception&)
    {
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
