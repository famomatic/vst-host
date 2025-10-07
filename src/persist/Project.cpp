#include "persist/Project.h"

namespace host::persist
{
    bool Project::load(const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        juce::String jsonText = file.loadFileAsString();
        juce::var root;
        auto result = juce::JSON::parse(jsonText, root);
        if (result.failed())
            return false;

        if (auto* object = root.getDynamicObject())
            projectName = object->getProperty("name");

        return true;
    }

    bool Project::save(const juce::File& file, const host::graph::GraphEngine& graph) const
    {
        juce::DynamicObject::Ptr root(new juce::DynamicObject());
        root->setProperty("name", projectName);
        root->setProperty("version", 1);

        juce::Array<juce::var> nodes;
        for (auto& id : graph.getSchedule())
        {
            if (auto* node = graph.getNode(id))
            {
                juce::DynamicObject::Ptr nodeObj(new juce::DynamicObject());
                nodeObj->setProperty("id", id.toString());
                nodeObj->setProperty("name", juce::String(node->name()));
                nodeObj->setProperty("latency", node->latencySamples());
                nodes.add(juce::var(nodeObj.get()));
            }
        }

        root->setProperty("nodes", juce::var(nodes));

        auto json = juce::JSON::toString(juce::var(root.get()), true);
        return file.replaceWithText(json);
    }
}
