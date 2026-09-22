/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("Unity gain passes the signal through very nearly unchanged", "[gain]")
{
    auto processor = test::makePreparedProcessor();

    // Not bit-exact any more, and deliberately so: the drive pedal's oversampler runs whether or
    // not the pedal is engaged, so its latency cannot change under the host. The price is the
    // half-band filters' ripple, which is about 3 parts in 100,000.
    REQUIRE_THAT (test::runConstant (*processor, 4), WithinAbs (0.5, 1.0e-4));
}

TEST_CASE ("The gains start at their settings rather than fading in", "[gain]")
{
    // A default-constructed juce::dsp::Gain sits at 0, so setting its target without a reset()
    // makes the plugin fade in from silence every time the host starts playback.
    //
    // Measured on the second block, not the first: a DC step into the drive pedal's cold
    // half-band filters overshoots, the way a step into any filter does. A 50 ms fade-in would
    // still only be at 0.43 by this point, so the thing this test is for is still caught.
    auto processor = test::makePreparedProcessor();

    REQUIRE_THAT (test::runConstant (*processor, 2), WithinAbs (0.5, 1.0e-4));
}

TEST_CASE ("Input gain scales the signal by its decibel value", "[gain]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    const auto gainDb = GENERATE (-12.0f, -6.0f, 0.0f, 6.0f, 12.0f);

    test::setParam (state, ParamID::inputGain, gainDb);

    const auto expected = 0.5f * std::pow (10.0f, gainDb / 20.0f);
    const auto measured = test::runConstant (*processor, test::blocksForRamp (0.05));

    // Not exact: the tone stack's biquads sit in the chain, and a 100 Hz shelf at 48 kHz has
    // poles close enough to z = 1 that float state accumulates about 0.002 dB of error at DC.
    // Inaudible, but larger than the bit-accurate tolerance the gain alone used to meet.
    REQUIRE_THAT (measured, WithinAbs (expected, 0.005 * expected));
}

TEST_CASE ("Input and output gain compose", "[gain]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    test::setParam (state, ParamID::inputGain, 6.0f);
    test::setParam (state, ParamID::outputGain, -6.0f);

    REQUIRE_THAT (test::runConstant (*processor, test::blocksForRamp (0.05)),
                  WithinAbs (0.5, 0.005 * 0.5));
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

            // The first block is the signal itself starting abruptly at 0.5 into cold filters,
            // which is a step response rather than a click. The knob moves at block 2.
            if (b > 0)
                worstJump = juce::jmax (worstJump, std::abs (sample - previous));

            previous = sample;
        }
    }

    // A hard switch to +24 dB would jump ~7.4; the 50 ms ramp spreads it over 2400 samples.
    REQUIRE (worstJump < 0.01f);
}
