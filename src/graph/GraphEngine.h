#pragma once

#include "graph/Node.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace host::graph
{
class GraphEngine
{
public:
    using NodeId = juce::Uuid;

    GraphEngine() = default;
    ~GraphEngine() = default;

    GraphEngine(const GraphEngine&) = delete;
    GraphEngine& operator=(const GraphEngine&) = delete;

    GraphEngine(GraphEngine&&) = delete;
    GraphEngine& operator=(GraphEngine&&) = delete;

    void clear();

    NodeId addNode(std::unique_ptr<Node> node);
    [[nodiscard]] Node* getNode(const NodeId& id) const;

    void setIO(NodeId inputNode, NodeId outputNode);
    void connect(NodeId from, NodeId to);

    void setEngineFormat(double sampleRate, int blockSize);
    void prepare();
    [[nodiscard]] int process(juce::AudioBuffer<float>& buffer);

    [[nodiscard]] std::vector<NodeId> getSchedule() const;

private:
    struct NodeEntry
    {
        NodeId id;
        std::unique_ptr<Node> node;
        std::vector<NodeId> outputs;
    };

    [[nodiscard]] static std::string toKey(const NodeId& id);
    [[nodiscard]] Node* getNodeUnlocked(const NodeId& id) const;
    [[nodiscard]] bool hasNodeUnlocked(const NodeId& id) const;
    void buildScheduleUnlocked();

    mutable std::mutex mutex_;
    std::vector<NodeEntry> nodes_;
    std::unordered_map<std::string, size_t> indexById_;
    std::vector<NodeId> schedule_;
    std::vector<float*> channelPointers_;

    NodeId inputNode_;
    NodeId outputNode_;

    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    bool prepared_ = false;
};
} // namespace host::graph
