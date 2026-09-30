/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "dsp/AmpModel.h"

#include <NAM/get_dsp.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    juce::File exampleModel (const juce::String& name)
    {
        return juce::File (AMPSIM_TEST_MODEL_DIR).getChildFile (name);
    }

    /** Loads a model the way the loader thread does, so the audio thread only ever sees one that
        is fully prepared. */
    std::unique_ptr<LoadedModel> loadFor (AmpModel& ampModel, const juce::File& file)
    {
        auto model = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
        REQUIRE (model != nullptr);

        return ampModel.prepareForLoading (std::move (model));
    }

    /** Pushes blocks through until the pending model has been taken up. */
    void runBlocks (AmpModel& ampModel, int numBlocks, float level = 0.1f)
    {
        std::vector<float> block ((size_t) test::blockSize);

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < test::blockSize; ++i)
                block[(size_t) i] = level * std::sin (juce::MathConstants<float>::twoPi
                                                      * 220.0f * (float) i / 48000.0f);

            ampModel.process (block.data(), test::blockSize);
        }
    }
}

TEST_CASE ("The example NAM models load and report their sample rate", "[model]")
{
    const auto name = GENERATE (juce::String ("wavenet.nam"), juce::String ("lstm.nam"));
    const auto file = exampleModel (name);

    REQUIRE (file.existsAsFile());

    auto model = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));

    REQUIRE (model != nullptr);
    REQUIRE (model->GetExpectedSampleRate() > 0.0);
}

TEST_CASE ("An AmpModel with no model loaded leaves the signal alone", "[model]")
{
    AmpModel ampModel;
    ampModel.prepare (48000.0, test::blockSize);

    std::vector<float> block ((size_t) test::blockSize, 0.25f);

    REQUIRE_FALSE (ampModel.process (block.data(), test::blockSize));
    REQUIRE_THAT (block[0], WithinAbs (0.25, 1.0e-9));
    REQUIRE (ampModel.getLatencySamples() == 0);
}

TEST_CASE ("A pending model is picked up by the audio thread and changes the signal", "[model]")
{
    AmpModel ampModel;
    ampModel.prepare (48000.0, test::blockSize);

    REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel ("wavenet.nam"))));

    // The swap happens under a fade, so it takes a couple of blocks.
    runBlocks (ampModel, 8);

    REQUIRE (ampModel.hasModel());
    REQUIRE (ampModel.getModelSampleRate() > 0.0);

    std::vector<float> block ((size_t) test::blockSize);

    for (int i = 0; i < test::blockSize; ++i)
        block[(size_t) i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 48000.0f);

    const auto before = block;

    REQUIRE (ampModel.process (block.data(), test::blockSize));

    bool changed = false;
    for (size_t i = 0; i < block.size(); ++i)
    {
        REQUIRE (std::isfinite (block[i]));
        REQUIRE (std::abs (block[i]) < 10.0f);          // no runaway output
        changed = changed || std::abs (block[i] - before[i]) > 1.0e-4f;
    }

    REQUIRE (changed);
}

TEST_CASE ("A model trained at another rate is resampled and its latency reported", "[model]")
{
    AmpModel ampModel;
    ampModel.prepare (44100.0, test::blockSize);          // model files are 48 kHz

    REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel ("wavenet.nam"))));
    runBlocks (ampModel, 8);

    REQUIRE (ampModel.hasModel());
    REQUIRE_THAT (ampModel.getModelSampleRate(), WithinAbs (48000.0, 1.0));
    REQUIRE (ampModel.getLatencySamples() > 0);
}

TEST_CASE ("Swapping models fades rather than cutting", "[model]")
{
    // A model's own output is full of fast transitions — it is a distortion box — so a fixed
    // "largest jump" threshold would say more about the model than about the swap. Measure the
    // same signal through each model with no swap at all, then compare a run that does swap.
    const auto signalAt = [] (int sampleIndex)
    {
        return 0.2f * std::sin (juce::MathConstants<float>::twoPi * 110.0f * (float) sampleIndex / 48000.0f);
    };

    const auto worstJumpOverRun = [&] (const juce::String& modelName, bool swapMidway, float& quietestBlock)
    {
        AmpModel ampModel;
        ampModel.prepare (48000.0, test::blockSize);

        REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel (modelName))));
        runBlocks (ampModel, 8);
        ampModel.collectRetiredModel();

        std::vector<float> block ((size_t) test::blockSize);
        float worstJump = 0.0f, previous = 0.0f;
        bool first = true;
        quietestBlock = 1.0f;

        for (int b = 0; b < 20; ++b)
        {
            if (swapMidway && b == 10)
                REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel ("lstm.nam"))));

            for (int i = 0; i < test::blockSize; ++i)
                block[(size_t) i] = signalAt (b * test::blockSize + i);

            ampModel.process (block.data(), test::blockSize);

            float blockPeak = 0.0f;

            for (int i = 0; i < test::blockSize; ++i)
            {
                const auto sample = block[(size_t) i];

                if (! first)
                    worstJump = juce::jmax (worstJump, std::abs (sample - previous));

                blockPeak = juce::jmax (blockPeak, std::abs (sample));
                previous = sample;
                first = false;
            }

            if (b >= 10)
                quietestBlock = juce::jmin (quietestBlock, blockPeak);
        }

        return worstJump;
    };

    float steadyWavenet = 0.0f, steadyLstm = 0.0f;
    const auto baseline = juce::jmax (worstJumpOverRun ("wavenet.nam", false, steadyWavenet),
                                      worstJumpOverRun ("lstm.nam", false, steadyLstm));

    float quietestDuringSwap = 0.0f;
    const auto withSwap = worstJumpOverRun ("wavenet.nam", true, quietestDuringSwap);

    INFO ("baseline " << baseline << ", with swap " << withSwap);

    // The swap must not introduce a discontinuity larger than the models make on their own.
    REQUIRE (withSwap <= baseline * 1.1f);

    // And the fade must actually happen: some block during the swap is far quieter than either
    // model is on its own. Measured against the models rather than against a fixed number,
    // because the two are normalised to a common loudness and so arrive at their own levels —
    // an absolute threshold here would be testing how loud the captures are.
    const auto steady = juce::jmax (steadyWavenet, steadyLstm);

    INFO ("steady " << steady << ", quietest during the swap " << quietestDuringSwap);
    REQUIRE (quietestDuringSwap < 0.35f * steady);
}

TEST_CASE ("A second model is refused until the first has been collected", "[model]")
{
    AmpModel ampModel;
    ampModel.prepare (48000.0, test::blockSize);

    REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel ("wavenet.nam"))));

    // Nothing has consumed the first one yet, so the slot is still occupied.
    REQUIRE_FALSE (ampModel.setPendingModel (loadFor (ampModel, exampleModel ("lstm.nam"))));

    runBlocks (ampModel, 8);
    ampModel.collectRetiredModel();

    REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel ("lstm.nam"))));
}

namespace
{
    /** Drives the plugin's audio thread until the background load has been swapped in, or the
        wait runs out. No message loop is involved: everything the test asserts on is readable
        without one, which is also what a host's audio thread relies on. */
    bool pumpUntilModelLoaded (AmpSimAudioProcessor& processor, int timeoutMs = 10000)
    {
        juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), test::blockSize);
        juce::MidiBuffer midi;

        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

        while (juce::Time::getMillisecondCounter() < deadline)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);

            if (processor.isModelLoaded())
                return true;

            juce::Thread::sleep (1);
        }

        return false;
    }
}

TEST_CASE ("The processor loads a model off the audio thread", "[model][processor]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    processor.loadModel (exampleModel ("wavenet.nam"));

    REQUIRE (pumpUntilModelLoaded (processor));
    REQUIRE (processor.getModelError().isEmpty());
    REQUIRE (processor.getModelFile().getFileName() == "wavenet.nam");
}

TEST_CASE ("A missing model file reports an error instead of loading", "[model][processor]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    processor.loadModel (juce::File ("/nowhere/at/all/missing.nam"));

    const auto deadline = juce::Time::getMillisecondCounter() + 5000;

    while (processor.getModelError().isEmpty() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep (5);

    REQUIRE (processor.getModelError().isNotEmpty());
    REQUIRE_FALSE (processor.isModelLoaded());
}

TEST_CASE ("The model path is saved with the state and reloaded", "[model][processor][state]")
{
    juce::MemoryBlock saved;

    {
        AmpSimAudioProcessor source;
        source.prepareToPlay (test::sampleRate, test::blockSize);
        source.loadModel (exampleModel ("lstm.nam"));

        REQUIRE (pumpUntilModelLoaded (source));

        source.getStateInformation (saved);
    }

    AmpSimAudioProcessor restored;
    restored.prepareToPlay (test::sampleRate, test::blockSize);

    REQUIRE_FALSE (restored.isModelLoaded());

    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (pumpUntilModelLoaded (restored));
    REQUIRE (restored.getModelFile().getFileName() == "lstm.nam");
}

TEST_CASE ("Gain drives the model into saturation rather than just raising the level", "[model][processor]")
{
    // The Gain knob's whole claim: it sits before the model, so more level in means more
    // saturation out. A snapshot capture of a cranked amp compresses hard, so a 12 dB boost at
    // the input must come out as far less than 12 dB at the output.
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (48000.0, test::blockSize);
    processor.loadModel (exampleModel ("wavenet_a1_standard.nam"));

    REQUIRE (pumpUntilModelLoaded (processor));

    juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), test::blockSize);
    juce::MidiBuffer midi;

    const auto outputRmsAtGain = [&] (float gainDb)
    {
        test::setParam (processor.getValueTreeState(), ParamID::inputGain, gainDb);

        double phase = 0.0;
        const auto step = juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
        double sumSquares = 0.0;
        int counted = 0;

        for (int b = 0; b < 60; ++b)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < test::blockSize; ++i)
                    buffer.setSample (ch, i, 0.05f * (float) std::sin (phase + step * i));

            phase += step * test::blockSize;
            processor.processBlock (buffer, midi);

            if (b >= 40)   // let the gain ramp and the model settle
            {
                for (int i = 0; i < test::blockSize; ++i)
                {
                    const auto sample = (double) buffer.getSample (0, i);
                    sumSquares += sample * sample;
                    ++counted;
                }
            }
        }

        return std::sqrt (sumSquares / counted);
    };

    const auto quiet = outputRmsAtGain (0.0f);
    const auto loud  = outputRmsAtGain (12.0f);

    const auto ratio = loud / quiet;
    const auto linearRatio = std::pow (10.0, 12.0 / 20.0);   // 3.98 if nothing saturated

    INFO ("output ratio " << ratio << " for a linear " << linearRatio);

    REQUIRE (ratio > 1.0);                  // the knob does something
    REQUIRE (ratio < linearRatio * 0.5);    // and that something is saturation, not volume
}

TEST_CASE ("A model prepared before the host settings were known is not swapped in", "[model]")
{
    // The loader reads the sample rate and block size when it runs. At startup a session restores
    // a model path before the audio device is open, so a load can be prepared while those are
    // still zero and published after prepareToPlay has set them. Its FIFOs are then sized for
    // nothing, and a real block processed through it writes past the end of them.
    AmpModel ampModel;

    // Prepared while nothing is known — exactly what the loader thread captures at startup.
    auto dsp = nam::get_dsp (std::filesystem::path (exampleModel ("wavenet.nam")
                                                        .getFullPathName().toStdString()));
    REQUIRE (dsp != nullptr);

    auto staleModel = ampModel.prepareForLoading (std::move (dsp));
    REQUIRE (staleModel != nullptr);

    // The device opens afterwards.
    ampModel.prepare (44100.0, test::blockSize);

    // And only now does the load publish itself.
    REQUIRE (ampModel.setPendingModel (std::move (staleModel)));

    std::vector<float> block ((size_t) test::blockSize);

    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < test::blockSize; ++i)
            block[(size_t) i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi
                                                 * 110.0f * (float) i / 44100.0f);

        ampModel.process (block.data(), test::blockSize);

        for (int i = 0; i < test::blockSize; ++i)
            REQUIRE (std::isfinite (block[(size_t) i]));
    }

    // It must not have been taken up as it was...
    REQUIRE_FALSE (ampModel.hasModel());

    // ...but the message thread re-sizes it, and then it plays.
    REQUIRE (ampModel.repreparePendingModelIfNeeded());

    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < test::blockSize; ++i)
            block[(size_t) i] = 0.2f * std::sin (juce::MathConstants<float>::twoPi
                                                 * 110.0f * (float) i / 44100.0f);

        ampModel.process (block.data(), test::blockSize);
    }

    REQUIRE (ampModel.hasModel());
}

TEST_CASE ("A capture is brought to a common loudness", "[model]")
{
    // Captures are not made to a common level, and the file says how loud it is precisely so that
    // a host need not make the player re-set Master by ear on every swap. The two example models
    // are 17.8 dB apart, which is the whole problem in one directory.
    const auto normalisationFor = [] (const juce::String& name)
    {
        AmpModel ampModel;
        ampModel.prepare (48000.0, test::blockSize);

        REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel (name))));
        runBlocks (ampModel, 8);

        return ampModel.getNormalisationDb();
    };

    // Loudness -20.02 dB and -37.84 dB respectively, against a reference of -18 dB.
    REQUIRE_THAT (normalisationFor ("wavenet.nam"),
                  WithinAbs (AmpModel::referenceLoudnessDb + 20.020729, 0.01));
    REQUIRE_THAT (normalisationFor ("lstm.nam"),
                  WithinAbs (AmpModel::referenceLoudnessDb + 37.840687, 0.01));

    // The gap between them, which is what a player would otherwise meet as a step in level.
    REQUIRE (std::abs (normalisationFor ("wavenet.nam") - normalisationFor ("lstm.nam")) > 17.0f);
}

TEST_CASE ("A capture that does not say how loud it is, is left alone", "[model]")
{
    // Guessing would be worse than doing nothing: a wrong correction is a surprise the player
    // cannot see, where an uncorrected model is merely the old behaviour.
    AmpModel ampModel;
    ampModel.prepare (48000.0, test::blockSize);

    REQUIRE (ampModel.setPendingModel (loadFor (ampModel, exampleModel ("my_model.nam"))));
    runBlocks (ampModel, 8);

    REQUIRE_THAT (ampModel.getNormalisationDb(), WithinAbs (0.0, 1.0e-6));
}
