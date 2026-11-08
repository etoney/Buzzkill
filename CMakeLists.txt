cmake_minimum_required(VERSION 3.22)
project(BUZZKILL VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

# Same JUCE version as DREAD (keep every Castle Danger plugin on one version).
# To build against a local JUCE checkout instead: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BuzzKill
    COMPANY_NAME "Castle Danger"
    PRODUCT_NAME "BuzzKill"
    BUNDLE_ID com.castledanger.buzzkill
    PLUGIN_MANUFACTURER_CODE Cdgr
    PLUGIN_CODE Bzk1
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Restoration Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(BuzzKill PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(BuzzKill PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(BuzzKill
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP test (no JUCE): synthetic guitar + amp noise -> before/after/removed WAVs + measurements
add_executable(buzzkill_render test/render_test.cpp)
target_include_directories(buzzkill_render PRIVATE Source)

# Headless GUI snapshot + state checks (CI renders the editor to gui.png for review)
juce_add_console_app(BuzzKillSnapshot PRODUCT_NAME "BuzzKillSnapshot")
target_sources(BuzzKillSnapshot PRIVATE test/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(BuzzKillSnapshot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    "JucePlugin_VersionString=\"${PROJECT_VERSION}\"")
target_link_libraries(BuzzKillSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
