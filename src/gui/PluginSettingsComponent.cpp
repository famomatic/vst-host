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

    // Same sizing flow as MainWindow: size the editor component to its
    // preferred/known size, let the dialog wrap it via launchAsync, then keep
    // the window fitted to the editor through PluginEditorWindowController.
    // This fixes clipped (too small) and oversized windows, and - because the
    // editor component itself (not a seeded 400x300 dialog) is the size
    // authority - stops the shrink-on-reopen cycle that the old
    // setSize(400,300)+setContentOwned(true) path caused.
    auto* editorComponent = editor.get();
    const bool editorResizable = plugin->isEditorResizable();
    const auto sizing = host::gui::calculatePluginEditorSizing(*editorComponent, editorResizable);
    if (editorComponent->getWidth() != sizing.targetContentWidth
        || editorComponent->getHeight() != sizing.targetContentHeight)
    {
        editorComponent->setSize(sizing.targetContentWidth, sizing.targetContentHeight);
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(editor.release());
    options.dialogTitle = juce::String(vstNode->name());
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = editorResizable;
    options.escapeKeyTriggersCloseButton = true;
    options.dialogBackgroundColour = juce::Colours::darkgrey.darker(0.6f);
    options.useBottomRightCornerResizer = editorResizable;

    if (auto* dialog = options.launchAsync())
    {
        dialog->setResizable(editorResizable, editorResizable);

        if (auto* content = dialog->getContentComponent())
        {
            auto controller = std::make_unique<host::gui::PluginEditorWindowController>(*dialog, *content, editorResizable);
            controller->applySizing();
            dialog->getProperties().set(host::gui::kPluginEditorControllerProperty,
                                        juce::var(new host::gui::PluginEditorControllerAttachment(std::move(controller))));
        }
    }
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
} // namespace host::gui
