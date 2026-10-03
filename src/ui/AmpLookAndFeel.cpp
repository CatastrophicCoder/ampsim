/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "AmpLookAndFeel.h"
#include "ParameterSlider.h"

#include "EmbeddedFonts.h"

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

const juce::Colour AmpMaterials::tolex     { 0xff17181c };
const juce::Colour AmpMaterials::piping    { 0xff3c4250 };
const juce::Colour AmpMaterials::cloth     { 0xff0e1014 };
const juce::Colour AmpMaterials::metal     { 0xff23262d };
const juce::Colour AmpMaterials::brassLit  { 0xffb99a55 };
const juce::Colour AmpMaterials::brassDark { 0xff7a6330 };
const juce::Colour AmpMaterials::engraving { 0xffd9dee6 };
const juce::Colour AmpMaterials::knobCap   { 0xff15171b };

void AmpMaterials::drawBox (juce::Graphics& g, juce::Rectangle<float> bounds, int grainSeed)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 3.0f), 11.0f);

    g.setColour (tolex);
    g.fillRoundedRectangle (bounds, 11.0f);

    // The covering's pebble grain. Coarse on purpose: a finer one disappears at 75 % and costs
    // four times as much to draw.
    juce::Random grain (grainSeed);

    for (int y = (int) bounds.getY() + 4; y < (int) bounds.getBottom() - 4; y += 5)
        for (int x = (int) bounds.getX() + 4 + (y % 10 == 0 ? 0 : 2); x < (int) bounds.getRight() - 4; x += 5)
        {
            g.setColour (juce::Colours::white.withAlpha (0.012f + 0.022f * grain.nextFloat()));
            g.fillRect (x, y, 1, 1);
        }

    // Piping, which is what stops a dark box looking like a dark rectangle.
    g.setColour (piping);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 11.0f, 1.5f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (bounds.reduced (2.5f), 9.0f, 1.0f);
}

void AmpMaterials::drawGrille (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (cloth);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Basket weave: two sets of diagonals, both faint. Drawn rather than cached so it stays crisp
    // at every scale, which is the whole reason nothing here is an image.
    g.saveState();
    g.reduceClipRegion (bounds.toNearestInt());

    const auto span = bounds.getWidth() + bounds.getHeight();

    for (float offset = -bounds.getHeight(); offset < span; offset += 6.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.055f));
        g.drawLine (bounds.getX() + offset, bounds.getY(),
                    bounds.getX() + offset + bounds.getHeight(), bounds.getBottom(), 1.6f);

        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawLine (bounds.getRight() - offset, bounds.getY(),
                    bounds.getRight() - offset - bounds.getHeight(), bounds.getBottom(), 1.6f);
    }

    g.restoreState();

    // Lit from above, like cloth stretched over a baffle.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f),
                                             bounds.getCentreX(), bounds.getY(),
                                             juce::Colours::black.withAlpha (0.28f),
                                             bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Sunk into the box rather than sitting on it.
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.5f);
}

void AmpMaterials::drawPlate (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), 4.0f);

    g.setGradientFill (juce::ColourGradient (metal.brighter (0.09f), bounds.getCentreX(), bounds.getY(),
                                             metal.darker (0.18f), bounds.getCentreX(), bounds.getBottom(),
                                             false));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Brushed across, the way a rolled aluminium fascia is.
    juce::Random grain (0x91a7e);

    for (float y = bounds.getY() + 1.0f; y < bounds.getBottom() - 1.0f; y += 1.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.018f * grain.nextFloat()));
        g.fillRect (bounds.getX() + 1.0f, y, bounds.getWidth() - 2.0f, 1.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Four screws, where a plate is actually held on.
    for (const auto corner : { bounds.getTopLeft(), bounds.getTopRight(),
                               bounds.getBottomLeft(), bounds.getBottomRight() })
    {
        const auto x = corner.x < bounds.getCentreX() ? corner.x + 9.0f : corner.x - 9.0f;
        const auto y = corner.y < bounds.getCentreY() ? corner.y + 9.0f : corner.y - 9.0f;
        const juce::Rectangle<float> screw { x - 3.0f, y - 3.0f, 6.0f, 6.0f };

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (screw);

        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawEllipse (screw.reduced (0.5f), 1.0f);

        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (screw.getX() + 1.2f, screw.getCentreY(), screw.getRight() - 1.2f, screw.getCentreY(), 1.0f);
    }
}

EmbeddedTypefaces::EmbeddedTypefaces()
    : interfaceRegular (juce::Typeface::createSystemTypefaceFor (EmbeddedFonts::FigtreeRegular_ttf,
                                                                 (size_t) EmbeddedFonts::FigtreeRegular_ttfSize)),
      interfaceMedium (juce::Typeface::createSystemTypefaceFor (EmbeddedFonts::FigtreeMedium_ttf,
                                                                (size_t) EmbeddedFonts::FigtreeMedium_ttfSize)),
      stencil (juce::Typeface::createSystemTypefaceFor (EmbeddedFonts::JostMedium_ttf,
                                                        (size_t) EmbeddedFonts::JostMedium_ttfSize))
{
}

// A JUCE font height is the face's ascent plus descent, which takes up a different share of the
// letters in every face. Every height on the panel was chosen for Avenir Next and Futura, so each
// face is scaled to put its capitals where theirs were: Figtree's cap height is 0.583 of its
// span against Avenir's 0.518, Jost's 0.484 against Futura's 0.586. At those scales Figtree's
// widths also land within one per cent of Avenir's; Jost sets about six per cent wider than
// Futura. Measured from the font files with fontTools, OS/2 cap height over the ascent and
// descent JUCE uses.
static constexpr float interfaceScale = 0.518f / 0.583f;
static constexpr float stencilScale   = 0.586f / 0.484f;

// Each call takes the shared typefaces rather than holding them in a static: a static would
// outlive JUCE's own shutdown, and on Windows release them after DirectWrite has gone. While an
// AmpLookAndFeel exists this is a reference count, not a reload.
juce::Font AmpLookAndFeel::font (float height, bool medium)
{
    juce::SharedResourcePointer<EmbeddedTypefaces> shared;
    return juce::Font (juce::FontOptions (medium ? shared->interfaceMedium : shared->interfaceRegular)
                           .withHeight (height * interfaceScale));
}

juce::Font AmpLookAndFeel::stencil (float height)
{
    juce::SharedResourcePointer<EmbeddedTypefaces> shared;
    return juce::Font (juce::FontOptions (shared->stencil).withHeight (height * stencilScale));
}

AmpLookAndFeel::AmpLookAndFeel()
{
    setDefaultSansSerifTypeface (typefaces->interfaceRegular);

    setColour (juce::Label::textColourId, AmpPalette::text);
    setColour (juce::TextButton::buttonColourId, AmpPalette::raised);
    setColour (juce::TextButton::textColourOffId, AmpPalette::text);
    setColour (juce::ToggleButton::textColourId, AmpPalette::textDim);
    setColour (juce::ToggleButton::tickColourId, AmpPalette::bypassed);
    setColour (knobBodyColourId, AmpPalette::raised);
    setColour (knobTrackColourId, AmpPalette::hairline);

    setColour (juce::PopupMenu::backgroundColourId, AmpPalette::surface);
    setColour (juce::PopupMenu::textColourId, AmpPalette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, AmpPalette::raised);
    setColour (juce::PopupMenu::highlightedTextColourId, AmpPalette::text);
}

void AmpLookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float proportion,
                               float originProportion, juce::Colour bodyColour, juce::Colour trackColour,
                               float ringThickness)
{
    const auto centre = bounds.getCentre();
    const auto ringRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - ringThickness * 0.5f;
    const auto bodyRadius = ringRadius - ringThickness * 1.5f;

    const auto angle = rotaryStart + proportion * (rotaryEnd - rotaryStart);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, rotaryStart, rotaryEnd, true);
    g.setColour (trackColour);
    g.strokePath (track, juce::PathStrokeType (ringThickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // The ring is drawn outward from wherever the control is doing nothing — the middle for a
    // band that cuts and boosts, the bottom for an amount, the top for a cut that is switched out
    // of the way when it is turned up. What it shows is the distance travelled from there, not the
    // absolute position, which is the thing worth seeing at a glance.
    const auto origin = rotaryStart + originProportion * (rotaryEnd - rotaryStart);

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

    // A shadow under the cap, so it reads as something mounted through the panel.
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (body.expanded (1.0f).translated (0.0f, 1.5f));

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

    auto origin = centred ? 0.5f : 0.0f;

    if (const auto* parameterSlider = dynamic_cast<const ParameterSlider*> (&slider))
        if (parameterSlider->ringOrigin >= 0.0f)
            origin = parameterSlider->ringOrigin;

    drawKnob (g, bounds, sliderPos, origin,
              slider.findColour (knobBodyColourId), slider.findColour (knobTrackColourId), thickness);
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
