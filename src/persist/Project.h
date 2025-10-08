#pragma once

#include <juce_core/juce_core.h>

#include "graph/GraphEngine.h"

#include <vector>

namespace host::persist
{
    class Project
    {
    public:
        bool load(const juce::File& file);
        bool save(const juce::File& file, const host::graph::GraphEngine& graph) const;

        juce::String getProjectName() const noexcept { return projectName; }
        struct NodeDefinition
        {
            juce::Uuid id;
            juce::String type;
            juce::String name;
            juce::String pluginId;
            juce::String pluginPath;
            juce::String pluginFormat;
            int inputs { 0 };
            int outputs { 0 };
            int latency { 0 };
            juce::MemoryBlock pluginState;
        };

        struct ConnectionDefinition
        {
            juce::Uuid from;
            juce::Uuid to;
        };

        const std::vector<NodeDefinition>& getNodes() const noexcept { return nodes; }
        const std::vector<ConnectionDefinition>& getConnections() const noexcept { return connections; }
        juce::Uuid getInputNodeId() const noexcept { return inputNodeId; }
        juce::Uuid getOutputNodeId() const noexcept { return outputNodeId; }

    private:
        juce::String projectName { "Untitled" };
        std::vector<NodeDefinition> nodes;
        std::vector<ConnectionDefinition> connections;
        juce::Uuid inputNodeId;
        juce::Uuid outputNodeId;
    };
}
