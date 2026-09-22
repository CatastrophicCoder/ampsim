cmake_minimum_required(VERSION 3.24)   # 3.24 for $<LINK_LIBRARY:WHOLE_ARCHIVE>, see nam_core below

# Must be set before project(): JUCE bakes the deployment target into the plugin bundles.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(AmpSim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests are for developing this project, not for anyone consuming it as a subproject.
option(AMPSIM_BUILD_TESTS "Build the offline DSP tests" ${PROJECT_IS_TOP_LEVEL})

# JUCE is a pinned submodule (9.0.2); see README for the update policy.
add_subdirectory(external/JUCE)

# --- Neural Amp Modeler Core -------------------------------------------------
# Built here rather than via NAM's own CMakeLists, which sets global compiler flags and builds
# its tools and tests. Sources are globbed the way nam-juce does it.
set(NAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore)
file(GLOB NAM_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)

add_library(nam_core STATIC ${NAM_SOURCES})

# SYSTEM: NAM and Eigen are not ours to keep warning-clean.
target_include_directories(nam_core SYSTEM PUBLIC
    ${NAM_DIR}
    ${NAM_DIR}/NAM
    ${NAM_DIR}/Dependencies/eigen
    ${NAM_DIR}/Dependencies/nlohmann)

# NAM_SAMPLE becomes float, matching JUCE's buffers — without this NAM processes in double
# and every block needs a conversion.
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT DSP_SAMPLE_FLOAT)

# NAM registers its architectures (WaveNet, LSTM, ConvNet, Linear) with file-scope static objects
# in their own .cpp files. Nothing references those objects, so a normal static-library link drops
# the whole translation unit and loading any model fails with "No config parser registered for
# architecture". Link the archive whole to keep them.
set(NAM_CORE_WHOLE $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>)
target_compile_features(nam_core PUBLIC cxx_std_20)
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    src/PluginEditor.cpp
    src/ModelLoader.cpp
    src/dsp/AmpModel.cpp
    src/dsp/CabSim.cpp
    src/dsp/ToneStack.cpp
    src/dsp/Tuner.cpp
    src/ui/AmpLookAndFeel.cpp
    src/ui/PedalTile.cpp
    src/ui/CabinetRow.cpp
    src/dsp/PedalChain.cpp
    src/dsp/pedals/DrivePedal.cpp
    src/dsp/pedals/DelayPedal.cpp)

target_include_directories(AmpSim PUBLIC src)

target_compile_definitions(AmpSim PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(AmpSim
    PRIVATE
        ${NAM_CORE_WHOLE}
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(AMPSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(external/Catch2)
    add_subdirectory(tests)
endif()
