/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "dsp/ModelResampler.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    /** Runs a signal through the resampler with a do-nothing callback, so what comes out should
        be the input again: delayed, band-limited, but otherwise unchanged. */
    std::vector<float> runIdentity (double hostRate, double modelRate, int blockSize,
                                    const std::vector<float>& input, int& latencyOut)
    {
        ModelResampler resampler;
        resampler.prepare (hostRate, modelRate, blockSize);
        latencyOut = resampler.getLatencyInHostSamples();

        std::vector<float> output (input.size(), 0.0f);

        for (size_t pos = 0; pos + (size_t) blockSize <= input.size(); pos += (size_t) blockSize)
        {
            std::copy (input.begin() + (long) pos, input.begin() + (long) (pos + (size_t) blockSize),
                       output.begin() + (long) pos);

            resampler.process (output.data() + pos, blockSize, [] (float*, int) {});
        }

        return output;
    }

    std::vector<float> makeSine (double frequency, double sampleRate, int numSamples)
    {
        std::vector<float> signal ((size_t) numSamples);

        for (int i = 0; i < numSamples; ++i)
            signal[(size_t) i] = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                  * (float) (frequency * i / sampleRate));

        return signal;
    }
}

TEST_CASE ("The resampler is bypassed when the host already runs at the model's rate", "[resampler]")
{
    ModelResampler resampler;
    resampler.prepare (48000.0, 48000.0, 512);

    REQUIRE (resampler.isPassthrough());
    REQUIRE (resampler.getLatencyInHostSamples() == 0);
}

TEST_CASE ("The resampler reconstructs the signal it was given", "[resampler]")
{
    const auto hostRate = GENERATE (44100.0, 88200.0, 96000.0);
    constexpr double modelRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int numSamples = blockSize * 40;

    const auto input = makeSine (440.0, hostRate, numSamples);

    int latency = 0;
    const auto output = runIdentity (hostRate, modelRate, blockSize, input, latency);

    REQUIRE (latency > 0);

    // Compare well past the latency and the interpolators' settling, and stop before the tail
    // that the final partial block never produced.
    const auto first = latency + 2048;
    const auto last  = numSamples - blockSize;

    REQUIRE (first < last);

    double worst = 0.0;

    for (int i = first; i < last; ++i)
        worst = juce::jmax (worst, (double) std::abs (output[(size_t) i] - input[(size_t) (i - latency)]));

    // Windowed-sinc conversion either way: the error is the filters', not a misalignment.
    REQUIRE (worst < 0.02);
}

TEST_CASE ("The reported latency matches the delay actually introduced", "[resampler]")
{
    const auto hostRate = GENERATE (44100.0, 88200.0, 96000.0);
    constexpr double modelRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int numSamples = blockSize * 30;

    // An impulse is easier to locate than a sine, whose peaks all look alike.
    std::vector<float> input ((size_t) numSamples, 0.0f);
    input[(size_t) blockSize] = 1.0f;

    int latency = 0;
    const auto output = runIdentity (hostRate, modelRate, blockSize, input, latency);

    int peakIndex = 0;
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        if (std::abs (output[(size_t) i]) > peak)
        {
            peak = std::abs (output[(size_t) i]);
            peakIndex = i;
        }
    }

    const auto measuredLatency = peakIndex - blockSize;

    INFO ("reported " << latency << ", measured " << measuredLatency);
    REQUIRE (std::abs (measuredLatency - latency) <= 2);
}

TEST_CASE ("The resampler returns a full block whatever the block size", "[resampler]")
{
    const auto blockSize = GENERATE (32, 64, 128, 512, 1024);

    ModelResampler resampler;
    resampler.prepare (44100.0, 48000.0, blockSize);

    int modelSamplesSeen = 0;

    std::vector<float> block ((size_t) blockSize, 0.25f);

    for (int b = 0; b < 20; ++b)
    {
        std::fill (block.begin(), block.end(), 0.25f);
        resampler.process (block.data(), blockSize,
                           [&] (float*, int n)
                           {
                               modelSamplesSeen += n;
                               REQUIRE (n <= resampler.getMaxModelBlockSize());
                           });
    }

    // 20 blocks at 44.1 kHz should have become roughly the same duration at 48 kHz.
    const auto expected = 20.0 * blockSize * 48000.0 / 44100.0;
    REQUIRE (std::abs (modelSamplesSeen - expected) < 2.0 * blockSize);
}
