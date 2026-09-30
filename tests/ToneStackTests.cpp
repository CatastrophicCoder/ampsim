/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "dsp/ToneStack.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    /** Steady-state gain in dB at one frequency, measured rather than derived from the
        coefficients: a test that recomputes the same formula the code uses proves nothing. */
    float measureGainDb (ToneStack& tone, double frequency)
    {
        constexpr int settleBlocks = 40;
        constexpr int measureBlocks = 20;

        std::vector<float> block ((size_t) test::blockSize);
        double phase = 0.0;
        const auto step = juce::MathConstants<double>::twoPi * frequency / sr;

        double inputSumSquares = 0.0, outputSumSquares = 0.0;

        for (int b = 0; b < settleBlocks + measureBlocks; ++b)
        {
            double blockInputSumSquares = 0.0;

            for (int i = 0; i < test::blockSize; ++i)
            {
                const auto sample = (float) std::sin (phase);
                phase += step;
                block[(size_t) i] = sample;
                blockInputSumSquares += (double) sample * sample;
            }

            tone.process (block.data(), test::blockSize);

            if (b >= settleBlocks)
            {
                inputSumSquares += blockInputSumSquares;

                for (int i = 0; i < test::blockSize; ++i)
                    outputSumSquares += (double) block[(size_t) i] * block[(size_t) i];
            }
        }

        return (float) (10.0 * std::log10 (outputSumSquares / inputSumSquares));
    }

    std::unique_ptr<ToneStack> makeToneStack (float bass, float mid, float treble,
                                              float presence = 0.0f, float depth = 0.0f)
    {
        auto tone = std::make_unique<ToneStack>();
        tone->prepare (sr, test::blockSize);
        tone->setBandGains (bass, mid, treble, presence, depth);
        tone->snapToTargets();
        return tone;
    }
}

TEST_CASE ("Every band centred is flat", "[tonestack]")
{
    auto tone = makeToneStack (0.0f, 0.0f, 0.0f);

    // Across the guitar's range, not just at the band centres.
    const auto frequency = GENERATE (80.0, 220.0, 440.0, 1000.0, 3000.0, 8000.0);

    INFO (frequency << " Hz");
    REQUIRE_THAT (measureGainDb (*tone, frequency), WithinAbs (0.0, 0.05));
}

TEST_CASE ("Each band delivers its stated gain at its own centre frequency", "[tonestack]")
{
    const auto gainDb = GENERATE (-12.0f, -6.0f, 6.0f, 12.0f);

    SECTION ("bass")
    {
        auto tone = makeToneStack (gainDb, 0.0f, 0.0f);

        // A shelf reaches its full gain below the corner, not at it.
        REQUIRE_THAT (measureGainDb (*tone, 30.0), WithinAbs (gainDb, 1.0));
    }

    SECTION ("mid")
    {
        auto tone = makeToneStack (0.0f, gainDb, 0.0f);

        REQUIRE_THAT (measureGainDb (*tone, ToneStack::midFrequency), WithinAbs (gainDb, 0.2));
    }

    SECTION ("treble")
    {
        auto tone = makeToneStack (0.0f, 0.0f, gainDb);

        REQUIRE_THAT (measureGainDb (*tone, 12000.0), WithinAbs (gainDb, 1.0));
    }
}

TEST_CASE ("The bands are independent of each other", "[tonestack]")
{
    // The point of choosing parametric bands over a modelled passive stack: turning one control
    // must not move another band's response.
    auto boosted = makeToneStack (12.0f, 0.0f, 0.0f);
    auto flat = makeToneStack (0.0f, 0.0f, 0.0f);

    // Well above the bass shelf's reach.
    REQUIRE_THAT (measureGainDb (*boosted, 8000.0) - measureGainDb (*flat, 8000.0),
                  WithinAbs (0.0, 0.2));

    auto trebleBoosted = makeToneStack (0.0f, 0.0f, 12.0f);

    // Well below the treble shelf's reach.
    REQUIRE_THAT (measureGainDb (*trebleBoosted, 80.0) - measureGainDb (*flat, 80.0),
                  WithinAbs (0.0, 0.2));
}

TEST_CASE ("The mid band is a peak, not a shelf", "[tonestack]")
{
    auto tone = makeToneStack (0.0f, 12.0f, 0.0f);

    const auto atCentre = measureGainDb (*tone, ToneStack::midFrequency);
    const auto wellBelow = measureGainDb (*tone, 60.0);
    const auto wellAbove = measureGainDb (*tone, 12000.0);

    REQUIRE (atCentre > 11.0f);
    REQUIRE (wellBelow < 1.0f);
    REQUIRE (wellAbove < 1.0f);
}

TEST_CASE ("Turning a tone control does not click", "[tonestack]")
{
    // Measured against the same settings applied from the start rather than against a fixed
    // number: five bands slammed at once make the signal itself much louder, and a threshold in
    // absolute samples would then be measuring how fast a loud sine moves rather than whether the
    // coefficients stepped.
    const auto worstJumpOverRun = [] (bool changeMidway)
    {
        auto tone = changeMidway ? makeToneStack (0.0f, 0.0f, 0.0f, 0.0f, 0.0f)
                                 : makeToneStack (12.0f, -12.0f, 12.0f, 12.0f, 12.0f);

        std::vector<float> block ((size_t) test::blockSize);
        double phase = 0.0;
        const auto step = juce::MathConstants<double>::twoPi * 220.0 / sr;

        float worstJump = 0.0f, previous = 0.0f;
        bool first = true;

        for (int b = 0; b < 20; ++b)
        {
            if (changeMidway && b == 5)
                tone->setBandGains (12.0f, -12.0f, 12.0f, 12.0f, 12.0f);

            for (int i = 0; i < test::blockSize; ++i)
            {
                block[(size_t) i] = 0.5f * (float) std::sin (phase);
                phase += step;
            }

            tone->process (block.data(), test::blockSize);

            for (int i = 0; i < test::blockSize; ++i)
            {
                if (! first)
                    worstJump = juce::jmax (worstJump, std::abs (block[(size_t) i] - previous));

                previous = block[(size_t) i];
                first = false;
            }
        }

        return worstJump;
    };

    const auto settled = worstJumpOverRun (false);
    const auto changed = worstJumpOverRun (true);

    INFO ("settled " << settled << ", changed mid-run " << changed);

    // Recomputing the coefficients in one jump rather than over the 50 ms ramp would put a step
    // in the signal larger than anything the settled sine does on its own.
    REQUIRE (changed <= settled * 1.1f);
}

TEST_CASE ("The tone stack starts at its settings rather than sweeping in", "[tonestack]")
{
    // The same trap as juce::dsp::Gain in milestone 1: set a target in prepare and the block
    // sweeps in from flat on every playback start.
    auto tone = makeToneStack (12.0f, 0.0f, 0.0f);

    ToneStack ramping;
    ramping.prepare (sr, test::blockSize);
    ramping.setBandGains (12.0f, 0.0f, 0.0f, 0.0f, 0.0f);   // deliberately no snapToTargets()

    std::vector<float> snapped ((size_t) test::blockSize), swept ((size_t) test::blockSize);

    for (int i = 0; i < test::blockSize; ++i)
        snapped[(size_t) i] = swept[(size_t) i] = (i == 0 ? 1.0f : 0.0f);

    tone->process (snapped.data(), test::blockSize);
    ramping.process (swept.data(), test::blockSize);

    // The snapped one is already at full boost; the ramping one is not, which is what
    // prepareToPlay must avoid.
    REQUIRE (snapped[0] > swept[0]);
}

TEST_CASE ("Presence and depth act where they are named for", "[tonestack]")
{
    // They are not a model of a presence or a depth control — those work by taking negative
    // feedback off a power amp, and the capture already contains the power amp. What they are is
    // two more bands placed where those controls act, so that is what is measured: a lift at the
    // top and one at the speaker's resonance, each leaving the other's end of the range alone.
    auto presence = makeToneStack (0.0f, 0.0f, 0.0f, 12.0f, 0.0f);

    REQUIRE (measureGainDb (*presence, 8000.0) > 9.0f);
    REQUIRE_THAT (measureGainDb (*presence, 200.0), WithinAbs (0.0, 0.5));

    auto depth = makeToneStack (0.0f, 0.0f, 0.0f, 0.0f, 12.0f);

    REQUIRE (measureGainDb (*depth, (double) ToneStack::depthFrequency) > 10.0f);
    REQUIRE_THAT (measureGainDb (*depth, 8000.0), WithinAbs (0.0, 0.5));

    // And depth is a resonance rather than a second bass shelf: an octave and a half under it the
    // lift is most of the way gone, where a shelf would still be holding it up.
    REQUIRE (measureGainDb (*depth, 30.0) < 6.0f);
}
