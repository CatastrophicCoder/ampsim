#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("Parameters survive a state save and reload", "[state]")
{
    juce::MemoryBlock saved;

    {
        AmpSimAudioProcessor source;
        auto& state = source.getValueTreeState();

        test::setParam (state, ParamID::inputGain, 7.5f);
        test::setParam (state, ParamID::outputGain, -3.2f);
        test::setParam (state, ParamID::bypass, 1.0f);

        source.getStateInformation (saved);
    }

    REQUIRE (saved.getSize() > 0);

    AmpSimAudioProcessor restored;
    auto& state = restored.getValueTreeState();

    // Leave it somewhere else first, so a no-op load cannot pass by accident.
    test::setParam (state, ParamID::inputGain, -20.0f);

    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE_THAT (test::getParam (state, ParamID::inputGain),  WithinAbs (7.5, 1.0e-4));
    REQUIRE_THAT (test::getParam (state, ParamID::outputGain), WithinAbs (-3.2, 1.0e-4));
    REQUIRE (state.getParameter (ParamID::bypass)->getValue() > 0.5f);
}

TEST_CASE ("A restored state is audible in the next block", "[state]")
{
    juce::MemoryBlock saved;

    {
        AmpSimAudioProcessor source;
        test::setParam (source.getValueTreeState(), ParamID::inputGain, 6.0f);
        source.getStateInformation (saved);
    }

    auto processor = test::makePreparedProcessor();
    processor->setStateInformation (saved.getData(), (int) saved.getSize());

    const auto expected = 0.5f * std::pow (10.0f, 6.0f / 20.0f);

    REQUIRE_THAT (test::runConstant (*processor, test::blocksForRamp (0.05)),
                  WithinAbs (expected, 1.0e-4));
}

TEST_CASE ("Garbage state is ignored rather than crashing", "[state]")
{
    AmpSimAudioProcessor processor;
    auto& state = processor.getValueTreeState();

    test::setParam (state, ParamID::inputGain, 5.0f);

    const char junk[] = "not a valid plugin state at all";
    processor.setStateInformation (junk, (int) sizeof (junk));

    REQUIRE_THAT (test::getParam (state, ParamID::inputGain), WithinAbs (5.0, 1.0e-4));
}
