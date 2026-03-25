#include "graph/GraphEngine.h"

#include <algorithm>
#include <array>
#include <queue>
#include <stdexcept>
#include <thread>

namespace host::graph
{
namespace
{
constexpr double defaultSampleRate = 48000.0;
constexpr int defaultBlockSize = 256;
constexpr int maxProcessChannels = 64;

struct ProcessCallbackGuard
{
    explicit ProcessCallbackGuard(std::atomic<int>& counterIn) : counter(counterIn)
    {
        counter.fetch_add(1, std::memory_order_acq_rel);
    }

    ~ProcessCallbackGuard()
    {
        counter.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::atomic<int>& counter;
};
} // namespace

std::string GraphEngine::toKey(const NodeId& id)
{
    return id.toString().toStdString();
}

void GraphEngine::waitForInFlightCallbacks() const
{
    while (inFlightProcessCallbacks_.load(std::memory_order_acquire) > 0)
        std::this_thread::yield();
}

void GraphEngine::suspendProcessingAndDrainUnlocked()
{
    processingSuspended_.store(true, std::memory_order_release);
    waitForInFlightCallbacks();
}

void GraphEngine::resumeProcessingUnlocked()
{
    processingSuspended_.store(false, std::memory_order_release);
}

void GraphEngine::invalidateRuntimeUnlocked()
{
    runtimeState_.store(nullptr, std::memory_order_release);
}

void GraphEngine::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    suspendProcessingAndDrainUnlocked();

    nodes_.clear();
    indexById_.clear();
    schedule_.clear();
    inputNode_ = {};
    outputNode_ = {};
    sampleRate_ = defaultSampleRate;
    blockSize_ = defaultBlockSize;
    invalidateRuntimeUnlocked();

    resumeProcessingUnlocked();
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
    invalidateRuntimeUnlocked();

    return id;
}

Node* GraphEngine::getNode(const NodeId& id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return getNodeUnlocked(id);
}

void GraphEngine::removeNode(NodeId id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (! hasNodeUnlocked(id))
        return;

    suspendProcessingAndDrainUnlocked();

    try
    {
        const auto key = toKey(id);
        const auto indexIt = indexById_.find(key);
        if (indexIt == indexById_.end())
        {
            resumeProcessingUnlocked();
            return;
        }

        const auto index = indexIt->second;
        if (index >= nodes_.size())
        {
            resumeProcessingUnlocked();
            return;
        }

        for (auto& entry : nodes_)
        {
            auto& outputs = entry.outputs;
            outputs.erase(std::remove(outputs.begin(), outputs.end(), id), outputs.end());
        }

        nodes_.erase(nodes_.begin() + static_cast<std::ptrdiff_t>(index));
        indexById_.erase(indexIt);

        indexById_.clear();
        for (size_t i = 0; i < nodes_.size(); ++i)
            indexById_[toKey(nodes_[i].id)] = i;

        if (inputNode_ == id)
            inputNode_ = {};
        if (outputNode_ == id)
            outputNode_ = {};

        invalidateRuntimeUnlocked();
        resumeProcessingUnlocked();
    }
    catch (...)
    {
        resumeProcessingUnlocked();
        throw;
    }
}

void GraphEngine::setIO(NodeId inputNode, NodeId outputNode)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (! hasNodeUnlocked(inputNode) || ! hasNodeUnlocked(outputNode))
        throw std::invalid_argument("GraphEngine::setIO: invalid node id");

    inputNode_ = inputNode;
    outputNode_ = outputNode;
    invalidateRuntimeUnlocked();
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

    invalidateRuntimeUnlocked();
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
        invalidateRuntimeUnlocked();
    }
}

void GraphEngine::setEngineFormat(double sampleRate, int blockSize)
{
    std::lock_guard<std::mutex> lock(mutex_);

    sampleRate_ = (sampleRate > 0.0) ? sampleRate : defaultSampleRate;
    blockSize_ = (blockSize > 0) ? blockSize : defaultBlockSize;
    invalidateRuntimeUnlocked();
}

void GraphEngine::prepare()
{
    std::lock_guard<std::mutex> lock(mutex_);
    suspendProcessingAndDrainUnlocked();

    try
    {
        buildScheduleUnlocked();

        if (schedule_.empty())
        {
            invalidateRuntimeUnlocked();
            resumeProcessingUnlocked();
            return;
        }

        for (const auto& id : schedule_)
        {
            if (auto* node = getNodeUnlocked(id))
                node->prepare(sampleRate_, blockSize_);
        }

        auto runtime = std::make_shared<RuntimeState>();
        runtime->sampleRate = sampleRate_;
        runtime->blockSize = blockSize_;
        runtime->scheduleNodes.reserve(schedule_.size());

        for (const auto& id : schedule_)
        {
            if (auto* node = getNodeUnlocked(id))
                runtime->scheduleNodes.push_back(node);
        }

        runtimeState_.store(std::move(runtime), std::memory_order_release);
        resumeProcessingUnlocked();
    }
    catch (...)
    {
        invalidateRuntimeUnlocked();
        resumeProcessingUnlocked();
        throw;
    }
}

int GraphEngine::process(juce::AudioBuffer<float>& buffer)
{
    if (processingSuspended_.load(std::memory_order_acquire))
    {
        buffer.clear();
        return 0;
    }

    ProcessCallbackGuard inFlight(inFlightProcessCallbacks_);

    if (processingSuspended_.load(std::memory_order_acquire))
    {
        buffer.clear();
        return 0;
    }

    auto runtime = runtimeState_.load(std::memory_order_acquire);
    if (! runtime || runtime->scheduleNodes.empty())
    {
        buffer.clear();
        return 0;
    }

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return 0;

    if (numChannels > maxProcessChannels)
    {
        buffer.clear();
        return 0;
    }

    std::array<float*, static_cast<size_t>(maxProcessChannels)> channelPointers {};
    for (int ch = 0; ch < numChannels; ++ch)
        channelPointers[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);

    ProcessContext context {
        buffer,
        channelPointers.data(),
        channelPointers.data(),
        numChannels,
        numChannels,
        runtime->sampleRate,
        runtime->blockSize,
        numSamples
    };

    for (auto* node : runtime->scheduleNodes)
    {
        if (node != nullptr)
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
        return;

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
