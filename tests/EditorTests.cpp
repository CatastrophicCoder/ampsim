/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "ui/ParameterSlider.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    void forEachKnob (juce::Component& parent, const std::function<void (ParameterSlider&)>& visit)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* slider = dynamic_cast<ParameterSlider*> (child))
                visit (*slider);

            forEachKnob (*child, visit);
        }
    }
}

TEST_CASE ("Every knob returns to its parameter's default on a double-click", "[editor]")
{
    auto processor = test::makePreparedProcessor();

    // The editor is a Component, so it needs the message thread that TestMain's
    // ScopedJuceInitialiser_GUI provides; it is deleted before the processor goes.
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    REQUIRE (editor != nullptr);

    auto& state = processor->getValueTreeState();
    int knobs = 0;

    forEachKnob (*editor, [&] (ParameterSlider& slider)
    {
        ++knobs;

        auto* parameter = state.getParameter (slider.getParameterID());
        REQUIRE (parameter != nullptr);

        INFO ("parameter: " << slider.getParameterID());
        REQUIRE (slider.isDoubleClickReturnEnabled());

        // Stated in normalised terms, which is what the host compares against: asserting the value
        // in the parameter's own units would only repeat the arithmetic the knob already does.
        REQUIRE_THAT (parameter->convertTo0to1 ((float) slider.getDoubleClickReturnValue()),
                      WithinAbs (parameter->getDefaultValue(), 1.0e-6));
    });

    // Seven on the amp, fifteen across the six pedals, four on the cab, and the transpose interval
    // in the bottom bar. A page that stopped being
    // built would otherwise pass this test by having nothing to check.
    REQUIRE (knobs == 29);
}

TEST_CASE ("A knob offers MIDI learn only where MIDI can arrive", "[editor][midi]")
{
    // Only the standalone receives MIDI: the plug-ins declare no MIDI input, because declaring one
    // made the Audio Unit a MIDI-controlled effect that Logic fed from a side chain. A "learn"
    // offered in a plug-in would wait for a controller it could never hear, so it is not offered.
    //
    // The wrapper type is fixed when the processor is constructed, from a value JUCE's wrappers set
    // just before they make one. Setting it here is how each wrapper is impersonated.
    const auto menuItemsAs = [] (juce::AudioProcessor::WrapperType type)
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (type);
        auto processor = std::make_unique<AmpSimAudioProcessor>();
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);

        REQUIRE (processor->wrapperType == type);
        processor->prepareToPlay (48000.0, test::blockSize);

        auto* editor = dynamic_cast<AmpSimAudioProcessorEditor*> (processor->createEditor());
        REQUIRE (editor != nullptr);

        const auto items = editor->buildParameterMenu (ParamID::inputGain).getNumItems();
        delete editor;
        return items;
    };

    REQUIRE (menuItemsAs (juce::AudioProcessor::wrapperType_Standalone) > 0);
    REQUIRE (menuItemsAs (juce::AudioProcessor::wrapperType_AudioUnit) == 0);
    REQUIRE (menuItemsAs (juce::AudioProcessor::wrapperType_VST3) == 0);
}
