#include "graph/GraphEngine.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

namespace host::graph
{
namespace
{
constexpr int maxChannels = 2;
}

void GraphEngine::setGraph(std::vector<std::unique_ptr<Node>> nodes,
                           std::vector<std::pair<int, int>> edges)
{
    nodes_ = std::move(nodes);
    edges_ = std::move(edges);
    schedule_.clear();
    delays_.clear();
    prepared_ = false;
}

void GraphEngine::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    const auto nodeCount = static_cast<int>(nodes_.size());
    std::vector<int> indegree(static_cast<size_t>(nodeCount), 0);
    std::vector<std::vector<int>> adjacency(static_cast<size_t>(nodeCount));

    delays_.assign(edges_.size(), EdgeDelay{});

    for (size_t edgeIndex = 0; edgeIndex < edges_.size(); ++edgeIndex)
    {
        const auto [from, to] = edges_[edgeIndex];
        if (from < 0 || to < 0 || from >= nodeCount || to >= nodeCount)
            throw std::runtime_error("GraphEngine::prepare: edge index out of range");

        adjacency[static_cast<size_t>(from)].push_back(to);
        ++indegree[static_cast<size_t>(to)];
    }

    std::queue<int> ready;
    for (int i = 0; i < nodeCount; ++i)
        if (indegree[static_cast<size_t>(i)] == 0)
            ready.push(i);

    schedule_.clear();
    schedule_.reserve(nodes_.size());

    int visited = 0;
    while (! ready.empty())
    {
        const int nodeIndex = ready.front();
        ready.pop();
        ++visited;

        schedule_.push_back(nodes_[static_cast<size_t>(nodeIndex)].get());

        for (const auto next : adjacency[static_cast<size_t>(nodeIndex)])
        {
            auto& deg = indegree[static_cast<size_t>(next)];
            if (--deg == 0)
                ready.push(next);
        }
    }

    if (visited != nodeCount)
        throw std::runtime_error("GraphEngine::prepare: graph contains a cycle");

    for (auto* node : schedule_)
        if (node != nullptr)
            node->prepare(sampleRate_, blockSize_);

    for (auto& channel : scratch_)
        channel.assign(static_cast<size_t>(blockSize_), 0.0f);

    prepared_ = true;
}

void GraphEngine::process(float** in, int inCh, int inFrames,
                          float** out, int outCh, int& outFrames,
                          double sampleRate, int blockSize)
{
    (void) sampleRate;
    (void) blockSize;

    if (! prepared_ || schedule_.empty())
    {
        outFrames = 0;
        return;
    }

    const int frames = std::min(inFrames, blockSize_);
    const int processChannels = maxChannels;

    for (int ch = 0; ch < processChannels; ++ch)
    {
        float* dst = scratch_[static_cast<size_t>(ch)].data();
        if (ch < inCh && in != nullptr && in[ch] != nullptr)
            std::copy_n(in[ch], static_cast<size_t>(frames), dst);
        else
            std::fill_n(dst, static_cast<size_t>(frames), 0.0f);
    }

    float* channelPtrs[maxChannels];
    for (int ch = 0; ch < processChannels; ++ch)
        channelPtrs[ch] = scratch_[static_cast<size_t>(ch)].data();

    ProcessCtx ctx{};
    ctx.in = channelPtrs;
    ctx.inCh = std::min(inCh, processChannels);
    ctx.out = channelPtrs;
    ctx.outCh = std::min(outCh, processChannels);
    ctx.numFrames = frames;
    ctx.sampleRate = sampleRate_;
    ctx.blockSize = blockSize_;

    for (auto* node : schedule_)
    {
        if (node == nullptr)
            continue;

        // TODO(PDC): apply per-edge latency offsets here when fan-out is supported.
        node->process(ctx);
    }

    if (out != nullptr)
    {
        const int copyChannels = std::min(outCh, processChannels);
        for (int ch = 0; ch < copyChannels; ++ch)
        {
            if (out[ch] != nullptr)
                std::copy_n(channelPtrs[ch], static_cast<size_t>(frames), out[ch]);
        }

        for (int ch = copyChannels; ch < outCh; ++ch)
        {
            if (out[ch] != nullptr)
                std::fill_n(out[ch], static_cast<size_t>(frames), 0.0f);
        }
    }

    outFrames = frames;
}
} // namespace host::graph
