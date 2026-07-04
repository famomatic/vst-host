#include "gui/Preferences.h"

#include <algorithm>
#include <cmath>

#include "util/Localization.h"

namespace host::gui
{
    using host::i18n::tr;
    namespace
    {
        void reportAudioDeviceError(const juce::String& action, const juce::String& error, bool showAlert)
        {
            if (error.isEmpty())
                return;

            const auto message = "Audio device operation failed (" + action + "): " + error;
            juce::Logger::writeToLog(message);

            if (showAlert)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       host::i18n::tr("dialog.audioSettings.title"),
                                                       message);
            }
        }
    }

    PreferencesComponent::PreferencesComponent(host::audio::DeviceEngine& engine,
                                               std::shared_ptr<host::plugin::PluginScanner> scanner,
                                               juce::AudioDeviceManager& manager,
                                               host::persist::Config& configIn,
                                               std::function<void(const host::persist::Config&)> onConfigChangedIn)
        : deviceEngine(engine),
          pluginScanner(std::move(scanner)),
          deviceManager(manager),
          config(configIn),
          onConfigChanged(std::move(onConfigChangedIn))
    {
        setSize(720, 540);
        addAndMakeVisible(tabs);
        buildAudioTab();
        buildPluginTab();
        buildStartupTab();
        pluginPathList.setRowHeight(24);
        host::i18n::manager().addChangeListener(this);
        // The AudioDeviceSelectorComponent mutates the device setup directly;
        // listen to the manager so we can persist sample rate / buffer size /
        // device choices back into Config and keep the control-panel button
        // state in sync after every change.
        deviceManager.addChangeListener(this);
        updateDefaultPresetDisplay();
        applyTranslations();
    }

    PreferencesComponent::~PreferencesComponent()
    {
        host::i18n::manager().removeChangeListener(this);
        deviceManager.removeChangeListener(this);
    }

    void PreferencesComponent::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colours::darkgrey);
    }

    void PreferencesComponent::resized()
    {
        tabs.setBounds(getLocalBounds().reduced(10));
        layoutAudioTab();
        layoutPluginTab();
        layoutStartupTab();
    }

    void PreferencesComponent::parentHierarchyChanged()
    {
        if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
        {
            window->setResizeLimits(720, 540, 4096, 4096);
            window->setSize(std::max(window->getWidth(), 720), std::max(window->getHeight(), 540));
        }
    }

    void PreferencesComponent::buildAudioTab()
    {
        audioTab = new juce::Component();
        auto configureLabel = [](juce::Label& label, const juce::String& text)
        {
            label.setText(text, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centredLeft);
            label.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
        };

        configureLabel(resamplerQualityLabel, tr("preferences.audio.resamplerQuality"));
        configureLabel(pdcLabel, tr("preferences.audio.pdc"));

        // Embed the full AudioDeviceSelectorComponent so the audio tab offers
        // the same detailed control as the standalone "Audio Device Settings"
        // dialog did: driver, input/output device, active channel count, sample
        // rate, buffer size, and visible-but-disabled input/output channel
        // toggles. This replaces the duplicated, less capable combo boxes that
        // previously lived here and could not toggle individual channels.
        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 2, 0, 2, true, true, true, false);
        deviceSelector->setSize(560, 320);
        audioTab->addAndMakeVisible(*deviceSelector);

        audioTab->addAndMakeVisible(resamplerQualityLabel);
        audioTab->addAndMakeVisible(resamplerQualityBox);
        audioTab->addAndMakeVisible(pdcLabel);
        audioTab->addAndMakeVisible(pdcToggle);

        // Resampler quality: trades CPU for SRC accuracy between the device
        // sample rate and the engine sample rate.
        resamplerQualityBox.addItem(tr("preferences.audio.quality.linear"), 1);
        resamplerQualityBox.addItem(tr("preferences.audio.quality.catmull"), 2);
        resamplerQualityBox.addItem(tr("preferences.audio.quality.lagrange"), 3);
        resamplerQualityBox.addItem(tr("preferences.audio.quality.sinc"), 4);
        {
            const auto settings = config.getEngineSettings();
            // EngineSettings.resamplerQuality indexes: 0..3 map to item ids 1..4.
            const int itemId = std::clamp(settings.resamplerQuality + 1, 1, 4);
            resamplerQualityBox.setSelectedId(itemId, juce::dontSendNotification);
        }
        resamplerQualityBox.onChange = [this]
        {
            if (isUpdating)
                return;
            const auto selected = resamplerQualityBox.getSelectedId();
            if (selected <= 0)
                return;
            const int qualityIndex = selected - 1;
            auto cfg = deviceEngine.getEngineConfig();
            cfg.resamplerQuality = qualityIndex;
            deviceEngine.setEngineConfig(cfg);
            auto settings = config.getEngineSettings();
            settings.resamplerQuality = qualityIndex;
            config.setEngineSettings(settings);
            notifyConfigChanged();
        };

        // PDC: keep parallel chains sample-aligned with the longest path.
        pdcToggle.setToggleState(config.getEngineSettings().pdcEnabled,
                                 juce::dontSendNotification);
        pdcToggle.setColour(juce::ToggleButton::textColourId, juce::Colours::whitesmoke);
        pdcToggle.onClick = [this]
        {
            if (isUpdating)
                return;
            const bool enabled = pdcToggle.getToggleState();
            auto cfg = deviceEngine.getEngineConfig();
            cfg.pdcEnabled = enabled;
            deviceEngine.setEngineConfig(cfg);
            auto settings = config.getEngineSettings();
            settings.pdcEnabled = enabled;
            config.setEngineSettings(settings);
            notifyConfigChanged();
        };

        // ASIO (and some WASAPI) devices expose a vendor control panel for
        // hardware buffer / latch / exclusive-mode settings. Without it the
        // user cannot tune the low-latency path that makes ASIO / WASAPI
        // exclusive worthwhile, so it is surfaced here directly.
        controlPanelHint.setJustificationType(juce::Justification::centredLeft);
        controlPanelHint.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        audioTab->addAndMakeVisible(controlPanelButton);
        audioTab->addAndMakeVisible(controlPanelHint);
        controlPanelButton.onClick = [this]
        {
            auto* device = deviceManager.getCurrentAudioDevice();
            if (device == nullptr || ! device->hasControlPanel())
                return;

            // showControlPanel() may block (ASIO runs a modal loop). Disable
            // the button for the duration to avoid re-entrancy.
            controlPanelButton.setEnabled(false);
            const auto changed = device->showControlPanel();
            controlPanelButton.setEnabled(true);

            // ASIO control panels can change buffer size / sample rate behind
            // our back; re-applying the current setup forces the device to
            // reopen with the new hardware settings.
            if (changed)
            {
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                deviceManager.getAudioDeviceSetup(setup);
                const auto error = deviceManager.setAudioDeviceSetup(setup, true);
                reportAudioDeviceError("setAudioDeviceSetup(afterControlPanel)", error, true);
                refreshEngineControls();
            }
        };
        refreshEngineControls();

        tabs.addTab(tr("preferences.tab.audio"), juce::Colours::grey, audioTab, true);
    }

    void PreferencesComponent::buildPluginTab()
    {
        pluginTab = new juce::Component();
        pluginTab->addAndMakeVisible(pluginPathList);
        pluginTab->addAndMakeVisible(addPathButton);
        pluginTab->addAndMakeVisible(removePathButton);
        pluginTab->addAndMakeVisible(rescanButton);

        addPathButton.setButtonText(tr("preferences.plugins.add"));
        removePathButton.setButtonText(tr("preferences.plugins.remove"));
        rescanButton.setButtonText(tr("preferences.plugins.rescan"));

        tabs.addTab(tr("preferences.tab.plugins"), juce::Colours::grey, pluginTab, true);

        pluginPaths = config.getPluginDirectories();

        if (pluginScanner)
            pluginScanner->setSearchPaths(pluginPaths);

        pluginPathList.updateContent();

        addPathButton.onClick = [this]
        {
            juce::FileChooser chooser(tr("fileChooser.pluginDirectory"), juce::File::getSpecialLocation(juce::File::userHomeDirectory));
            if (chooser.browseForDirectory())
            {
                auto dir = chooser.getResult();
                if (std::find(pluginPaths.begin(), pluginPaths.end(), dir) != pluginPaths.end())
                    return;

                pluginPaths.push_back(dir);
                if (pluginScanner)
                    pluginScanner->setSearchPaths(pluginPaths);
                config.setPluginDirectories(pluginPaths);
                notifyConfigChanged();
                pluginPathList.updateContent();
                pluginPathList.repaint();
            }
        };

        removePathButton.onClick = [this]
        {
            auto selected = pluginPathList.getSelectedRow();
            if (selected >= 0 && selected < static_cast<int>(pluginPaths.size()))
            {
                auto file = pluginPaths[static_cast<size_t>(selected)];
                pluginPaths.erase(pluginPaths.begin() + selected);
                if (pluginScanner)
                    pluginScanner->setSearchPaths(pluginPaths);
                config.setPluginDirectories(pluginPaths);
                notifyConfigChanged();
                pluginPathList.updateContent();
                pluginPathList.repaint();
            }
        };

        rescanButton.onClick = [this]
        {
            if (pluginScanner)
                pluginScanner->scanAsync();
        };
    }

    void PreferencesComponent::buildStartupTab()
    {
        startupTab = new juce::Component();
        auto configureLabel = [](juce::Label& label, const juce::String& text)
        {
            label.setText(text, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centredLeft);
            label.setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
        };

        configureLabel(defaultPresetLabel, tr("preferences.startup.defaultPreset"));
        configureLabel(languageLabel, tr("preferences.startup.language"));
        defaultPresetValue.setJustificationType(juce::Justification::centredLeft);
        defaultPresetValue.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

        choosePresetButton.onClick = [this]
        {
            juce::FileChooser chooser(tr("fileChooser.defaultPreset"), config.getDefaultPreset());
            if (chooser.browseForFileToOpen())
            {
                config.setDefaultPreset(chooser.getResult());
                updateDefaultPresetDisplay();
                notifyConfigChanged();
            }
        };

        clearPresetButton.onClick = [this]
        {
            config.clearDefaultPreset();
            updateDefaultPresetDisplay();
            notifyConfigChanged();
        };

        choosePresetButton.setButtonText(tr("preferences.startup.browse"));
        clearPresetButton.setButtonText(tr("preferences.startup.clear"));

        languageBox.onChange = [this]
        {
            const auto index = languageBox.getSelectedItemIndex();
            if (index < 0 || index >= languageCodes.size())
                return;

            const auto code = languageCodes[index];
            if (host::i18n::manager().setLanguage(code))
            {
                config.setLanguage(code);
                notifyConfigChanged();
            }
        };

        startupTab->addAndMakeVisible(defaultPresetLabel);
        startupTab->addAndMakeVisible(defaultPresetValue);
        startupTab->addAndMakeVisible(choosePresetButton);
        startupTab->addAndMakeVisible(clearPresetButton);
        startupTab->addAndMakeVisible(languageLabel);
        startupTab->addAndMakeVisible(languageBox);

        tabs.addTab(tr("preferences.tab.startup"), juce::Colours::grey, startupTab, true);
        refreshLanguageOptions();
    }

    void PreferencesComponent::refreshEngineControls()
    {
        const juce::ScopedValueSetter<bool> guard(isUpdating, true);

        auto* device = deviceManager.getCurrentAudioDevice();
        const bool supported = (device != nullptr && device->hasControlPanel());

        controlPanelButton.setEnabled(supported);
        controlPanelButton.setButtonText(tr("preferences.audio.controlPanel"));

        if (supported)
        {
            controlPanelHint.setText(tr("preferences.audio.controlPanelHint"),
                                     juce::dontSendNotification);
        }
        else
        {
            controlPanelHint.setText(tr("preferences.audio.controlPanelUnavailable"),
                                     juce::dontSendNotification);
        }

        // The AudioDeviceSelectorComponent can change sample rate / buffer
        // size / active channels. Mirror those into the device engine config
        // and persist them so the choice survives a restart. DeviceEngine is
        // already registered as an AudioIODeviceCallback, so it picks up the
        // new format from audioDeviceAboutToStart; we additionally push the
        // engine config so resampler ratios and the persisted config stay
        // aligned with whatever the selector just applied.
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);

        auto cfg = deviceEngine.getEngineConfig();
        bool changed = false;
        if (device != nullptr)
        {
            const double deviceRate = device->getCurrentSampleRate();
            const int deviceBlock = device->getCurrentBufferSizeSamples();
            if (deviceRate > 0.0 && std::abs(deviceRate - cfg.sampleRate) > 0.5)
            {
                cfg.sampleRate = deviceRate;
                changed = true;
            }
            if (deviceBlock > 0 && deviceBlock != cfg.blockSize)
            {
                cfg.blockSize = deviceBlock;
                changed = true;
            }
        }

        if (changed)
        {
            deviceEngine.setEngineConfig(cfg);
            config.setEngineSettings({ cfg.sampleRate, cfg.blockSize });
            notifyConfigChanged();
        }
        else
        {
            // Still persist the device selection (input/output device names)
            // even when the numeric format is unchanged, so the chosen device
            // is remembered. AudioDeviceManager persists its own setup via the
            // AudioDeviceSetup, but Config is the host's source of truth.
            notifyConfigChanged();
        }
    }

    void PreferencesComponent::layoutAudioTab()
    {
        if (audioTab == nullptr)
            return;

        auto area = audioTab->getLocalBounds().reduced(20);
        const auto labelWidth = 200;
        const auto rowHeight = 48;
        const auto controlInset = 6;

        // The embedded AudioDeviceSelectorComponent owns the driver / device /
        // sample-rate / buffer-size / channel toggles, so it takes the top of
        // the tab. Host-engine-only options (resampler quality, PDC, vendor
        // control panel) live below it.
        const int selectorHeight = 320;
        if (deviceSelector != nullptr)
            deviceSelector->setBounds(area.removeFromTop(selectorHeight).reduced(controlInset));
        area.removeFromTop(12);

        auto layoutRow = [&](juce::Label& label, juce::Component& field)
        {
            auto row = area.removeFromTop(rowHeight);
            auto labelArea = row.removeFromLeft(labelWidth);
            label.setBounds(labelArea);
            field.setBounds(row.reduced(0, controlInset));
            area.removeFromTop(8);
        };

        layoutRow(resamplerQualityLabel, resamplerQualityBox);
        layoutRow(pdcLabel, pdcToggle);

        // Control panel row: ASIO / WASAPI-exclusive vendor panel.
        auto controlRow = area.removeFromTop(rowHeight);
        controlPanelButton.setBounds(controlRow.removeFromLeft(labelWidth).reduced(0, controlInset));
        controlPanelHint.setBounds(controlRow.reduced(0, controlInset));
        area.removeFromTop(8);
    }

    void PreferencesComponent::layoutPluginTab()
    {
        if (pluginTab == nullptr)
            return;

        auto area = pluginTab->getLocalBounds().reduced(10);
        auto controls = area.removeFromBottom(32);
        addPathButton.setBounds(controls.removeFromLeft(100).reduced(2));
        removePathButton.setBounds(controls.removeFromLeft(100).reduced(2));
        rescanButton.setBounds(controls.removeFromLeft(100).reduced(2));
        pluginPathList.setBounds(area);
    }

    void PreferencesComponent::layoutStartupTab()
    {
        if (startupTab == nullptr)
            return;

        auto area = startupTab->getLocalBounds().reduced(20);

        auto presetRow = area.removeFromTop(36);
        defaultPresetLabel.setBounds(presetRow.removeFromLeft(150));

        auto presetButtons = presetRow.removeFromRight(220);
        choosePresetButton.setBounds(presetButtons.removeFromLeft(140).reduced(4));
        clearPresetButton.setBounds(presetButtons.reduced(4));
        defaultPresetValue.setBounds(presetRow.reduced(4));

        area.removeFromTop(12);
        auto languageRow = area.removeFromTop(32);
        languageLabel.setBounds(languageRow.removeFromLeft(150));
        languageBox.setBounds(languageRow.reduced(4));
    }

    int PreferencesComponent::getNumRows()
    {
        return static_cast<int>(pluginPaths.size());
    }

    void PreferencesComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
    {
        if (row < 0 || row >= getNumRows())
            return;

        auto bounds = juce::Rectangle<int>(0, 0, width, height);
        g.setColour(rowIsSelected ? juce::Colours::darkorange : juce::Colours::transparentBlack);
        g.fillRect(bounds);
        g.setColour(juce::Colours::white);
        g.drawText(pluginPaths[static_cast<size_t>(row)].getFullPathName(), bounds.reduced(4), juce::Justification::centredLeft);
    }

    void PreferencesComponent::applyTranslations()
    {
        resamplerQualityLabel.setText(tr("preferences.audio.resamplerQuality"), juce::dontSendNotification);
        pdcLabel.setText(tr("preferences.audio.pdc"), juce::dontSendNotification);
        pdcToggle.setButtonText(tr("preferences.audio.pdc"));

        controlPanelButton.setButtonText(tr("preferences.audio.controlPanel"));
        controlPanelHint.setText(tr("preferences.audio.controlPanelHint"), juce::dontSendNotification);

        defaultPresetLabel.setText(tr("preferences.startup.defaultPreset"), juce::dontSendNotification);
        languageLabel.setText(tr("preferences.startup.language"), juce::dontSendNotification);

        addPathButton.setButtonText(tr("preferences.plugins.add"));
        removePathButton.setButtonText(tr("preferences.plugins.remove"));
        rescanButton.setButtonText(tr("preferences.plugins.rescan"));
        choosePresetButton.setButtonText(tr("preferences.startup.browse"));
        clearPresetButton.setButtonText(tr("preferences.startup.clear"));

        if (tabs.getNumTabs() >= 1)
            tabs.setTabName(0, tr("preferences.tab.audio"));
        if (tabs.getNumTabs() >= 2)
            tabs.setTabName(1, tr("preferences.tab.plugins"));
        if (tabs.getNumTabs() >= 3)
            tabs.setTabName(2, tr("preferences.tab.startup"));

        refreshLanguageOptions();
        updateDefaultPresetDisplay();
    }

    void PreferencesComponent::refreshLanguageOptions()
    {
        const auto currentLanguage = host::i18n::manager().getLanguage();
        languageCodes.clear();
        languageBox.clear(juce::dontSendNotification);

        auto languages = host::i18n::manager().getAvailableLanguages();
        int itemId = 1;
        int selectedId = 0;

        for (const auto& entry : languages)
        {
            juce::String displayName = entry.second;
            if (entry.first.equalsIgnoreCase("en"))
                displayName = tr("preferences.language.english");
            else if (entry.first.equalsIgnoreCase("ko"))
                displayName = tr("preferences.language.korean");

            languageBox.addItem(displayName, itemId);
            languageCodes.add(entry.first);
            if (entry.first.equalsIgnoreCase(currentLanguage))
                selectedId = itemId;
            ++itemId;
        }

        if (selectedId == 0 && languageBox.getNumItems() > 0)
            selectedId = 1;

        if (selectedId != 0)
            languageBox.setSelectedId(selectedId, juce::dontSendNotification);
    }

    void PreferencesComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
    {
        if (source == &deviceManager)
        {
            // A device change came from the embedded selector (or the vendor
            // control panel). Refresh the engine/control-panel state and
            // persist the new settings. Guard against recursive notifications
            // triggered by our own setEngineConfig.
            if (! isUpdating)
                refreshEngineControls();
        }
        else if (source == &host::i18n::manager())
            applyTranslations();
    }

    void PreferencesComponent::updateDefaultPresetDisplay()
    {
        const auto preset = config.getDefaultPreset();
        if (preset.getFullPathName().isEmpty())
        {
            defaultPresetValue.setText(tr("preferences.startup.noPreset"), juce::dontSendNotification);
            defaultPresetValue.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            return;
        }

        const bool exists = preset.existsAsFile();
        juce::String display = exists
                                    ? preset.getFullPathName()
                                    : tr("preferences.startup.missingPreset").replace("%1", preset.getFullPathName());

        defaultPresetValue.setText(display, juce::dontSendNotification);
        defaultPresetValue.setColour(juce::Label::textColourId, exists ? juce::Colours::whitesmoke : juce::Colours::orange);
    }

    void PreferencesComponent::notifyConfigChanged()
    {
        const auto cfg = deviceEngine.getEngineConfig();
        config.setEngineSettings({ cfg.sampleRate, cfg.blockSize });
        if (onConfigChanged)
            onConfigChanged(config);
    }
}
