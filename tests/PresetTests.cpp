/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "PresetManager.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    /** Presets go in a temporary directory, never the user's own. */
    struct TempPresetDirectory
    {
        TempPresetDirectory()
            : directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("ampsim_presets_"
                                            + juce::String (juce::Random::getSystemRandom().nextInt())))
        {
            directory.createDirectory();
        }

        ~TempPresetDirectory() { directory.deleteRecursively(); }

        juce::File directory;
    };
}

TEST_CASE ("A preset round-trips the controls", "[preset]")
{
    TempPresetDirectory temp;

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);
    PresetManager presets (processor, temp.directory);

    auto& state = processor.getValueTreeState();

    test::setParam (state, ParamID::inputGain, 11.0f);
    test::setParam (state, ParamID::mid, -7.5f);
    test::setParam (state, ParamID::driveOn, 1.0f);

    REQUIRE (presets.save ("Test tone").isEmpty());

    test::setParam (state, ParamID::inputGain, -20.0f);
    test::setParam (state, ParamID::mid, 0.0f);
    test::setParam (state, ParamID::driveOn, 0.0f);

    REQUIRE (presets.load ("Test tone").isEmpty());

    REQUIRE_THAT (test::getParam (state, ParamID::inputGain), WithinAbs (11.0, 0.05));
    REQUIRE_THAT (test::getParam (state, ParamID::mid), WithinAbs (-7.5, 0.05));
    REQUIRE (state.getParameter (ParamID::driveOn)->getValue() > 0.5f);
    REQUIRE (presets.getCurrentName() == "Test tone");
}

TEST_CASE ("A preset with no model of its own keeps the one already loaded", "[preset]")
{
    // The point of the rule: a preset saved on another machine cannot know where your models
    // live, and a preset that only sets the controls should not unload your amp.
    TempPresetDirectory temp;

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);
    PresetManager presets (processor, temp.directory);

    // Saved with nothing loaded.
    REQUIRE (presets.save ("Knobs only").isEmpty());

    const auto model = juce::File (AMPSIM_TEST_MODEL_DIR).getChildFile ("wavenet.nam");
    processor.loadModel (model);

    REQUIRE (presets.load ("Knobs only").isEmpty());
    REQUIRE (processor.getModelFile() == model);
}

TEST_CASE ("A preset that names a model loads it", "[preset]")
{
    TempPresetDirectory temp;

    const auto model = juce::File (AMPSIM_TEST_MODEL_DIR).getChildFile ("lstm.nam");

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);
    PresetManager presets (processor, temp.directory);

    processor.loadModel (model);
    REQUIRE (presets.save ("With an amp").isEmpty());

    processor.loadModel (juce::File (AMPSIM_TEST_MODEL_DIR).getChildFile ("wavenet.nam"));
    REQUIRE (presets.load ("With an amp").isEmpty());

    REQUIRE (processor.getModelFile() == model);
}

TEST_CASE ("Presets are listed and stepped through in order", "[preset]")
{
    TempPresetDirectory temp;

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);
    PresetManager presets (processor, temp.directory);

    auto& state = processor.getValueTreeState();

    for (const auto& entry : { std::pair { "Alpha", 3.0f }, std::pair { "Bravo", 6.0f },
                               std::pair { "Charlie", 9.0f } })
    {
        test::setParam (state, ParamID::inputGain, entry.second);
        REQUIRE (presets.save (entry.first).isEmpty());
    }

    REQUIRE (presets.getNames() == juce::StringArray { "Alpha", "Bravo", "Charlie" });

    presets.load ("Alpha");
    presets.step (1);
    REQUIRE (presets.getCurrentName() == "Bravo");
    REQUIRE_THAT (test::getParam (state, ParamID::inputGain), WithinAbs (6.0, 0.05));

    presets.step (-1);
    REQUIRE (presets.getCurrentName() == "Alpha");

    // Wrapping, in both directions.
    presets.step (-1);
    REQUIRE (presets.getCurrentName() == "Charlie");
    presets.step (1);
    REQUIRE (presets.getCurrentName() == "Alpha");
}

TEST_CASE ("A preset can be deleted", "[preset]")
{
    TempPresetDirectory temp;

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);
    PresetManager presets (processor, temp.directory);

    REQUIRE (presets.save ("Temporary").isEmpty());
    REQUIRE (presets.getNames().contains ("Temporary"));

    REQUIRE (presets.remove ("Temporary").isEmpty());
    REQUIRE_FALSE (presets.getNames().contains ("Temporary"));
    REQUIRE (presets.getCurrentName().isEmpty());
}

TEST_CASE ("Loading a preset that is not there reports it", "[preset]")
{
    TempPresetDirectory temp;

    AmpSimAudioProcessor processor;
    PresetManager presets (processor, temp.directory);

    REQUIRE (presets.load ("Nothing here").isNotEmpty());
    REQUIRE (presets.save ("   ").isNotEmpty());
}

TEST_CASE ("The built-in preset is written once and not overwritten afterwards", "[preset]")
{
    TempPresetDirectory temp;

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);
    PresetManager presets (processor, temp.directory);

    presets.createFactoryPresetsIfMissing();

    // One built-in preset, and only one: the rest of the list belongs to whoever uses this.
    REQUIRE (presets.getNames() == juce::StringArray { "Default" });

    // Edited and saved over, it must survive the next run.
    test::setParam (processor.getValueTreeState(), ParamID::inputGain, 20.0f);
    REQUIRE (presets.save ("Default").isEmpty());

    presets.createFactoryPresetsIfMissing();
    REQUIRE (presets.load ("Default").isEmpty());
    REQUIRE_THAT (test::getParam (processor.getValueTreeState(), ParamID::inputGain),
                  WithinAbs (20.0, 0.05));
}

TEST_CASE ("The current preset name survives a session save", "[preset][state]")
{
    TempPresetDirectory temp;

    juce::MemoryBlock saved;

    {
        AmpSimAudioProcessor source;
        source.prepareToPlay (test::sampleRate, test::blockSize);
        PresetManager presets (source, temp.directory);

        REQUIRE (presets.save ("Remembered").isEmpty());
        source.getStateInformation (saved);
    }

    AmpSimAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (restored.getCurrentPresetName() == "Remembered");
}
