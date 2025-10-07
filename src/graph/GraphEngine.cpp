#include "graph/GraphEngine.h"

#include <algorithm>
#include <map>
#include <queue>

namespace host::graph
{
    GraphEngine::GraphEngine() = default;
    GraphEngine::~GraphEngine() = default;

    void GraphEngine::setEngineFormat(double sampleRate, int blockSize)
    {
        currentSampleRate = sampleRate;
        currentBlockSize = blockSize;
        needsScheduleRebuild = true;
    }

    NodeId GraphEngine::addNode(std::unique_ptr<Node> node)
    {
        NodeId id;
        nodes.emplace(id, std::move(node));
        needsScheduleRebuild = true;
        return id;
    }

    void GraphEngine::removeNode(NodeId id)
    {
        nodes.erase(id);
        connections.erase(std::remove_if(connections.begin(), connections.end(), [id](const Connection& c) {
            return c.source == id || c.destination == id;
        }), connections.end());
        schedule.erase(std::remove(schedule.begin(), schedule.end(), id), schedule.end());
        needsScheduleRebuild = true;
    }

    void GraphEngine::clear()
    {
        nodes.clear();
        connections.clear();
        schedule.clear();
        audioInputId = {};
        audioOutputId = {};
        needsScheduleRebuild = true;
    }

    bool GraphEngine::connect(NodeId source, NodeId dest)
    {
        if (source == dest)
            return false;

        connections.push_back({ source, dest });
        needsScheduleRebuild = true;
        return true;
    }

    void GraphEngine::disconnect(NodeId source, NodeId dest)
    {
        connections.erase(std::remove_if(connections.begin(), connections.end(), [source, dest](const Connection& c) {
            return c.source == source && c.destination == dest;
        }), connections.end());
        needsScheduleRebuild = true;
    }

    void GraphEngine::setIO(NodeId input, NodeId output)
    {
        audioInputId = input;
        audioOutputId = output;
    }

    void GraphEngine::prepare()
    {
        if (needsScheduleRebuild)
        {
            rebuildSchedule();
            updateLatencyAlignment();
            needsScheduleRebuild = false;
        }

        for (auto& [id, node] : nodes)
            if (node)
                node->prepare(currentSampleRate, currentBlockSize);
    }

    void GraphEngine::process(juce::AudioBuffer<float>& buffer)
    {
        ProcessContext context { buffer, currentSampleRate, currentBlockSize };
        for (auto& nodeId : schedule)
        {
            if (auto* node = getNode(nodeId))
                node->process(context);
        }
    }

    Node* GraphEngine::getNode(NodeId id) const
    {
        if (auto it = nodes.find(id); it != nodes.end())
            return it->second.get();
        return nullptr;
    }

    void GraphEngine::rebuildSchedule()
    {
        schedule.clear();

        std::map<NodeId, int> indegree;
        for (auto& [id, node] : nodes)
            indegree[id] = 0;
        for (auto& c : connections)
            ++indegree[c.destination];

        std::queue<NodeId> q;
        for (auto& [id, deg] : indegree)
            if (deg == 0)
                q.push(id);

        while (! q.empty())
        {
            auto id = q.front();
            q.pop();
            schedule.push_back(id);

            for (auto& c : connections)
            {
                if (c.source == id)
                {
                    auto& deg = indegree[c.destination];
                    if (--deg == 0)
                        q.push(c.destination);
                }
            }
        }
    }

    bool GraphEngine::detectCycle(NodeId, std::vector<NodeId>&, std::vector<NodeId>&) const
    {
        // TODO: implement proper cycle detection.
        return false;
    }

    void GraphEngine::updateLatencyAlignment()
    {
        // TODO: implement PDC alignment once latency reporting is hooked up.
    }
}
