#include "gui/GraphView.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <unordered_set>

#include "graph/Nodes/VstFx.h"

namespace host::gui
{
namespace
{
    constexpr float kNodeWidth = 180.0f;
    constexpr float kNodeHeight = 96.0f;
    constexpr float kNodeCornerRadius = 8.0f;
    constexpr float kConnectorRadius = 7.0f;
    constexpr float kConnectorHitRadius = kConnectorRadius + 4.0f;
    constexpr float kNodeHorizontalSpacing = 200.0f;
    constexpr float kDefaultTop = 80.0f;
}

class GraphView::NodeComponent : public juce::Component
{
public:
    enum class Role
    {
        General,
        Input,
        Output,
        Plugin
    };

    NodeComponent(GraphView& ownerIn,
                  NodeId idIn,
                  juce::String nameIn,
                  Role roleIn,
                  int inputsIn,
                  int outputsIn)
        : owner(ownerIn),
          nodeId(idIn),
          displayName(std::move(nameIn)),
          nodeRole(roleIn),
          numInputs(std::max(0, inputsIn)),
          numOutputs(std::max(0, outputsIn))
    {
        setSize(static_cast<int>(kNodeWidth), static_cast<int>(kNodeHeight));
        setInterceptsMouseClicks(true, true);
    }

    [[nodiscard]] NodeId getId() const noexcept { return nodeId; }

    void setDisplayName(const juce::String& newName)
    {
        if (displayName != newName)
        {
            displayName = newName;
            repaint();
        }
    }

    void setIO(int inputs, int outputs)
    {
        inputs = std::max(0, inputs);
        outputs = std::max(0, outputs);
        if (numInputs != inputs || numOutputs != outputs)
        {
            numInputs = inputs;
            numOutputs = outputs;
            repaint();
        }
    }

    void setRole(Role newRole)
    {
        if (nodeRole != newRole)
        {
            nodeRole = newRole;
            repaint();
        }
    }

    [[nodiscard]] juce::Point<float> getInputConnectorPosition() const
    {
        return {
            static_cast<float>(getX()) + 12.0f,
            static_cast<float>(getY()) + static_cast<float>(getHeight()) / 2.0f
        };
    }

    [[nodiscard]] juce::Point<float> getOutputConnectorPosition() const
    {
        return {
            static_cast<float>(getX()) + static_cast<float>(getWidth()) - 12.0f,
            static_cast<float>(getY()) + static_cast<float>(getHeight()) / 2.0f
        };
    }

    [[nodiscard]] bool isPointOverInput(juce::Point<float> parentSpacePoint) const
    {
        const auto localPoint = parentSpacePoint - juce::Point<float>(static_cast<float>(getX()),
                                                                      static_cast<float>(getY()));
        return localPoint.getDistanceFrom({ 12.0f, getHeight() / 2.0f }) <= kConnectorHitRadius;
    }

    [[nodiscard]] bool isPointOverOutput(juce::Point<float> parentSpacePoint) const
    {
        const auto localPoint = parentSpacePoint - juce::Point<float>(static_cast<float>(getX()),
                                                                      static_cast<float>(getY()));
        return localPoint.getDistanceFrom({ getWidth() - 12.0f, getHeight() / 2.0f }) <= kConnectorHitRadius;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(bounds, kNodeCornerRadius);

        g.setColour(pickRoleColour(nodeRole));
        g.drawRoundedRectangle(bounds, kNodeCornerRadius, 2.0f);

        g.setColour(juce::Colours::white);
        const auto titleOptions = juce::FontOptions().withHeight(16.0f).withStyle("Bold");
        g.setFont(juce::Font(titleOptions));
        g.drawFittedText(displayName, bounds.reduced(12).toNearestInt(), juce::Justification::topLeft, 2);

        juce::String ioText = "In " + juce::String(numInputs) + " / Out " + juce::String(numOutputs);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(12.0f);
        g.drawText(ioText,
                   juce::Rectangle<int>(0, 0, getWidth(), getHeight()).reduced(14, 0).withTrimmedTop(36).withHeight(18),
                   juce::Justification::topLeft);

        drawConnector(g, { 12.0f, getHeight() / 2.0f }, numInputs > 0 || nodeRole == Role::Output);
        drawConnector(g, { getWidth() - 12.0f, getHeight() / 2.0f }, numOutputs > 0 || nodeRole == Role::Input);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            owner.showNodeContextMenu(nodeId, event.getScreenPosition());
            return;
        }

        auto* parent = getParentComponent();
        if (parent == nullptr)
            return;

        const auto parentPos = event.getEventRelativeTo(parent).position;

        if (isPointOverOutput(parentPos))
        {
            draggingConnection = true;
            owner.beginConnectionDrag(nodeId, getOutputConnectorPosition());
            owner.updateConnectionDrag(parentPos);
            return;
        }

        draggingNode = true;
        dragger.startDraggingComponent(this, event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        auto* parent = getParentComponent();
        if (parent == nullptr)
            return;

        if (draggingConnection)
        {
            owner.updateConnectionDrag(event.getEventRelativeTo(parent).position);
            return;
        }

        if (draggingNode)
        {
            dragger.dragComponent(this, event, nullptr);
            owner.updateNodePosition(nodeId, getBounds().toFloat().getPosition());
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        auto* parent = getParentComponent();
        if (parent == nullptr)
            return;

        if (draggingConnection)
        {
            draggingConnection = false;
            owner.completeConnectionDragAt(event.getEventRelativeTo(parent).position);
            return;
        }

        if (draggingNode)
        {
            draggingNode = false;
            owner.updateNodePosition(nodeId, getBounds().toFloat().getPosition());
        }
    }

private:
    void drawConnector(juce::Graphics& g, juce::Point<float> centre, bool enabled) const
    {
        const juce::Colour fill = enabled ? pickRoleColour(nodeRole) : juce::Colours::darkgrey;
        g.setColour(fill.withMultipliedAlpha(enabled ? 1.0f : 0.6f));
        g.fillEllipse(juce::Rectangle<float>(kConnectorRadius * 2.0f, kConnectorRadius * 2.0f).withCentre(centre));
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawEllipse(juce::Rectangle<float>(kConnectorRadius * 2.0f, kConnectorRadius * 2.0f).withCentre(centre), 1.2f);
    }

    static juce::Colour pickRoleColour(Role role) noexcept
    {
        switch (role)
        {
            case Role::Input:  return juce::Colours::skyblue;
            case Role::Output: return juce::Colours::mediumseagreen;
            case Role::Plugin: return juce::Colours::orange;
            default:           return juce::Colours::darkorange;
        }
    }

    GraphView& owner;
    NodeId nodeId;
    juce::String displayName;
    Role nodeRole { Role::General };
    int numInputs;
    int numOutputs;
    juce::ComponentDragger dragger;
    bool draggingConnection { false };
    bool draggingNode { false };
};

GraphView::GraphView()
{
    setInterceptsMouseClicks(true, true);
}

GraphView::~GraphView() = default;

void GraphView::setGraph(std::shared_ptr<host::graph::GraphEngine> graphEngine)
{
    graph = std::move(graphEngine);
    refreshGraph(false);
}

void GraphView::refreshGraph(bool preservePositions)
{
    syncNodes(preservePositions);
}

void GraphView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey.darker(0.4f));

    drawConnections(g);

    if (nodeComponents.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(16.0f);
        g.drawText("Graph is empty",
                   getLocalBounds(),
                   juce::Justification::centred);
    }
}

void GraphView::resized()
{
    for (const auto& component : nodeComponents)
    {
        updateNodePosition(component->getId(), component->getBounds().toFloat().getPosition());
    }
}

void GraphView::syncNodes(bool preservePositions)
{
    if (! graph)
    {
        nodeLookup.clear();
        nodePositions.clear();
        nodeComponents.clear();
        repaint();
        return;
    }

    try
    {
        graph->prepare();
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Graph prepare failed: " + juce::String(e.what()));
    }

    auto nodeIds = graph->getNodeIds();
    std::unordered_set<std::string> seen;
    seen.reserve(nodeIds.size());

    const auto inputId = graph->getInputNode();
    const auto outputId = graph->getOutputNode();

    for (const auto& id : nodeIds)
    {
        const auto key = id.toString().toStdString();
        seen.insert(key);

        auto* node = graph->getNode(id);
        juce::String displayName = node != nullptr ? juce::String(node->name()) : juce::String("Node");

        NodeComponent::Role role = NodeComponent::Role::General;
        if (! inputId.isNull() && id == inputId)
            role = NodeComponent::Role::Input;
        else if (! outputId.isNull() && id == outputId)
            role = NodeComponent::Role::Output;

        int inputs = 2;
        int outputs = 2;

        if (role == NodeComponent::Role::Input)
        {
            inputs = 0;
            outputs = 2;
        }
        else if (role == NodeComponent::Role::Output)
        {
            inputs = 2;
            outputs = 0;
        }

        if (auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node))
        {
            if (auto info = vstNode->pluginInfo())
            {
                inputs = info->ins;
                outputs = info->outs;
            }
            role = NodeComponent::Role::Plugin;
        }

        NodeComponent* component = findNodeComponent(id);
        if (component == nullptr)
        {
            auto newComponent = std::make_unique<NodeComponent>(*this, id, displayName, role, inputs, outputs);
            component = newComponent.get();
            addAndMakeVisible(component);
            nodeLookup[key] = component;
            nodeComponents.push_back(std::move(newComponent));

            if (! preservePositions || nodePositions.find(key) == nodePositions.end())
            {
                const auto index = static_cast<int>(nodeComponents.size()) - 1;
                const float x = 40.0f + (kNodeHorizontalSpacing * static_cast<float>(index));
                const float y = std::max(kDefaultTop, getHeight() / 2.0f - kNodeHeight / 2.0f);
                nodePositions[key] = { x, y };
            }
        }
        else
        {
            component->setDisplayName(displayName);
            component->setRole(role);
            component->setIO(inputs, outputs);
        }

        if (const auto posIt = nodePositions.find(key); posIt != nodePositions.end())
        {
            const auto& pos = posIt->second;
            component->setTopLeftPosition(static_cast<int>(std::round(pos.x)),
                                          static_cast<int>(std::round(pos.y)));
        }
    }

    for (auto it = nodeComponents.begin(); it != nodeComponents.end();)
    {
        const auto key = it->get()->getId().toString().toStdString();
        if (seen.find(key) == seen.end())
        {
            removeChildComponent(it->get());
            nodeLookup.erase(key);
            nodePositions.erase(key);
            it = nodeComponents.erase(it);
        }
        else
        {
            ++it;
        }
    }

    repaint();
}

void GraphView::updateNodePosition(NodeId id, juce::Point<float> topLeft)
{
    if (id.isNull())
        return;

    const auto bounds = getLocalBounds().toFloat();
    const float maxX = std::max(0.0f, bounds.getWidth() - kNodeWidth);
    const float maxY = std::max(0.0f, bounds.getHeight() - kNodeHeight);

    juce::Point<float> clamped {
        juce::jlimit(0.0f, maxX, topLeft.x),
        juce::jlimit(0.0f, maxY, topLeft.y)
    };

    const auto key = id.toString().toStdString();
    nodePositions[key] = clamped;

    if (auto* component = findNodeComponent(id))
    {
        component->setTopLeftPosition(static_cast<int>(std::round(clamped.x)),
                                      static_cast<int>(std::round(clamped.y)));
    }

    repaint();
}

void GraphView::beginConnectionDrag(NodeId id, juce::Point<float> startPosition)
{
    if (id.isNull())
        return;

    isDraggingConnection = true;
    connectionSource = id;
    connectionDragPoint = startPosition;
    repaint();
}

void GraphView::updateConnectionDrag(juce::Point<float> currentPosition)
{
    if (! isDraggingConnection)
        return;

    connectionDragPoint = currentPosition;
    repaint();
}

void GraphView::completeConnectionDragAt(juce::Point<float> position)
{
    if (! isDraggingConnection || connectionSource.isNull() || ! graph)
    {
        cancelConnectionDrag();
        return;
    }

    NodeComponent* targetComponent = nullptr;
    for (const auto& entry : nodeComponents)
    {
        auto* candidate = entry.get();
        if (candidate == nullptr || candidate->getId() == connectionSource)
            continue;

        if (candidate->isPointOverInput(position))
        {
            targetComponent = candidate;
            break;
        }
    }

    if (targetComponent != nullptr)
    {
        try
        {
            graph->connect(connectionSource, targetComponent->getId());
            graph->prepare();
            refreshGraph(true);
        }
        catch (const std::exception& e)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Connect Nodes",
                                                   "Failed to connect nodes:\n" + juce::String(e.what()));
        }
    }

    cancelConnectionDrag();
}

void GraphView::cancelConnectionDrag()
{
    isDraggingConnection = false;
    connectionSource = {};
    repaint();
}

void GraphView::showNodeContextMenu(NodeId id, juce::Point<int> screenPosition)
{
    if (id.isNull() || ! graph)
        return;

    juce::PopupMenu menu;
    menu.addItem(1, "Clear outgoing connections");
    menu.addItem(2, "Clear incoming connections");
    menu.addSeparator();
    menu.addItem(3, "Reset position");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPosition.x, screenPosition.y, 1, 1 }),
                       [this, id](int result)
                       {
                           switch (result)
                           {
                               case 1: clearConnectionsFrom(id); break;
                               case 2: clearConnectionsTo(id); break;
                               case 3:
                               {
                                   nodePositions.erase(id.toString().toStdString());
                                   refreshGraph(false);
                                   break;
                               }
                               default:
                                   break;
                           }
                       });
}

void GraphView::drawConnections(juce::Graphics& g)
{
    if (! graph)
        return;

    auto connections = graph->getConnections();

    g.setColour(juce::Colours::orange.withAlpha(0.85f));

    for (const auto& connection : connections)
    {
        const auto* from = findNodeComponent(connection.first);
        const auto* to = findNodeComponent(connection.second);

        if (from == nullptr || to == nullptr)
            continue;

        const auto start = from->getOutputConnectorPosition();
        const auto end = to->getInputConnectorPosition();

        juce::Path path;
        path.startNewSubPath(start);
        const auto controlOffset = std::max(40.0f, std::abs(end.x - start.x) / 2.0f);
        path.cubicTo({ start.x + controlOffset, start.y },
                     { end.x - controlOffset, end.y },
                     end);

        g.strokePath(path, juce::PathStrokeType(2.4f));
    }

    if (isDraggingConnection)
    {
        if (auto* sourceComponent = findNodeComponent(connectionSource))
        {
            const auto start = sourceComponent->getOutputConnectorPosition();
            juce::Path preview;
            preview.startNewSubPath(start);
            const auto controlOffset = std::max(40.0f, std::abs(connectionDragPoint.x - start.x) / 2.0f);
            preview.cubicTo({ start.x + controlOffset, start.y },
                            { connectionDragPoint.x - controlOffset, connectionDragPoint.y },
                            connectionDragPoint);

            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.strokePath(preview, juce::PathStrokeType(1.8f, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
        }
    }
}

void GraphView::clearConnectionsFrom(NodeId id)
{
    if (! graph)
        return;

    auto connections = graph->getConnections();
    for (const auto& connection : connections)
    {
        if (connection.first == id)
            graph->disconnect(connection.first, connection.second);
    }

    try
    {
        graph->prepare();
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Failed to prepare after clearing connections: " + juce::String(e.what()));
    }

    refreshGraph(true);
}

void GraphView::clearConnectionsTo(NodeId id)
{
    if (! graph)
        return;

    auto connections = graph->getConnections();
    for (const auto& connection : connections)
    {
        if (connection.second == id)
            graph->disconnect(connection.first, connection.second);
    }

    try
    {
        graph->prepare();
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("Failed to prepare after clearing incoming connections: " + juce::String(e.what()));
    }

    refreshGraph(true);
}

GraphView::NodeComponent* GraphView::findNodeComponent(NodeId id) const
{
    const auto key = id.toString().toStdString();
    const auto it = nodeLookup.find(key);
    if (it == nodeLookup.end())
        return nullptr;
    return it->second;
}

} // namespace host::gui
