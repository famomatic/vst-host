#include "persist/ChainPreset.h"

#include "graph/Nodes/VstFx.h"

namespace host::persist
{
void ChainPreset::captureFromGraph(const host::graph::GraphEngine& graph)
{
    snapshots.clear();

    for (const auto& id : graph.getNodeIds())
    {
        auto node = graph.getNode(id);
        if (! node)
            continue;

        // VST nodes keep their own state via the per-plugin Preset format;
        // chain presets only snapshot built-in effect parameters.
        if (dynamic_cast<host::graph::nodes::VstFxNode*>(node.get()) != nullptr)
            continue;

        auto params = node->getParameters();
        if (params.empty())
            continue;

        ChainNodeSnapshot snapshot;
        snapshot.nodeId = id;
        snapshot.typeId = node->typeId();
        snapshot.displayName = node->name();
        snapshot.parameters = std::move(params);
        snapshots.push_back(std::move(snapshot));
    }
}

void ChainPreset::applyToGraph(host::graph::GraphEngine& graph) const
{
    // Push every captured parameter through requestParameterChange so the
    // audio thread drains them together at the next block boundary. This is
    // the delay-free path: no swap-buffer latency, no crossfade buffer.
    for (const auto& snapshot : snapshots)
    {
        // Match by node id when possible; otherwise fall back to typeId so a
        // preset recalled into a rebuilt graph (different ids) still lands on
        // the right node type.
        auto node = graph.getNode(snapshot.nodeId);
        if (! node)
        {
            for (const auto& candidateId : graph.getNodeIds())
            {
                auto candidate = graph.getNode(candidateId);
                if (candidate && candidate->typeId() == snapshot.typeId)
                {
                    node = candidate;
                    break;
                }
            }
        }

        if (! node)
            continue;

        for (const auto& p : snapshot.parameters)
            node->requestParameterChange(p.id, p.value);
    }
}

bool ChainPreset::load(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    juce::var root;
    if (juce::JSON::parse(file.loadFileAsString(), root).failed())
        return false;

    auto* object = root.getDynamicObject();
    if (object == nullptr)
        return false;

    name = object->getProperty("name").toString();
    if (name.isEmpty())
        name = file.getFileNameWithoutExtension();

    snapshots.clear();
    if (auto* nodeArray = object->getProperty("nodes").getArray())
    {
        for (const auto& nodeVar : *nodeArray)
        {
            auto* nodeObj = nodeVar.getDynamicObject();
            if (nodeObj == nullptr)
                continue;

            ChainNodeSnapshot snapshot;
            snapshot.nodeId = juce::Uuid(nodeObj->getProperty("id").toString());
            snapshot.typeId = nodeObj->getProperty("typeId").toString().toStdString();
            snapshot.displayName = nodeObj->getProperty("name").toString().toStdString();

            if (auto* paramArray = nodeObj->getProperty("parameters").getArray())
            {
                for (const auto& paramVar : *paramArray)
                {
                    auto* paramObj = paramVar.getDynamicObject();
                    if (paramObj == nullptr)
                        continue;

                    host::graph::NodeParameter p;
                    p.id = paramObj->getProperty("id").toString().toStdString();
                    p.value = paramObj->getProperty("value");
                    snapshot.parameters.push_back(std::move(p));
                }
            }

            snapshots.push_back(std::move(snapshot));
        }
    }

    return true;
}

bool ChainPreset::save(const juce::File& file) const
{
    if (! file.getParentDirectory().isDirectory())
        file.getParentDirectory().createDirectory();

    juce::DynamicObject::Ptr root(new juce::DynamicObject());
    root->setProperty("name", name);
    root->setProperty("version", 1);

    juce::Array<juce::var> nodeArray;
    for (const auto& snapshot : snapshots)
    {
        juce::DynamicObject::Ptr nodeObj(new juce::DynamicObject());
        nodeObj->setProperty("id", snapshot.nodeId.toString());
        nodeObj->setProperty("typeId", juce::String(snapshot.typeId));
        nodeObj->setProperty("name", juce::String(snapshot.displayName));

        juce::Array<juce::var> paramArray;
        for (const auto& p : snapshot.parameters)
        {
            juce::DynamicObject::Ptr paramObj(new juce::DynamicObject());
            paramObj->setProperty("id", juce::String(p.id));
            paramObj->setProperty("value", p.value);
            paramArray.add(juce::var(paramObj.get()));
        }
        nodeObj->setProperty("parameters", juce::var(paramArray));

        nodeArray.add(juce::var(nodeObj.get()));
    }

    root->setProperty("nodes", juce::var(nodeArray));

    const auto json = juce::JSON::toString(juce::var(root.get()), true);
    return file.replaceWithText(json);
}
} // namespace host::persist