/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "AmpLookAndFeel.h"

const juce::Colour AmpPalette::background { 0xff131519 };
const juce::Colour AmpPalette::bar        { 0xff0e1013 };
const juce::Colour AmpPalette::surface    { 0xff1b1e24 };
const juce::Colour AmpPalette::raised     { 0xff252931 };
const juce::Colour AmpPalette::recess     { 0xff0f1115 };
const juce::Colour AmpPalette::hairline   { 0xff2f343d };

const juce::Colour AmpPalette::text       { 0xffe9ecf1 };
const juce::Colour AmpPalette::textDim    { 0xff939cab };
const juce::Colour AmpPalette::textFaint  { 0xff5d6676 };

const juce::Colour AmpPalette::value      { 0xffe8a33d };
const juce::Colour AmpPalette::engaged    { 0xff4ad07a };
const juce::Colour AmpPalette::bypassed   { 0xffdb4b3f };

juce::Font AmpLookAndFeel::font (float height, bool medium)
{
    auto options = juce::FontOptions ("Avenir Next", height, juce::Font::plain);

    if (medium)
        options = options.withStyle ("Medium");

    return juce::Font (options);
}

juce::Font AmpLookAndFeel::stencil (float height)
{
    return juce::Font (juce::FontOptions ("Futura", height, juce::Font::plain).withStyle ("Medium"));
}

AmpLookAndFeel::AmpLookAndFeel()
{
    setColour (juce::Label::textColourId, AmpPalette::text);
    setColour (juce::TextButton::buttonColourId, AmpPalette::raised);
    setColour (juce::TextButton::textColourOffId, AmpPalette::text);
    setColour (juce::ToggleButton::textColourId, AmpPalette::textDim);
    setColour (juce::ToggleButton::tickColourId, AmpPalette::bypassed);

    setColour (juce::PopupMenu::backgroundColourId, AmpPalette::surface);
    setColour (juce::PopupMenu::textColourId, AmpPalette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, AmpPalette::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, AmpPalette::text);
}

void AmpLookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion,
                               bool centred, juce::Colour bodyColour, float ringThickness)
{
    const auto centre = bounds.getCentre();
    const auto ringRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - ringThickness * 0.5f;
    const auto bodyRadius = ringRadius - ringThickness * 1.5f;

    const auto angle = rotaryStart + proportion * (rotaryEnd - rotaryStart);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotaryStart, rotaryEnd, true);
    g.setColour (AmpPalette::hairline);
    g.strokePath (track, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // A control that rests in the middle reads its value out from there; one that runs bottom to
    // top reads from the bottom. Either way the ring shows the distance travelled, not the
    // absolute position, which is the thing you actually want to see at a glance.
    const auto origin = centred ? rotaryStart + 0.5f * (rotaryEnd - rotaryStart) : rotaryStart;

    if (std::abs (angle - origin) > 1.0e-3f)
    {
        juce::Path reading;
        reading.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                               juce::jmin (origin, angle), juce::jmax (origin, angle), true);

        g.setColour (AmpPalette::value);
        g.strokePath (reading, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (bodyColour.brighter (0.16f), body.getCentreX(), body.getY(),
                                             bodyColour.darker (0.22f), body.getCentreX(), body.getBottom(),
                                             false));
    g.fillEllipse (body);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    const juce::Point<float> pointer { std::sin (angle), -std::cos (angle) };
    g.setColour (AmpPalette::text);
    g.drawLine ({ centre + pointer * (bodyRadius * 0.42f), centre + pointer * (bodyRadius * 0.84f) },
                juce::jmax (1.5f, ringThickness * 0.7f));
}

void AmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float, float, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (1.0f);
    const auto centred = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto thickness = juce::jmax (2.5f, bounds.getWidth() * 0.055f);

    drawKnob (g, bounds, sliderPos, centred, AmpPalette::raised, thickness);
}

void AmpLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool)
{
    auto bounds = button.getLocalBounds();
    const auto on = button.getToggleState();
    const auto lit = button.findColour (juce::ToggleButton::tickColourId);

    const auto size = juce::jmin (9, bounds.getHeight());
    const auto lamp = bounds.removeFromLeft (size + 14).withSizeKeepingCentre (size, size).toFloat();

    if (on)
    {
        g.setColour (lit.withAlpha (0.25f));
        g.fillEllipse (lamp.expanded (3.5f));
    }

    g.setColour (on ? lit : AmpPalette::hairline);
    g.fillEllipse (lamp);

    g.setFont (font (12.0f));
    g.setColour (on ? AmpPalette::text
                    : AmpPalette::textDim.withAlpha (shouldDrawButtonAsHighlighted ? 1.0f : 0.8f));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centredLeft, false);
}

void AmpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = 4.0f;

    g.setColour (shouldDrawButtonAsDown ? AmpPalette::recess
                                        : (shouldDrawButtonAsHighlighted ? AmpPalette::raised.brighter (0.15f)
                                                                         : AmpPalette::raised));
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (AmpPalette::hairline);
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

juce::Font AmpLookAndFeel::getTextButtonFont (juce::TextButton&, int)  { return font (12.0f); }
juce::Font AmpLookAndFeel::getLabelFont (juce::Label& label)           { return font (label.getFont().getHeight()); }

juce::Label* AmpLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    label->setFont (font (11.0f));
    label->setJustificationType (juce::Justification::centred);

    return label;
}
