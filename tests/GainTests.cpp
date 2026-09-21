#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("Unity gain passes the signal through unchanged", "[gain]")
{
    auto processor = test::makePreparedProcessor();

    REQUIRE_THAT (test::runConstant (*processor, 4), WithinAbs (0.5, 1.0e-6));
}

TEST_CASE ("The first block after prepareToPlay is already at full level", "[gain]")
{
    // A default-constructed juce::dsp::Gain sits at 0, so setting its target without a reset()
    // makes the plugin fade in from silence every time the host starts playback.
    auto processor = test::makePreparedProcessor();

    REQUIRE_THAT (test::runConstant (*processor, 1), WithinAbs (0.5, 1.0e-6));
}

TEST_CASE ("Input gain scales the signal by its decibel value", "[gain]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    const auto gainDb = GENERATE (-12.0f, -6.0f, 0.0f, 6.0f, 12.0f);

    test::setParam (state, ParamID::inputGain, gainDb);

    const auto expected = 0.5f * std::pow (10.0f, gainDb / 20.0f);
    const auto measured = test::runConstant (*processor, test::blocksForRamp (0.05));

    REQUIRE_THAT (measured, WithinAbs (expected, 1.0e-4));
}

TEST_CASE ("Input and output gain compose", "[gain]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    test::setParam (state, ParamID::inputGain, 6.0f);
    test::setParam (state, ParamID::outputGain, -6.0f);

    REQUIRE_THAT (test::runConstant (*processor, test::blocksForRamp (0.05)),
                  WithinAbs (0.5, 1.0e-4));
}

TEST_CASE ("Gain changes ramp rather than stepping", "[gain]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), test::blockSize);
    juce::MidiBuffer midi;

    float worstJump = 0.0f, previous = 0.5f;

    for (int b = 0; b < 8; ++b)
    {
        if (b == 2)
            test::setParam (state, ParamID::inputGain, 24.0f);   // a full-range jerk of the knob

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, test::blockSize);

        processor->processBlock (buffer, midi);

        for (int i = 0; i < test::blockSize; ++i)
        {
            const auto sample = buffer.getSample (0, i);
            worstJump = juce::jmax (worstJump, std::abs (sample - previous));
            previous = sample;
        }
    }

    // A hard switch to +24 dB would jump ~7.4; the 50 ms ramp spreads it over 2400 samples.
    REQUIRE (worstJump < 0.01f);
}
