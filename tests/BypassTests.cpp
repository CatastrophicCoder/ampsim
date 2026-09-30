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
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE ("Bypass returns the dry signal whatever the gains are set to", "[bypass]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    test::setParam (state, ParamID::inputGain, 18.0f);
    test::setParam (state, ParamID::outputGain, -12.0f);
    test::setParam (state, ParamID::bypass, 1.0f);

    REQUIRE_THAT (test::runConstant (*processor, test::blocksForRamp (0.02)),
                  WithinAbs (0.5, 1.0e-6));
}

TEST_CASE ("Toggling bypass crossfades rather than stepping", "[bypass]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    test::setParam (state, ParamID::outputGain, 12.0f);

    juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), test::blockSize);
    juce::MidiBuffer midi;

    float worstJump = 0.0f, previous = 0.0f;
    bool first = true;

    for (int b = 0; b < 40; ++b)
    {
        if (b == 10) test::setParam (state, ParamID::bypass, 1.0f);
        if (b == 25) test::setParam (state, ParamID::bypass, 0.0f);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, test::blockSize);

        processor->processBlock (buffer, midi);

        for (int i = 0; i < test::blockSize; ++i)
        {
            const auto sample = buffer.getSample (0, i);

            // Skip the first block: that is the signal starting abruptly into cold filters, not
            // anything the bypass switch did. Bypass is toggled at blocks 10 and 25.
            if (! first && b > 0)
                worstJump = juce::jmax (worstJump, std::abs (sample - previous));

            previous = sample;
            first = false;
        }
    }

    // Switching +12 dB straight to dry would jump ~1.5.
    REQUIRE (worstJump < 0.01f);
}

TEST_CASE ("The bypass parameter is exposed to the host", "[bypass]")
{
    AmpSimAudioProcessor processor;

    auto* bypass = processor.getBypassParameter();

    REQUIRE (bypass != nullptr);
    REQUIRE (bypass == processor.getValueTreeState().getParameter (ParamID::bypass));
}

TEST_CASE ("Power off silences the amp", "[power]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    test::setParam (state, ParamID::power, 0.0f);

    REQUIRE_THAT (test::runConstant (*processor, test::blocksForRamp (0.03)),
                  WithinAbs (0.0, 1.0e-6));
}

TEST_CASE ("Power off does not silence a bypassed plugin", "[power]")
{
    // The amp is out of the chain when the plugin is bypassed, so whether it is switched on is
    // not the question — a host that bypasses an effect expects to hear what went into it.
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    test::setParam (state, ParamID::power, 0.0f);
    test::setParam (state, ParamID::bypass, 1.0f);

    REQUIRE_THAT (test::runConstant (*processor, test::blocksForRamp (0.03)),
                  WithinAbs (0.5, 1.0e-6));
}

TEST_CASE ("Switching the power off ramps rather than cutting", "[power]")
{
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), test::blockSize);
    juce::MidiBuffer midi;

    const auto runBlock = [&]
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, test::blockSize);

        processor->processBlock (buffer, midi);
    };

    for (int i = 0; i < test::blocksForRamp (0.05); ++i)
        runBlock();

    auto previous = buffer.getSample (0, test::blockSize - 1);

    test::setParam (state, ParamID::power, 0.0f);

    float worstJump = 0.0f;

    for (int i = 0; i < test::blocksForRamp (0.05); ++i)
    {
        runBlock();

        for (int s = 0; s < test::blockSize; ++s)
        {
            const auto sample = buffer.getSample (0, s);
            worstJump = juce::jmax (worstJump, std::abs (sample - previous));
            previous = sample;
        }
    }

    // A 30 ms ramp at this block size moves far less than this per sample; an instant cut of a
    // 0.5 DC signal would move by the whole thing.
    REQUIRE (worstJump < 0.01f);

    // And it has to have arrived at silence, or the test above passes on a power switch that
    // does nothing at all.
    REQUIRE_THAT (buffer.getSample (0, test::blockSize - 1), WithinAbs (0.0, 1.0e-6));
}
