#pragma once

#include "graph/Node.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
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
    NodeId addNodeWithId(const NodeId& id, std::unique_ptr<Node> node);
    [[nodiscard]] std::shared_ptr<Node> getNode(const NodeId& id) const;
    void removeNode(NodeId id);

    void setIO(NodeId inputNode, NodeId outputNode);
    void connect(NodeId from, NodeId to);
    void disconnect(NodeId from, NodeId to);

    void setEngineFormat(double sampleRate, int blockSize);
    void prepare();
    /// Enable/disable Plugin Delay Compensation. When enabled (default) the
    /// runtime inserts delay lines so every path stays aligned with the
    /// longest-latency chain. Disabled = low-latency passthrough, paths drift.
    void setPdcEnabled(bool enabled);
    // hostTimeNs optional: when provided by the device callback (ASIO), it is
    // forwarded into each node's ProcessContext so time-aware plugins stay in
    // sync with the audio hardware clock.
    [[nodiscard]] int process(juce::AudioBuffer<float>& buffer, const std::uint64_t* hostTimeNs = nullptr);

    [[nodiscard]] std::vector<NodeId> getSchedule() const;
    [[nodiscard]] std::vector<NodeId> getNodeIds() const;
    [[nodiscard]] std::vector<std::pair<NodeId, NodeId>> getConnections() const;
    [[nodiscard]] NodeId getInputNode() const;
    [[nodiscard]] NodeId getOutputNode() const;

private:
    struct RuntimeNode
    {
        NodeId id;
        std::shared_ptr<Node> node;
        std::vector<size_t> inputIndices;
        juce::AudioBuffer<float> buffer;
        juce::AudioBuffer<float> inputBuffer;
        // PDC delay line: compensationSamples < 0 means "this node sits on the
        // longest path" and introduces no delay; > 0 means earlier paths are
        // delayed by that many samples to realign with the longest chain.
        juce::AudioBuffer<float> pdcDelayBuffer;
        int pdcWritePos { 0 };
        int compensationSamples { 0 };
        bool receivesHostInput = false;
        int numInputChannels = 0;
        int numOutputChannels = 0;
    };

    struct RuntimeState
    {
        std::vector<RuntimeNode> nodes;
        std::unordered_map<std::string, size_t> indexByNodeId;
        size_t outputNodeIndex = 0;
        bool hasOutputNode = false;
        double sampleRate = 0.0;
        int blockSize = 0;
        bool pdcEnabled = true;
    };

    struct NodeEntry
    {
        NodeId id;
        std::shared_ptr<Node> node;
        std::vector<NodeId> outputs;
    };

    [[nodiscard]] static std::string toKey(const NodeId& id);
    [[nodiscard]] std::shared_ptr<Node> getNodeUnlocked(const NodeId& id) const;
    [[nodiscard]] bool hasNodeUnlocked(const NodeId& id) const;
    NodeId addNodeUnlocked(std::unique_ptr<Node> node, std::optional<NodeId> requestedId);
    void invalidateRuntimeUnlocked();
    void suspendProcessingAndDrainUnlocked();
    void resumeProcessingUnlocked();
    void waitForInFlightCallbacks() const;
    void buildScheduleUnlocked();

    mutable std::mutex mutex_;
    std::vector<NodeEntry> nodes_;
    std::unordered_map<std::string, size_t> indexById_;
    std::vector<NodeId> schedule_;

    NodeId inputNode_;
    NodeId outputNode_;

    double sampleRate_ = 0.0;
    int blockSize_ = 0;
    bool pdcEnabled_ = true;

    std::atomic<bool> processingSuspended_ { false };
    std::atomic<int> inFlightProcessCallbacks_ { 0 };
    mutable std::mutex inFlightCallbackMutex_;
    mutable std::condition_variable inFlightCallbackCv_;
    std::atomic<std::shared_ptr<RuntimeState>> runtimeState_ { nullptr };
};
} // namespace host::graph
