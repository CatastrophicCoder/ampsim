/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "AmpKnob.h"

namespace
{
    constexpr int nameHeight = 16;
    constexpr int valueHeight = 16;

    /** A knob is a thing you grip, not a thing that grows to fill a panel: past this it stops
        reading as a control and starts reading as a dial on a wall. */
    constexpr int maxSize = 88;
}

AmpKnob::AmpKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  const juce::String& labelText)
    : name (labelText), slider (parameterID), attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, valueHeight);
    slider.setRotaryParameters (AmpLookAndFeel::rotaryStart, AmpLookAndFeel::rotaryEnd, true);

    // The read-out is type on the panel, not a field: its frame and fill come from the slider.
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, AmpPalette::textDim);
    slider.setColour (juce::Slider::textBoxHighlightColourId, AmpPalette::value.withAlpha (0.3f));

    addAndMakeVisible (slider);
}

void AmpKnob::paint (juce::Graphics& g)
{
    g.setFont (AmpLookAndFeel::font (12.5f, true));
    g.setColour (AmpPalette::text);
    g.drawText (name, getLocalBounds().removeFromTop (nameHeight), juce::Justification::centred, false);
}

void AmpKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (nameHeight);

    const auto size = juce::jmin (area.getWidth(), area.getHeight() - valueHeight, maxSize);
    slider.setBounds (area.withSizeKeepingCentre (size, size + valueHeight).withY (area.getY()));
}
