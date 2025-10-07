#include "graph/GraphEngine.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

namespace host::graph
{
namespace
{
constexpr double defaultSampleRate = 48000.0;
constexpr int defaultBlockSize = 256;
}

std::string GraphEngine::toKey(const NodeId& id)
{
    return id.toString().toStdString();
}

void GraphEngine::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.clear();
    indexById_.clear();
    schedule_.clear();
    channelPointers_.clear();
    inputNode_ = {};
    outputNode_ = {};
    sampleRate_ = defaultSampleRate;
    blockSize_ = defaultBlockSize;
    prepared_ = false;
}

GraphEngine::NodeId GraphEngine::addNode(std::unique_ptr<Node> node)
{
    if (node == nullptr)
        throw std::invalid_argument("GraphEngine::addNode: node must not be null");

    std::lock_guard<std::mutex> lock(mutex_);
    return addNodeUnlocked(std::move(node), std::nullopt);
}

GraphEngine::NodeId GraphEngine::addNodeWithId(const NodeId& id, std::unique_ptr<Node> node)
{
    if (node == nullptr)
        throw std::invalid_argument("GraphEngine::addNodeWithId: node must not be null");

    std::lock_guard<std::mutex> lock(mutex_);
    return addNodeUnlocked(std::move(node), id);
}

GraphEngine::NodeId GraphEngine::addNodeUnlocked(std::unique_ptr<Node> node, std::optional<NodeId> requestedId)
{
    NodeId id;
    std::string key;

    if (requestedId.has_value())
    {
        id = requestedId.value();
        key = toKey(id);
        if (indexById_.find(key) != indexById_.end())
            throw std::invalid_argument("GraphEngine::addNodeWithId: id already exists");
    }
    else
    {
        id = NodeId();
        key = toKey(id);
        while (indexById_.find(key) != indexById_.end())
        {
            id = NodeId();
            key = toKey(id);
        }
    }

    NodeEntry entry;
    entry.id = id;
    entry.node = std::move(node);

    indexById_[key] = nodes_.size();
    nodes_.push_back(std::move(entry));
    prepared_ = false;

    return id;
}

Node* GraphEngine::getNode(const NodeId& id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return getNodeUnlocked(id);
}

void GraphEngine::setIO(NodeId inputNode, NodeId outputNode)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (! hasNodeUnlocked(inputNode) || ! hasNodeUnlocked(outputNode))
        throw std::invalid_argument("GraphEngine::setIO: invalid node id");

    inputNode_ = inputNode;
    outputNode_ = outputNode;
}

void GraphEngine::connect(NodeId from, NodeId to)
{
    if (from == to)
        throw std::invalid_argument("GraphEngine::connect: cannot connect node to itself");

    std::lock_guard<std::mutex> lock(mutex_);

    if (! hasNodeUnlocked(from) || ! hasNodeUnlocked(to))
        throw std::invalid_argument("GraphEngine::connect: invalid node id");

    const auto fromKey = toKey(from);
    const auto fromIndex = indexById_.at(fromKey);
    auto& outputs = nodes_[fromIndex].outputs;

    const bool alreadyConnected = std::any_of(outputs.begin(), outputs.end(),
                                              [&to](const NodeId& existing) { return existing == to; });
    if (! alreadyConnected)
        outputs.push_back(to);

    prepared_ = false;
}

void GraphEngine::disconnect(NodeId from, NodeId to)
{
    if (from == to)
        return;

    std::lock_guard<std::mutex> lock(mutex_);

    if (! hasNodeUnlocked(from) || ! hasNodeUnlocked(to))
        return;

    const auto fromKey = toKey(from);
    const auto fromIndex = indexById_.at(fromKey);
    auto& outputs = nodes_[fromIndex].outputs;

    const auto newEnd = std::remove(outputs.begin(), outputs.end(), to);
    if (newEnd != outputs.end())
    {
        outputs.erase(newEnd, outputs.end());
        prepared_ = false;
    }
}

void GraphEngine::setEngineFormat(double sampleRate, int blockSize)
{
    std::lock_guard<std::mutex> lock(mutex_);

    sampleRate_ = (sampleRate > 0.0) ? sampleRate : defaultSampleRate;
    blockSize_ = (blockSize > 0) ? blockSize : defaultBlockSize;
    prepared_ = false;
}

void GraphEngine::prepare()
{
    std::lock_guard<std::mutex> lock(mutex_);

    buildScheduleUnlocked();

    if (schedule_.empty())
    {
        prepared_ = true;
        return;
    }

    for (const auto& id : schedule_)
    {
        if (auto* node = getNodeUnlocked(id))
            node->prepare(sampleRate_, blockSize_);
    }

    prepared_ = true;
}

int GraphEngine::process(juce::AudioBuffer<float>& buffer)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (! prepared_ || schedule_.empty())
    {
        buffer.clear();
        return 0;
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    channelPointers_.resize(static_cast<size_t>(std::max(0, numChannels)), nullptr);
    for (int ch = 0; ch < numChannels; ++ch)
        channelPointers_[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);

    ProcessContext context {
        buffer,
        channelPointers_.empty() ? nullptr : channelPointers_.data(),
        channelPointers_.empty() ? nullptr : channelPointers_.data(),
        numChannels,
        numChannels,
        sampleRate_,
        blockSize_,
        numSamples
    };

    for (const auto& id : schedule_)
    {
        if (auto* node = getNodeUnlocked(id))
            node->process(context);
    }

    return buffer.getNumSamples();
}

std::vector<GraphEngine::NodeId> GraphEngine::getSchedule() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return schedule_;
}

std::vector<GraphEngine::NodeId> GraphEngine::getNodeIds() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeId> ids;
    ids.reserve(nodes_.size());
    for (const auto& entry : nodes_)
        ids.push_back(entry.id);
    return ids;
}

std::vector<std::pair<GraphEngine::NodeId, GraphEngine::NodeId>> GraphEngine::getConnections() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<NodeId, NodeId>> connections;
    for (const auto& node : nodes_)
    {
        for (const auto& target : node.outputs)
            connections.emplace_back(node.id, target);
    }
    return connections;
}

GraphEngine::NodeId GraphEngine::getInputNode() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return inputNode_;
}

GraphEngine::NodeId GraphEngine::getOutputNode() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return outputNode_;
}

Node* GraphEngine::getNodeUnlocked(const NodeId& id) const
{
    const auto key = toKey(id);
    const auto it = indexById_.find(key);
    if (it == indexById_.end())
        return nullptr;

    const auto idx = it->second;
    if (idx >= nodes_.size())
        return nullptr;

    return nodes_[idx].node.get();
}

bool GraphEngine::hasNodeUnlocked(const NodeId& id) const
{
    const auto key = toKey(id);
    return indexById_.find(key) != indexById_.end();
}

void GraphEngine::buildScheduleUnlocked()
{
    schedule_.clear();

    if (nodes_.empty())
    {
        prepared_ = true;
        return;
    }

    const size_t nodeCount = nodes_.size();
    std::vector<int> indegree(nodeCount, 0);

    for (const auto& entry : nodes_)
    {
        for (const auto& targetId : entry.outputs)
        {
            const auto key = toKey(targetId);
            const auto targetIt = indexById_.find(key);
            if (targetIt == indexById_.end())
                throw std::runtime_error("GraphEngine::prepare: connection references unknown node");

            const auto targetIdx = targetIt->second;
            if (targetIdx >= nodeCount)
                throw std::runtime_error("GraphEngine::prepare: connection index out of range");

            ++indegree[targetIdx];
        }
    }

    std::queue<size_t> ready;
    for (size_t i = 0; i < nodeCount; ++i)
    {
        if (indegree[i] == 0)
            ready.push(i);
    }

    std::vector<size_t> order;
    order.reserve(nodeCount);

    while (! ready.empty())
    {
        const auto idx = ready.front();
        ready.pop();
        order.push_back(idx);

        for (const auto& targetId : nodes_[idx].outputs)
        {
            const auto targetKey = toKey(targetId);
            const auto targetIt = indexById_.find(targetKey);
            if (targetIt == indexById_.end())
                continue;

            const auto targetIdx = targetIt->second;
            if (indegree[targetIdx] <= 0)
                continue;

            --indegree[targetIdx];
            if (indegree[targetIdx] == 0)
                ready.push(targetIdx);
        }
    }

    if (order.size() != nodeCount)
        throw std::runtime_error("GraphEngine::prepare: graph contains a cycle");

    schedule_.reserve(nodeCount);
    for (const auto idx : order)
        schedule_.push_back(nodes_[idx].id);
}
} // namespace host::graph
