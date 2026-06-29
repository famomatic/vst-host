#pragma once

#include "graph/GraphEngine.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace host::persist
{
/// One effect node's captured parameters inside a chain preset.
struct ChainNodeSnapshot
{
    juce::Uuid nodeId;
    std::string typeId;
    std::string displayName;
    std::vector<host::graph::NodeParameter> parameters;
};

/// Snapshot of every parameterised node in the graph. Recalling a chain preset
/// re-applies all captured parameters in a single pass so the whole sound
/// morphs at once - the "macro panel" use case.
///
/// VST plugin state is intentionally NOT captured here: that is owned by the
/// per-plugin Preset format. Chain presets only cover built-in effect nodes
/// (Gain, EQ, Compressor, Reverb, Delay, ...), which is where instant
/// glitch-free switching is actually achievable.
class ChainPreset
{
public:
    /// Capture the current parameters of every node that exposes any.
    void captureFromGraph(const host::graph::GraphEngine& graph);

    /// Apply the snapshot back to the graph. Uses requestParameterChange so the
    /// audio thread applies each change at the next block boundary instead of
    /// mid-block, which is what makes the switch delay-free and glitch-free.
    void applyToGraph(host::graph::GraphEngine& graph) const;

    bool load(const juce::File& file);
    bool save(const juce::File& file) const;

    juce::String getName() const noexcept { return name; }
    void setName(juce::String newName) { name = std::move(newName); }

    const std::vector<ChainNodeSnapshot>& getSnapshots() const noexcept { return snapshots; }

private:
    juce::String name { "Chain Preset" };
    std::vector<ChainNodeSnapshot> snapshots;
};
} // namespace host::persist