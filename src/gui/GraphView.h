#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "graph/GraphEngine.h"

namespace host::gui
{
    class GraphView : public juce::Component
    {
    public:
        GraphView();
        ~GraphView() override;

        void setGraph(std::shared_ptr<host::graph::GraphEngine> graphEngine);
        void refreshGraph(bool preservePositions = true);

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        class NodeComponent;
        friend class NodeComponent;

        using NodeId = host::graph::GraphEngine::NodeId;

        void syncNodes(bool preservePositions);
        void updateNodePosition(NodeId id, juce::Point<float> topLeft);
        void beginConnectionDrag(NodeId id, juce::Point<float> startPosition);
        void updateConnectionDrag(juce::Point<float> currentPosition);
        void completeConnectionDragAt(juce::Point<float> position);
        void cancelConnectionDrag();
        void showNodeContextMenu(NodeId id, juce::Point<int> screenPosition);
        void drawConnections(juce::Graphics& g);
        void clearConnectionsFrom(NodeId id);
        void clearConnectionsTo(NodeId id);

        [[nodiscard]] NodeComponent* findNodeComponent(NodeId id) const;

        std::shared_ptr<host::graph::GraphEngine> graph;
        std::vector<std::unique_ptr<NodeComponent>> nodeComponents;
        std::unordered_map<std::string, NodeComponent*> nodeLookup;
        std::unordered_map<std::string, juce::Point<float>> nodePositions;

        bool isDraggingConnection = false;
        NodeId connectionSource {};
        juce::Point<float> connectionDragPoint {};
    };
}
