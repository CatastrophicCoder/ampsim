cmake_minimum_required(VERSION 3.24)   # 3.24 for $<LINK_LIBRARY:WHOLE_ARCHIVE>, see nam_core below

# Must be set before project(): JUCE bakes the deployment target into the plugin bundles.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(AmpSim VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Tests are for developing this project, not for anyone consuming it as a subproject.
option(AMPSIM_BUILD_TESTS "Build the offline DSP tests" ${PROJECT_IS_TOP_LEVEL})
option(AMPSIM_BUILD_TOOLS "Build the offline measurement tools" OFF)

# JUCE is a pinned submodule (9.0.2); see README for the update policy.
add_subdirectory(external/JUCE)

# Windows builds require AVX2. Eigen then uses AVX2 and FMA instead of SSE2, which measured 27 to
# 38 % cheaper on the amp model; the price is a CPU from the Haswell / Zen generation onwards,
# and a CPU without AVX2 crashes on the first such instruction rather than refusing to load. See
# docs/windows.md. MSVC is also true for clang-cl. Before nam_core and AmpSim, so both get it.
if(MSVC)
    add_compile_options(/arch:AVX2)
endif()

# The C++ runtime linked statically on Windows (/MT), so the standalone and the VST3 need nothing
# installed beside them. CMake's default is the DLL runtime, and a plugin that cannot find
# VCRUNTIME140.dll fails to load in the host with no useful message. A variable rather than a
# per-target property so every target agrees, NAM and Catch2 included; mixing runtimes does not
# link. packaging/package-windows.ps1 refuses a binary that still imports the DLL runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

# NAM's specialised WaveNet for the A2 shape, which the bundled capture has. NAM's own CMake turns
# it on by default; building NAM here instead had silently left it compiled out. It measured 3 to
# 7 % cheaper on the Mac. tests/ModelTests.cpp null-tests it against the generic WaveNet, since it
# is a second implementation of the same model.
target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)

# NAM registers its architectures (WaveNet, LSTM, ConvNet, Linear) with file-scope static objects
# in their own .cpp files. Nothing references those objects, so a normal static-library link drops
# the whole translation unit and loading any model fails with "No config parser registered for
# architecture". Link the archive whole to keep them.
set(NAM_CORE_WHOLE $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>)
target_compile_features(nam_core PUBLIC cxx_std_20)
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Only a Release build installs itself into ~/Library/Audio/Plug-Ins. Both build trees used to, and
# whichever built last won — so running the Debug tests after a Release build left a Debug AU
# where Logic loads it. Unoptimised Eigen runs the amp model about sixty times slower: the plugin
# needed 195 % of a core at 48 kHz, Logic reported an overload, and nothing came out. A Debug
# plugin is still built, in build/AmpSim_artefacts/Debug, for pluginval and for loading by hand.
# Override with -DAMPSIM_COPY_PLUGIN=ON or OFF.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(ampsim_copy_default ON)
else()
    set(ampsim_copy_default OFF)
endif()
option(AMPSIM_COPY_PLUGIN "Install the built AU and VST3 into ~/Library/Audio/Plug-Ins" ${ampsim_copy_default})

juce_add_plugin(AmpSim
    PRODUCT_NAME                "AmpSim"
    ICON_BIG                    "resources/icon-1024.png"
    ICON_SMALL                  "resources/icon-128.png"
    COMPANY_NAME                "Catastrophic Audio"
    BUNDLE_ID                   "com.catastrophicaudio.ampsim"
    PLUGIN_MANUFACTURER_CODE    Ctcd          # four chars, at least one uppercase: Catastrophic Audio
    PLUGIN_CODE                 Amp1          # unique per plugin; change if you ship a second one
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    FALSE
    # FALSE, which makes the AU an `aufx` effect rather than an `aumf` music effect. An aumf in
    # Logic goes in an instrument track's instrument slot and takes its audio from a side chain,
    # so inserted the way anyone would insert an amp sim it is handed silence. The cost is the
    # VST3's event input bus, and with it MIDI learn in VST3 hosts; the standalone keeps MIDI
    # either way, because StandalonePluginHolder routes it through AudioProcessorPlayer rather
    # than through this flag.
    NEEDS_MIDI_INPUT            FALSE

    # Without this the standalone has no NSMicrophoneUsageDescription, and macOS denies it audio
    # input without ever asking — the app opens, the device appears, and nothing comes in. The
    # plugin builds are unaffected, since they record under the host's permission.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "AmpSim needs your audio input to amplify the guitar plugged into it."

    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${AMPSIM_COPY_PLUGIN})   # Release only by default; see above

# The amp model and cabinet impulse response travel inside the binary, packed. See AssetPack.h.
juce_add_binary_data(AmpSimAssets SOURCES
    resources/MARS2204.ampsimpack
    "resources/V30 SM57.ampsimpack")

set_target_properties(AmpSimAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The panel's two typefaces, embedded so it looks the same on every platform: Avenir Next and
# Futura ship with macOS only. Figtree and Jost are under the SIL Open Font License, whose text is
# beside them in resources/fonts; the .ttf files are static instances cut from the variable fonts
# in the google/fonts repository. Not packed: unlike the model, nothing about a font needs hiding.
juce_add_binary_data(AmpSimFonts
    NAMESPACE EmbeddedFonts
    HEADER_NAME EmbeddedFonts.h
    SOURCES
        resources/fonts/Figtree-Regular.ttf
        resources/fonts/Figtree-Medium.ttf
        resources/fonts/Jost-Medium.ttf)

set_target_properties(AmpSimFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(AmpSim PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ModelLoader.cpp
    src/MidiLearn.cpp
    src/BundledAssets.cpp
    src/PresetManager.cpp
    src/dsp/AmpModel.cpp
    src/dsp/CabSim.cpp
    src/dsp/Metronome.cpp
    src/dsp/ToneStack.cpp
    src/dsp/Transpose.cpp
    src/dsp/Tuner.cpp
    src/ui/AmpLookAndFeel.cpp
    src/ui/AmpKnob.cpp
    src/ui/AmpPage.cpp
    src/ui/PedalsPage.cpp
    src/ui/PedalObject.cpp
    src/ui/CabPage.cpp
    src/ui/PresetRow.cpp
    src/dsp/PedalChain.cpp
    src/dsp/pedals/DirtPedal.cpp
    src/dsp/pedals/ModulationPedal.cpp
    src/dsp/pedals/DelayPedal.cpp)

target_include_directories(AmpSim PUBLIC src)

target_compile_definitions(AmpSim PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

# ASIO in the Windows standalone. Every Windows Audio mode goes through Windows' own audio engine,
# which on a typical interface stops at a 10 ms period and the device's shared sample rate; ASIO
# talks to the interface's own driver, which is what a guitarist on Windows plays through. The
# headers are the ones bundled with JUCE, from Steinberg's SDK, used under its GPLv3 option, which
# is compatible with this project's AGPLv3; the installer ships that licence text. A plug-in uses
# its host's driver, so this only matters to the standalone.
if(WIN32)
    target_compile_definitions(AmpSim PUBLIC JUCE_ASIO=1)
endif()

target_link_libraries(AmpSim
    PRIVATE
        AmpSimAssets
        AmpSimFonts
        ${NAM_CORE_WHOLE}
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Packaging is a shell script rather than CPack: it has to ad-hoc sign the bundles between
# building and packaging them, which CPack has no good place for. See packaging/README.md.
if(APPLE AND PROJECT_IS_TOP_LEVEL)
    add_custom_target(package-macos
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/packaging/package.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building and packaging AmpSim for macOS"
        USES_TERMINAL)
endif()

if(AMPSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(external/Catch2)
    add_subdirectory(tests)
endif()

# Off by default: these measure the plugin rather than being part of it, and only a Release build
# of them says anything true.
if(AMPSIM_BUILD_TOOLS)
    # macOS only: it hosts the AU, which no other platform has, and looks for the bundles where
    # macOS installs them.
    if(APPLE)
        # A host, not the plugin: it loads the built bundles the way Logic does, through the AU
        # and VST3 wrappers, which is the layer every other test here skips.
        juce_add_console_app(ampsim_hostcheck PRODUCT_NAME "ampsim_hostcheck")
        target_sources(ampsim_hostcheck PRIVATE tools/HostCheck.cpp)
        target_compile_definitions(ampsim_hostcheck PRIVATE
            JUCE_PLUGINHOST_AU=1
            JUCE_PLUGINHOST_VST3=1
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        target_link_libraries(ampsim_hostcheck PRIVATE juce::juce_audio_utils)
    endif()

    add_executable(ampsim_bench tools/Benchmark.cpp)
    target_include_directories(ampsim_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(ampsim_bench PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ampsim_bench
        PRIVATE
            AmpSim
            ${NAM_CORE_WHOLE}
            juce::juce_audio_utils
            juce::juce_dsp)

    # Renders each page of the panel to a PNG. See tools/PanelSnapshot.cpp.
    add_executable(ampsim_snapshot tools/PanelSnapshot.cpp)
    target_include_directories(ampsim_snapshot PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(ampsim_snapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ampsim_snapshot
        PRIVATE
            AmpSim
            ${NAM_CORE_WHOLE}
            juce::juce_audio_utils
            juce::juce_dsp)
endif()
