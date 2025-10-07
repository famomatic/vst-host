#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include <map>
#include <string>

namespace host::graph
{
    struct ProcessContext
    {
        juce::AudioBuffer<float>& audioBuffer;
        double sampleRate {};
        int blockSize {};
    };

    class Node
    {
    public:
        virtual ~Node() = default;
        virtual void prepare(double sampleRate, int blockSize) = 0;
        virtual void process(ProcessContext& context) = 0;
        virtual int latencySamples() const noexcept { return 0; }
        virtual std::string name() const = 0;
    };

    using NodeId = juce::Uuid;

    struct Connection
    {
        NodeId source;
        NodeId destination;
    };

    class GraphEngine
    {
    public:
        GraphEngine();
        ~GraphEngine();

        void setEngineFormat(double sampleRate, int blockSize);

        NodeId addNode(std::unique_ptr<Node> node);
        void removeNode(NodeId id);
        void clear();

        bool connect(NodeId source, NodeId dest);
        void disconnect(NodeId source, NodeId dest);

        void setIO(NodeId input, NodeId output);

        void prepare();
        void process(juce::AudioBuffer<float>& buffer);

        [[nodiscard]] Node* getNode(NodeId id) const;
        [[nodiscard]] std::vector<NodeId> getSchedule() const { return schedule; }

    private:
        void rebuildSchedule();
        bool detectCycle(NodeId node, std::vector<NodeId>& stack, std::vector<NodeId>& visited) const;
        void updateLatencyAlignment();

        double currentSampleRate { 48000.0 };
        int currentBlockSize { 256 };
        std::map<NodeId, std::unique_ptr<Node>> nodes;
        std::vector<Connection> connections;
        std::vector<NodeId> schedule;
        NodeId audioInputId;
        NodeId audioOutputId;
        bool needsScheduleRebuild { true };
    };
}
