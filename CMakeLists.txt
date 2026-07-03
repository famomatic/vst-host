cmake_minimum_required(VERSION 3.22)

project(VSTHostScaffold VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /EHsc /utf-8 /FS)
    add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/DNDEBUG>)
endif()

set(JUCE_SUBMODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/externals/JUCE)

include_directories(
    ${CMAKE_SOURCE_DIR}/externals/JUCE/modules
    ${CMAKE_SOURCE_DIR}/externals/vst2headers
)

if(NOT EXISTS ${JUCE_SUBMODULE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "JUCE submodule not found. Please initialise submodules under externals/JUCE.")
endif()

add_subdirectory(externals/JUCE)
add_subdirectory(src)
