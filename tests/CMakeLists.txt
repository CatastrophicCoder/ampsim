# The tests link the plugin's shared-code target, so they exercise exactly the objects the
# AU/VST3/Standalone builds do — no second copy of the sources, no test-only build flags.

add_executable(AmpSimTests
    TestMain.cpp
    GainTests.cpp
    BypassTests.cpp
    StateTests.cpp
    ResamplerTests.cpp
    ModelTests.cpp
    CabTests.cpp
    ToneStackTests.cpp
    PedalTests.cpp
    TunerTests.cpp
    MidiLearnTests.cpp
    PresetTests.cpp
    AssetPackTests.cpp
    EditorTests.cpp
    TransposeTests.cpp)

# NAM Core ships example models; the tests use them rather than carrying a copy.
target_compile_definitions(AmpSimTests PRIVATE
    AMPSIM_TEST_MODEL_DIR="${CMAKE_SOURCE_DIR}/external/NeuralAmpModelerCore/example_models")

target_include_directories(AmpSimTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(AmpSimTests
    PRIVATE
        AmpSim                  # the plugin's shared code, with its JucePlugin_* defines
        ${NAM_CORE_WHOLE}
        Catch2::Catch2
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Registers each TEST_CASE with CTest individually, so `ctest -R <name>` can run one.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/external/Catch2/extras)
include(Catch)
catch_discover_tests(AmpSimTests)
