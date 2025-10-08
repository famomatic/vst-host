cmake_minimum_required(VERSION 3.22)

project(VSTHostScaffold VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /EHsc /O2 /DNDEBUG /utf-8)
endif()

add_definitions(-DVST3_SDK -DENABLE_VST2)

set(JUCE_SUBMODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/externals/JUCE)
set(VST3_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/externals/VST3_SDK)
set(VST2_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/externals/vst2sdk)

if (EXISTS "${CMAKE_SOURCE_DIR}/externals/vst2sdk/include/vst.h")
    message(STATUS "Found Xaymar VST2 SDK at ${CMAKE_SOURCE_DIR}/externals/vst2sdk")
    add_definitions(-DENABLE_VST2)
    include_directories("${CMAKE_SOURCE_DIR}/externals/vst2sdk/include")
else()
    message(FATAL_ERROR "Xaymar VST2 SDK not found. Expected ${CMAKE_SOURCE_DIR}/externals/vst2sdk/include/vst.h")
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/externals/VST3_SDK
    ${CMAKE_SOURCE_DIR}/externals/vst2sdk/include
)

if(NOT EXISTS ${JUCE_SUBMODULE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "JUCE submodule not found. Please initialise submodules under externals/JUCE.")
endif()

add_subdirectory(externals/JUCE)
add_subdirectory(src)
