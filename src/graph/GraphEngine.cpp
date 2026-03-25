#include "graph/GraphEngine.h"

#include <algorithm>
#include <array>
#include <queue>
#include <stdexcept>

namespace host::graph
{
namespace
{
constexpr double defaultSampleRate = 48000.0;
constexpr int defaultBlockSize = 256;
constexpr int maxProcessChannels = 64;
} // namespace

std::string GraphEngine::toKey(const NodeId& id)
{
    return id.toString().toStdString();
}

void GraphEngine::waitForInFlightCallbacks() const
{
    std::unique_lock<std::mutex> lock(inFlightCallbackMutex_);
    inFlightCallbackCv_.wait(lock, [this]
    {
        return inFlightProcessCallbacks_.load(std::memory_order_acquire) == 0;
    });
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
    entry.node = std::shared_ptr<Node>(std::move(node));

    indexById_[key] = nodes_.size();
    nodes_.push_back(std::move(entry));
    invalidateRuntimeUnlocked();

    return id;
}

std::shared_ptr<Node> GraphEngine::getNode(const NodeId& id) const
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
            auto node = getNodeUnlocked(id);
            if (node)
                node->prepare(sampleRate_, blockSize_);
        }

        auto runtime = std::make_shared<RuntimeState>();
        runtime->sampleRate = sampleRate_;
        runtime->blockSize = blockSize_;
        runtime->nodes.reserve(schedule_.size());
        runtime->indexByNodeId.reserve(schedule_.size());

        for (const auto& id : schedule_)
        {
            auto node = getNodeUnlocked(id);
            if (! node)
                continue;

            RuntimeNode runtimeNode;
            runtimeNode.id = id;
            runtimeNode.node = std::move(node);
            runtimeNode.receivesHostInput = (! inputNode_.isNull() && id == inputNode_);
            runtimeNode.buffer.setSize(maxProcessChannels, std::max(1, blockSize_), false, false, true);
            runtimeNode.buffer.clear();

            const auto runtimeIndex = runtime->nodes.size();
            runtime->indexByNodeId[toKey(id)] = runtimeIndex;
            runtime->nodes.push_back(std::move(runtimeNode));
        }

        for (const auto& entry : nodes_)
        {
            const auto targetRuntimeIt = runtime->indexByNodeId.find(toKey(entry.id));
            if (targetRuntimeIt == runtime->indexByNodeId.end())
                continue;

            auto& targetRuntimeNode = runtime->nodes[targetRuntimeIt->second];
            for (const auto& sourceEntry : nodes_)
            {
                const auto hasEdge = std::any_of(sourceEntry.outputs.begin(), sourceEntry.outputs.end(),
                                                 [&entry](const NodeId& target) { return target == entry.id; });
                if (! hasEdge)
                    continue;

                const auto sourceRuntimeIt = runtime->indexByNodeId.find(toKey(sourceEntry.id));
                if (sourceRuntimeIt != runtime->indexByNodeId.end())
                    targetRuntimeNode.inputIndices.push_back(sourceRuntimeIt->second);
            }
        }

        if (! outputNode_.isNull())
        {
            const auto outputRuntimeIt = runtime->indexByNodeId.find(toKey(outputNode_));
            if (outputRuntimeIt != runtime->indexByNodeId.end())
            {
                runtime->hasOutputNode = true;
                runtime->outputNodeIndex = outputRuntimeIt->second;
            }
        }

        if (! runtime->hasOutputNode && ! runtime->nodes.empty())
        {
            runtime->hasOutputNode = true;
            runtime->outputNodeIndex = runtime->nodes.size() - 1;
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

    const int previousCallbacks = inFlightProcessCallbacks_.fetch_add(1, std::memory_order_acq_rel);
    const auto releaseInFlightCallback = [this]()
    {
        const int previous = inFlightProcessCallbacks_.fetch_sub(1, std::memory_order_acq_rel);
        if (previous <= 1)
            inFlightCallbackCv_.notify_all();
    };
    struct InFlightScopeExit
    {
        explicit InFlightScopeExit(const decltype(releaseInFlightCallback)& fnIn)
            : fn(fnIn)
        {
        }

        ~InFlightScopeExit()
        {
            fn();
        }

        const decltype(releaseInFlightCallback)& fn;
    } inFlightScopeExit(releaseInFlightCallback);

    if (previousCallbacks > 0)
    {
        buffer.clear();
        return 0;
    }

    if (processingSuspended_.load(std::memory_order_acquire))
    {
        buffer.clear();
        return 0;
    }

    auto runtime = runtimeState_.load(std::memory_order_acquire);
    if (! runtime || runtime->nodes.empty() || ! runtime->hasOutputNode)
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

    if (numSamples > runtime->blockSize)
    {
        buffer.clear();
        return 0;
    }

    std::array<float*, static_cast<size_t>(maxProcessChannels)> inPointers {};
    std::array<float*, static_cast<size_t>(maxProcessChannels)> outPointers {};

    for (auto& runtimeNode : runtime->nodes)
    {
        runtimeNode.buffer.clear();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* destination = runtimeNode.buffer.getWritePointer(ch);
            if (destination == nullptr)
                continue;

            if (runtimeNode.inputIndices.empty())
            {
                if (runtimeNode.receivesHostInput)
                {
                    const auto* source = buffer.getReadPointer(ch);
                    juce::FloatVectorOperations::copy(destination, source, numSamples);
                }
            }
            else
            {
                for (const auto sourceIndex : runtimeNode.inputIndices)
                {
                    if (sourceIndex >= runtime->nodes.size())
                        continue;

                    const auto* source = runtime->nodes[sourceIndex].buffer.getReadPointer(ch);
                    if (source != nullptr)
                        juce::FloatVectorOperations::add(destination, source, numSamples);
                }
            }

            inPointers[static_cast<size_t>(ch)] = runtimeNode.buffer.getWritePointer(ch);
            outPointers[static_cast<size_t>(ch)] = runtimeNode.buffer.getWritePointer(ch);
        }

        ProcessContext context {
            runtimeNode.buffer,
            inPointers.data(),
            outPointers.data(),
            numChannels,
            numChannels,
            runtime->sampleRate,
            runtime->blockSize,
            numSamples
        };

        if (runtimeNode.node != nullptr)
            runtimeNode.node->process(context);
    }

    if (runtime->outputNodeIndex >= runtime->nodes.size())
    {
        buffer.clear();
        return 0;
    }

    const auto& outputBuffer = runtime->nodes[runtime->outputNodeIndex].buffer;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* dest = buffer.getWritePointer(ch);
        const auto* src = outputBuffer.getReadPointer(ch);
        if (dest == nullptr || src == nullptr)
            continue;

        juce::FloatVectorOperations::copy(dest, src, numSamples);
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

std::shared_ptr<Node> GraphEngine::getNodeUnlocked(const NodeId& id) const
{
    const auto key = toKey(id);
    const auto it = indexById_.find(key);
    if (it == indexById_.end())
        return {};

    const auto idx = it->second;
    if (idx >= nodes_.size())
        return {};

    return nodes_[idx].node;
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
