#include "gui/PluginSettingsComponent.h"

#include <filesystem>
#include <utility>

#include "graph/Nodes/VstFx.h"
#include "gui/PluginEditorSizing.h"
#include "persist/Preset.h"
#include "util/Localization.h"

namespace host::gui
{
using host::i18n::tr;

namespace
{
    constexpr int kDefaultWidth = 480;
    constexpr int kDefaultHeight = 460;
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

    openEditorButton.setButtonText(tr("plugin.settings.openEditor"));
    openEditorButton.onClick = [this]() { openEditor(); };
    addAndMakeVisible(openEditorButton);

    savePresetButton.setButtonText(tr("plugin.settings.savePreset"));
    savePresetButton.onClick = [this]() { savePreset(); };
    addAndMakeVisible(savePresetButton);

        loadPresetButton.setButtonText(tr("plugin.settings.loadPreset"));
        loadPresetButton.onClick = [this]() { loadPreset(); };
        addAndMakeVisible(loadPresetButton);

        // Size last so resized() lays out the already-added children and the
        // DialogWindow picks up the correct content bounds.
        setSize(kDefaultWidth, kDefaultHeight);

        refresh();

        // Watch the target node so this panel auto-closes (and frees the
        // dialog window that owns it) if the plugin is removed from the graph
        // while the settings dialog is open. Without this, deleting the node
        // and then closing the dialog dereferences a dead VstFxNode.
        startTimerHz(20);
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

    auto editorRow = area.removeFromTop(kRowHeight);
    openEditorButton.setBounds(editorRow.removeFromLeft(kLabelWidth + 220));

    auto presetRow = area.removeFromTop(kRowHeight);
    savePresetButton.setBounds(presetRow.removeFromLeft(140).reduced(2));
    loadPresetButton.setBounds(presetRow.removeFromLeft(140).reduced(2));
}

void PluginSettingsComponent::updateContent()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
    {
        // Graph gone: surface as unavailable and let the timer close the
        // hosting dialog.
        closeHostDialogIfPresent();
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

    auto node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node.get());
    if (! vstNode)
    {
        // Node was removed from the graph. Close the dialog so we never touch
        // a dangling node reference again.
        closeHostDialogIfPresent();
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
    const bool supportsEditor = hasInstance && vstNode->plugin()->hasEditor();
    openEditorButton.setEnabled(supportsEditor);

    latencyValue.setText(formatLatencySamples(vstNode->latencySamples()), juce::dontSendNotification);

    if (const auto& info = vstNode->pluginInfo())
    {
        const bool isVst3 = info->format == host::plugin::PluginFormat::VST3;
        formatValue.setText(isVst3 ? tr("plugin.format.vst3") : tr("plugin.format.vst2"),
                            juce::dontSendNotification);

        pathValue.setText(formatPathText(info->path), juce::dontSendNotification);
        pathValue.setTooltip(pathValue.getText());

        inputsValue.setText(info->ins > 0 ? juce::String(info->ins)
                                          : tr("plugin.settings.notAvailable"),
                            juce::dontSendNotification);
        outputsValue.setText(info->outs > 0 ? juce::String(info->outs)
                                            : tr("plugin.settings.notAvailable"),
                             juce::dontSendNotification);
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

    auto node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node.get());
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

    auto node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node.get());
    if (! vstNode)
        return;

    vstNode->setBypassed(bypassToggle.getToggleState());
    if (onSettingsChanged)
        onSettingsChanged();

    updateContent();
}

void PluginSettingsComponent::openEditor()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
        return;

    auto node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node.get());
    if (! vstNode)
        return;

    auto* plugin = vstNode->plugin();
    if (! plugin || ! plugin->hasEditor())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.editorUnavailable.title"),
                                               tr("plugin.settings.editorUnavailable.message"));
        return;
    }

    auto editor = plugin->createEditorComponent();
    if (! editor)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.editorUnavailable.title"),
                                               tr("plugin.settings.editorUnavailable.message"));
        return;
    }

    // Use a DocumentWindow with resizeToFitWhenContentChanges = true, exactly
    // like the reference host. The editor component is the size authority and
    // the window follows it - no host<->plugin resize loop, so editors stop
    // shrinking on focus loss and stop clipping async (re)sizes on open.
    const bool editorResizable = plugin->isEditorResizable();
    // Pass the graph + node id so the editor window auto-closes if the plugin
    // node is removed from the graph while the editor is open. This prevents
    // use-after-free crashes when the node is deleted out from under the open
    // editor.
    new host::gui::PluginEditorWindow(juce::String(vstNode->name()),
                                      std::move(editor),
                                      editorResizable,
                                      graph,
                                      targetId);
}

void PluginSettingsComponent::savePreset()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
        return;

    auto node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node.get());
    if (vstNode == nullptr)
        return;

    auto* plugin = vstNode->plugin();
    if (plugin == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.title"),
                                               tr("plugin.settings.unavailable"));
        return;
    }

    std::vector<std::uint8_t> stateData;
    if (! plugin->getState(stateData) || stateData.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.title"),
                                               tr("plugin.settings.unavailable"));
        return;
    }

    host::persist::Preset preset;
    preset.setName(juce::String(vstNode->name()));
    preset.captureFromState(juce::MemoryBlock(stateData.data(), stateData.size()));

    juce::FileChooser chooser(tr("plugin.settings.savePreset"),
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.vstpreset");
    if (! chooser.browseForFileToSave(true))
        return;

    if (! preset.save(chooser.getResult()))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.title"),
                                               tr("plugin.settings.unavailable"));
    }
}

void PluginSettingsComponent::loadPreset()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
        return;

    auto node = graphPtr->getNode(targetId);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node.get());
    if (vstNode == nullptr)
        return;

    auto* plugin = vstNode->plugin();
    if (plugin == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.title"),
                                               tr("plugin.settings.unavailable"));
        return;
    }

    juce::FileChooser chooser(tr("plugin.settings.loadPreset"),
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.vstpreset");
    if (! chooser.browseForFileToOpen())
        return;

    host::persist::Preset preset;
    if (! preset.load(chooser.getResult()))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.title"),
                                               tr("plugin.settings.unavailable"));
        return;
    }

    juce::MemoryBlock state;
    if (! preset.applyToState(state))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.title"),
                                               tr("plugin.settings.unavailable"));
        return;
    }

    if (! plugin->setState(static_cast<const std::uint8_t*>(state.getData()),
                          static_cast<std::size_t>(state.getSize())))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               tr("plugin.settings.title"),
                                               tr("plugin.settings.unavailable"));
        return;
    }

    updateContent();
}

void PluginSettingsComponent::timerCallback()
{
    auto graphPtr = graph.lock();
    if (! graphPtr)
    {
        closeHostDialogIfPresent();
        return;
    }

    // If the node has been removed from the graph, close the hosting dialog so
    // the panel (and its captured NodeId) are destroyed before any further
    // interaction can dereference the dead node.
    if (auto node = graphPtr->getNode(targetId))
    {
        if (dynamic_cast<host::graph::nodes::VstFxNode*>(node.get()) == nullptr)
            closeHostDialogIfPresent();
    }
    else
    {
        closeHostDialogIfPresent();
    }
}

void PluginSettingsComponent::closeHostDialogIfPresent()
{
    // Only attempt to close once. The dialog window owns this component, so
    // closing it deletes us too; stop the timer first to avoid callbacks
    // during destruction.
    if (! std::exchange(closedDialog_, true))
    {
        stopTimer();

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        {
            dw->exitModalState(0);
        }
        else if (auto* dw = dynamic_cast<juce::DialogWindow*>(findParentComponentOfClass<juce::DocumentWindow>()))
        {
            dw->exitModalState(0);
        }
    }
}
} // namespace host::gui
