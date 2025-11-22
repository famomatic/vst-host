#include "gui/MainWindow.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "graph/Nodes/AudioIn.h"
#include "graph/Nodes/AudioOut.h"
#include "graph/Nodes/GainNode.h"
#include "graph/Nodes/Merge.h"
#include "graph/Nodes/Mix.h"
#include "graph/Nodes/Split.h"
#include "graph/Nodes/VstFx.h"
#include "gui/ConsoleWindow.h"
#include "gui/PluginSettingsComponent.h"
#include "persist/Project.h"
#include "util/Localization.h"

namespace
{
    enum MenuItem
    {
        menuOpen = 1,
        menuSave,
        menuNewEmpty,
        menuPreferences,
        menuDeviceSelector,
        menuRescan,
        menuExit,
        menuHelpShow,
        menuViewConsole
    };

    constexpr int kPluginEditorMinWidth = 50;
    constexpr int kPluginEditorMinHeight = 50;
    constexpr int kPluginEditorMaxWidth = 4096;
    constexpr int kPluginEditorMaxHeight = 3072;
    constexpr int kPluginSettingsMinWidth = 440;
    constexpr int kPluginSettingsMinHeight = 300;
    constexpr int kPluginSettingsMaxWidth = 2048;
    constexpr int kPluginSettingsMaxHeight = 1400;

    const juce::Identifier kPluginEditorControllerProperty("pluginEditorController");

    struct PluginEditorSizing
    {
        int minContentWidth {};
        int minContentHeight {};
        int maxContentWidth {};
        int maxContentHeight {};
        int targetContentWidth {};
        int targetContentHeight {};
    };

    PluginEditorSizing calculatePluginEditorSizing(juce::Component& editorComponent, bool editorResizable)
    {
        int preferredContentWidth = juce::jmax(1, editorComponent.getWidth());
        int preferredContentHeight = juce::jmax(1, editorComponent.getHeight());

        if (preferredContentWidth <= 1)
            preferredContentWidth = kPluginEditorMinWidth;

        if (preferredContentHeight <= 1)
            preferredContentHeight = kPluginEditorMinHeight;

        int minContentWidth = editorResizable ? 1 : preferredContentWidth;
        int minContentHeight = editorResizable ? 1 : preferredContentHeight;
        int maxContentWidth = kPluginEditorMaxWidth;
        int maxContentHeight = kPluginEditorMaxHeight;
        int constrainerMinWidth = 0;
        int constrainerMinHeight = 0;

        if (auto* audioProcessorEditor = dynamic_cast<juce::AudioProcessorEditor*>(&editorComponent))
        {
            if (auto* constrainer = audioProcessorEditor->getConstrainer())
            {
                constrainerMinWidth = constrainer->getMinimumWidth();
                constrainerMinHeight = constrainer->getMinimumHeight();

                if (constrainerMinWidth > 0)
                    minContentWidth = juce::jmax(minContentWidth, constrainerMinWidth);

                if (constrainerMinHeight > 0)
                    minContentHeight = juce::jmax(minContentHeight, constrainerMinHeight);

                const int constrainerMaxWidth = constrainer->getMaximumWidth();
                if (constrainerMaxWidth > 0)
                    maxContentWidth = juce::jmin(maxContentWidth, constrainerMaxWidth);

                const int constrainerMaxHeight = constrainer->getMaximumHeight();
                if (constrainerMaxHeight > 0)
                    maxContentHeight = juce::jmin(maxContentHeight, constrainerMaxHeight);
            }
        }

        if (editorResizable)
        {
            if (constrainerMinWidth <= 0)
            {
                const int fallbackWidth = preferredContentWidth > 1 ? juce::jmin(preferredContentWidth, kPluginEditorMinWidth)
                                                                    : kPluginEditorMinWidth;
                minContentWidth = juce::jmax(minContentWidth, fallbackWidth);
            }

            if (constrainerMinHeight <= 0)
            {
                const int fallbackHeight = preferredContentHeight > 1 ? juce::jmin(preferredContentHeight, kPluginEditorMinHeight)
                                                                      : kPluginEditorMinHeight;
                minContentHeight = juce::jmax(minContentHeight, fallbackHeight);
            }
        }

        preferredContentWidth = juce::jmax(preferredContentWidth, minContentWidth);
        preferredContentHeight = juce::jmax(preferredContentHeight, minContentHeight);

        const int resolvedMaxContentWidth = maxContentWidth > 0 ? juce::jmax(minContentWidth, maxContentWidth) : std::numeric_limits<int>::max();
        const int resolvedMaxContentHeight = maxContentHeight > 0 ? juce::jmax(minContentHeight, maxContentHeight) : std::numeric_limits<int>::max();

        const int targetContentWidth = juce::jlimit(minContentWidth, resolvedMaxContentWidth, preferredContentWidth);
        const int targetContentHeight = juce::jlimit(minContentHeight, resolvedMaxContentHeight, preferredContentHeight);

        return { minContentWidth, minContentHeight, resolvedMaxContentWidth, resolvedMaxContentHeight, targetContentWidth, targetContentHeight };
    }

    void applyPluginEditorSizingToDialog(juce::DialogWindow& dialog, juce::Component& editorComponent, const PluginEditorSizing& sizing)
    {
        const auto contentBorder = dialog.getContentComponentBorder();
        const auto windowBorder = dialog.getBorderThickness();
        const int horizontalPadding = contentBorder.getLeftAndRight() + windowBorder.getLeftAndRight();
        const int verticalPadding = contentBorder.getTopAndBottom() + windowBorder.getTopAndBottom();

        const auto currentBounds = dialog.getBounds();
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(currentBounds);
        const auto userArea = display != nullptr ? display->userArea
                                                 : juce::Desktop::getInstance().getDisplays().getTotalBounds(true);

        const int availableContentWidth = juce::jmax(1, userArea.getWidth() - horizontalPadding);
        const int availableContentHeight = juce::jmax(1, userArea.getHeight() - verticalPadding);

        const int screenMinContentWidth = juce::jmin(sizing.minContentWidth, availableContentWidth);
        const int screenMinContentHeight = juce::jmin(sizing.minContentHeight, availableContentHeight);

        int screenMaxContentWidth = sizing.maxContentWidth == std::numeric_limits<int>::max()
                                        ? availableContentWidth
                                        : juce::jmin(sizing.maxContentWidth, availableContentWidth);
        int screenMaxContentHeight = sizing.maxContentHeight == std::numeric_limits<int>::max()
                                          ? availableContentHeight
                                          : juce::jmin(sizing.maxContentHeight, availableContentHeight);

        screenMaxContentWidth = juce::jmax(screenMinContentWidth, screenMaxContentWidth);
        screenMaxContentHeight = juce::jmax(screenMinContentHeight, screenMaxContentHeight);

        const int minWindowWidth = screenMinContentWidth + horizontalPadding;
        const int minWindowHeight = screenMinContentHeight + verticalPadding;
        const int maxWindowWidth = screenMaxContentWidth + horizontalPadding;
        const int maxWindowHeight = screenMaxContentHeight + verticalPadding;

        dialog.setResizeLimits(minWindowWidth, minWindowHeight, maxWindowWidth, maxWindowHeight);

        const int desiredWindowWidth = sizing.targetContentWidth + horizontalPadding;
        const int desiredWindowHeight = sizing.targetContentHeight + verticalPadding;

        const int targetWindowWidth = juce::jlimit(minWindowWidth, maxWindowWidth, desiredWindowWidth);
        const int targetWindowHeight = juce::jlimit(minWindowHeight, maxWindowHeight, desiredWindowHeight);

        auto adjustedBounds = currentBounds.withSizeKeepingCentre(targetWindowWidth, targetWindowHeight);
        adjustedBounds = adjustedBounds.constrainedWithin(userArea);
        dialog.setBounds(adjustedBounds);

        const int targetContentWidth = juce::jlimit(screenMinContentWidth, screenMaxContentWidth, sizing.targetContentWidth);
        const int targetContentHeight = juce::jlimit(screenMinContentHeight, screenMaxContentHeight, sizing.targetContentHeight);

        if (editorComponent.getWidth() != targetContentWidth || editorComponent.getHeight() != targetContentHeight)
            editorComponent.setSize(targetContentWidth, targetContentHeight);
    }

    class PluginEditorWindowController : public juce::ComponentListener
    {
    public:
        PluginEditorWindowController(juce::DialogWindow& dialogIn,
                                     juce::Component& editorIn,
                                     bool editorResizableIn)
            : dialog(&dialogIn),
              editor(&editorIn),
              editorResizable(editorResizableIn)
        {
            if (auto* comp = editor.getComponent())
                comp->addComponentListener(this);
        }

        ~PluginEditorWindowController() override
        {
            if (auto* comp = editor.getComponent())
                comp->removeComponentListener(this);
        }

        void applySizing()
        {
            if (auto* dialogPtr = dialog.getComponent())
                if (auto* editorPtr = editor.getComponent())
                    applySizingInternal(*dialogPtr, *editorPtr);
        }

        void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
        {
            if (wasResized)
                applySizing();
        }

    private:
        void applySizingInternal(juce::DialogWindow& dialogWindow, juce::Component& editorComponent)
        {
            if (updating)
                return;

            const juce::ScopedValueSetter<bool> scope(updating, true);
            const auto sizing = calculatePluginEditorSizing(editorComponent, editorResizable);
            applyPluginEditorSizingToDialog(dialogWindow, editorComponent, sizing);
        }

        bool updating { false };
        juce::Component::SafePointer<juce::DialogWindow> dialog;
        juce::Component::SafePointer<juce::Component> editor;
        bool editorResizable { false };
    };

    struct PluginEditorControllerAttachment final : public juce::DynamicObject
    {
        explicit PluginEditorControllerAttachment(std::unique_ptr<PluginEditorWindowController> controllerIn)
            : controller(std::move(controllerIn))
        {
        }

        std::unique_ptr<PluginEditorWindowController> controller;
    };
}

class MainWindow::TrayIcon : public juce::SystemTrayIconComponent
{
public:
    explicit TrayIcon(MainWindow& ownerIn) : owner(ownerIn)
    {
        auto colourImage = createIconImage();
        juce::Image templateImage;
        setIconImage(colourImage, templateImage);
        setIconTooltip(host::i18n::tr("app.title"));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            showMenu();
        }
        else if (event.mods.isLeftButtonDown())
        {
            owner.toggleVisibilityFromTray();
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            showMenu();
    }

private:
    static juce::Image createIconImage()
    {
        constexpr int size = 64;
        juce::Image image(juce::Image::ARGB, size, size, true);
        juce::Graphics g(image);
        auto bounds = image.getBounds().toFloat().reduced(4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillEllipse(bounds);
        g.setColour(juce::Colours::darkorange);
        g.fillEllipse(bounds.reduced(6.0f));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::FontOptions().withHeight(28.0f).withStyle("Bold"));
        g.drawFittedText("V", bounds.toNearestInt(), juce::Justification::centred, 1);
        return image;
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        enum TrayCommands
        {
            toggleWindow = 1,
            openSettings = 2,
            exitApp = 3
        };

        const bool windowVisible = owner.isVisible() && ! owner.hiddenToTray;
        menu.addItem(toggleWindow, windowVisible ? host::i18n::tr("tray.hide") : host::i18n::tr("tray.show"));
        menu.addItem(openSettings, host::i18n::tr("tray.settings"));
        menu.addSeparator();
        menu.addItem(exitApp, host::i18n::tr("tray.exit"));

        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [this](int result)
                           {
                               switch (result)
                               {
                                   case toggleWindow: owner.toggleVisibilityFromTray(); break;
                                   case openSettings: owner.showSettingsFromTray(); break;
                                   case exitApp: owner.exitApplication(); break;
                                   default: break;
                               }
                           });
    }

    MainWindow& owner;
};

MainWindow::MainWindow()
    : juce::DocumentWindow(host::i18n::tr("app.title"), juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
      graphEngine(std::make_shared<host::graph::GraphEngine>()),
      pluginScanner(std::make_shared<host::plugin::PluginScanner>()),
      deviceEngine()
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);

    deviceEngine.setGraph(graphEngine);
    deviceEngine.setEngineConfig({ 48000.0, 256 });

    deviceManager.initialise(0, 2, nullptr, true);
    deviceManager.addAudioCallback(&deviceEngine);

    pluginBrowser.setScanner(pluginScanner);
    pluginBrowser.setOnPluginChosen([this](const host::plugin::PluginInfo& info)
    {
        addPluginToGraph(info);
    });
    graphView.setGraph(graphEngine);
    graphView.setOnRequestNodeSettings([this](host::graph::GraphEngine::NodeId id)
    {
        openPluginSettings(id);
    });

    leftPanel.addAndMakeVisible(pluginBrowser);
    rightPanel.addAndMakeVisible(graphView);

    layoutManager.setItemLayout(0, 200, 400, 260);
    layoutManager.setItemLayout(1, 4, 8, 6);
    layoutManager.setItemLayout(2, 200, -0.9, -0.9);

    auto* content = new juce::Component();
    content->addAndMakeVisible(leftPanel);
    content->addAndMakeVisible(resizerBar);
    content->addAndMakeVisible(rightPanel);
    setContentOwned(content, true);

    setMenuBar(this);

    loadConfiguration();

    if (! loadStartupGraph())
        initialiseGraph();

    trayIcon = std::make_unique<TrayIcon>(*this);

    refreshTranslations();
    host::i18n::manager().addChangeListener(this);

    setCentrePosition(200, 200);
    setSize(1024, 768);
    setVisible(true);
}

MainWindow::~MainWindow()
{
    saveLastSession();
    saveConfiguration();
    host::i18n::manager().removeChangeListener(this);
    if (pluginScanner && pluginCacheFile.getFullPathName().isNotEmpty())
        pluginScanner->saveCache(pluginCacheFile);
    trayIcon.reset();
    setMenuBar(nullptr);
    deviceManager.removeAudioCallback(&deviceEngine);
}

void MainWindow::closeButtonPressed()
{
    if (trayIcon != nullptr)
        minimiseToTray();
    else
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::resized()
{
    juce::DocumentWindow::resized();

    if (auto* content = getContentComponent())
    {
        auto area = content->getLocalBounds();
        juce::Component* comps[] = { &leftPanel, &resizerBar, &rightPanel };
        layoutManager.layOutComponents(comps, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);
        pluginBrowser.setBounds(leftPanel.getLocalBounds());
        graphView.setBounds(rightPanel.getLocalBounds());
    }
}

juce::StringArray MainWindow::getMenuBarNames()
{
    juce::StringArray names;
    names.add(host::i18n::tr("menu.file"));
    names.add(host::i18n::tr("menu.edit"));
    names.add(host::i18n::tr("menu.view"));
    names.add(host::i18n::tr("menu.help"));
    return names;
}

juce::PopupMenu MainWindow::getMenuForIndex(int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)
    {
        menu.addItem(menuOpen, host::i18n::tr("menu.file.open"));
        menu.addItem(menuSave, host::i18n::tr("menu.file.save"));
        menu.addItem(menuNewEmpty, host::i18n::tr("menu.file.newEmpty"));
        menu.addSeparator();
        menu.addItem(menuDeviceSelector, host::i18n::tr("menu.file.audioSettings"));
        menu.addItem(menuPreferences, host::i18n::tr("menu.file.preferences"));
        menu.addSeparator();
        menu.addItem(menuExit, host::i18n::tr("menu.file.exit"));
    }
    else if (menuIndex == 1)
    {
        menu.addItem(menuRescan, host::i18n::tr("menu.edit.rescan"));
    }
    else if (menuIndex == 2)
    {
        const bool consoleVisible = consoleWindow != nullptr && consoleWindow->isVisible();
        menu.addItem(menuViewConsole, host::i18n::tr("menu.view.console"), true, consoleVisible);
    }
    else if (menuIndex == 3)
    {
        menu.addItem(menuHelpShow, host::i18n::tr("menu.help.show"));
    }

    return menu;
}

void MainWindow::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
        case menuOpen:      loadProject(); break;
        case menuSave:      saveProject(); break;
        case menuNewEmpty:  initialiseGraph(); break;
        case menuPreferences: openPreferences(); break;
        case menuDeviceSelector: showDeviceSelector(); break;
        case menuRescan:
            if (pluginScanner)
                pluginScanner->scanAsync();
            break;
        case menuExit:      exitApplication(); break;
        case menuHelpShow:  showHelpDialog(); break;
        case menuViewConsole: toggleConsoleWindow(); break;
        default:
            break;
    }
}

void MainWindow::minimiseToTray()
{
    hiddenToTray = true;
    setVisible(false);
}

void MainWindow::restoreFromTray()
{
    hiddenToTray = false;
    setVisible(true);
    setMinimised(false);
    toFront(true);
    graphView.grabKeyboardFocus();
}

void MainWindow::toggleVisibilityFromTray()
{
    if (hiddenToTray || ! isVisible())
        restoreFromTray();
    else
        minimiseToTray();
}

void MainWindow::showSettingsFromTray()
{
    restoreFromTray();
    openPreferences();
}

void MainWindow::exitApplication()
{
    if (auto* app = juce::JUCEApplication::getInstance())
        app->systemRequestedQuit();
}

void MainWindow::loadConfiguration()
{
    configDirectory = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VSTHost");
    if (! configDirectory.isDirectory())
        configDirectory.createDirectory();

    configFile = configDirectory.getChildFile("config.json");
    pluginCacheFile = configDirectory.getChildFile("plugin-cache.json");
    lastSessionFile = configDirectory.getChildFile("last-session.json");

    const bool loaded = config.load(configFile);
    bool needsSave = ! loaded;

    if (config.getPluginDirectories().empty())
    {
        config.setPluginDirectories(getDefaultPluginDirectories());
        needsSave = true;
    }

    auto settings = config.getEngineSettings();
    if (settings.sampleRate <= 0.0 || settings.blockSize <= 0)
    {
        auto current = deviceEngine.getEngineConfig();
        settings.sampleRate = current.sampleRate;
        settings.blockSize = current.blockSize;
        config.setEngineSettings(settings);
        needsSave = true;
    }

    host::audio::EngineConfig engineCfg { settings.sampleRate, settings.blockSize };
    deviceEngine.setEngineConfig(engineCfg);

    if (pluginScanner)
    {
        pluginScanner->setSearchPaths(config.getPluginDirectories());
        pluginScanner->loadCache(pluginCacheFile);
    }

    auto languageCode = config.getLanguage();
    if (languageCode.isEmpty())
        languageCode = "en";

    auto languageDir = configDirectory.getChildFile("i18n");
    if (languageDir.isDirectory())
    {
        host::i18n::manager().loadOverridesFromFile(languageDir.getChildFile("en.json"));
        host::i18n::manager().loadOverridesFromFile(languageDir.getChildFile(languageCode + ".json"));
    }

    host::i18n::manager().setLanguage(languageCode);

    if (needsSave)
        saveConfiguration();
}

void MainWindow::saveConfiguration()
{
    if (! configDirectory.isDirectory())
        configDirectory.createDirectory();

    auto engineCfg = deviceEngine.getEngineConfig();
    config.setEngineSettings({ engineCfg.sampleRate, engineCfg.blockSize });

    if (pluginScanner)
    {
        std::vector<juce::File> paths;
        const auto& scannerPaths = pluginScanner->getSearchPaths();
        paths.reserve(static_cast<size_t>(scannerPaths.size()));
        for (auto& path : scannerPaths)
            paths.push_back(path);
        config.setPluginDirectories(paths);
    }

    config.setLanguage(host::i18n::manager().getLanguage());

    config.save(configFile);
}

bool MainWindow::loadStartupGraph()
{
    const auto preset = config.getDefaultPreset();
    if (preset.getFullPathName().isNotEmpty())
    {
        if (preset.existsAsFile())
        {
            if (loadProjectFromFile(preset))
                return true;

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   host::i18n::tr("error.loadPreset.title"),
                                                   host::i18n::tr("error.loadPreset.message").replace("%1", preset.getFullPathName()));
        }
        else
        {
            juce::Logger::writeToLog("Default preset not found: " + preset.getFullPathName());
        }
    }

    if (lastSessionFile.existsAsFile())
        return loadProjectFromFile(lastSessionFile);

    return false;
}

bool MainWindow::loadProjectFromFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    host::persist::Project project;
    if (! project.load(file))
        return false;

    rebuildGraphFromProject(project);
    return true;
}

void MainWindow::saveLastSession()
{
    if (! graphEngine)
        return;

    if (lastSessionFile.getFullPathName().isEmpty())
        return;

    auto parent = lastSessionFile.getParentDirectory();
    if (! parent.isDirectory())
        parent.createDirectory();

    host::persist::Project project;
    project.save(lastSessionFile, *graphEngine);
}

std::vector<juce::File> MainWindow::getDefaultPluginDirectories() const
{
    std::vector<juce::File> defaults;
#if JUCE_MAC
    defaults.emplace_back("/Library/Audio/Plug-Ins/VST3");
    defaults.emplace_back(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST3"));
#elif JUCE_WINDOWS
    defaults.emplace_back("C:/Program Files/Common Files/VST3");
    defaults.emplace_back("C:/Program Files (x86)/Common Files/VST3");
    defaults.emplace_back("C:/Program Files/Steinberg/VstPlugins");
#else
    defaults.emplace_back(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".vst3"));
    defaults.emplace_back("/usr/lib/vst3");
    defaults.emplace_back("/usr/local/lib/vst3");
#endif
    return defaults;
}

void MainWindow::refreshTranslations()
{
    setName(host::i18n::tr("app.title"));
    if (trayIcon)
        trayIcon->setIconTooltip(host::i18n::tr("app.title"));
    if (consoleWindow)
        consoleWindow->setName(host::i18n::tr("console.title"));

    menuItemsChanged();
    pluginBrowser.refreshTranslations();
    pluginBrowser.repaint();
    graphView.repaint();
}

void MainWindow::showHelpDialog()
{
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                           host::i18n::tr("help.title"),
                                           host::i18n::tr("help.content"));
}

void MainWindow::toggleConsoleWindow()
{
    if (consoleWindow != nullptr && consoleWindow->isVisible())
    {
        consoleWindow->setVisible(false);
        menuItemsChanged();
        return;
    }

    showConsoleWindow();
    menuItemsChanged();
}

void MainWindow::showConsoleWindow()
{
    if (consoleWindow == nullptr)
    {
        consoleWindow = std::make_unique<host::gui::ConsoleWindow>();
        consoleWindow->setOnHide([this]()
        {
            menuItemsChanged();
        });
        consoleWindow->centreWithSize(consoleWindow->getWidth(), consoleWindow->getHeight());
    }

    if (! consoleWindow->isVisible())
        consoleWindow->setVisible(true);

    consoleWindow->toFront(true);
}

void MainWindow::openPluginSettings(host::graph::GraphEngine::NodeId id)
{
    if (id.isNull() || ! graphEngine)
        return;

    auto* node = graphEngine->getNode(id);
    auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node);
    if (vstNode == nullptr)
        return;

    if (auto* plugin = vstNode->plugin())
    {
        if (plugin->hasEditor())
        {
            if (auto editor = plugin->createEditorComponent())
            {
                auto* editorComponent = editor.get();
                auto* audioProcessorEditor = dynamic_cast<juce::AudioProcessorEditor*>(editorComponent);
                const bool editorResizable = audioProcessorEditor != nullptr && audioProcessorEditor->isResizable();

                const auto sizing = calculatePluginEditorSizing(*editorComponent, editorResizable);

                if (editorComponent->getWidth() != sizing.targetContentWidth || editorComponent->getHeight() != sizing.targetContentHeight)
                    editorComponent->setSize(sizing.targetContentWidth, sizing.targetContentHeight);

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
                        auto controller = std::make_unique<PluginEditorWindowController>(*dialog, *content, editorResizable);
                        controller->applySizing();
                        dialog->getProperties().set(kPluginEditorControllerProperty,
                                                    juce::var(new PluginEditorControllerAttachment(std::move(controller))));
                    }
                }
                return;
            }
        }

        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("plugin.settings.editorUnavailable.title"),
                                               host::i18n::tr("plugin.settings.editorUnavailable.message"));
    }

    auto settingsComponent = std::make_unique<host::gui::PluginSettingsComponent>(graphEngine,
                                                                                  id,
                                                                                  [this]()
                                                                                  {
                                                                                      graphView.refreshGraph(true);
                                                                                  });

    auto* settingsComponentPtr = settingsComponent.get();
    const int settingsMinWidth = juce::jmax(settingsComponentPtr->getWidth(), kPluginSettingsMinWidth);
    const int settingsMinHeight = juce::jmax(settingsComponentPtr->getHeight(), kPluginSettingsMinHeight);

    if (settingsComponentPtr->getWidth() != settingsMinWidth || settingsComponentPtr->getHeight() != settingsMinHeight)
        settingsComponentPtr->setSize(settingsMinWidth, settingsMinHeight);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(settingsComponent.release());
    options.dialogTitle = host::i18n::tr("plugin.settings.title");
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.useBottomRightCornerResizer = true;
    options.escapeKeyTriggersCloseButton = true;
    options.dialogBackgroundColour = juce::Colours::darkgrey.darker(0.6f);

    if (auto* dialog = options.launchAsync())
    {
        const int settingsMaxWidth = juce::jmax(settingsMinWidth, kPluginSettingsMaxWidth);
        const int settingsMaxHeight = juce::jmax(settingsMinHeight, kPluginSettingsMaxHeight);
        dialog->setResizeLimits(settingsMinWidth, settingsMinHeight, settingsMaxWidth, settingsMaxHeight);
        const auto currentBounds = dialog->getBounds();
        dialog->setBounds(currentBounds.withSizeKeepingCentre(juce::jmax(currentBounds.getWidth(), settingsMinWidth),
                                                              juce::jmax(currentBounds.getHeight(), settingsMinHeight)));
    }
}

void MainWindow::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &host::i18n::manager())
        refreshTranslations();
}

void MainWindow::initialiseGraph()
{
    graphEngine->clear();
    const auto engineCfg = deviceEngine.getEngineConfig();
    graphEngine->setEngineFormat(engineCfg.sampleRate, engineCfg.blockSize);

    auto inputId = graphEngine->addNode(std::make_unique<host::graph::nodes::AudioInNode>());
    auto outputId = graphEngine->addNode(std::make_unique<host::graph::nodes::AudioOutNode>());

    graphEngine->setIO(inputId, outputId);
    graphEngine->connect(inputId, outputId);
    graphEngine->prepare();
    graphView.refreshGraph(false);
}

void MainWindow::openPreferences()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new host::gui::PreferencesComponent(deviceEngine,
                                                                 pluginScanner,
                                                                 deviceManager,
                                                                 config,
                                                                 [this](const host::persist::Config&)
                                                                 {
                                                                     saveConfiguration();
                                                                 }));
    options.dialogTitle = host::i18n::tr("dialog.preferences.title");
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.runModal();
}

void MainWindow::showDeviceSelector()
{
    juce::AudioDeviceSelectorComponent selector(deviceManager, 0, 2, 0, 2, true, true, true, false);
    selector.setSize(500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setNonOwned(&selector);
    options.dialogTitle = host::i18n::tr("dialog.audioSettings.title");
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.runModal();
}

void MainWindow::rebuildGraphFromProject(const host::persist::Project& project)
{
    if (! graphEngine)
        return;

    const auto engineConfig = deviceEngine.getEngineConfig();
    graphEngine->clear();
    graphEngine->setEngineFormat(engineConfig.sampleRate, engineConfig.blockSize);

    std::unordered_map<juce::Uuid, host::graph::GraphEngine::NodeId> idMap;
    idMap.reserve(project.getNodes().size());

    std::vector<host::graph::GraphEngine::NodeId> orderedIds;
    orderedIds.reserve(project.getNodes().size());

    juce::StringArray missingPlugins;

    auto createNodeForDefinition = [&](const host::persist::Project::NodeDefinition& definition)
        -> std::unique_ptr<host::graph::Node>
    {
        const auto normalisedType = definition.type.isNotEmpty()
                                        ? definition.type.toLowerCase().removeCharacters(" ")
                                        : definition.name.toLowerCase().removeCharacters(" ");

        if (normalisedType == "audioin" || definition.name.equalsIgnoreCase("Audio In"))
            return std::make_unique<host::graph::nodes::AudioInNode>();
        if (normalisedType == "audioout" || definition.name.equalsIgnoreCase("Audio Out"))
            return std::make_unique<host::graph::nodes::AudioOutNode>();
        if (normalisedType == "gain")
            return std::make_unique<host::graph::nodes::GainNode>();
        if (normalisedType == "mix")
            return std::make_unique<host::graph::nodes::MixNode>();
        if (normalisedType == "split")
            return std::make_unique<host::graph::nodes::SplitNode>();
        if (normalisedType == "merge")
            return std::make_unique<host::graph::nodes::MergeNode>();

        const bool looksLikePlugin = normalisedType == "vstfx"
                                     || definition.pluginPath.isNotEmpty()
                                     || definition.pluginId.isNotEmpty();

        if (looksLikePlugin)
        {
            host::plugin::PluginInfo info;
            info.id = definition.pluginId.toStdString();
            info.name = definition.name.toStdString();
            info.latency = definition.latency;
            info.ins = definition.inputs > 0 ? definition.inputs : 2;
            info.outs = definition.outputs > 0 ? definition.outputs : 2;

            if (definition.pluginFormat.equalsIgnoreCase("VST2"))
                info.format = host::plugin::PluginFormat::VST2;
            else
                info.format = host::plugin::PluginFormat::VST3;

            if (definition.pluginPath.isNotEmpty())
                info.path = std::filesystem::path(definition.pluginPath.toStdString());

            const auto hydrateFromScanner = [&]()
            {
                if ((! info.path.empty()) && definition.pluginPath.isNotEmpty())
                    return;

                if (! pluginScanner)
                    return;

                const auto discovered = pluginScanner->getDiscoveredPlugins();
                const auto it = std::find_if(discovered.begin(), discovered.end(),
                                             [&](const host::plugin::PluginInfo& candidate)
                                             {
                                                 const bool idMatches = ! info.id.empty() && candidate.id == info.id;
                                                 const bool nameMatches = ! info.name.empty() && candidate.name == info.name;
                                                 return idMatches || nameMatches;
                                             });

                if (it != discovered.end())
                    info = *it;
            };

            hydrateFromScanner();

            const auto pluginFileExists = [&]() -> bool
            {
                if (info.path.empty())
                    return false;

                std::error_code ec;
                const bool exists = std::filesystem::exists(info.path, ec);
                return exists && ec.value() == 0;
            }();

            std::unique_ptr<host::plugin::PluginInstance> instance;

            if (pluginFileExists)
            {
                if (info.format == host::plugin::PluginFormat::VST2)
                    instance = host::plugin::loadVst2(info);
                else
                    instance = host::plugin::loadVst3(info);

                if (instance && definition.pluginState.getSize() > 0)
                {
                    instance->setState(static_cast<const std::uint8_t*>(definition.pluginState.getData()),
                                       static_cast<std::size_t>(definition.pluginState.getSize()));
                }
            }

            if (! instance)
            {
                const auto descriptor = definition.name.isNotEmpty() ? definition.name : juce::String(info.id);
                missingPlugins.add("• " + descriptor);
            }

            std::optional<host::plugin::PluginInfo> storedInfo;
            if (! info.id.empty() || ! info.name.empty() || ! info.path.empty())
                storedInfo = info;

            return std::make_unique<host::graph::nodes::VstFxNode>(std::move(instance),
                                                                   definition.name.toStdString(),
                                                                   storedInfo);
        }

        return {};
    };

    for (const auto& nodeDef : project.getNodes())
    {
        auto node = createNodeForDefinition(nodeDef);
        if (node == nullptr)
        {
            if (nodeDef.type.isNotEmpty())
                juce::Logger::writeToLog("Unknown node type in project: " + nodeDef.type);
            continue;
        }

        host::graph::GraphEngine::NodeId assignedId;

        if (! nodeDef.id.isNull())
        {
            try
            {
                assignedId = graphEngine->addNodeWithId(nodeDef.id, std::move(node));
            }
            catch (const std::exception& e)
            {
                juce::Logger::writeToLog("Failed to reuse node id " + nodeDef.id.toString() + ": " + e.what());
                assignedId = graphEngine->addNode(std::move(node));
            }
        }
        else
        {
            assignedId = graphEngine->addNode(std::move(node));
        }

        if (! nodeDef.id.isNull())
            idMap[nodeDef.id] = assignedId;

        orderedIds.push_back(assignedId);
    }

    if (! project.getConnections().empty())
    {
        for (const auto& connection : project.getConnections())
        {
            const auto fromIt = idMap.find(connection.from);
            const auto toIt = idMap.find(connection.to);
            if (fromIt != idMap.end() && toIt != idMap.end())
            {
                try
                {
                    graphEngine->connect(fromIt->second, toIt->second);
                }
                catch (const std::exception& e)
                {
                    juce::Logger::writeToLog("Failed to connect nodes: " + juce::String(e.what()));
                }
            }
        }
    }
    else
    {
        for (size_t i = 1; i < orderedIds.size(); ++i)
        {
            try
            {
                graphEngine->connect(orderedIds[i - 1], orderedIds[i]);
            }
            catch (const std::exception& e)
            {
                juce::Logger::writeToLog("Failed to connect sequential nodes: " + juce::String(e.what()));
            }
        }
    }

    const auto resolveNodeId = [&](juce::Uuid desiredId, bool useFrontFallback)
    {
        if (! desiredId.isNull())
        {
            const auto it = idMap.find(desiredId);
            if (it != idMap.end())
                return it->second;
        }

        if (! orderedIds.empty())
            return useFrontFallback ? orderedIds.front() : orderedIds.back();

        return host::graph::GraphEngine::NodeId {};
    };

    const auto inputId = resolveNodeId(project.getInputNodeId(), true);
    const auto outputId = resolveNodeId(project.getOutputNodeId(), false);

    if (! inputId.isNull() && ! outputId.isNull())
    {
        try
        {
            graphEngine->setIO(inputId, outputId);
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("Failed to assign graph IO: " + juce::String(e.what()));
        }
    }

    graphEngine->prepare();
    graphView.refreshGraph(false);

    if (missingPlugins.size() > 0)
    {
        const auto missingList = missingPlugins.joinIntoString("\n");
        const auto message = host::i18n::tr("error.missingPlugins.message").replace("%1", missingList);
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.missingPlugins.title"),
                                               message);
    }
}

void MainWindow::addPluginToGraph(const host::plugin::PluginInfo& info)
{
    if (! graphEngine)
        return;

    host::plugin::PluginInfo pluginInfo = info;

    std::unique_ptr<host::plugin::PluginInstance> instance;

    try
    {
        if (pluginInfo.format == host::plugin::PluginFormat::VST2)
            instance = host::plugin::loadVst2(pluginInfo);
        else
            instance = host::plugin::loadVst3(pluginInfo);
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.loadPlugin.title"),
                                               host::i18n::tr("error.loadPlugin.failed").replace("%1", juce::String(e.what())));
        return;
    }

    if (! instance)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.loadPlugin.title"),
                                               host::i18n::tr("error.loadPlugin.instantiate"));
        return;
    }

    auto node = std::make_unique<host::graph::nodes::VstFxNode>(std::move(instance), pluginInfo.name, pluginInfo);
    host::graph::GraphEngine::NodeId newNodeId;

    try
    {
        newNodeId = graphEngine->addNode(std::move(node));
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.graphUpdate.title"),
                                               host::i18n::tr("error.graphUpdate.message").replace("%1", juce::String(e.what())));
        return;
    }

    const auto inputId = graphEngine->getInputNode();
    const auto outputId = graphEngine->getOutputNode();

    std::vector<host::graph::GraphEngine::NodeId> previousSources;
    auto connections = graphEngine->getConnections();

    if (! outputId.isNull())
    {
        for (const auto& connection : connections)
        {
            if (connection.second == outputId)
            {
                previousSources.push_back(connection.first);
                graphEngine->disconnect(connection.first, connection.second);
            }
        }
    }

    if (previousSources.empty())
    {
        if (! inputId.isNull())
            graphEngine->connect(inputId, newNodeId);
    }
    else
    {
        for (const auto& source : previousSources)
        {
            if (source != newNodeId)
                graphEngine->connect(source, newNodeId);
        }
    }

    if (! outputId.isNull())
        graphEngine->connect(newNodeId, outputId);

    bool preparedOK = false;
    try
    {
        graphEngine->prepare();
        preparedOK = true;
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.graphPrepare.title"),
                                               host::i18n::tr("error.graphPrepare.message").replace("%1", juce::String(e.what())));
    }

    if (! preparedOK)
    {
        if (! outputId.isNull())
            graphEngine->disconnect(newNodeId, outputId);

        if (previousSources.empty())
        {
            if (! inputId.isNull())
                graphEngine->disconnect(inputId, newNodeId);
        }
        else
        {
            for (const auto& source : previousSources)
                graphEngine->disconnect(source, newNodeId);
        }

        if (! outputId.isNull())
        {
            for (const auto& source : previousSources)
                graphEngine->connect(source, outputId);

            if (previousSources.empty() && ! inputId.isNull())
                graphEngine->connect(inputId, outputId);
        }

        try
        {
            graphEngine->prepare();
        }
        catch (const std::exception&)
        {
            // If preparation still fails, let the user fix connections manually.
        }
    }

    graphView.refreshGraph(true);
    if (! newNodeId.isNull())
        graphView.focusOnNode(newNodeId);
}

void MainWindow::loadProject()
{
    juce::FileChooser chooser(host::i18n::tr("fileChooser.openProject"),
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.json");
    if (! chooser.browseForFileToOpen())
        return;

    auto file = chooser.getResult();
    if (! loadProjectFromFile(file))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("error.loadProject.title"),
                                               host::i18n::tr("error.loadProject.message"));
    }
}

void MainWindow::saveProject()
{
    juce::FileChooser chooser(host::i18n::tr("fileChooser.saveProject"),
                              juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                              "*.json");
    if (! chooser.browseForFileToSave(true))
        return;

    host::persist::Project project;
    project.save(chooser.getResult(), *graphEngine);
}
