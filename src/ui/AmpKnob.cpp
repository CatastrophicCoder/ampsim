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

    // Double-click puts a control back where it started. The default comes from the parameter, so
    // there is no second copy of it here to drift out of step with the one the host is told about.
    // Rounded to the range's own step, because a parameter knows its default only as a normalised
    // number and coming back through a skewed dB range lands a millionth of a decibel off zero.
    // Near enough to hear nothing, but far enough that a host comparing against the default would
    // not call it one.
    if (auto* parameter = state.getParameter (parameterID))
    {
        const auto& range = parameter->getNormalisableRange();
        auto value = (double) range.convertFrom0to1 (parameter->getDefaultValue());

        if (range.interval > 0.0f)
        {
            const auto steps = 1.0 / (double) range.interval;
            value = std::round (value * steps) / steps;
        }

        slider.setDoubleClickReturnValue (true, (float) value);
    }

    // The read-out is type on the panel, not a field: its frame and fill come from the slider.
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, AmpPalette::textDim);
    slider.setColour (juce::Slider::textBoxHighlightColourId, AmpPalette::value.withAlpha (0.3f));

    addAndMakeVisible (slider);
}

void AmpKnob::setBodyColour (juce::Colour colour)
{
    slider.setColour (AmpLookAndFeel::knobBodyColourId, colour);

    // A pale track would glare against a black cap; a dark one reads as the channel the pointer
    // travels in, which is what it is.
    slider.setColour (AmpLookAndFeel::knobTrackColourId,
                      colour.brighter (0.22f).interpolatedWith (AmpPalette::hairline, 0.35f));
}

void AmpKnob::setEngravedOnMetal (bool shouldBeEngraved)
{
    engraved = shouldBeEngraved;

    slider.setColour (juce::Slider::textBoxTextColourId,
                      engraved ? juce::Colour (0xffd9dee6).withAlpha (0.72f) : AmpPalette::textDim);
    repaint();
}

void AmpKnob::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().removeFromTop (nameHeight);

    if (! engraved)
    {
        g.setFont (AmpLookAndFeel::font (12.5f, true));
        g.setColour (AmpPalette::text);
        g.drawText (name, area, juce::Justification::centred, false);
        return;
    }

    g.setFont (AmpLookAndFeel::font (9.5f, true).withExtraKerningFactor (0.2f));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText (name, area.translated (0, 1), juce::Justification::centredBottom, false);
    g.setColour (juce::Colour (0xffd9dee6));
    g.drawText (name, area, juce::Justification::centredBottom, false);
}

void AmpKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (nameHeight);

    const auto size = juce::jmin (area.getWidth(), area.getHeight() - valueHeight, maxSize);
    slider.setBounds (area.withSizeKeepingCentre (size, size + valueHeight).withY (area.getY()));
}
