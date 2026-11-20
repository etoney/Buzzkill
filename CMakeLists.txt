cmake_minimum_required(VERSION 3.22)
# Must come BEFORE project(), or CMake ignores them: universal (Apple Silicon + Intel), macOS 11+
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(DEADAIR VERSION 2.4.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same JUCE version as DREAD (keep every Castle Danger plugin on one version).
# To build against a local JUCE checkout instead: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DeadAir
    COMPANY_NAME "Castle Danger"
    PRODUCT_NAME "Dead Air"
    BUNDLE_ID com.castledanger.deadair
    PLUGIN_MANUFACTURER_CODE Cdgr
    PLUGIN_CODE Dair
    FORMATS VST3 AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Restoration Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(DeadAir PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DeadAir PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DeadAir
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP test (no JUCE): synthetic guitar + amp noise -> before/after/removed WAVs + measurements
add_executable(deadair_render test/render_test.cpp)
target_include_directories(deadair_render PRIVATE Source)

# Headless GUI snapshot + state checks (CI renders the editor to gui.png for review)
juce_add_console_app(DeadAirSnapshot PRODUCT_NAME "DeadAirSnapshot")
target_sources(DeadAirSnapshot PRIVATE test/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(DeadAirSnapshot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_ALSA=0 JUCE_JACK=0
    "JucePlugin_VersionString=\"${PROJECT_VERSION}\"")
target_link_libraries(DeadAirSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
