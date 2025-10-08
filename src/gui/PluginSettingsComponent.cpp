#include "gui/PluginSettingsComponent.h"

#include <filesystem>
#include <utility>

#include "graph/Nodes/VstFx.h"
#include "util/Localization.h"

namespace host::gui
{
using host::i18n::tr;

namespace
{
    constexpr int kDefaultWidth = 440;
    constexpr int kDefaultHeight = 300;
    constexpr int kLabelWidth = 150;
    constexpr int kRowHeight = 28;
    constexpr int kVerticalGap = 8;

    [[nodiscard]] juce::String formatPathText(const std::filesystem::path& path)
    {
        if (path.empty())
            return tr("plugin.settings.notAvailable");

        return juce::String(path.generic_string());
    }

    [[nodiscard]] juce::String formatLatencySamples(int samples)
    {
        return juce::String(samples) + " " + tr("plugin.settings.samplesLabel");
    }
}

PluginSettingsComponent::PluginSettingsComponent(std::shared_ptr<host::graph::GraphEngine> graphEngine,
                                                 host::graph::GraphEngine::NodeId nodeId,
                                                 std::function<void()> onChanged)
    : graph(std::move(graphEngine)),
      targetId(nodeId),
      onSettingsChanged(std::move(onChanged))
{
    setSize(kDefaultWidth, kDefaultHeight);

    nameLabel.setText(tr("plugin.settings.name"), juce::dontSendNotification);
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nameLabel);

    nameEditor.setSelectAllWhenFocused(true);
    nameEditor.onReturnKey = [this]() { commitNameChange(); };
    nameEditor.onFocusLost = [this]() { commitNameChange(); };
    addAndMakeVisible(nameEditor);

    statusLabel.setText(tr("plugin.settings.status"), juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    statusValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusValue);

    formatLabel.setText(tr("plugin.settings.format"), juce::dontSendNotification);
    formatLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(formatLabel);

    formatValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(formatValue);

    pathLabel.setText(tr("plugin.settings.path"), juce::dontSendNotification);
    pathLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(pathLabel);

    pathValue.setJustificationType(juce::Justification::topLeft);
    pathValue.setMinimumHorizontalScale(1.0f);
    pathValue.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(pathValue);

    inputsLabel.setText(tr("plugin.settings.inputs"), juce::dontSendNotification);
    inputsLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(inputsLabel);

    inputsValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(inputsValue);

    outputsLabel.setText(tr("plugin.settings.outputs"), juce::dontSendNotification);
    outputsLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(outputsLabel);

    outputsValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(outputsValue);

    latencyLabel.setText(tr("plugin.settings.latency"), juce::dontSendNotification);
    latencyLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyLabel);

    latencyValue.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyValue);

    bypassToggle.setButtonText(tr("plugin.settings.bypass"));
    bypassToggle.onClick = [this]() { applyBypassState(); };
    addAndMakeVisible(bypassToggle);

    refresh();
}

void PluginSettingsComponent::refresh()
{
    updateContent();
    repaint();
}

void PluginSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(16);

    auto placeRow = [&](juce::Component& label, juce::Component& value, int height) -> void
    {
        auto row = area.removeFromTop(height);
        label.setBounds(row.removeFromLeft(kLabelWidth));
        value.setBounds(row);
        area.removeFromTop(kVerticalGap);
    };

    placeRow(nameLabel, nameEditor, kRowHeight);
    placeRow(statusLabel, statusValue, kRowHeight);
    placeRow(formatLabel, formatValue, kRowHeight);

    auto pathRow = area.removeFromTop(64);
    pathLabel.setBounds(pathRow.removeFromLeft(kLabelWidth));
    pathValue.setBounds(pathRow);
    area.removeFromTop(kVerticalGap);

    placeRow(inputsLabel, inputsValue, kRowHeight);
    placeRow(outputsLabel, outputsValue, kRowHeight);
    placeRow(latencyLabel, latencyValue, kRowHeight);

    auto bypassRow = area.removeFromTop(kRowHeight);
    bypassToggle.setBounds(bypassRow.removeFromLeft(kLabelWidth + 220));
}

void PluginSettingsComponent::updateContent()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
    {
        nameEditor.setEnabled(false);
        bypassToggle.setEnabled(false);
        statusValue.setText(tr("plugin.settings.unavailable"), juce::dontSendNotification);
        formatValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        pathValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        inputsValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        outputsValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        latencyValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        return;
    }

    auto* node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node);
    if (! vstNode)
    {
        nameEditor.setEnabled(false);
        bypassToggle.setEnabled(false);
        statusValue.setText(tr("plugin.settings.unavailable"), juce::dontSendNotification);
        formatValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        pathValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        inputsValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        outputsValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        latencyValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        return;
    }

    nameEditor.setEnabled(true);
    bypassToggle.setEnabled(true);

    nameEditor.setText(juce::String(vstNode->name()), juce::dontSendNotification);

    const bool hasInstance = vstNode->plugin() != nullptr;
    statusValue.setText(hasInstance
                            ? tr("plugin.settings.status.loaded")
                            : tr("plugin.settings.status.missing"),
                        juce::dontSendNotification);

    bypassToggle.setToggleState(vstNode->isBypassed(), juce::dontSendNotification);
    bypassToggle.setEnabled(hasInstance);

    latencyValue.setText(formatLatencySamples(vstNode->latencySamples()), juce::dontSendNotification);

    if (const auto& info = vstNode->pluginInfo())
    {
        const bool isVst3 = info->format == host::plugin::PluginFormat::VST3;
        formatValue.setText(isVst3 ? tr("plugin.format.vst3") : tr("plugin.format.vst2"),
                            juce::dontSendNotification);

        pathValue.setText(formatPathText(info->path), juce::dontSendNotification);
        pathValue.setTooltip(pathValue.getText());

        inputsValue.setText(juce::String(info->ins), juce::dontSendNotification);
        outputsValue.setText(juce::String(info->outs), juce::dontSendNotification);
    }
    else
    {
        formatValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        pathValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        pathValue.setTooltip({});
        inputsValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
        outputsValue.setText(tr("plugin.settings.notAvailable"), juce::dontSendNotification);
    }
}

void PluginSettingsComponent::commitNameChange()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
        return;

    auto* node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node);
    if (! vstNode)
        return;

    const auto newName = nameEditor.getText().trim();
    if (newName.isEmpty())
    {
        // Allow clearing the name, but avoid redundant updates.
        if (! juce::String(vstNode->name()).isEmpty())
        {
            vstNode->setDisplayName({});
            if (onSettingsChanged)
                onSettingsChanged();
            updateContent();
        }
        return;
    }

    if (newName == juce::String(vstNode->name()))
        return;

    vstNode->setDisplayName(newName.toStdString());
    if (onSettingsChanged)
        onSettingsChanged();
    updateContent();
}

void PluginSettingsComponent::applyBypassState()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
        return;

    auto* node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node);
    if (! vstNode)
        return;

    vstNode->setBypassed(bypassToggle.getToggleState());
    if (onSettingsChanged)
        onSettingsChanged();

    updateContent();
}
} // namespace host::gui
