#pragma once

#include <juce_core/juce_core.h>

#include "graph/GraphEngine.h"

namespace host::persist
{
    class Project
    {
    public:
        bool load(const juce::File& file);
        bool save(const juce::File& file, const host::graph::GraphEngine& graph) const;

        juce::String getProjectName() const noexcept { return projectName; }

    private:
        juce::String projectName { "Untitled" };
    };
}
