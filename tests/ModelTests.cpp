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

    float ignored = 0.0f;
    const auto baseline = juce::jmax (worstJumpOverRun ("wavenet.nam", false, ignored),
                                      worstJumpOverRun ("lstm.nam", false, ignored));

    float quietestDuringSwap = 0.0f;
    const auto withSwap = worstJumpOverRun ("wavenet.nam", true, quietestDuringSwap);

    INFO ("baseline " << baseline << ", with swap " << withSwap);

    // The swap must not introduce a discontinuity larger than the models make on their own.
    REQUIRE (withSwap <= baseline * 1.1f);

    // And the fade must actually happen: some block during the swap is near silent.
    REQUIRE (quietestDuringSwap < 0.02f);
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
