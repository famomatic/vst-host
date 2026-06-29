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
#include "graph/Node.h"
#include "gui/ConsoleWindow.h"
#include "gui/PluginSettingsComponent.h"
#include "persist/Project.h"
#include "util/Localization.h"

    namespace
    {
    // DocumentWindow that reports close via a callback, since closeButtonPressed
    // is a virtual override rather than a public member.
    class CloseableDocumentWindow : public juce::DocumentWindow
    {
    public:
        using CloseCallback = std::function<void()>;
        CloseableDocumentWindow(const juce::String& name, juce::Colour bg, int buttons, CloseCallback cb)
            : juce::DocumentWindow(name, bg, buttons), onClose(std::move(cb)) {}
        void closeButtonPressed() override { if (onClose) onClose(); }
    private:
        CloseCallback onClose;
    };
    }

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
        menuViewConsole,
        menuViewChainPresets
    };

    constexpr int kPluginEditorMinWidth = 50;
    constexpr int kPluginEditorMinHeight = 50;
    constexpr int kPluginEditorMaxWidth = 4096;
    constexpr int kPluginEditorMaxHeight = 3072;
    constexpr int kPluginSettingsMinWidth = 480;
    constexpr int kPluginSettingsMinHeight = 460;
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

        // For non-resizable editors the window is fixed to the editor's
        // preferred size. For resizable ones, clamp the user-resize range to
        // the editor minimum/maximum so the content never gets clipped.
        int minContentWidth = editorResizable ? kPluginEditorMinWidth : preferredContentWidth;
        int minContentHeight = editorResizable ? kPluginEditorMinHeight : preferredContentHeight;
        int maxContentWidth = editorResizable ? kPluginEditorMaxWidth : preferredContentWidth;
        int maxContentHeight = editorResizable ? kPluginEditorMaxHeight : preferredContentHeight;

        if (auto* audioProcessorEditor = dynamic_cast<juce::AudioProcessorEditor*>(&editorComponent))
        {
            if (auto* constrainer = audioProcessorEditor->getConstrainer())
            {
                const int constrainerMinWidth = constrainer->getMinimumWidth();
                const int constrainerMinHeight = constrainer->getMinimumHeight();

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

        // Keep the preferred size inside the resolved limits so the dialog
        // opens at the editor's requested size instead of collapsing to the
        // minimum, which previously caused the 'opens at minimum size' bug.
        preferredContentWidth = juce::jlimit(minContentWidth, maxContentWidth, preferredContentWidth);
        preferredContentHeight = juce::jlimit(minContentHeight, maxContentHeight, preferredContentHeight);

        const int resolvedMaxContentWidth = juce::jmax(minContentWidth, maxContentWidth);
        const int resolvedMaxContentHeight = juce::jmax(minContentHeight, maxContentHeight);

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

    const auto deviceInitError = deviceManager.initialise(2, 2, nullptr, true);
    if (deviceInitError.isNotEmpty())
    {
        juce::Logger::writeToLog("Audio device initialization failed: " + deviceInitError);
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               host::i18n::tr("dialog.audioSettings.title"),
                                               "Audio device initialization failed.\n" + deviceInitError);
    }

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
    graphView.setOnRequestAddNode([this](const std::string& typeId)
    {
        addBuiltinNodeToGraph(typeId);
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
       const bool chainVisible = chainPresetWindow != nullptr && chainPresetWindow->isVisible();
       menu.addItem(menuViewChainPresets, host::i18n::tr("menu.view.chainPresets"), true, chainVisible);
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
       case menuViewChainPresets: toggleChainPresetPanel(); break;
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

    host::audio::EngineConfig engineCfg;
    engineCfg.sampleRate = settings.sampleRate;
    engineCfg.blockSize = settings.blockSize;
    engineCfg.resamplerQuality = settings.resamplerQuality;
    engineCfg.pdcEnabled = settings.pdcEnabled;
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

void MainWindow::toggleChainPresetPanel()
{
    if (chainPresetWindow != nullptr && chainPresetWindow->isVisible())
    {
        chainPresetWindow->setVisible(false);
        menuItemsChanged();
        return;
    }

    if (chainPresetWindow == nullptr)
    {
        const auto presetDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                    .getChildFile("VST Host")
                                    .getChildFile("Chain Presets");

        chainPresetPanel = std::make_unique<host::gui::ChainPresetPanel>(graphEngine, presetDir, [this]()
        {
            updateChainPresetPanel();
        });

        chainPresetWindow = std::make_unique<CloseableDocumentWindow>(host::i18n::tr("chainPreset.windowTitle"),
                                                                      juce::Colours::darkgrey,
                                                                      juce::DocumentWindow::closeButton,
                                                                      [this]()
                                                                      {
                                                                          if (chainPresetWindow)
                                                                              chainPresetWindow->setVisible(false);
                                                                          menuItemsChanged();
                                                                      });
        chainPresetWindow->setUsingNativeTitleBar(true);
        chainPresetWindow->setContentOwned(chainPresetPanel.release(), true);
        chainPresetWindow->setResizable(true, false);
        chainPresetWindow->centreWithSize(320, 420);
    }

    if (! chainPresetWindow->isVisible())
        chainPresetWindow->setVisible(true);

    updateChainPresetPanel();
    chainPresetWindow->toFront(true);
    menuItemsChanged();
}

void MainWindow::updateChainPresetPanel()
{
    if (chainPresetWindow != nullptr)
    {
        if (auto* content = dynamic_cast<host::gui::ChainPresetPanel*>(chainPresetWindow->getContentComponent()))
            content->refresh();
    }
}

void MainWindow::openPluginSettings(host::graph::GraphEngine::NodeId id)
{
    if (id.isNull() || ! graphEngine)
        return;

   auto node = graphEngine->getNode(id);
   auto* vstNode = dynamic_cast<host::graph::nodes::VstFxNode*>(node.get());
   if (vstNode == nullptr)
   {
       // Built-in effect node: show a generic parameter panel built from
       // the node's exposed parameters. This lets the user tweak EQ bands,
       // compressor thresholds, reverb mix, etc. without a full editor.
       if (! node)
           return;

       auto params = node->getParameters();
       if (params.empty())
           return;

       struct EffectParameterPanel : public juce::Component
       {
           EffectParameterPanel(std::shared_ptr<host::graph::Node> nodeIn,
                                std::vector<host::graph::NodeParameter> paramsIn)
               : node(std::move(nodeIn)), params(std::move(paramsIn))
           {
               const int rowHeight = 32;
               const int labelWidth = 140;
               const int padding = 12;

               int y = padding;
               sliders.reserve(params.size());
               labels.reserve(params.size());
               for (auto& p : params)
               {
                   auto* label = new juce::Label({}, p.displayName);
                   label->setJustificationType(juce::Justification::centredLeft);
                   label->setColour(juce::Label::textColourId, juce::Colours::whitesmoke);
                   addAndMakeVisible(label);
                   label->setBounds(padding, y, labelWidth, rowHeight);

                   auto* slider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                   slider->setRange(p.min, p.max, 0.01);
                   slider->setValue(p.value, juce::dontSendNotification);
                   slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, rowHeight - 4);
                   addAndMakeVisible(slider);
                   slider->setBounds(padding + labelWidth + 4, y, 320, rowHeight);

                   slider->onValueChange = [this, idx = sliders.size()]() mutable
                   {
                      if (idx < params.size() && sliders[idx])
                      {
                          params[idx].value = sliders[idx]->getValue();
                          // Live UI edits go through the lock-free queue so the
                          // audio thread applies them at the next block boundary
                          // rather than racing on the atomics mid-block.
                          if (node)
                              node->requestParameterChange(params[idx].id, params[idx].value);
                      }
                   };

                   labels.emplace_back(label);
                   sliders.emplace_back(slider);
                   y += rowHeight + 4;
               }

               setSize(padding * 2 + labelWidth + 4 + 320 + 80, y + padding);
           }

           void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::darkgrey.darker(0.4f)); }

           std::shared_ptr<host::graph::Node> node;
           std::vector<host::graph::NodeParameter> params;
           std::vector<std::unique_ptr<juce::Label>> labels;
           std::vector<std::unique_ptr<juce::Slider>> sliders;
       };

       auto panel = std::make_unique<EffectParameterPanel>(node, std::move(params));

       juce::DialogWindow::LaunchOptions options;
       options.content.setOwned(panel.release());
       options.dialogTitle = juce::String(node->name());
       options.componentToCentreAround = this;
       options.useNativeTitleBar = true;
       options.escapeKeyTriggersCloseButton = true;
       options.resizable = false;
       options.dialogBackgroundColour = juce::Colours::darkgrey.darker(0.6f);
       options.launchAsync();
       return;
   }

    if (auto* plugin = vstNode->plugin())
    {
        if (plugin->hasEditor())
        {
            if (auto editor = plugin->createEditorComponent())
            {
                auto* editorComponent = editor.get();
                auto* audioProcessorEditor = dynamic_cast<juce::AudioProcessorEditor*>(editorComponent);
                const bool editorResizable = audioProcessorEditor != nullptr
                                              ? audioProcessorEditor->isResizable()
                                              : plugin->isEditorResizable();

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
    // The component sizes itself in its constructor; respect that and only
    // clamp to a sane minimum so tiny content does not collapse the dialog.
    const int contentWidth = juce::jmax(settingsComponentPtr->getWidth(), kPluginSettingsMinWidth);
    const int contentHeight = juce::jmax(settingsComponentPtr->getHeight(), kPluginSettingsMinHeight);
    if (settingsComponentPtr->getWidth() != contentWidth || settingsComponentPtr->getHeight() != contentHeight)
        settingsComponentPtr->setSize(contentWidth, contentHeight);

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
        // Force the dialog window to wrap the content component exactly,
        // otherwise launchAsync may open at a stale/default size and clip it.
        if (auto* content = dialog->getContentComponent())
        {
            const auto contentBorder = dialog->getContentComponentBorder();
            const auto windowBorder = dialog->getBorderThickness();
            const int targetWidth = content->getWidth() + contentBorder.getLeftAndRight() + windowBorder.getLeftAndRight();
            const int targetHeight = content->getHeight() + contentBorder.getTopAndBottom() + windowBorder.getTopAndBottom();
            const auto bounds = dialog->getBounds();
            dialog->setBounds(bounds.withSizeKeepingCentre(targetWidth, targetHeight));
        }
        dialog->setResizeLimits(kPluginSettingsMinWidth, kPluginSettingsMinHeight,
                                kPluginSettingsMaxWidth, kPluginSettingsMaxHeight);
    }
}

void MainWindow::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &host::i18n::manager())
        refreshTranslations();
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
