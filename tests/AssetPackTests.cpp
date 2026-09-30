/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "AssetPack.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::MemoryBlock blockOf (const juce::String& text)
    {
        return { text.toRawUTF8(), (size_t) text.getNumBytesAsUTF8() };
    }
}

TEST_CASE ("A packed asset comes back byte for byte", "[assetpack]")
{
    juce::Random random (0x1234);
    juce::MemoryBlock original (64 * 1024);

    for (size_t i = 0; i < original.getSize(); ++i)
        original[i] = (char) random.nextInt (256);

    const auto packed = AssetPack::pack (original, "1986 Something 2204.nam");
    const auto result = AssetPack::unpack (packed.getData(), packed.getSize());

    REQUIRE (result.valid);
    REQUIRE (result.originalName == "1986 Something 2204.nam");
    REQUIRE (result.data.getSize() == original.getSize());
    REQUIRE (result.data == original);
}

TEST_CASE ("A packed asset does not carry its contents in the clear", "[assetpack]")
{
    // The point of the format: what lands in the repository should not be a playable file with a
    // header anyone can spot, nor should it carry the original name where a search would find it.
    const auto original = blockOf ("{\"architecture\": \"WaveNet\", \"weights\": [0.1, 0.2]}");
    const auto packed = AssetPack::pack (original, "1986 JCM 800 2204.nam");

    const juce::String asText ((const char*) packed.getData(), packed.getSize());

    REQUIRE_FALSE (asText.contains ("WaveNet"));
    REQUIRE_FALSE (asText.contains ("architecture"));
    REQUIRE_FALSE (asText.contains ("JCM"));
}

TEST_CASE ("Anything that is not one of ours is refused", "[assetpack]")
{
    SECTION ("a plain file")
    {
        const auto junk = blockOf ("RIFF....WAVEfmt ");
        REQUIRE_FALSE (AssetPack::unpack (junk.getData(), junk.getSize()).valid);
    }

    SECTION ("something far too short")
    {
        const auto tiny = blockOf ("AMP");
        REQUIRE_FALSE (AssetPack::unpack (tiny.getData(), tiny.getSize()).valid);
    }

    SECTION ("a pack with a corrupted payload")
    {
        auto packed = AssetPack::pack (blockOf ("the original contents"), "thing.nam");

        // Flip a byte well inside the payload.
        auto* bytes = static_cast<juce::uint8*> (packed.getData());
        bytes[packed.getSize() - 4] = (juce::uint8) (bytes[packed.getSize() - 4] ^ 0xff);

        REQUIRE_FALSE (AssetPack::unpack (packed.getData(), packed.getSize()).valid);
    }
}

TEST_CASE ("Packing is deterministic", "[assetpack]")
{
    // So that re-packing an unchanged asset does not show up as a change in the repository.
    const auto original = blockOf ("some impulse response bytes");

    const auto first = AssetPack::pack (original, "cab.wav");
    const auto second = AssetPack::pack (original, "cab.wav");

    REQUIRE (first == second);
}

TEST_CASE ("A new instance comes with the built-in amp and cab loaded", "[assetpack][processor]")
{
    // Unpacked on first run and loaded, so the plugin makes a sound the moment it is opened
    // rather than passing audio through untouched.
    struct EnableBundledAssets
    {
        EnableBundledAssets()  { AmpSimAudioProcessor::loadBundledAssetsOnCreation = true; }
        ~EnableBundledAssets() { AmpSimAudioProcessor::loadBundledAssetsOnCreation = false; }
    } enabled;

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (48000.0, test::blockSize);

    REQUIRE (processor.getModelFile().getFileName() == "MARS2204.nam");
    REQUIRE (processor.getImpulseResponseFile (CabSim::Slot::centreClose).getFileName() == "V30 SM57.wav");
    REQUIRE (processor.getModelError().isEmpty());
    REQUIRE (processor.getImpulseResponseError().isEmpty());

    // And the unpacked model really is a model: it loads and runs.
    juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), test::blockSize);
    juce::MidiBuffer midi;

    const auto deadline = juce::Time::getMillisecondCounter() + 20000;

    while (! processor.isModelLoaded() && juce::Time::getMillisecondCounter() < deadline)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        juce::Thread::sleep (2);
    }

    REQUIRE (processor.isModelLoaded());
}
