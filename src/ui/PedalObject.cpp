/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PedalObject.h"

namespace
{
    constexpr int knobLabelHeight = 13;
    constexpr int maxKnobWidth = 44;
    constexpr float enclosureCorner = 7.0f;
}

PedalKnob::PedalKnob (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& labelText)
    : name (labelText), slider (parameterID), attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (AmpLookAndFeel::rotaryStart, AmpLookAndFeel::rotaryEnd, true);
    addAndMakeVisible (slider);
}

void PedalKnob::parentHierarchyChanged()
{
    // The popup lives in whatever component it is given, and a pedal knob is 30 px across, so it
    // belongs to the editor or it is clipped away to nothing.
    slider.setPopupDisplayEnabled (true, true, getTopLevelComponent());
}

void PedalKnob::paint (juce::Graphics& g)
{
    g.setFont (AmpLookAndFeel::font (9.5f));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText (name, getLocalBounds().removeFromBottom (knobLabelHeight),
                juce::Justification::centred, false);
}

void PedalKnob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (knobLabelHeight));
}

//==============================================================================
Footswitch::Footswitch() : juce::Button ("footswitch")
{
    setClickingTogglesState (true);
}

void Footswitch::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto face = juce::Rectangle<float> (size, size).withCentre (bounds.getCentre());

    // Seated in a threaded collar, the way a stomp switch sits in a drilled enclosure.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (face);

    // A latching stomp switch stays down while it is on, so the cap does too. It is a second
    // reading of the same state as the LED, for anyone who is not looking straight at the lamp.
    const auto pressed = shouldDrawButtonAsDown || getToggleState();
    const auto cap = face.reduced (size * 0.13f).translated (0.0f, pressed ? 1.5f : 0.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (pressed ? 0xffb9bfc7 : 0xffe8ebef),
                                             cap.getCentreX(), cap.getY(),
                                             juce::Colour (pressed ? 0xff5a616a : 0xff6e757e),
                                             cap.getCentreX(), cap.getBottom(), false));
    g.fillEllipse (cap);

    g.setColour (juce::Colours::white.withAlpha (shouldDrawButtonAsHighlighted ? 0.7f : 0.35f));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    // A dish in the middle, so it reads as something to press rather than a bead.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9aa1aa), cap.getCentreX(), cap.getCentreY(),
                                             juce::Colour (0xffd6dae0), cap.getCentreX(), cap.getY(),
                                             false));
    g.fillEllipse (cap.reduced (size * 0.22f));
}

//==============================================================================
PedalObject::PedalObject (juce::AudioProcessorValueTreeState& state,
                          const juce::String& pedalName,
                          const char* engageParameterID,
                          juce::Colour bodyColour,
                          std::initializer_list<Knob> knobsToAdd)
    : name (pedalName), body (bodyColour),
      engageAttachment (state, engageParameterID, footswitch)
{
    // A click repaints the switch, and the LED is not inside it. This also covers the state
    // arriving from somewhere else — a preset, the host's automation — rather than from a foot.
    footswitch.onStateChange = [this]
    {
        if (const auto on = footswitch.getToggleState(); on != lampLit)
        {
            lampLit = on;
            repaint();
        }
    };

    addAndMakeVisible (footswitch);

    for (const auto& knob : knobsToAdd)
        addAndMakeVisible (knobs.add (new PedalKnob (state, knob.parameterID, knob.label)));
}

void PedalObject::setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
{
    for (auto* knob : knobs)
        knob->setContextMenuHandler (handler);
}

void PedalObject::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), enclosureCorner);

    // The enclosure: anodised, so it is lighter where the light falls and the grain runs down it.
    g.setGradientFill (juce::ColourGradient (body.brighter (0.16f), bounds.getCentreX(), bounds.getY(),
                                             body.darker (0.24f), bounds.getCentreX(), bounds.getBottom(),
                                             false));
    g.fillRoundedRectangle (bounds, enclosureCorner);

    juce::Random grain (0x9e37 + name.hashCode());

    for (float x = bounds.getX() + 1.0f; x < bounds.getRight() - 1.0f; x += 1.0f)
    {
        if (grain.nextFloat() > 0.45f)
            continue;

        g.setColour (juce::Colours::white.withAlpha (0.015f + 0.025f * grain.nextFloat()));
        g.fillRect (x, bounds.getY() + 1.0f, 1.0f, bounds.getHeight() - 2.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), enclosureCorner, 1.0f);

    // The name, screen-printed across the bottom of the box.
    g.setFont (AmpLookAndFeel::stencil (13.0f).withExtraKerningFactor (0.06f));
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawText (name.toUpperCase(), nameArea.translated (0, 1), juce::Justification::centred, false);
    g.setColour (juce::Colour (0xfff6f1e4));
    g.drawText (name.toUpperCase(), nameArea, juce::Justification::centred, false);

    // The LED, above the name: lit means this pedal is in your signal.
    const auto on = footswitch.getToggleState();
    const auto led = juce::Rectangle<float> (9.0f, 9.0f).withCentre (ledCentre);

    if (on)
    {
        // Two rings of spill, so a lit LED reads across the board and not only when looked at.
        g.setColour (AmpPalette::engaged.withAlpha (0.16f));
        g.fillEllipse (led.expanded (8.0f));

        g.setColour (AmpPalette::engaged.withAlpha (0.35f));
        g.fillEllipse (led.expanded (3.5f));
    }

    g.setColour (on ? AmpPalette::engaged : juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (led);

    if (on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.fillEllipse (led.reduced (2.8f).translated (0.0f, -0.8f));
    }

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (led, 1.0f);
}

void PedalObject::resized()
{
    auto bounds = getLocalBounds().reduced (7, 9);

    const auto switchSize = juce::jmin (34, bounds.getWidth() / 2);
    footswitch.setBounds (bounds.removeFromBottom (switchSize)
                              .withSizeKeepingCentre (switchSize, switchSize));

    bounds.removeFromBottom (6);
    nameArea = bounds.removeFromBottom (16);
    ledCentre = { bounds.toFloat().getCentreX(), (float) bounds.getBottom() - 7.0f };
    bounds.removeFromBottom (14);

    if (knobs.isEmpty())
        return;

    // Up to three across, as a compact pedal has them; a fourth would go on a second row. The row
    // sits in the upper half of what is left, which is where a pedal's controls actually are —
    // the space under them is the blank face the name is printed on.
    const auto perRow = juce::jmin (knobs.size(), 3);
    const auto knobWidth = juce::jmin (bounds.getWidth() / perRow, maxKnobWidth);
    const auto knobHeight = juce::jmin (bounds.getHeight(), knobWidth + knobLabelHeight);

    auto row = bounds.removeFromTop (knobHeight + (bounds.getHeight() - knobHeight) / 3)
                     .removeFromBottom (knobHeight);

    // A single knob is centred rather than pushed to the left-hand third.
    row = row.withSizeKeepingCentre (knobWidth * perRow, knobHeight);

    for (auto* knob : knobs)
        knob->setBounds (row.removeFromLeft (knobWidth).reduced (2, 0));
}
