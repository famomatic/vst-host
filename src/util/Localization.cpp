#include "util/Localization.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace host::i18n
{
namespace
{
    juce::StringPairArray makeEnglishStrings()
    {
        juce::StringPairArray strings;
        strings.set("app.title", "VST Host Scaffold");

        strings.set("menu.file", "File");
        strings.set("menu.edit", "Edit");
        strings.set("menu.view", "View");
        strings.set("menu.view.console", "Show Console");
        strings.set("menu.help", "Help");

        strings.set("menu.file.open", "Open Project...");
        strings.set("menu.file.save", "Save Project");
        strings.set("menu.file.newEmpty", "Open Empty Browser");
        strings.set("menu.file.audioSettings", "Audio Device Setup...");
        strings.set("menu.file.preferences", "Preferences...");
        strings.set("menu.file.exit", "Exit");

        strings.set("menu.edit.rescan", "Rescan Plugins");
        strings.set("menu.help.show", "View Help");

        strings.set("tray.show", "Show");
        strings.set("tray.hide", "Hide");
        strings.set("tray.settings", "Settings...");
        strings.set("tray.exit", "Exit");

        strings.set("console.title", "Console");

        strings.set("graph.io", "In %1 / Out %2");
        strings.set("graph.empty", "Graph is empty");
        strings.set("graph.menu.focus", "Focus Selected Node");
        strings.set("graph.menu.resetView", "Reset View");
        strings.set("graph.menu.clearSelection", "Clear Selection");
        strings.set("graph.context.openPluginSettings", "Open plugin settings");
        strings.set("graph.context.clearOutgoing", "Clear outgoing connections");
        strings.set("graph.context.clearIncoming", "Clear incoming connections");
        strings.set("graph.context.resetPosition", "Reset position");
        strings.set("graph.context.delete", "Delete node");
        strings.set("graph.error.connect.title", "Connect Nodes");
        strings.set("graph.error.connect.body", "Failed to connect nodes:\n%1");
        strings.set("graph.error.delete.title", "Delete Node");
        strings.set("graph.error.delete.cannot", "Input and output nodes cannot be removed.");
        strings.set("graph.error.delete.failed", "Failed to delete node:\n%1");
        strings.set("graph.node.default", "Node");

        strings.set("browser.searchPlaceholder", "Search plugins");

        strings.set("preferences.tab.audio", "Audio");
        strings.set("preferences.tab.plugins", "Plugins");
        strings.set("preferences.tab.startup", "Startup");
        strings.set("preferences.audio.driver", "Driver");
        strings.set("preferences.audio.input", "Input Device");
        strings.set("preferences.audio.output", "Output Device");
        strings.set("preferences.audio.sampleRate", "Sample Rate");
        strings.set("preferences.audio.blockSize", "Block Size");
        strings.set("preferences.plugins.add", "Add");
        strings.set("preferences.plugins.remove", "Remove");
        strings.set("preferences.plugins.rescan", "Rescan");
        strings.set("preferences.startup.defaultPreset", "Default preset");
        strings.set("preferences.startup.language", "Language");
        strings.set("preferences.startup.browse", "Browse");
        strings.set("preferences.startup.clear", "Clear");
        strings.set("preferences.startup.noPreset", "(Not set)");
        strings.set("preferences.startup.missingPreset", "%1 (missing)");

        strings.set("fileChooser.pluginDirectory", "Select plugin directory");
        strings.set("fileChooser.defaultPreset", "Select default preset");
        strings.set("fileChooser.openProject", "Open project");
        strings.set("fileChooser.saveProject", "Save project");

        strings.set("dialog.preferences.title", "Preferences");
        strings.set("dialog.audioSettings.title", "Audio Device Settings");

        strings.set("error.loadPreset.title", "Load Preset");
        strings.set("error.loadPreset.message", "Failed to load default preset:\n%1");
        strings.set("error.missingPlugins.title", "Missing Plugins");
        strings.set("error.missingPlugins.message", "Some plugins could not be loaded:\n%1");
        strings.set("error.loadPlugin.title", "Load Plugin");
        strings.set("error.loadPlugin.failed", "Failed to load plugin:\n%1");
        strings.set("error.loadPlugin.instantiate", "Could not instantiate the selected plugin.");
        strings.set("error.graphUpdate.title", "Graph Update");
        strings.set("error.graphUpdate.message", "Failed to add plugin node:\n%1");
        strings.set("error.graphPrepare.title", "Graph Prepare");
        strings.set("error.graphPrepare.message", "The graph could not be prepared:\n%1");
        strings.set("error.loadProject.title", "Load Failed");
        strings.set("error.loadProject.message", "Unable to load the selected project file.");

        strings.set("plugin.settings.title", "Plugin Settings");
        strings.set("plugin.settings.name", "Display name");
        strings.set("plugin.settings.status", "Status");
        strings.set("plugin.settings.status.loaded", "Loaded");
        strings.set("plugin.settings.status.missing", "Not loaded");
        strings.set("plugin.settings.format", "Format");
        strings.set("plugin.settings.path", "Plugin path");
        strings.set("plugin.settings.inputs", "Input channels");
        strings.set("plugin.settings.outputs", "Output channels");
        strings.set("plugin.settings.latency", "Reported latency");
        strings.set("plugin.settings.bypass", "Bypass processing");
        strings.set("plugin.settings.openEditor", "Open plug-in editor");
        strings.set("plugin.settings.editorUnavailable.title", "Editor unavailable");
        strings.set("plugin.settings.editorUnavailable.message", "This plug-in does not expose a native editor.");
        strings.set("plugin.settings.notAvailable", "Not available");
        strings.set("plugin.settings.unavailable", "Plugin unavailable");
        strings.set("plugin.settings.samplesLabel", "samples");
        strings.set("plugin.format.vst2", "VST2");
        strings.set("plugin.format.vst3", "VST3");

        strings.set("help.title", "Help");
        strings.set("help.content",
                    "• Use the plugin browser to double-click a plugin to add it.\n"
                    "• Drag nodes to arrange them and use Delete to remove selected plugins.\n"
                    "• Right-click a plugin node or press Enter to open its settings.\n"
                    "• Close the window to minimise to the tray; right-click the tray icon for settings or exit.");

        strings.set("preferences.language.english", "English");
        strings.set("preferences.language.korean", "한국어");

        return strings;
    }

    juce::StringPairArray makeKoreanStrings()
    {
        juce::StringPairArray strings;
        strings.set("app.title", juce::String::fromUTF8("VST 호스트"));

        strings.set("menu.file", juce::String::fromUTF8("파일"));
        strings.set("menu.edit", juce::String::fromUTF8("편집"));
        strings.set("menu.view", juce::String::fromUTF8("보기"));
        strings.set("menu.view.console", juce::String::fromUTF8("콘솔 보기"));
        strings.set("menu.help", juce::String::fromUTF8("도움말"));

        strings.set("menu.file.open", juce::String::fromUTF8("프로젝트 열기..."));
        strings.set("menu.file.save", juce::String::fromUTF8("프로젝트 저장"));
        strings.set("menu.file.newEmpty", juce::String::fromUTF8("빈 브라우저 열기"));
        strings.set("menu.file.audioSettings", juce::String::fromUTF8("오디오 장치 설정..."));
        strings.set("menu.file.preferences", juce::String::fromUTF8("환경설정..."));
        strings.set("menu.file.exit", juce::String::fromUTF8("종료"));

        strings.set("menu.edit.rescan", juce::String::fromUTF8("플러그인 다시 검색"));
        strings.set("menu.help.show", juce::String::fromUTF8("도움말 보기"));

        strings.set("tray.show", juce::String::fromUTF8("창 열기"));
        strings.set("tray.hide", juce::String::fromUTF8("창 숨기기"));
        strings.set("tray.settings", juce::String::fromUTF8("설정..."));
        strings.set("tray.exit", juce::String::fromUTF8("종료"));

        strings.set("console.title", juce::String::fromUTF8("콘솔"));

        strings.set("graph.io", juce::String::fromUTF8("입력 %1 / 출력 %2"));
        strings.set("graph.empty", juce::String::fromUTF8("그래프가 비어 있습니다"));
        strings.set("graph.menu.focus", juce::String::fromUTF8("선택 노드로 이동"));
        strings.set("graph.menu.resetView", juce::String::fromUTF8("보기 초기화"));
        strings.set("graph.menu.clearSelection", juce::String::fromUTF8("선택 해제"));
        strings.set("graph.context.openPluginSettings", juce::String::fromUTF8("플러그인 설정 열기"));
        strings.set("graph.context.clearOutgoing", juce::String::fromUTF8("출력 연결 지우기"));
        strings.set("graph.context.clearIncoming", juce::String::fromUTF8("입력 연결 지우기"));
        strings.set("graph.context.resetPosition", juce::String::fromUTF8("위치 초기화"));
        strings.set("graph.context.delete", juce::String::fromUTF8("노드 삭제"));
        strings.set("graph.error.connect.title", juce::String::fromUTF8("노드 연결"));
        strings.set("graph.error.connect.body", juce::String::fromUTF8("노드를 연결하지 못했습니다:\n%1"));
        strings.set("graph.error.delete.title", juce::String::fromUTF8("노드 삭제"));
        strings.set("graph.error.delete.cannot", juce::String::fromUTF8("입출력 노드는 삭제할 수 없습니다."));
        strings.set("graph.error.delete.failed", juce::String::fromUTF8("노드를 삭제하지 못했습니다:\n%1"));
        strings.set("graph.node.default", juce::String::fromUTF8("노드"));

        strings.set("browser.searchPlaceholder", juce::String::fromUTF8("플러그인 검색"));

        strings.set("preferences.tab.audio", juce::String::fromUTF8("오디오"));
        strings.set("preferences.tab.plugins", juce::String::fromUTF8("플러그인"));
        strings.set("preferences.tab.startup", juce::String::fromUTF8("시작"));
        strings.set("preferences.audio.driver", juce::String::fromUTF8("드라이버"));
        strings.set("preferences.audio.input", juce::String::fromUTF8("입력 장치"));
        strings.set("preferences.audio.output", juce::String::fromUTF8("출력 장치"));
        strings.set("preferences.audio.sampleRate", juce::String::fromUTF8("샘플 레이트"));
        strings.set("preferences.audio.blockSize", juce::String::fromUTF8("블록 크기"));
        strings.set("preferences.plugins.add", juce::String::fromUTF8("추가"));
        strings.set("preferences.plugins.remove", juce::String::fromUTF8("삭제"));
        strings.set("preferences.plugins.rescan", juce::String::fromUTF8("다시 검색"));
        strings.set("preferences.startup.defaultPreset", juce::String::fromUTF8("기본 프리셋"));
        strings.set("preferences.startup.language", juce::String::fromUTF8("언어"));
        strings.set("preferences.startup.browse", juce::String::fromUTF8("찾아보기"));
        strings.set("preferences.startup.clear", juce::String::fromUTF8("해제"));
        strings.set("preferences.startup.noPreset", juce::String::fromUTF8("(설정되지 않음)"));
        strings.set("preferences.startup.missingPreset", juce::String::fromUTF8("%1 (없음)"));

        strings.set("fileChooser.pluginDirectory", juce::String::fromUTF8("플러그인 폴더 선택"));
        strings.set("fileChooser.defaultPreset", juce::String::fromUTF8("기본 프리셋 선택"));
        strings.set("fileChooser.openProject", juce::String::fromUTF8("프로젝트 열기"));
        strings.set("fileChooser.saveProject", juce::String::fromUTF8("프로젝트 저장"));

        strings.set("dialog.preferences.title", juce::String::fromUTF8("환경설정"));
        strings.set("dialog.audioSettings.title", juce::String::fromUTF8("오디오 장치 설정"));

        strings.set("error.loadPreset.title", juce::String::fromUTF8("프리셋 불러오기"));
        strings.set("error.loadPreset.message", juce::String::fromUTF8("기본 프리셋을 불러오지 못했습니다:\n%1"));
        strings.set("error.missingPlugins.title", juce::String::fromUTF8("플러그인 누락"));
        strings.set("error.missingPlugins.message", juce::String::fromUTF8("일부 플러그인을 불러오지 못했습니다:\n%1"));
        strings.set("error.loadPlugin.title", juce::String::fromUTF8("플러그인 불러오기"));
        strings.set("error.loadPlugin.failed", juce::String::fromUTF8("플러그인을 불러오지 못했습니다:\n%1"));
        strings.set("error.loadPlugin.instantiate", juce::String::fromUTF8("선택한 플러그인을 인스턴스화할 수 없습니다."));
        strings.set("error.graphUpdate.title", juce::String::fromUTF8("그래프 업데이트"));
        strings.set("error.graphUpdate.message", juce::String::fromUTF8("플러그인 노드를 추가하지 못했습니다:\n%1"));
        strings.set("error.graphPrepare.title", juce::String::fromUTF8("그래프 준비"));
        strings.set("error.graphPrepare.message", juce::String::fromUTF8("그래프를 준비하지 못했습니다:\n%1"));
        strings.set("error.loadProject.title", juce::String::fromUTF8("로드 실패"));
        strings.set("error.loadProject.message", juce::String::fromUTF8("선택한 프로젝트 파일을 불러올 수 없습니다."));

        strings.set("plugin.settings.title", juce::String::fromUTF8("플러그인 설정"));
        strings.set("plugin.settings.name", juce::String::fromUTF8("표시 이름"));
        strings.set("plugin.settings.status", juce::String::fromUTF8("상태"));
        strings.set("plugin.settings.status.loaded", juce::String::fromUTF8("로드됨"));
        strings.set("plugin.settings.status.missing", juce::String::fromUTF8("로드되지 않음"));
        strings.set("plugin.settings.format", juce::String::fromUTF8("형식"));
        strings.set("plugin.settings.path", juce::String::fromUTF8("플러그인 경로"));
        strings.set("plugin.settings.inputs", juce::String::fromUTF8("입력 채널"));
        strings.set("plugin.settings.outputs", juce::String::fromUTF8("출력 채널"));
        strings.set("plugin.settings.latency", juce::String::fromUTF8("보고된 레이턴시"));
        strings.set("plugin.settings.bypass", juce::String::fromUTF8("이 플러그인 우회"));
        strings.set("plugin.settings.openEditor", juce::String::fromUTF8("플러그인 편집창 열기"));
        strings.set("plugin.settings.editorUnavailable.title", juce::String::fromUTF8("편집창을 열 수 없습니다"));
        strings.set("plugin.settings.editorUnavailable.message", juce::String::fromUTF8("이 플러그인은 고유 편집창을 제공하지 않습니다."));
        strings.set("plugin.settings.notAvailable", juce::String::fromUTF8("정보 없음"));
        strings.set("plugin.settings.unavailable", juce::String::fromUTF8("플러그인을 사용할 수 없습니다"));
        strings.set("plugin.settings.samplesLabel", juce::String::fromUTF8("샘플"));
        strings.set("plugin.format.vst2", juce::String::fromUTF8("VST2"));
        strings.set("plugin.format.vst3", juce::String::fromUTF8("VST3"));

        strings.set("help.title", juce::String::fromUTF8("도움말"));
        strings.set("help.content",
                    juce::String::fromUTF8("• 플러그인 브라우저에서 플러그인을 더블 클릭하면 그래프에 추가됩니다.\n"
                                            "• 노드를 드래그하여 배치하고 Delete 키로 선택한 플러그인을 삭제할 수 있습니다.\n"
                                            "• 플러그인 노드를 우클릭하거나 Enter 키를 눌러 설정 창을 열 수 있습니다.\n"
                                            "• 창을 닫으면 프로그램이 트레이로 이동하며, 트레이 아이콘을 우클릭하면 설정과 종료를 선택할 수 있습니다."));

        strings.set("preferences.language.english", juce::String::fromUTF8("영어"));
        strings.set("preferences.language.korean", juce::String::fromUTF8("한국어"));

        return strings;
    }
}

LocalizationManager& LocalizationManager::getInstance()
{
    static LocalizationManager instance;
    return instance;
}

LocalizationManager::LocalizationManager()
{
    registerLanguage("en", "English", makeEnglishStrings());
    registerLanguage("ko", juce::String::fromUTF8("한국어"), makeKoreanStrings());
}

void LocalizationManager::registerLanguage(const juce::String& code,
                                           const juce::String& displayName,
                                           const juce::StringPairArray& strings)
{
    if (code.isEmpty())
        return;

    const auto key = code.toLowerCase().toStdString();
    auto& table = tables[key];
    for (const auto& k : strings.getAllKeys())
        table.set(k, strings.getValue(k, {}));

    if (std::find(orderedCodes.begin(), orderedCodes.end(), key) == orderedCodes.end())
        orderedCodes.push_back(key);

    names[key] = displayName;
}

bool LocalizationManager::loadOverridesFromFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    juce::var parsed;
    const auto text = file.loadFileAsString();
    const auto parseResult = juce::JSON::parse(text, parsed);
    if (parseResult.failed())
        return false;
    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return false;

    juce::String code = object->getProperty("code").toString();
    if (code.isEmpty())
        code = file.getFileNameWithoutExtension();
    if (code.isEmpty())
        return false;

    juce::String name = object->getProperty("name").toString();

    juce::StringPairArray entries;
    const auto stringsVar = object->getProperty("strings");
    if (auto* stringsObject = stringsVar.getDynamicObject())
    {
        for (const auto& property : stringsObject->getProperties())
            entries.set(property.name.toString(), property.value.toString());
    }
    else if (auto* array = stringsVar.getArray())
    {
        for (const auto& element : *array)
        {
            if (auto* entry = element.getDynamicObject())
            {
                const auto key = entry->getProperty("key").toString();
                const auto value = entry->getProperty("value").toString();
                if (key.isNotEmpty())
                    entries.set(key, value);
            }
        }
    }

    if (entries.size() == 0)
        return false;

    if (name.isEmpty())
    {
        const auto lookup = code.toLowerCase().toStdString();
        if (auto it = names.find(lookup); it != names.end())
            name = it->second;
    }

    if (name.isEmpty())
        name = code;

    registerLanguage(code, name, entries);
    return true;
}

bool LocalizationManager::setLanguage(const juce::String& code)
{
    auto lookup = code.toLowerCase().toStdString();
    if (tables.find(lookup) == tables.end())
        lookup = std::string("en");

    if (currentCode == lookup)
        return false;

    currentCode = lookup;
    sendChangeMessage();
    return true;
}

const juce::StringPairArray& LocalizationManager::getTable(const std::string& code) const
{
    if (auto it = tables.find(code); it != tables.end())
        return it->second;

    if (auto english = tables.find("en"); english != tables.end())
        return english->second;

    return emptyTable;
}

juce::String LocalizationManager::translate(const juce::String& key) const
{
    if (key.isEmpty())
        return {};

    const auto& currentTable = getTable(currentCode);
    const auto value = currentTable.getValue(key, juce::String());
    if (value.isNotEmpty())
        return value;

    if (currentCode != "en")
    {
        const auto& english = getTable("en");
        const auto fallback = english.getValue(key, juce::String());
        if (fallback.isNotEmpty())
            return fallback;
    }

    return key;
}

std::vector<std::pair<juce::String, juce::String>> LocalizationManager::getAvailableLanguages() const
{
    std::vector<std::pair<juce::String, juce::String>> list;
    list.reserve(orderedCodes.size());
    for (const auto& code : orderedCodes)
    {
        const auto nameIt = names.find(code);
        if (nameIt != names.end())
            list.emplace_back(juce::String(code), nameIt->second);
    }
    return list;
}

juce::String tr(const juce::String& key)
{
    return LocalizationManager::getInstance().translate(key);
}
}
