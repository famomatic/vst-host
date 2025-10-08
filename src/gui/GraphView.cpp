#include "gui/GraphView.h"

#include "util/Localization.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <unordered_set>
#include <limits>

#include "graph/Nodes/VstFx.h"

namespace host::gui
{
using host::i18n::tr;
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
    [[nodiscard]] Role getRole() const noexcept { return nodeRole; }

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

    void setSelected(bool shouldBeSelected)
    {
        if (isSelected != shouldBeSelected)
        {
            isSelected = shouldBeSelected;
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

        if (isSelected)
        {
            g.setColour(juce::Colours::goldenrod.withAlpha(0.35f));
            g.fillRoundedRectangle(bounds.reduced(2.0f), kNodeCornerRadius);
        }

        g.setColour(pickRoleColour(nodeRole));
        g.drawRoundedRectangle(bounds, kNodeCornerRadius, 2.0f);

        if (isSelected)
        {
            g.setColour(juce::Colours::goldenrod);
            g.drawRoundedRectangle(bounds.reduced(2.0f), kNodeCornerRadius, 2.0f);
        }

        g.setColour(juce::Colours::white);
        const auto titleOptions = juce::FontOptions().withHeight(16.0f).withStyle("Bold");
        g.setFont(juce::Font(titleOptions));
        g.drawFittedText(displayName, bounds.reduced(12).toNearestInt(), juce::Justification::topLeft, 2);

        juce::String ioText = tr("graph.io");
        ioText = ioText.replace("%1", juce::String(numInputs))
                       .replace("%2", juce::String(numOutputs));
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
        owner.selectNode(nodeId);
        owner.grabKeyboardFocus();

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
    bool isSelected { false };
    int numInputs;
    int numOutputs;
    juce::ComponentDragger dragger;
    bool draggingConnection { false };
    bool draggingNode { false };
};

GraphView::GraphView()
{
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);
}

GraphView::~GraphView() = default;

void GraphView::setGraph(std::shared_ptr<host::graph::GraphEngine> graphEngine)
{
    graph = std::move(graphEngine);
    selectedNode = {};
    viewOffset = {};
    nodePositions.clear();
    nodeLookup.clear();
    nodeComponents.clear();
    removeAllChildren();
    refreshGraph(false);
}

void GraphView::refreshGraph(bool preservePositions)
{
    syncNodes(preservePositions);
}

void GraphView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey.darker(0.4f));

    const auto bounds = getLocalBounds().toFloat();
    constexpr float gridSize = 64.0f;
    const float startX = std::fmod(-viewOffset.x, gridSize);
    const float startY = std::fmod(-viewOffset.y, gridSize);
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    for (float x = startX; x < bounds.getWidth(); x += gridSize)
        g.drawVerticalLine(static_cast<int>(std::round(x)), 0.0f, bounds.getHeight());
    for (float y = startY; y < bounds.getHeight(); y += gridSize)
        g.drawHorizontalLine(static_cast<int>(std::round(y)), 0.0f, bounds.getWidth());

    drawConnections(g);

    if (nodeComponents.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(16.0f);
        g.drawText(tr("graph.empty"),
                   getLocalBounds(),
                   juce::Justification::centred);
    }
}

void GraphView::resized()
{
    setViewOffset(viewOffset);
}

void GraphView::syncNodes(bool preservePositions)
{
    if (! graph)
    {
        nodeLookup.clear();
        nodePositions.clear();
        nodeComponents.clear();
        removeAllChildren();
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
    int newPlacementIndex = 0;

    for (const auto& id : nodeIds)
    {
        const auto key = id.toString().toStdString();
        seen.insert(key);

        auto* node = graph->getNode(id);
        juce::String displayName = node != nullptr ? juce::String(node->name()) : tr("graph.node.default");

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
                const float column = static_cast<float>(newPlacementIndex % 5);
                const float row = static_cast<float>(newPlacementIndex / 5);
                const float baseX = viewOffset.x + std::max(40.0f, getWidth() / 2.0f - kNodeWidth / 2.0f);
                const float baseY = viewOffset.y + std::max(kDefaultTop, getHeight() / 2.0f - kNodeHeight / 2.0f);
                const float x = baseX + (column * kNodeHorizontalSpacing);
                const float y = baseY + (row * (kNodeHeight + 40.0f));
                nodePositions[key] = { x, y };
                ++newPlacementIndex;
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

    if (! selectedNode.isNull())
    {
        const auto selectedKey = selectedNode.toString().toStdString();
        if (nodeLookup.find(selectedKey) == nodeLookup.end())
            selectedNode = {};
    }

    updateSelectionVisuals();
    updateComponentPositions();
    repaint();
}

void GraphView::updateComponentPositions()
{
    for (const auto& component : nodeComponents)
    {
        if (component == nullptr)
            continue;

        const auto key = component->getId().toString().toStdString();
        if (const auto posIt = nodePositions.find(key); posIt != nodePositions.end())
        {
            const auto& world = posIt->second;
            const auto viewPos = world - viewOffset;
            component->setTopLeftPosition(static_cast<int>(std::round(viewPos.x)),
                                          static_cast<int>(std::round(viewPos.y)));
        }
    }
}

void GraphView::updateNodePosition(NodeId id, juce::Point<float> topLeft)
{
    if (id.isNull())
        return;

    juce::Point<float> world {
        std::max(0.0f, topLeft.x + viewOffset.x),
        std::max(0.0f, topLeft.y + viewOffset.y)
    };
    const auto key = id.toString().toStdString();
    nodePositions[key] = world;

    if (auto* component = findNodeComponent(id))
    {
        const auto viewPos = world - viewOffset;
        component->setTopLeftPosition(static_cast<int>(std::round(viewPos.x)),
                                      static_cast<int>(std::round(viewPos.y)));
    }

    repaint();
}

void GraphView::focusOnNode(NodeId id)
{
    if (id.isNull())
        return;

    selectNode(id);
    centerOnSelectedNode();
    grabKeyboardFocus();
}

void GraphView::deselectAll()
{
    if (selectedNode.isNull())
        return;

    selectedNode = {};
    updateSelectionVisuals();
    repaint();
}

void GraphView::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (event.mods.isPopupMenu())
    {
        showBackgroundMenu(event.getScreenPosition());
        return;
    }

    if (event.mods.isLeftButtonDown() && ! event.mods.isAnyModifierKeyDown())
        deselectAll();

    isPanning = event.mods.isMiddleButtonDown()
                || event.mods.isAltDown()
                || (event.mods.isLeftButtonDown() && ! event.mods.isAnyModifierKeyDown());

    panAnchor = event.position;
    panStartOffset = viewOffset;
}

void GraphView::mouseDrag(const juce::MouseEvent& event)
{
    if (! isPanning)
        return;

    const auto delta = event.position - panAnchor;
    setViewOffset(panStartOffset - juce::Point<float>(delta.x, delta.y));
}

void GraphView::mouseUp(const juce::MouseEvent&)
{
    isPanning = false;
}

bool GraphView::keyPressed(const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelectedNode();
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        deselectAll();
        return true;
    }

    if (mods.isCommandDown())
    {
        constexpr float nudgeAmount = 12.0f;
        if (code == juce::KeyPress::leftKey)
        {
            nudgeSelectedNode({ -nudgeAmount, 0.0f });
            return true;
        }
        if (code == juce::KeyPress::rightKey)
        {
            nudgeSelectedNode({ nudgeAmount, 0.0f });
            return true;
        }
        if (code == juce::KeyPress::upKey)
        {
            nudgeSelectedNode({ 0.0f, -nudgeAmount });
            return true;
        }
        if (code == juce::KeyPress::downKey)
        {
            nudgeSelectedNode({ 0.0f, nudgeAmount });
            return true;
        }

        if (code == 'F' || code == 'f')
        {
            centerOnSelectedNode();
            return true;
        }
    }

    return false;
}

void GraphView::showBackgroundMenu(juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    enum MenuAction
    {
        focusItemId = 1,
        resetItemId = 2,
        clearItemId = 3
    };

    if (! selectedNode.isNull())
        menu.addItem(focusItemId, tr("graph.menu.focus"));

    menu.addItem(resetItemId, tr("graph.menu.resetView"));
    menu.addItem(clearItemId, tr("graph.menu.clearSelection"));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPosition.x, screenPosition.y, 1, 1 }),
                       [this](int result)
                       {
                           switch (result)
                           {
                               case focusItemId: centerOnSelectedNode(); break;
                               case resetItemId: setViewOffset({}); break;
                               case clearItemId: deselectAll(); break;
                               default: break;
                           }
                       });
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
            juce::String message = tr("graph.error.connect.body").replace("%1", juce::String(e.what()));
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   tr("graph.error.connect.title"),
                                                   message);
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

    const auto* component = findNodeComponent(id);

    juce::PopupMenu menu;
    enum NodeMenuAction
    {
        clearOutgoingId = 1,
        clearIncomingId = 2,
        resetPositionId = 3,
        deleteNodeId = 4
    };

    menu.addItem(clearOutgoingId, tr("graph.context.clearOutgoing"));
    menu.addItem(clearIncomingId, tr("graph.context.clearIncoming"));
    menu.addSeparator();
    menu.addItem(resetPositionId, tr("graph.context.resetPosition"));

    const bool canDelete = component != nullptr
                           && component->getRole() != NodeComponent::Role::Input
                           && component->getRole() != NodeComponent::Role::Output;
    menu.addItem(deleteNodeId, tr("graph.context.delete"), canDelete);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({ screenPosition.x, screenPosition.y, 1, 1 }),
                       [this, id](int result)
                       {
                           switch (result)
                           {
                               case clearOutgoingId: clearConnectionsFrom(id); break;
                               case clearIncomingId: clearConnectionsTo(id); break;
                               case resetPositionId:
                               {
                                   nodePositions.erase(id.toString().toStdString());
                                   refreshGraph(false);
                                   break;
                               }
                               case deleteNodeId:
                               {
                                   selectedNode = id;
                                   deleteSelectedNode();
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

void GraphView::selectNode(NodeId id)
{
    if (id.isNull())
    {
        deselectAll();
        return;
    }

    if (selectedNode == id)
        return;

    const auto key = id.toString().toStdString();
    if (nodeLookup.find(key) == nodeLookup.end())
        return;

    selectedNode = id;
    updateSelectionVisuals();
    repaint();
}

void GraphView::updateSelectionVisuals()
{
    for (const auto& component : nodeComponents)
    {
        if (component == nullptr)
            continue;
        component->setSelected(! selectedNode.isNull() && component->getId() == selectedNode);
    }
}

void GraphView::deleteSelectedNode()
{
    if (! graph || selectedNode.isNull())
        return;

    auto* component = findNodeComponent(selectedNode);
    if (component == nullptr)
        return;

    const auto role = component->getRole();
    if (role == NodeComponent::Role::Input || role == NodeComponent::Role::Output)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                               tr("graph.error.delete.title"),
                                               tr("graph.error.delete.cannot"));
        return;
    }

    const auto key = selectedNode.toString().toStdString();

    auto connections = graph->getConnections();
    for (const auto& connection : connections)
    {
        if (connection.first == selectedNode || connection.second == selectedNode)
            graph->disconnect(connection.first, connection.second);
    }

    try
    {
        graph->removeNode(selectedNode);
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("graph.error.delete.title"),
                                               tr("graph.error.delete.failed").replace("%1", juce::String(e.what())));
        return;
    }

    nodePositions.erase(key);
    selectedNode = {};

    try
    {
        graph->prepare();
    }
    catch (const std::exception&)
    {
        // Allow the graph to recover on the next edit.
    }

    refreshGraph(true);
}

void GraphView::nudgeSelectedNode(juce::Point<float> delta)
{
    if (selectedNode.isNull())
        return;

    const auto key = selectedNode.toString().toStdString();
    if (auto it = nodePositions.find(key); it != nodePositions.end())
    {
        it->second += delta;
        it->second.x = std::max(0.0f, it->second.x);
        it->second.y = std::max(0.0f, it->second.y);
        updateComponentPositions();
        repaint();
    }
}

void GraphView::centerOnSelectedNode()
{
    if (selectedNode.isNull())
        return;

    const auto key = selectedNode.toString().toStdString();
    const auto it = nodePositions.find(key);
    if (it == nodePositions.end())
        return;

    const auto nodePosition = it->second;
    const float targetX = std::max(0.0f, nodePosition.x - (static_cast<float>(getWidth()) - kNodeWidth) / 2.0f);
    const float targetY = std::max(0.0f, nodePosition.y - (static_cast<float>(getHeight()) - kNodeHeight) / 2.0f);
    setViewOffset({ targetX, targetY });
}

void GraphView::setViewOffset(juce::Point<float> newOffset)
{
    const auto content = computeContentBounds();
    const float width = static_cast<float>(std::max(1, getWidth()));
    const float height = static_cast<float>(std::max(1, getHeight()));
    const float maxX = std::max(0.0f, content.getRight() - width);
    const float maxY = std::max(0.0f, content.getBottom() - height);

    viewOffset.x = juce::jlimit(0.0f, maxX, newOffset.x);
    viewOffset.y = juce::jlimit(0.0f, maxY, newOffset.y);

    updateComponentPositions();
    repaint();
}

void GraphView::panBy(juce::Point<float> delta)
{
    setViewOffset(viewOffset + delta);
}

juce::Rectangle<float> GraphView::computeContentBounds() const
{
    if (nodePositions.empty())
        return { 0.0f, 0.0f, static_cast<float>(getWidth()), static_cast<float>(getHeight()) };

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = 0.0f;
    float maxY = 0.0f;

    for (const auto& entry : nodePositions)
    {
        const auto& pos = entry.second;
        minX = std::min(minX, pos.x);
        minY = std::min(minY, pos.y);
        maxX = std::max(maxX, pos.x + kNodeWidth);
        maxY = std::max(maxY, pos.y + kNodeHeight);
    }

    if (minX == std::numeric_limits<float>::max() || minY == std::numeric_limits<float>::max())
        return { 0.0f, 0.0f, kNodeWidth, kNodeHeight };

    const float width = std::max(kNodeWidth, maxX - minX);
    const float height = std::max(kNodeHeight, maxY - minY);
    return { minX, minY, width, height };
}

} // namespace host::gui
