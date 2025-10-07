set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES
    ${SRC_DIR}/AppMain.cpp
    ${SRC_DIR}/audio/DeviceEngine.cpp
    ${SRC_DIR}/audio/Resampler.cpp
    ${SRC_DIR}/host/PluginHost.cpp
    ${SRC_DIR}/host/PluginScanner.cpp
    ${SRC_DIR}/graph/GraphEngine.cpp
    ${SRC_DIR}/graph/Nodes/AudioIn.h
    ${SRC_DIR}/graph/Nodes/AudioOut.h
    ${SRC_DIR}/graph/Nodes/VstFx.h
    ${SRC_DIR}/graph/Nodes/Gain.h
    ${SRC_DIR}/graph/Nodes/Mix.h
    ${SRC_DIR}/graph/Nodes/Split.h
    ${SRC_DIR}/graph/Nodes/Merge.h
    ${SRC_DIR}/gui/MainWindow.cpp
    ${SRC_DIR}/gui/GraphView.cpp
    ${SRC_DIR}/gui/Preferences.cpp
    ${SRC_DIR}/gui/PluginBrowser.cpp
    ${SRC_DIR}/persist/Config.cpp
    ${SRC_DIR}/persist/Project.cpp
    ${SRC_DIR}/persist/Preset.cpp
)

add_executable(VSTHostApp ${SOURCES})

if(ENABLE_VST2)
    target_compile_definitions(VSTHostApp PRIVATE ENABLE_VST2)
    target_include_directories(VSTHostApp PRIVATE ${VST2_SDK_DIR}/include)
endif()

target_include_directories(VSTHostApp
    PRIVATE
        ${SRC_DIR}
        ${VST3_SDK_DIR}
)

target_link_libraries(VSTHostApp
    PRIVATE
        juce::juce_audio_devices
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
)

juce_generate_juce_header(VSTHostApp)

if(MSVC)
    target_compile_options(VSTHostApp PRIVATE /permissive- /Zc:__cplusplus /EHsc /O2 /DNDEBUG)
endif()
