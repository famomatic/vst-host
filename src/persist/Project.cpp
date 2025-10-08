#include "persist/Project.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "graph/Nodes/AudioIn.h"
#include "graph/Nodes/AudioOut.h"
#include "graph/Nodes/GainNode.h"
#include "graph/Nodes/Merge.h"
#include "graph/Nodes/Mix.h"
#include "graph/Nodes/Split.h"
#include "graph/Nodes/VstFx.h"

namespace host::persist
{
namespace
{
    [[nodiscard]] juce::Uuid readUuid(const juce::var& value)
    {
        if (value.isString())
        {
            const auto text = value.toString().trim();
            if (text.isNotEmpty())
                return juce::Uuid(text);
        }

        return {};
    }

    [[nodiscard]] juce::String readString(const juce::var& value)
    {
        if (value.isString())
            return value.toString();
        return {};
    }

    [[nodiscard]] int readInt(const juce::var& value, int defaultValue = 0)
    {
        if (value.isInt() || value.isInt64())
            return static_cast<int>(value);

        if (value.isDouble())
            return static_cast<int>(static_cast<double>(value));

        if (value.isBool())
            return static_cast<bool>(value) ? 1 : 0;

        if (value.isString())
            return value.toString().getIntValue();

        return defaultValue;
    }

    [[nodiscard]] juce::String nodeTypeFromInstance(const host::graph::Node& node)
    {
        using namespace host::graph::nodes;

        if (dynamic_cast<const AudioInNode*>(&node) != nullptr)
            return "AudioIn";
        if (dynamic_cast<const AudioOutNode*>(&node) != nullptr)
            return "AudioOut";
        if (dynamic_cast<const GainNode*>(&node) != nullptr)
            return "Gain";
        if (dynamic_cast<const MixNode*>(&node) != nullptr)
            return "Mix";
        if (dynamic_cast<const SplitNode*>(&node) != nullptr)
            return "Split";
        if (dynamic_cast<const MergeNode*>(&node) != nullptr)
            return "Merge";
        if (dynamic_cast<const VstFxNode*>(&node) != nullptr)
            return "VstFx";

        return juce::String(node.name());
    }

    [[nodiscard]] juce::String pluginFormatToString(host::plugin::PluginFormat format)
    {
        switch (format)
        {
            case host::plugin::PluginFormat::VST2: return "VST2";
            case host::plugin::PluginFormat::VST3: return "VST3";
            default: break;
        }

        return {};
    }

    [[nodiscard]] bool matchesType(const Project::NodeDefinition& definition, const juce::String& desired)
    {
        const auto normalise = [](juce::String text)
        {
            return text.toLowerCase().removeCharacters(" ");
        };

        const auto target = normalise(desired);
        return normalise(definition.type) == target
               || normalise(definition.name) == target;
    }
} // namespace

    bool Project::load(const juce::File& file)
    {
        nodes.clear();
        connections.clear();
        inputNodeId = {};
        outputNodeId = {};

        if (! file.existsAsFile())
            return false;

        juce::String jsonText = file.loadFileAsString();
        juce::var root;
        const auto result = juce::JSON::parse(jsonText, root);
        if (result.failed())
            return false;

        if (auto* object = root.getDynamicObject())
        {
            if (object->hasProperty("name"))
                projectName = object->getProperty("name").toString();

            inputNodeId = readUuid(object->getProperty("inputNodeId"));
            outputNodeId = readUuid(object->getProperty("outputNodeId"));

            if (auto* nodeArray = object->getProperty("nodes").getArray())
            {
                nodes.reserve(nodeArray->size());
                for (const auto& nodeVar : *nodeArray)
                {
                    if (auto* nodeObj = nodeVar.getDynamicObject())
                    {
                        NodeDefinition definition;
                        definition.id = readUuid(nodeObj->getProperty("id"));
                        definition.name = readString(nodeObj->getProperty("name"));
                        definition.type = readString(nodeObj->getProperty("type"));
                        if (definition.type.isEmpty())
                            definition.type = definition.name;
                        definition.pluginId = readString(nodeObj->getProperty("pluginId"));
                        definition.pluginPath = readString(nodeObj->getProperty("pluginPath"));
                        definition.pluginFormat = readString(nodeObj->getProperty("pluginFormat"));
                        definition.inputs = readInt(nodeObj->getProperty("inputs"));
                        definition.outputs = readInt(nodeObj->getProperty("outputs"));
                        definition.latency = readInt(nodeObj->getProperty("latency"));
                        const auto stateVar = nodeObj->getProperty("pluginState");
                        if (stateVar.isString())
                        {
                            definition.pluginState.fromBase64Encoding(stateVar.toString());
                        }
                        nodes.push_back(std::move(definition));
                    }
                }
            }

            if (auto* connectionArray = object->getProperty("connections").getArray())
            {
                connections.reserve(connectionArray->size());
                for (const auto& connVar : *connectionArray)
                {
                    if (auto* connObj = connVar.getDynamicObject())
                    {
                        ConnectionDefinition connection;
                        connection.from = readUuid(connObj->getProperty("from"));
                        connection.to = readUuid(connObj->getProperty("to"));
                        if (! connection.from.isNull() && ! connection.to.isNull())
                            connections.push_back(connection);
                    }
                }
            }
        }

        if (inputNodeId.isNull())
        {
            const auto it = std::find_if(nodes.begin(), nodes.end(),
                                         [](const NodeDefinition& def)
                                         {
                                             return matchesType(def, "AudioIn") || matchesType(def, "Audio In");
                                         });
            if (it != nodes.end())
                inputNodeId = it->id;
        }

        if (outputNodeId.isNull())
        {
            const auto it = std::find_if(nodes.begin(), nodes.end(),
                                         [](const NodeDefinition& def)
                                         {
                                             return matchesType(def, "AudioOut") || matchesType(def, "Audio Out");
                                         });
            if (it != nodes.end())
                outputNodeId = it->id;
        }

        return true;
    }

    bool Project::save(const juce::File& file, const host::graph::GraphEngine& graph) const
    {
        juce::DynamicObject::Ptr root(new juce::DynamicObject());
        root->setProperty("name", projectName);
        root->setProperty("version", 1);

        juce::Array<juce::var> nodeArray;
        for (auto& id : graph.getSchedule())
        {
            if (auto* node = graph.getNode(id))
            {
                juce::DynamicObject::Ptr nodeObj(new juce::DynamicObject());
                nodeObj->setProperty("id", id.toString());
                nodeObj->setProperty("name", juce::String(node->name()));
                nodeObj->setProperty("type", nodeTypeFromInstance(*node));
                nodeObj->setProperty("latency", node->latencySamples());

                if (const auto* vstNode = dynamic_cast<const host::graph::nodes::VstFxNode*>(node))
                {
                    if (const auto& info = vstNode->pluginInfo())
                    {
                        nodeObj->setProperty("pluginId", juce::String(info->id));
                        nodeObj->setProperty("pluginFormat", pluginFormatToString(info->format));
                        nodeObj->setProperty("pluginPath", juce::String(info->path.generic_string()));
                        nodeObj->setProperty("inputs", info->ins);
                        nodeObj->setProperty("outputs", info->outs);
                        nodeObj->setProperty("pluginLatency", info->latency);
                    }

                    if (auto* instance = vstNode->plugin())
                    {
                        std::vector<std::uint8_t> stateData;
                        if (instance->getState(stateData) && ! stateData.empty())
                        {
                            juce::MemoryBlock block(stateData.data(), stateData.size());
                            nodeObj->setProperty("pluginState", block.toBase64Encoding());
                        }
                    }
                }

                nodeArray.add(juce::var(nodeObj.get()));
            }
        }

        root->setProperty("nodes", juce::var(nodeArray));

        juce::Array<juce::var> connectionArray;
        for (const auto& connection : graph.getConnections())
        {
            juce::DynamicObject::Ptr connObj(new juce::DynamicObject());
            connObj->setProperty("from", connection.first.toString());
            connObj->setProperty("to", connection.second.toString());
            connectionArray.add(juce::var(connObj.get()));
        }

        root->setProperty("connections", juce::var(connectionArray));

        const auto inputId = graph.getInputNode();
        if (! inputId.isNull())
            root->setProperty("inputNodeId", inputId.toString());

        const auto outputId = graph.getOutputNode();
        if (! outputId.isNull())
            root->setProperty("outputNodeId", outputId.toString());

        const auto json = juce::JSON::toString(juce::var(root.get()), true);
        return file.replaceWithText(json);
    }
}
