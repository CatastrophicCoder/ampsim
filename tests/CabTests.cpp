/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

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

    std::vector<float> impulseThrough (CabSim& cab, int numSamples)
    {
        std::vector<float> block ((size_t) test::blockSize, 0.0f);
        block[0] = 1.0f;
        cab.process (block.data(), test::blockSize, false);

        return { block.begin(), block.begin() + numSamples };
    }

    /** The impulse response the cab actually produces, once the IR being loaded has arrived.

        Two things have to finish, in different domains. The file is read on the convolution's own
        thread, which takes wall-clock time — JUCE reports a one-sample default engine until it
        lands, so that is what to poll for. The new engine is then crossfaded in over about 50 ms
        of *processed samples*, which is deterministic and just needs blocks pushed through.

        Waiting on a fixed duration instead passes on an idle machine and fails on a busy one.
    */
    std::vector<float> measureImpulseResponse (CabSim& cab, int numSamples = 16)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;

        const auto everySlotArrived = [&cab]
        {
            for (int slot = 0; slot < CabSim::numSlots; ++slot)
                if (cab.isSlotLoaded ((CabSim::Slot) slot)
                    && cab.getLoadedSize ((CabSim::Slot) slot) <= 1)
                    return false;

            return true;
        };

        std::vector<float> silence ((size_t) test::blockSize, 0.0f);

        while (! everySlotArrived() && juce::Time::getMillisecondCounter() < deadline)
        {
            std::fill (silence.begin(), silence.end(), 0.0f);
            cab.process (silence.data(), test::blockSize, false);
            juce::Thread::sleep (2);
        }

        // A timeout here means an IR never arrived — which a one-tap fixture also looks like.
        // Fail on it rather than quietly measuring JUCE's default engine.
        REQUIRE (everySlotArrived());

        // The crossfade is measured in samples, not seconds: 12 blocks is well past its 50 ms.
        for (int b = 0; b < 12; ++b)
        {
            std::fill (silence.begin(), silence.end(), 0.0f);
            cab.process (silence.data(), test::blockSize, false);
        }

        return impulseThrough (cab, numSamples);
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
    cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);

    REQUIRE (cab.hasImpulseResponse());

    const auto measured = measureImpulseResponse (cab, (int) taps.size() + 2);

    // Shape, not level: the cab normalises, so what comes back is the impulse response scaled by
    // one factor. Each tap must keep its proportion to the first.
    REQUIRE (std::abs (measured[0]) > 1.0e-4f);

    for (size_t i = 1; i < taps.size(); ++i)
    {
        INFO ("tap " << i);
        REQUIRE_THAT (measured[i] / measured[0], WithinAbs (taps[i] / taps[0], 1.0e-3));
    }

    // Nothing beyond the IR's own length.
    REQUIRE_THAT (measured[taps.size()] / measured[0], WithinAbs (0.0, 1.0e-3));
}

TEST_CASE ("The cab adds no latency", "[cab]")
{
    const TestIR ir ({ 0.5f, 0.25f });

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);

    measureImpulseResponse (cab);

    REQUIRE (cab.getLatencySamples() == 0);
}

TEST_CASE ("Cab bypass returns the dry signal", "[cab]")
{
    const TestIR ir ({ 0.5f, 0.25f });

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);
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
    cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);
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
        source.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);

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

    processor.loadImpulseResponse (CabSim::Slot::centreClose, junk);

    REQUIRE (processor.getImpulseResponseError().isNotEmpty());
    REQUIRE_FALSE (processor.isImpulseResponseLoaded());

    junk.deleteFile();
}


TEST_CASE ("A single IR behaves the same however the mic position is set", "[cab]")
{
    // With one corner filled there is nothing to interpolate between, so the knobs must do
    // nothing rather than fade the cab away as they cross an empty corner.
    const std::vector<float> taps { 0.5f, 0.0f, 0.25f, -0.125f };
    const TestIR ir (taps);

    std::vector<float> reference;

    for (const auto position : { std::pair { 0.0f, 0.0f }, std::pair { 1.0f, 0.0f },
                                 std::pair { 0.5f, 0.5f }, std::pair { 1.0f, 1.0f } })
    {
        CabSim cab;
        cab.prepare (48000.0, test::blockSize);
        cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);
        cab.setMicPosition (position.first, position.second);

        const auto measured = measureImpulseResponse (cab, (int) taps.size());

        INFO ("axis " << position.first << ", distance " << position.second);

        if (reference.empty())
        {
            reference = measured;
            REQUIRE (std::abs (reference[0]) > 1.0e-4f);
            continue;
        }

        for (size_t i = 0; i < taps.size(); ++i)
            REQUIRE_THAT (measured[i], WithinAbs (reference[i], 1.0e-4));
    }
}

TEST_CASE ("The mic position blends between the corners it is between", "[cab]")
{
    // Two corners along the axis control, with deliberately different responses. Neither starts
    // with silence: Convolution's Trim::yes would strip it and shift the taps forward.
    // Two taps each, and neither leading with silence: a single tap after Trim::yes cannot be
    // told apart from the default engine JUCE installs, and leading silence would be stripped.
    const std::vector<float> centre { 0.5f, 0.1f, 0.0f, 0.0f };
    const std::vector<float> edge   { 0.2f, 0.3f, 0.0f, 0.0f };

    const TestIR centreIR (centre);
    const TestIR edgeIR (edge);

    const auto responseAt = [&] (float axis)
    {
        CabSim cab;
        cab.prepare (48000.0, test::blockSize);
        cab.loadImpulseResponse (CabSim::Slot::centreClose, centreIR.file);
        cab.loadImpulseResponse (CabSim::Slot::edgeClose, edgeIR.file);
        cab.setMicPosition (axis, 0.0f);

        return measureImpulseResponse (cab, 6);
    };

    const auto atCentre = responseAt (0.0f);
    const auto atEdge = responseAt (1.0f);
    const auto halfway = responseAt (0.5f);

    // Hard over, each corner is its own capture: different shapes, so different tap ratios.
    REQUIRE_THAT (atCentre[1] / atCentre[0], WithinAbs (0.1 / 0.5, 1.0e-3));
    REQUIRE_THAT (atEdge[1] / atEdge[0], WithinAbs (0.3 / 0.2, 1.0e-3));

    // Halfway is half of each: convolution is linear, so blending the outputs is the same as
    // convolving with the blended impulse response. True of the normalised output too, since one
    // factor is applied to the whole grid.
    REQUIRE_THAT (halfway[0], WithinAbs (0.5 * (atCentre[0] + atEdge[0]), 1.0e-3));
    REQUIRE_THAT (halfway[1], WithinAbs (0.5 * (atCentre[1] + atEdge[1]), 1.0e-3));
}

TEST_CASE ("An unfilled corner does not drop the level as the knob crosses it", "[cab]")
{
    // Three corners filled, the fourth empty: the weights are renormalised over what is there.
    const TestIR ir ({ 0.5f, 0.25f });

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);
    cab.loadImpulseResponse (CabSim::Slot::edgeClose, ir.file);
    cab.loadImpulseResponse (CabSim::Slot::centreFar, ir.file);
    // Slot::edgeFar deliberately left empty.

    cab.setMicPosition (0.0f, 0.0f);
    const auto atFilledCorner = measureImpulseResponse (cab, 4);

    cab.setMicPosition (1.0f, 1.0f);   // straight at the missing corner
    const auto atMissingCorner = measureImpulseResponse (cab, 4);

    REQUIRE (std::abs (atFilledCorner[0]) > 1.0e-4f);
    REQUIRE_THAT (atMissingCorner[0], WithinAbs (atFilledCorner[0], 1.0e-3));
}

TEST_CASE ("Clearing a corner leaves the rest of the grid working", "[cab]")
{
    const TestIR ir ({ 0.5f, 0.25f });

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);
    cab.loadImpulseResponse (CabSim::Slot::edgeClose, ir.file);

    REQUIRE (cab.isSlotLoaded (CabSim::Slot::edgeClose));

    cab.clearSlot (CabSim::Slot::edgeClose);

    REQUIRE_FALSE (cab.isSlotLoaded (CabSim::Slot::edgeClose));
    REQUIRE (cab.hasImpulseResponse());

    cab.setMicPosition (0.0f, 0.0f);
    const auto atRemaining = measureImpulseResponse (cab, 4);

    cab.setMicPosition (1.0f, 0.0f);   // pointing at the corner just cleared
    const auto atCleared = measureImpulseResponse (cab, 4);

    REQUIRE (std::abs (atRemaining[0]) > 1.0e-4f);
    REQUIRE_THAT (atCleared[0], WithinAbs (atRemaining[0], 1.0e-3));
}

TEST_CASE ("Every corner's IR path is saved with the state", "[cab][processor][state]")
{
    const TestIR centreClose ({ 0.5f, 0.0f });
    const TestIR edgeFar ({ 0.0f, 0.3f });

    juce::MemoryBlock saved;

    {
        AmpSimAudioProcessor source;
        source.prepareToPlay (test::sampleRate, test::blockSize);
        source.loadImpulseResponse (CabSim::Slot::centreClose, centreClose.file);
        source.loadImpulseResponse (CabSim::Slot::edgeFar, edgeFar.file);
        source.getStateInformation (saved);
    }

    AmpSimAudioProcessor restored;
    restored.prepareToPlay (test::sampleRate, test::blockSize);
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (restored.getImpulseResponseFile (CabSim::Slot::centreClose) == centreClose.file);
    REQUIRE (restored.getImpulseResponseFile (CabSim::Slot::edgeFar) == edgeFar.file);
    REQUIRE (restored.getImpulseResponseFile (CabSim::Slot::edgeClose) == juce::File());
}

TEST_CASE ("The cab's level does not depend on how hot the IR file is", "[cab]")
{
    // The promise of normalisation: a capture carrying 15 dB of broadband gain and the same
    // capture at a sane level should come out at the same loudness. Same shape, ten times the
    // scale — the output must not move.
    const TestIR quiet ({ 0.6f, 0.3f, -0.15f });
    const TestIR hot   ({ 6.0f, 3.0f, -1.5f });

    const auto responseFor = [] (const juce::File& file)
    {
        CabSim cab;
        cab.prepare (48000.0, test::blockSize);
        cab.loadImpulseResponse (CabSim::Slot::centreClose, file);

        return measureImpulseResponse (cab, 4);
    };

    const auto fromQuiet = responseFor (quiet.file);
    const auto fromHot = responseFor (hot.file);

    INFO ("quiet " << fromQuiet[0] << ", hot " << fromHot[0]);

    REQUIRE (std::abs (fromQuiet[0]) > 1.0e-4f);
    REQUIRE_THAT (fromHot[0], WithinAbs (fromQuiet[0], 1.0e-3));
}

TEST_CASE ("Normalisation brings a capture to about unity", "[cab]")
{
    // Not exact — the factor is an average across the guitar band, and a two-tap impulse response
    // is not flat — but a cab must not be a 20 dB gain stage.
    const TestIR ir ({ 4.0f, 2.0f, -1.0f });

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (CabSim::Slot::centreClose, ir.file);

    const auto measured = measureImpulseResponse (cab, 4);

    INFO ("correction " << cab.getNormalisationDb() << " dB, first tap " << measured[0]);

    REQUIRE (cab.getNormalisationDb() < -6.0f);          // it did tame something
    REQUIRE (std::abs (measured[0]) < 2.0f);             // and the result is not a gain stage
    REQUIRE (std::abs (measured[0]) > 0.05f);            // nor has it been flattened to nothing
}

TEST_CASE ("The corners keep their level relative to each other", "[cab]")
{
    // One factor for the whole grid, not one per corner: a corner that really is 6 dB quieter —
    // a mic backed off, or off axis — has to stay 6 dB quieter, or the mic position control is
    // reduced to an EQ.
    const std::vector<float> loud { 0.6f, 0.3f, -0.15f };
    std::vector<float> quiet;

    for (auto tap : loud)
        quiet.push_back (tap * 0.5f);   // exactly 6 dB down, same shape

    const TestIR loudIR (loud);
    const TestIR quietIR (quiet);

    CabSim cab;
    cab.prepare (48000.0, test::blockSize);
    cab.loadImpulseResponse (CabSim::Slot::centreClose, loudIR.file);
    cab.loadImpulseResponse (CabSim::Slot::edgeClose, quietIR.file);

    cab.setMicPosition (0.0f, 0.0f);
    const auto atLoudCorner = measureImpulseResponse (cab, 4);

    cab.setMicPosition (1.0f, 0.0f);
    const auto atQuietCorner = measureImpulseResponse (cab, 4);

    REQUIRE (std::abs (atLoudCorner[0]) > 1.0e-4f);

    const auto ratio = atQuietCorner[0] / atLoudCorner[0];

    INFO ("quiet corner is " << juce::Decibels::gainToDecibels (std::abs (ratio)) << " dB down");
    REQUIRE_THAT (ratio, WithinAbs (0.5, 0.02));
}

TEST_CASE ("The normalisation factor does not depend on the order corners are filled", "[cab]")
{
    const TestIR loudIR ({ 0.8f, 0.4f });
    const TestIR quietIR ({ 0.2f, 0.1f });

    const auto responseWithOrder = [&] (bool loudFirst)
    {
        CabSim cab;
        cab.prepare (48000.0, test::blockSize);

        if (loudFirst)
        {
            cab.loadImpulseResponse (CabSim::Slot::centreClose, loudIR.file);
            cab.loadImpulseResponse (CabSim::Slot::edgeClose, quietIR.file);
        }
        else
        {
            cab.loadImpulseResponse (CabSim::Slot::edgeClose, quietIR.file);
            cab.loadImpulseResponse (CabSim::Slot::centreClose, loudIR.file);
        }

        cab.setMicPosition (0.0f, 0.0f);
        return measureImpulseResponse (cab, 4);
    };

    const auto loudFirst = responseWithOrder (true);
    const auto quietFirst = responseWithOrder (false);

    REQUIRE (std::abs (loudFirst[0]) > 1.0e-4f);
    REQUIRE_THAT (quietFirst[0], WithinAbs (loudFirst[0], 1.0e-3));
}
