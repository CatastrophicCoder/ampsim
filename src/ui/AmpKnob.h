/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "AmpLookAndFeel.h"
#include "ParameterSlider.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** One of the amp's controls: its name above, the knob, its value below.

    The amp's knobs carry a permanent read-out where a pedal's do not — these are the controls you
    set deliberately and want to be able to report, and there are only five of them.
*/
class AmpKnob final : public juce::Component
{
public:
    AmpKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
             const juce::String& labelText);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
    {
        slider.onContextMenu = std::move (handler);
    }

private:
    juce::String name;
    ParameterSlider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpKnob)
};
