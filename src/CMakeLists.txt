set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES
    ${SRC_DIR}/AppMain.cpp
    ${SRC_DIR}/audio/DeviceEngine.cpp
    ${SRC_DIR}/audio/Resampler.cpp
    ${SRC_DIR}/host/PluginHost.cpp
    ${SRC_DIR}/host/PluginScanner.cpp
    ${SRC_DIR}/graph/GraphEngine.cpp
    ${SRC_DIR}/graph/Nodes/VstFx.cpp
    ${SRC_DIR}/graph/Nodes/GainNode.cpp
    ${SRC_DIR}/gui/ConsoleView.cpp
    ${SRC_DIR}/gui/ConsoleWindow.cpp
    ${SRC_DIR}/gui/MainWindow.cpp
    ${SRC_DIR}/gui/GraphView.cpp
    ${SRC_DIR}/gui/Preferences.cpp
    ${SRC_DIR}/gui/PluginBrowser.cpp
    ${SRC_DIR}/gui/PluginSettingsComponent.cpp
    ${SRC_DIR}/util/ConsoleLogger.cpp
    ${SRC_DIR}/util/Localization.cpp
    ${SRC_DIR}/persist/Config.cpp
    ${SRC_DIR}/persist/Project.cpp
    ${SRC_DIR}/persist/Preset.cpp
)

juce_add_gui_app(VSTHostApp
    COMPANY_NAME "Waktaverse"
    PRODUCT_NAME "VST Host"
    VERSION "0.1.0"
)

# 2) 소스 연결
target_sources(VSTHostApp PRIVATE ${SOURCES})

# 3) JUCE가 생성 헤더/빌드 아티팩트를 세팅하도록 호출
juce_generate_juce_header(VSTHostApp)

# 4) 인클루드 경로
target_include_directories(VSTHostApp
    PRIVATE
        ${SRC_DIR}
        ${VST3_SDK_DIR}
        ${VST2_SDK_DIR}/include
)

# 5) 필요한 JUCE 모듈 링크
target_link_libraries(VSTHostApp
    PRIVATE
        juce::juce_core
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp

        sdk_hosting
        sdk
        base
        pluginterfaces
)

# 6) 빌드 옵션
target_compile_definitions(VSTHostApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

if (MSVC)
    target_compile_options(VSTHostApp PRIVATE /permissive- /Zc:__cplusplus /EHsc /O2 /DNDEBUG)
endif()
