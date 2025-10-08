#include "gui/Preferences.h"

#include <algorithm>
#include <cmath>

#include "util/Localization.h"

namespace host::gui
{
    using host::i18n::tr;
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
        updateDefaultPresetDisplay();
        applyTranslations();
    }

    PreferencesComponent::~PreferencesComponent()
    {
        host::i18n::manager().removeChangeListener(this);
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

        configureLabel(driverLabel, tr("preferences.audio.driver"));
        configureLabel(inputDeviceLabel, tr("preferences.audio.input"));
        configureLabel(outputDeviceLabel, tr("preferences.audio.output"));
        configureLabel(sampleRateLabel, tr("preferences.audio.sampleRate"));
        configureLabel(blockSizeLabel, tr("preferences.audio.blockSize"));

        audioTab->addAndMakeVisible(driverLabel);
        audioTab->addAndMakeVisible(driverBox);
        audioTab->addAndMakeVisible(inputDeviceLabel);
        audioTab->addAndMakeVisible(inputDeviceBox);
        audioTab->addAndMakeVisible(outputDeviceLabel);
        audioTab->addAndMakeVisible(outputDeviceBox);
        audioTab->addAndMakeVisible(sampleRateLabel);
        audioTab->addAndMakeVisible(engineSampleRateBox);
        audioTab->addAndMakeVisible(blockSizeLabel);
        audioTab->addAndMakeVisible(engineBlockBox);

        tabs.addTab(tr("preferences.tab.audio"), juce::Colours::grey, audioTab, true);

        driverBox.onChange = [this]
        {
            if (isUpdating)
                return;

            const auto selectedDriver = driverBox.getText();
            if (selectedDriver.isNotEmpty())
            {
                deviceManager.setCurrentAudioDeviceType(selectedDriver, true);
                refreshDeviceLists();
            }
        };

        inputDeviceBox.onChange = [this]
        {
            if (isUpdating)
                return;

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup(setup);
            const auto selected = inputDeviceBox.getText();
            setup.inputDeviceName = (selected.isNotEmpty() && selected != "(None)") ? selected : juce::String();
            deviceManager.setAudioDeviceSetup(setup, true);
            refreshDeviceLists();
        };

        outputDeviceBox.onChange = [this]
        {
            if (isUpdating)
                return;

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup(setup);
            const auto selected = outputDeviceBox.getText();
            setup.outputDeviceName = (selected.isNotEmpty() && selected != "(None)") ? selected : juce::String();
            deviceManager.setAudioDeviceSetup(setup, true);
            refreshDeviceLists();
        };

        engineSampleRateBox.onChange = [this]
        {
            if (isUpdating)
                return;

            const auto selectedRate = engineSampleRateBox.getText().getDoubleValue();
            if (selectedRate <= 0.0)
                return;

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup(setup);
            setup.sampleRate = selectedRate;
            deviceManager.setAudioDeviceSetup(setup, true);

            auto cfg = deviceEngine.getEngineConfig();
            cfg.sampleRate = selectedRate;
            deviceEngine.setEngineConfig(cfg);
            refreshEngineOptions();
            config.setEngineSettings({ cfg.sampleRate, cfg.blockSize });
            notifyConfigChanged();
        };

        engineBlockBox.onChange = [this]
        {
            if (isUpdating)
                return;

            const auto selectedBlock = engineBlockBox.getText().getIntValue();
            if (selectedBlock <= 0)
                return;

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup(setup);
            setup.bufferSize = selectedBlock;
            deviceManager.setAudioDeviceSetup(setup, true);

            auto cfg = deviceEngine.getEngineConfig();
            cfg.blockSize = selectedBlock;
            deviceEngine.setEngineConfig(cfg);
            refreshEngineOptions();
            config.setEngineSettings({ cfg.sampleRate, cfg.blockSize });
            notifyConfigChanged();
        };

        refreshDriverList();
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

    void PreferencesComponent::refreshDriverList()
    {
        const juce::ScopedValueSetter<bool> guard(isUpdating, true);
        driverBox.clear(juce::dontSendNotification);

        auto& types = deviceManager.getAvailableDeviceTypes();
        const auto currentType = deviceManager.getCurrentAudioDeviceType();
        int selectedId = 0;

        for (int i = 0; i < types.size(); ++i)
        {
            if (auto* type = types[i])
            {
                type->scanForDevices();
                const auto itemId = i + 1;
                driverBox.addItem(type->getTypeName(), itemId);
                if (type->getTypeName() == currentType)
                    selectedId = itemId;
            }
        }

        if (driverBox.getNumItems() == 0)
            return;

        if (selectedId == 0)
        {
            selectedId = driverBox.getItemId(0);
            const auto fallbackType = driverBox.getItemText(0);
            deviceManager.setCurrentAudioDeviceType(fallbackType, true);
        }

        driverBox.setSelectedId(selectedId, juce::dontSendNotification);
        refreshDeviceLists();
    }

    void PreferencesComponent::refreshDeviceLists()
    {
        const juce::ScopedValueSetter<bool> guard(isUpdating, true);
        inputDeviceBox.clear(juce::dontSendNotification);
        outputDeviceBox.clear(juce::dontSendNotification);

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);

        auto currentTypeName = deviceManager.getCurrentAudioDeviceType();
        auto& types = deviceManager.getAvailableDeviceTypes();
        juce::StringArray inputNames;
        juce::StringArray outputNames;

        for (auto* type : types)
        {
            if (type != nullptr && type->getTypeName() == currentTypeName)
            {
                type->scanForDevices();
                inputNames = type->getDeviceNames(true);
                outputNames = type->getDeviceNames(false);
                break;
            }
        }

        bool updatedSelection = false;
        if (setup.inputDeviceName.isEmpty() && inputNames.size() > 0)
        {
            setup.inputDeviceName = inputNames[0];
            updatedSelection = true;
        }

        if (setup.outputDeviceName.isEmpty() && outputNames.size() > 0)
        {
            setup.outputDeviceName = outputNames[0];
            updatedSelection = true;
        }

        if (updatedSelection)
            deviceManager.setAudioDeviceSetup(setup, true);

        deviceManager.getAudioDeviceSetup(setup);

        constexpr auto noneText = "(None)";
        inputDeviceBox.addItem(noneText, 1);
        outputDeviceBox.addItem(noneText, 1);

        int inputId = 2;
        for (const auto& name : inputNames)
            inputDeviceBox.addItem(name, inputId++);

        int outputId = 2;
        for (const auto& name : outputNames)
            outputDeviceBox.addItem(name, outputId++);

        const auto inputSelection = setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : juce::String(noneText);
        const auto outputSelection = setup.outputDeviceName.isNotEmpty() ? setup.outputDeviceName : juce::String(noneText);

        auto findItemIdForText = [](juce::ComboBox& box, const juce::String& text)
        {
            for (int i = 0; i < box.getNumItems(); ++i)
            {
                if (box.getItemText(i) == text)
                    return box.getItemId(i);
            }
            return 0;
        };

        if (auto id = findItemIdForText(inputDeviceBox, inputSelection); id != 0)
            inputDeviceBox.setSelectedId(id, juce::dontSendNotification);
        else
            inputDeviceBox.setText(inputSelection, juce::dontSendNotification);

        if (auto id = findItemIdForText(outputDeviceBox, outputSelection); id != 0)
            outputDeviceBox.setSelectedId(id, juce::dontSendNotification);
        else
            outputDeviceBox.setText(outputSelection, juce::dontSendNotification);

        refreshEngineOptions();
    }

    void PreferencesComponent::refreshEngineOptions()
    {
        const juce::ScopedValueSetter<bool> guard(isUpdating, true);
        engineSampleRateBox.clear(juce::dontSendNotification);
        engineBlockBox.clear(juce::dontSendNotification);

        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);

        auto* currentDevice = deviceManager.getCurrentAudioDevice();

        juce::Array<double> sampleRates;
        juce::Array<int> bufferSizes;

        if (currentDevice != nullptr)
        {
            sampleRates = currentDevice->getAvailableSampleRates();
            bufferSizes = currentDevice->getAvailableBufferSizes();
        }

        if (sampleRates.isEmpty())
            sampleRates = { 44100.0, 48000.0, 96000.0 };

        if (bufferSizes.isEmpty())
            bufferSizes = { 128, 256, 512 };

        int rateId = 1;
        int selectedRateId = 0;
        for (auto rate : sampleRates)
        {
            const auto label = juce::String(rate, 0);
            engineSampleRateBox.addItem(label, rateId);
            if (std::abs(rate - setup.sampleRate) < 1.0)
                selectedRateId = rateId;
            ++rateId;
        }

        int blockId = 1;
        int selectedBlockId = 0;
        for (auto size : bufferSizes)
        {
            engineBlockBox.addItem(juce::String(size), blockId);
            if (size == setup.bufferSize)
                selectedBlockId = blockId;
            ++blockId;
        }

        if (selectedRateId == 0)
            selectedRateId = 1;
        if (selectedBlockId == 0)
            selectedBlockId = 1;

        engineSampleRateBox.setSelectedId(selectedRateId, juce::dontSendNotification);
        engineBlockBox.setSelectedId(selectedBlockId, juce::dontSendNotification);
    }

    void PreferencesComponent::layoutAudioTab()
    {
        if (audioTab == nullptr)
            return;

        auto area = audioTab->getLocalBounds().reduced(20);
        const auto labelWidth = 160;
        const auto rowHeight = 48;
        const auto controlInset = 6;

        auto layoutRow = [&](juce::Label& label, juce::Component& field)
        {
            auto row = area.removeFromTop(rowHeight);
            auto labelArea = row.removeFromLeft(labelWidth);
            label.setBounds(labelArea);
            field.setBounds(row.reduced(0, controlInset));
            area.removeFromTop(8);
        };

        layoutRow(driverLabel, driverBox);
        layoutRow(inputDeviceLabel, inputDeviceBox);
        layoutRow(outputDeviceLabel, outputDeviceBox);
        layoutRow(sampleRateLabel, engineSampleRateBox);
        layoutRow(blockSizeLabel, engineBlockBox);
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
        driverLabel.setText(tr("preferences.audio.driver"), juce::dontSendNotification);
        inputDeviceLabel.setText(tr("preferences.audio.input"), juce::dontSendNotification);
        outputDeviceLabel.setText(tr("preferences.audio.output"), juce::dontSendNotification);
        sampleRateLabel.setText(tr("preferences.audio.sampleRate"), juce::dontSendNotification);
        blockSizeLabel.setText(tr("preferences.audio.blockSize"), juce::dontSendNotification);

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
        if (source == &host::i18n::manager())
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
