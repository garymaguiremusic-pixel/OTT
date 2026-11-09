cmake_minimum_required(VERSION 3.22)

project(OTT VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(OTT_BUILD_STANDALONE "Also build the standalone app"                              ON)
option(OTT_BUILD_TESTS      "Build the OTT_Tests unit test app"                          ON)
option(OTT_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

# ------------------------------------------------------------------------------
# JUCE
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin: VST3 and Standalone (plus AU on macOS)
set(OTT_FORMATS VST3)
if(APPLE)
    list(APPEND OTT_FORMATS AU)
endif()
if(OTT_BUILD_STANDALONE)
    list(APPEND OTT_FORMATS Standalone)
endif()

# The VST3 manifest helper has to run on the build machine, which isn't possible
# when cross-compiling.
if(CMAKE_CROSSCOMPILING)
    set(OTT_VST3_MANIFEST FALSE)
else()
    set(OTT_VST3_MANIFEST TRUE)
endif()

juce_add_plugin(OTT
    PRODUCT_NAME                "OTT Multiband"
    COMPANY_NAME                "Gary Maguire"
    BUNDLE_ID                   com.garymaguire.ottmultiband
    PLUGIN_MANUFACTURER_CODE    Gmgr
    PLUGIN_CODE                 Ott1
    FORMATS                     ${OTT_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    VST3_AUTO_MANIFEST          ${OTT_VST3_MANIFEST}
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${OTT_COPY_AFTER_BUILD})

set(OTT_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CustomLookAndFeel.cpp
    Source/UI/OttComponents.cpp
    Source/DSP/LinearPhaseCrossover.cpp)

target_sources(OTT PRIVATE ${OTT_SOURCES})

target_include_directories(OTT PRIVATE Source)

target_compile_definitions(OTT PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(OTT
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Unit tests (JUCE UnitTest): build the OTT_Tests target and run it; it exits non-zero on failure.
if(OTT_BUILD_TESTS)
    juce_add_console_app(OTT_Tests PRODUCT_NAME "OTT_Tests")
    target_sources(OTT_Tests PRIVATE Tests/OttTests.cpp ${OTT_SOURCES})
    target_include_directories(OTT_Tests PRIVATE Source)
    target_compile_definitions(OTT_Tests PRIVATE
        JucePlugin_Name="OTT Multiband"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(OTT_Tests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
