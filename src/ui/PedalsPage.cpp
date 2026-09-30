/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PedalsPage.h"

namespace
{
    constexpr int captionHeight = 22;
    constexpr int ampGapWidth   = 52;
    constexpr int pedalHeight   = 236;

    // Enclosure colours, kept muted and away from the three colours that carry meaning: an
    // anodised box is not trying to tell you anything, and the LED on it is.
    const juce::Colour gateBody   { 0xff424a58 };
    const juce::Colour compBody   { 0xff2d5a63 };
    const juce::Colour driveBody  { 0xff8c3f2e };
    const juce::Colour chorusBody { 0xff3a4f86 };
    const juce::Colour delayBody  { 0xff4a3d72 };
    const juce::Colour reverbBody { 0xff2f6147 };
}

PedalsPage::PedalsPage (juce::AudioProcessorValueTreeState& state)
    : gate (state, "gate", ParamID::gateOn, gateBody,
            { { ParamID::gateThreshold, "thresh" } }),
      compressor (state, "comp", ParamID::compOn, compBody,
                  { { ParamID::compAmount, "amount" }, { ParamID::compLevel, "level" } }),
      drive (state, "drive", ParamID::driveOn, driveBody,
             { { ParamID::driveAmount, "drive" }, { ParamID::driveTone, "tone" },
               { ParamID::driveLevel, "level" } }),
      chorus (state, "chorus", ParamID::chorusOn, chorusBody,
              { { ParamID::chorusRate, "rate" }, { ParamID::chorusDepth, "depth" },
                { ParamID::chorusMix, "mix" } }),
      delay (state, "delay", ParamID::delayOn, delayBody,
             { { ParamID::delayTime, "time" }, { ParamID::delayDivision, "sync" },
               { ParamID::delayFeedback, "repeats" }, { ParamID::delayMix, "mix" } }),
      reverb (state, "reverb", ParamID::reverbOn, reverbBody,
              { { ParamID::reverbSize, "size" }, { ParamID::reverbMix, "mix" } })
{
    for (auto* pedal : { &gate, &compressor, &drive, &chorus, &delay, &reverb })
        addAndMakeVisible (*pedal);
}

void PedalsPage::setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
{
    for (auto* pedal : { &gate, &compressor, &drive, &chorus, &delay, &reverb })
        pedal->setContextMenuHandler (handler);
}

void PedalsPage::paint (juce::Graphics& g)
{
    // Directly above the boards rather than at the top of the page: the caption names the group
    // under it, so it has to sit on it.
    auto caption = juce::Rectangle<int> (0, ampGap.getY() - captionHeight, getWidth(), captionHeight);

    g.setFont (AmpLookAndFeel::font (10.0f, true).withExtraKerningFactor (0.1f));
    g.setColour (AmpPalette::textFaint);
    g.drawText ("INTO THE AMP", caption.withRight (ampGap.getCentreX()),
                juce::Justification::bottomLeft, false);
    g.drawText ("AFTER THE AMP, BEFORE THE CAB", caption.withLeft (ampGap.getCentreX()),
                juce::Justification::bottomRight, false);

    // The amp, as a break in the run: a patch lead arriving, the head, and a lead leaving. It is
    // not a control, so it is drawn rather than built, and it is the quietest thing on the page.
    const auto lead = (float) ampGap.getCentreY();

    g.setColour (AmpPalette::hairline);
    g.drawLine ((float) ampGap.getX() - 6.0f, lead, (float) ampGap.getRight() + 6.0f, lead, 1.5f);

    const auto head = juce::Rectangle<float> (34.0f, 26.0f).withCentre (ampGap.getCentre().toFloat());

    g.setColour (AmpPalette::background);
    g.fillRoundedRectangle (head.expanded (4.0f, 3.0f), 4.0f);

    g.setColour (AmpPalette::raised);
    g.fillRoundedRectangle (head, 4.0f);

    g.setColour (AmpPalette::hairline);
    g.drawRoundedRectangle (head.reduced (0.5f), 4.0f, 1.0f);

    g.setColour (AmpPalette::value.withAlpha (0.85f));
    g.fillRect (head.reduced (7.0f, 9.0f).withHeight (2.0f));

    g.setFont (AmpLookAndFeel::stencil (10.0f).withExtraKerningFactor (0.1f));
    g.setColour (AmpPalette::textDim);
    g.drawText ("AMP", ampGap.withY (juce::roundToInt (head.getBottom()) + 5).withHeight (14),
                juce::Justification::centred, false);
}

void PedalsPage::resized()
{
    auto area = getLocalBounds().withTrimmedTop (captionHeight);

    area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), pedalHeight));

    const auto width = (area.getWidth() - ampGapWidth) / 6;

    const auto place = [&area, width] (PedalObject& pedal)
    {
        pedal.setBounds (area.removeFromLeft (width).reduced (5, 0));
    };

    place (gate);
    place (compressor);
    place (drive);

    ampGap = area.removeFromLeft (ampGapWidth);

    place (chorus);
    place (delay);
    place (reverb);
}
