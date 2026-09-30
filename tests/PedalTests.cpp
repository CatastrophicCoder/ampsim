/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "dsp/PedalChain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    struct Signal
    {
        static std::vector<float> sine (double frequency, int numSamples, float amplitude = 0.3f)
        {
            std::vector<float> out ((size_t) numSamples);

            for (int i = 0; i < numSamples; ++i)
                out[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                * frequency * i / sr);

            return out;
        }

        static float rms (const std::vector<float>& x, int from = 0)
        {
            double sum = 0.0;

            for (size_t i = (size_t) from; i < x.size(); ++i)
                sum += (double) x[i] * x[i];

            return (float) std::sqrt (sum / (double) (x.size() - (size_t) from));
        }

        static float peak (const std::vector<float>& x, int from = 0)
        {
            float result = 0.0f;

            for (size_t i = (size_t) from; i < x.size(); ++i)
                result = juce::jmax (result, std::abs (x[i]));

            return result;
        }
    };

    /** Runs a signal through one side of the chain, block by block. */
    std::vector<float> runThrough (PedalChain& chain, const std::vector<float>& input, bool beforeAmp)
    {
        auto output = input;

        for (size_t pos = 0; pos + (size_t) test::blockSize <= output.size(); pos += (size_t) test::blockSize)
        {
            if (beforeAmp)
                chain.processBeforeAmp (output.data() + pos, test::blockSize);
            else
                chain.processAfterAmp (output.data() + pos, test::blockSize);
        }

        return output;
    }

    std::unique_ptr<PedalChain> makeChain (const PedalChain::Settings& settings)
    {
        auto chain = std::make_unique<PedalChain>();
        chain->prepare (sr, test::blockSize);
        chain->setSettings (settings);
        chain->snapToSettings();
        return chain;
    }
}

TEST_CASE ("With every pedal off the chain leaves the signal alone", "[pedals]")
{
    auto chain = makeChain ({});

    const auto input = Signal::sine (440.0, test::blockSize * 8);

    auto before = runThrough (*chain, input, true);
    auto after = runThrough (*chain, input, false);

    // The drive pedal's oversampler always runs, so "alone" means within its filters' ripple and
    // its five samples of latency — compared well past both.
    REQUIRE_THAT (Signal::rms (before, test::blockSize), WithinAbs (Signal::rms (input, test::blockSize), 1.0e-3));
    REQUIRE_THAT (Signal::rms (after, test::blockSize), WithinAbs (Signal::rms (input, test::blockSize), 1.0e-6));
}

TEST_CASE ("The gate closes on a signal below its threshold", "[pedals]")
{
    PedalChain::Settings settings;
    settings.gateEngaged = true;
    settings.gateThresholdDb = -40.0f;

    auto chain = makeChain (settings);

    // A quiet hiss, well under the threshold.
    auto quiet = Signal::sine (440.0, test::blockSize * 20, 0.002f);
    const auto gated = runThrough (*chain, quiet, true);

    // And a note well over it.
    auto loud = Signal::sine (440.0, test::blockSize * 20, 0.3f);
    auto openChain = makeChain (settings);
    const auto passed = runThrough (*openChain, loud, true);

    const auto measureFrom = test::blockSize * 10;

    REQUIRE (Signal::rms (gated, measureFrom) < 0.25f * Signal::rms (quiet, measureFrom));
    REQUIRE (Signal::rms (passed, measureFrom) > 0.9f * Signal::rms (loud, measureFrom));
}

TEST_CASE ("The compressor narrows the range between quiet and loud", "[pedals]")
{
    PedalChain::Settings settings;
    settings.compressorEngaged = true;
    settings.compressorAmount = 1.0f;
    settings.compressorLevelDb = 0.0f;

    const auto outputFor = [&] (float amplitude)
    {
        auto chain = makeChain (settings);
        auto input = Signal::sine (220.0, test::blockSize * 30, amplitude);
        return Signal::rms (runThrough (*chain, input, true), test::blockSize * 20);
    };

    const auto quietOut = outputFor (0.05f);
    const auto loudOut = outputFor (0.5f);

    // 20 dB in should come out as appreciably less than 20 dB.
    const auto outputRangeDb = juce::Decibels::gainToDecibels (loudOut / quietOut);

    INFO ("input range 20 dB became " << outputRangeDb << " dB");
    REQUIRE (outputRangeDb < 14.0f);
    REQUIRE (outputRangeDb > 0.0f);
}

TEST_CASE ("The drive pedal adds harmonics rather than level alone", "[pedals]")
{
    PedalChain::Settings settings;
    settings.driveEngaged = true;
    settings.driveAmount = 0.9f;
    settings.driveTone = 1.0f;
    settings.driveLevelDb = 0.0f;

    auto chain = makeChain (settings);

    constexpr double frequency = 220.0;
    auto input = Signal::sine (frequency, test::blockSize * 16, 0.3f);
    const auto driven = runThrough (*chain, input, true);

    // Energy at the fundamental against everything else, by correlating with the input tone.
    const auto fundamentalEnergy = [&] (const std::vector<float>& x)
    {
        double real = 0.0, imaginary = 0.0;
        const auto from = (size_t) test::blockSize * 4;

        for (size_t i = from; i < x.size(); ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * frequency * (double) i / sr;
            real += x[i] * std::cos (phase);
            imaginary += x[i] * std::sin (phase);
        }

        const auto count = (double) (x.size() - from);
        return 2.0 * std::sqrt (real * real + imaginary * imaginary) / count;
    };

    const auto total = Signal::rms (driven, test::blockSize * 4) * std::sqrt (2.0f);
    const auto fundamental = (float) fundamentalEnergy (driven);

    REQUIRE (fundamental > 0.0f);

    // A clean gain stage would put everything at the fundamental; a clipper does not.
    const auto harmonicShare = 1.0f - (fundamental * fundamental) / (total * total);

    INFO ("harmonic share " << harmonicShare);
    REQUIRE (harmonicShare > 0.02f);
}

TEST_CASE ("The delay repeats the signal at the time it was given", "[pedals]")
{
    PedalChain::Settings settings;
    settings.delayEngaged = true;
    settings.delayTimeSeconds = 0.1f;
    settings.delayFeedback = 0.0f;
    settings.delayMix = 1.0f;          // wet only, so the repeat is unmistakable

    auto chain = makeChain (settings);

    // One short burst, then silence.
    std::vector<float> input ((size_t) test::blockSize * 40, 0.0f);
    for (int i = 0; i < 64; ++i)
        input[(size_t) i] = 0.5f;

    const auto out = runThrough (*chain, input, false);

    const auto expectedAt = (int) (0.1 * sr);

    // The burst should be gone from the start and present around the delay time.
    const auto atStart = Signal::peak ({ out.begin() + 128, out.begin() + 1000 });
    const auto atDelay = Signal::peak ({ out.begin() + expectedAt - 32, out.begin() + expectedAt + 128 });

    INFO ("start " << atStart << ", at delay " << atDelay);
    REQUIRE (atDelay > 0.3f);
    REQUIRE (atStart < 0.05f);
}

TEST_CASE ("The reverb keeps sounding after the signal stops", "[pedals]")
{
    PedalChain::Settings settings;
    settings.reverbEngaged = true;
    settings.reverbSize = 0.8f;
    settings.reverbMix = 0.6f;

    auto chain = makeChain (settings);

    std::vector<float> input ((size_t) test::blockSize * 40, 0.0f);
    const auto burst = Signal::sine (440.0, test::blockSize * 4, 0.5f);
    std::copy (burst.begin(), burst.end(), input.begin());

    const auto out = runThrough (*chain, input, false);

    const auto tail = Signal::rms ({ out.begin() + test::blockSize * 8, out.begin() + test::blockSize * 12 });

    REQUIRE (tail > 0.001f);
}

TEST_CASE ("The chorus modulates the signal over time", "[pedals]")
{
    PedalChain::Settings settings;
    settings.chorusEngaged = true;
    settings.chorusRateHz = 4.0f;
    settings.chorusDepth = 1.0f;
    settings.chorusMix = 0.5f;

    auto chain = makeChain (settings);

    auto input = Signal::sine (440.0, test::blockSize * 40, 0.3f);
    const auto out = runThrough (*chain, input, false);

    // A modulated signal's level wanders as the delayed copy drifts in and out of phase; a static
    // one does not.
    float quietest = 1.0f, loudest = 0.0f;

    for (int b = 10; b < 38; ++b)
    {
        const std::vector<float> block { out.begin() + b * test::blockSize,
                                         out.begin() + (b + 1) * test::blockSize };
        const auto level = Signal::rms (block);
        quietest = juce::jmin (quietest, level);
        loudest = juce::jmax (loudest, level);
    }

    INFO ("level wandered between " << quietest << " and " << loudest);
    REQUIRE (loudest > quietest * 1.05f);
}

TEST_CASE ("Each pedal can be switched in and out without a click", "[pedals]")
{
    // A drive pedal's own output is nearly a square wave, whose sample-to-sample slope is far
    // larger than any switching artefact — so a fixed threshold would measure the pedal rather
    // than the switch. Measure each pedal running continuously, then compare a run that switches
    // it in halfway.
    struct Case { const char* name; void (*engage) (PedalChain::Settings&); bool beforeAmp; };

    const Case cases[]
    {
        { "gate",       [] (PedalChain::Settings& s) { s.gateEngaged = true; s.gateThresholdDb = -30.0f; }, true },
        { "compressor", [] (PedalChain::Settings& s) { s.compressorEngaged = true; s.compressorLevelDb = 9.0f; }, true },
        { "drive",      [] (PedalChain::Settings& s) { s.driveEngaged = true; s.driveAmount = 1.0f; }, true },
        { "chorus",     [] (PedalChain::Settings& s) { s.chorusEngaged = true; }, false },
        { "delay",      [] (PedalChain::Settings& s) { s.delayEngaged = true; s.delayMix = 1.0f; }, false },
        { "reverb",     [] (PedalChain::Settings& s) { s.reverbEngaged = true; s.reverbMix = 1.0f; }, false },
    };

    for (const auto& testCase : cases)
    {
        PedalChain::Settings engaged;
        testCase.engage (engaged);

        const auto worstJumpOver = [&] (bool switchMidway)
        {
            auto chain = makeChain (switchMidway ? PedalChain::Settings {} : engaged);

            std::vector<float> block ((size_t) test::blockSize);
            double phase = 0.0;
            const auto step = juce::MathConstants<double>::twoPi * 110.0 / sr;

            float worstJump = 0.0f, previous = 0.0f;

            for (int b = 0; b < 30; ++b)
            {
                if (switchMidway && b == 10)
                    chain->setSettings (engaged);

                for (int i = 0; i < test::blockSize; ++i)
                {
                    block[(size_t) i] = 0.3f * (float) std::sin (phase);
                    phase += step;
                }

                if (testCase.beforeAmp)
                    chain->processBeforeAmp (block.data(), test::blockSize);
                else
                    chain->processAfterAmp (block.data(), test::blockSize);

                for (int i = 0; i < test::blockSize; ++i)
                {
                    if (b > 0)
                        worstJump = juce::jmax (worstJump, std::abs (block[(size_t) i] - previous));

                    previous = block[(size_t) i];
                }
            }

            return worstJump;
        };

        const auto running = worstJumpOver (false);
        const auto switched = worstJumpOver (true);

        INFO (testCase.name << ": running " << running << ", switched " << switched);

        // Switching must not introduce a discontinuity beyond what the pedal already makes.
        REQUIRE (switched <= running * 1.15f + 0.005f);
    }
}

TEST_CASE ("The pedal groups stay on their own side of the amp", "[pedals][processor]")
{
    // The placement is the design, so it is worth a test that fails if someone merges the two
    // groups into one list: a drive pedal in front of the amp changes what the amp distorts, and
    // after it would only add fizz to an already-distorted signal.
    auto chain = makeChain ({});

    PedalChain::Settings driveOnly;
    driveOnly.driveEngaged = true;
    driveOnly.driveAmount = 1.0f;
    chain->setSettings (driveOnly);
    chain->snapToSettings();

    auto input = Signal::sine (220.0, test::blockSize * 8, 0.3f);

    const auto throughFront = runThrough (*chain, input, true);

    auto rearChain = makeChain (driveOnly);
    const auto throughRear = runThrough (*rearChain, input, false);

    // Drive belongs to the front group, so only that side changes the signal.
    const auto untouched = Signal::rms (input, test::blockSize * 2);
    REQUIRE (std::abs (Signal::rms (throughFront, test::blockSize * 2) - untouched) > 0.02f * untouched);
    REQUIRE_THAT (Signal::rms (throughRear, test::blockSize * 2),
                  WithinAbs (Signal::rms (input, test::blockSize * 2), 1.0e-6));
}

TEST_CASE ("The gate fades what is under the threshold rather than muting it", "[pedals]")
{
    // A downward expander takes (ratio - 1) times however far a signal sits below the threshold.
    // At 10:1 a note tail 10 dB under loses 90 dB and simply disappears, which is what makes a
    // gate feel like a guillotine. The ratio here is chosen so that the same tail fades.
    const auto reductionAt = [] (float thresholdDb, float signalDb)
    {
        PedalChain::Settings settings;
        settings.gateEngaged = true;
        settings.gateThresholdDb = thresholdDb;

        auto chain = makeChain (settings);

        const auto amplitude = juce::Decibels::decibelsToGain (signalDb) * std::sqrt (2.0f);
        auto input = Signal::sine (220.0, test::blockSize * 60, amplitude);

        const auto output = runThrough (*chain, input, true);

        const auto measureFrom = test::blockSize * 40;
        const auto before = Signal::rms (input, measureFrom);
        const auto after = Signal::rms (output, measureFrom);

        return juce::Decibels::gainToDecibels (after / juce::jmax (1.0e-9f, before));
    };

    // Ten decibels under: audibly reduced, still clearly there.
    const auto justUnder = reductionAt (-50.0f, -60.0f);
    INFO ("10 dB under the threshold: " << justUnder << " dB");
    REQUIRE (justUnder < -8.0f);
    REQUIRE (justUnder > -40.0f);

    // Well under, which is where hiss lives: gone.
    const auto wellUnder = reductionAt (-50.0f, -85.0f);
    INFO ("35 dB under the threshold: " << wellUnder << " dB");
    REQUIRE (wellUnder < -50.0f);

    // Above it, untouched.
    const auto above = reductionAt (-50.0f, -30.0f);
    INFO ("20 dB over the threshold: " << above << " dB");
    REQUIRE (above > -0.5f);
}
