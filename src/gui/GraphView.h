#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "graph/GraphEngine.h"

namespace host::gui
{
    class GraphView : public juce::Component
    {
    public:
        using NodeId = host::graph::GraphEngine::NodeId;

        GraphView();
        ~GraphView() override;

        void setGraph(std::shared_ptr<host::graph::GraphEngine> graphEngine);
        void refreshGraph(bool preservePositions = true);
        void focusOnNode(NodeId id);
        void deselectAll();
        void setOnRequestNodeSettings(std::function<void(NodeId)> callback);

        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        bool keyPressed(const juce::KeyPress& key) override;

    private:
        class NodeComponent;
        friend class NodeComponent;

        void syncNodes(bool preservePositions);
        void updateComponentPositions();
        void updateNodePosition(NodeId id, juce::Point<float> topLeft);
        void beginConnectionDrag(NodeId id, juce::Point<float> startPosition);
        void updateConnectionDrag(juce::Point<float> currentPosition);
        void completeConnectionDragAt(juce::Point<float> position);
        void cancelConnectionDrag();
        void showNodeContextMenu(NodeId id, juce::Point<int> screenPosition);
        void showBackgroundMenu(juce::Point<int> screenPosition);
        void drawConnections(juce::Graphics& g);
        void clearConnectionsFrom(NodeId id);
        void clearConnectionsTo(NodeId id);
        void selectNode(NodeId id);
        void updateSelectionVisuals();
        void deleteSelectedNode();
        void nudgeSelectedNode(juce::Point<float> delta);
        void centerOnSelectedNode();
        void setViewOffset(juce::Point<float> newOffset);
        void panBy(juce::Point<float> delta);
        [[nodiscard]] juce::Rectangle<float> computeContentBounds() const;
        [[nodiscard]] bool nodeSupportsSettings(NodeId id) const;
        [[nodiscard]] bool openNodeSettings(NodeId id);

        [[nodiscard]] NodeComponent* findNodeComponent(NodeId id) const;

        std::shared_ptr<host::graph::GraphEngine> graph;
        std::vector<std::unique_ptr<NodeComponent>> nodeComponents;
        std::unordered_map<std::string, NodeComponent*> nodeLookup;
        std::unordered_map<std::string, juce::Point<float>> nodePositions;
        std::function<void(NodeId)> onRequestNodeSettings;

        NodeId selectedNode {};
        juce::Point<float> viewOffset {};
        bool isPanning = false;
        juce::Point<float> panAnchor {};
        juce::Point<float> panStartOffset {};

        bool isDraggingConnection = false;
        NodeId connectionSource {};
        juce::Point<float> connectionDragPoint {};
    };
}
