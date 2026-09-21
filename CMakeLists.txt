cmake_minimum_required(VERSION 3.22)

# Must be set before project(): JUCE bakes the deployment target into the plugin bundles.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(AmpSim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is a pinned submodule (9.0.2); see README for the update policy.
add_subdirectory(external/JUCE)

juce_add_plugin(AmpSim
    PRODUCT_NAME                "AmpSim"
    COMPANY_NAME                "AmpSim"
    BUNDLE_ID                   "com.ampsim.ampsim"
    PLUGIN_MANUFACTURER_CODE    Amps          # four chars, at least one uppercase
    PLUGIN_CODE                 Amp1          # unique per plugin; change if you ship a second one
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE)         # installs AU/VST3 to ~/Library/Audio/Plug-Ins on build

target_sources(AmpSim PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(AmpSim PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0        # requires a JUCE licence that permits it; see README
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(AmpSim
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
