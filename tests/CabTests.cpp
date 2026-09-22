#include "TestHelpers.h"
#include "dsp/CabSim.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    /** A .wav impulse response written to a temporary file, deleted with the object.

        Generated rather than committed: the tests need to know exactly what is in it, and a
        binary fixture in the repo would only hide that.
    */
    struct TestIR
    {
        explicit TestIR (const std::vector<float>& taps, double sampleRate = 48000.0)
            : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("ampsim_test_ir_" + juce::String (juce::Random::getSystemRandom().nextInt()) + ".wav"))
        {
            juce::AudioBuffer<float> buffer (1, (int) taps.size());

            for (int i = 0; i < (int) taps.size(); ++i)
                buffer.setSample (0, i, taps[(size_t) i]);

            juce::WavAudioFormat wav;

            const auto options = juce::AudioFormatWriterOptions()
                                     .withSampleRate (sampleRate)
                                     .withNumChannels (1)
                                     .withBitsPerSample (32);

            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            const auto writer = wav.createWriterFor (stream, options);

            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        ~TestIR() { file.deleteFile(); }

        juce::File file;
    };

    /** Runs blocks until the convolution has swapped the new IR in, then returns the impulse
        response the cab actually produces. */
    std::vector<float> measureImpulseResponse (CabSim& cab, int numSamples = 16)
    {
        std::vector<float> block ((size_t) test::blockSize, 0.0f);

        // The convolution loads on its own thread and crossfades the new engine in.
        for (int b = 0; b < 40; ++b)
        {
            std::fill (block.begin(), block.end(), 0.0f);
            cab.process (block.data(), test::blockSize, false);
            juce::Thread::sleep (2);
        }

        std::fill (block.begin(), block.end(), 0.0f);
        block[0] = 1.0f;
        cab.process (block.data(), test::blockSize, false);

        return { block.begin(), block.begin() + numSamples };
    }
}

TEST_CASE ("A cab with no IR leaves the signal alone", "[cab]")
{
    CabSim cab;
    cab.prepare (test::sampleRate, test::blockSize);

    std::vector<float> block ((size_t) test::blockSize, 0.25f);
    cab.process (block.data(), test::blockSize, false);

    REQUIRE_FALSE (cab.hasImpulseResponse());
    REQUIRE_THAT (block[0], WithinAbs (0.25, 1.0e-9));
    REQUIRE_THAT (block[(size_t) test::blockSize - 1], WithinAbs (0.25, 1.0e-9));
}

TEST_CASE ("An impulse through the cab comes back as the impulse response", "[cab]")
{
    const std::vector<float> taps { 0.5f, 0.0f, 0.25f, -0.125f };
    const TestIR ir (taps);

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (ir.file);

    REQUIRE (cab.hasImpulseResponse());

    const auto measured = measureImpulseResponse (cab, (int) taps.size() + 2);

    for (size_t i = 0; i < taps.size(); ++i)
    {
        INFO ("tap " << i);
        REQUIRE_THAT (measured[i], WithinAbs (taps[i], 1.0e-4));
    }

    // Nothing beyond the IR's own length.
    REQUIRE_THAT (measured[taps.size()], WithinAbs (0.0, 1.0e-4));
}

TEST_CASE ("The cab adds no latency", "[cab]")
{
    const TestIR ir ({ 0.5f, 0.25f });

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (ir.file);

    measureImpulseResponse (cab);

    REQUIRE (cab.getLatencySamples() == 0);
}

TEST_CASE ("Cab bypass returns the dry signal", "[cab]")
{
    const TestIR ir ({ 0.5f, 0.25f });

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (ir.file);
    measureImpulseResponse (cab);

    std::vector<float> block ((size_t) test::blockSize, 0.25f);

    // Let the bypass ramp settle.
    for (int b = 0; b < 8; ++b)
    {
        std::fill (block.begin(), block.end(), 0.25f);
        cab.process (block.data(), test::blockSize, true);
    }

    REQUIRE_THAT (block[0], WithinAbs (0.25, 1.0e-6));
}

TEST_CASE ("Toggling cab bypass crossfades rather than stepping", "[cab]")
{
    // A dull IR, so bypassed and engaged sound very different and a hard switch would be obvious.
    std::vector<float> taps ((size_t) 64, 0.0f);
    for (size_t i = 0; i < taps.size(); ++i)
        taps[i] = 0.4f * std::exp (-(float) i / 12.0f);

    const TestIR ir (taps);

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (ir.file);
    measureImpulseResponse (cab);

    std::vector<float> block ((size_t) test::blockSize);
    float worstJump = 0.0f, previous = 0.0f;
    bool first = true;

    for (int b = 0; b < 30; ++b)
    {
        const auto bypassed = b >= 10 && b < 20;

        for (int i = 0; i < test::blockSize; ++i)
            block[(size_t) i] = 0.3f * std::sin (juce::MathConstants<float>::twoPi
                                                 * 100.0f * (float) (b * test::blockSize + i) / 48000.0f);

        cab.process (block.data(), test::blockSize, bypassed);

        for (int i = 0; i < test::blockSize; ++i)
        {
            if (! first)
                worstJump = juce::jmax (worstJump, std::abs (block[(size_t) i] - previous));

            previous = block[(size_t) i];
            first = false;
        }
    }

    // A 100 Hz sine at 48 kHz moves ~0.004 per sample; switching a dull IR in and out without a
    // ramp would jump by an order of magnitude more.
    REQUIRE (worstJump < 0.02f);
}

TEST_CASE ("The IR path is saved with the state and reloaded", "[cab][processor][state]")
{
    const TestIR ir ({ 0.5f, 0.25f });

    juce::MemoryBlock saved;

    {
        AmpSimAudioProcessor source;
        source.prepareToPlay (test::sampleRate, test::blockSize);
        source.loadImpulseResponse (ir.file);

        REQUIRE (source.isImpulseResponseLoaded());
        REQUIRE (source.getImpulseResponseError().isEmpty());

        source.getStateInformation (saved);
    }

    AmpSimAudioProcessor restored;
    restored.prepareToPlay (test::sampleRate, test::blockSize);

    REQUIRE_FALSE (restored.isImpulseResponseLoaded());

    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (restored.isImpulseResponseLoaded());
    REQUIRE (restored.getImpulseResponseFile() == ir.file);
}

TEST_CASE ("A file that is not audio is reported rather than silently ignored", "[cab][processor]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    const auto junk = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("ampsim_not_audio.txt");
    junk.replaceWithText ("this is not a wav file");

    processor.loadImpulseResponse (junk);

    REQUIRE (processor.getImpulseResponseError().isNotEmpty());
    REQUIRE_FALSE (processor.isImpulseResponseLoaded());

    junk.deleteFile();
}
