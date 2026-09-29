/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PresetManager.h"
#include "PluginProcessor.h"

namespace
{
    /** The built-in presets: parameter values only, so loading one keeps your amp and cab. */
    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<FactoryPreset> factoryPresets
    {
        { "Clean start",
          { { ParamID::inputGain, -6.0f }, { ParamID::bass, 1.0f }, { ParamID::mid, 0.0f },
            { ParamID::treble, 2.0f }, { ParamID::outputGain, 0.0f } } },

        { "Crunch",
          { { ParamID::inputGain, 6.0f }, { ParamID::bass, 2.0f }, { ParamID::mid, 3.0f },
            { ParamID::treble, 1.0f }, { ParamID::outputGain, -2.0f },
            { ParamID::driveOn, 1.0f }, { ParamID::driveAmount, 0.35f }, { ParamID::driveTone, 0.6f } } },

        { "Lead",
          { { ParamID::inputGain, 14.0f }, { ParamID::bass, -1.0f }, { ParamID::mid, 5.0f },
            { ParamID::treble, 2.0f }, { ParamID::outputGain, -4.0f },
            { ParamID::gateOn, 1.0f }, { ParamID::gateThreshold, -52.0f },
            { ParamID::delayOn, 1.0f }, { ParamID::delayTime, 0.42f },
            { ParamID::delayFeedback, 0.3f }, { ParamID::delayMix, 0.22f } } },

        { "Ambient",
          { { ParamID::inputGain, -2.0f }, { ParamID::bass, 3.0f }, { ParamID::mid, -3.0f },
            { ParamID::treble, 3.0f }, { ParamID::outputGain, -2.0f },
            { ParamID::compOn, 1.0f }, { ParamID::compAmount, 0.6f }, { ParamID::compLevel, 4.0f },
            { ParamID::chorusOn, 1.0f }, { ParamID::chorusRate, 0.7f }, { ParamID::chorusDepth, 0.5f },
            { ParamID::reverbOn, 1.0f }, { ParamID::reverbSize, 0.75f }, { ParamID::reverbMix, 0.35f } } },
    };
}

PresetManager::PresetManager (AmpSimAudioProcessor& processorToUse, juce::File directoryToUse)
    : processor (processorToUse), directory (directoryToUse)
{
    if (directory == juce::File())
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        // JUCE's userApplicationDataDirectory is ~/Library on macOS, not ~/Library/Application
        // Support. Everything an app stores for itself belongs in the latter.
        base = base.getChildFile ("Application Support");
       #endif

        directory = base.getChildFile ("AmpSim").getChildFile ("Presets");
    }

    directory.createDirectory();
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    return directory.getChildFile (juce::File::createLegalFileName (name) + fileExtension);
}

juce::StringArray PresetManager::getNames() const
{
    juce::StringArray names;

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false,
                                                      juce::String ("*") + fileExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

juce::String PresetManager::save (const juce::String& name)
{
    if (name.trim().isEmpty())
        return "A preset needs a name.";

    const auto xml = processor.getPresetState().createXml();

    if (xml == nullptr)
        return "Could not read the plugin's state.";

    if (! fileFor (name).replaceWithText (xml->toString()))
        return "Could not write to " + directory.getFullPathName();

    processor.setCurrentPresetName (name);

    if (onChanged != nullptr)
        onChanged();

    return {};
}

juce::String PresetManager::load (const juce::String& name)
{
    const auto file = fileFor (name);

    if (! file.existsAsFile())
        return "No preset called " + name;

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return name + " could not be read.";

    processor.applyPresetState (juce::ValueTree::fromXml (*xml));
    processor.setCurrentPresetName (name);

    if (onChanged != nullptr)
        onChanged();

    return {};
}

juce::String PresetManager::remove (const juce::String& name)
{
    if (! fileFor (name).deleteFile())
        return "Could not delete " + name;

    if (processor.getCurrentPresetName() == name)
        processor.setCurrentPresetName ({});

    if (onChanged != nullptr)
        onChanged();

    return {};
}

void PresetManager::step (int delta)
{
    const auto names = getNames();

    if (names.isEmpty())
        return;

    const auto current = names.indexOf (processor.getCurrentPresetName());

    // Nothing loaded yet: stepping forwards starts at the first, backwards at the last.
    const auto next = current < 0 ? (delta > 0 ? 0 : names.size() - 1)
                                  : (current + delta + names.size()) % names.size();

    load (names[next]);
}

juce::String PresetManager::getCurrentName() const
{
    return processor.getCurrentPresetName();
}

void PresetManager::createFactoryPresetsIfMissing()
{
    for (const auto& preset : factoryPresets)
    {
        const auto file = fileFor (preset.name);

        if (file.existsAsFile())
            continue;

        // Built from a default state, so nothing of the current session leaks into them.
        AmpSimAudioProcessor blank;
        auto& state = blank.getValueTreeState();

        for (const auto& [id, value] : preset.values)
            if (auto* parameter = state.getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));

        if (const auto xml = blank.getPresetState().createXml())
            file.replaceWithText (xml->toString());
    }

    if (onChanged != nullptr)
        onChanged();
}
