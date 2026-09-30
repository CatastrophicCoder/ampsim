/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "AmpPage.h"

namespace
{
    constexpr int inset        = 18;
    constexpr int plateHeight  = 54;
    constexpr int buttonWidth  = 100;
    constexpr int stripPadding = 22;
    constexpr int knobSize     = 88;
    constexpr int knobLabels   = 32;   // the name above and the value below

    constexpr int stripHeight = knobSize + knobLabels + 2 * stripPadding;
    constexpr int headHeight  = 2 * inset + plateHeight + inset + stripHeight;
}

AmpPage::AmpPage (AmpSimAudioProcessor& p)
    : processorRef (p),
      gainKnob   (p.getValueTreeState(), ParamID::inputGain,  "Gain"),
      bassKnob   (p.getValueTreeState(), ParamID::bass,       "Bass"),
      midKnob    (p.getValueTreeState(), ParamID::mid,        "Mid"),
      trebleKnob (p.getValueTreeState(), ParamID::treble,     "Treble"),
      masterKnob (p.getValueTreeState(), ParamID::outputGain, "Master")
{
    loadButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load a NAM model",
                                                           processorRef.getModelFile(), "*.nam");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();

                                      if (file != juce::File())
                                          processorRef.loadModel (file);
                                  });
    };

    addAndMakeVisible (loadButton);

    for (auto* knob : { &gainKnob, &bassKnob, &midKnob, &trebleKnob, &masterKnob })
        addAndMakeVisible (*knob);

    updateContents();
}

void AmpPage::setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
{
    for (auto* knob : { &gainKnob, &bassKnob, &midKnob, &trebleKnob, &masterKnob })
        knob->setContextMenuHandler (handler);
}

void AmpPage::updateContents()
{
    const auto error = processorRef.getModelError();
    const auto file = processorRef.getModelFile();

    showingError = error.isNotEmpty();
    modelName = showingError ? error
                             : (file == juce::File() ? "No model loaded"
                                                     : file.getFileNameWithoutExtension());
    repaint();
}

/** The chassis. Sized to what is on it rather than to the page, so the space around it reads as
    room around the amp instead of as an empty panel. */
juce::Rectangle<int> AmpPage::head() const
{
    return getLocalBounds().withSizeKeepingCentre (getWidth(),
                                                   juce::jmin (getHeight(), headHeight));
}

juce::Rectangle<int> AmpPage::plate() const
{
    return head().reduced (inset, 0).withY (head().getY() + inset).withHeight (plateHeight);
}

juce::Rectangle<int> AmpPage::knobStrip() const
{
    return head().withTop (plate().getBottom() + inset);
}

void AmpPage::paint (juce::Graphics& g)
{
    const auto chassis = head().toFloat();

    // The amp as one object rather than a page of controls: a chassis, a plate stamped with what
    // capture is loaded, and a control strip with the five knobs on it.
    g.setColour (AmpPalette::raised.darker (0.35f));
    g.fillRoundedRectangle (chassis, 8.0f);

    g.setColour (AmpPalette::hairline);
    g.drawRoundedRectangle (chassis.reduced (0.5f), 8.0f, 1.0f);

    // The strip the knobs sit on, so the row reads as a front panel and not as five knobs adrift.
    const auto strip = knobStrip().toFloat();

    g.setColour (AmpPalette::surface.darker (0.25f));
    g.fillRect (strip.withTrimmedBottom (8.0f));

    juce::Path skirt;
    skirt.addRoundedRectangle (strip.getX(), strip.getBottom() - 16.0f, strip.getWidth(), 16.0f,
                               8.0f, 8.0f, false, false, true, true);
    g.fillPath (skirt);

    g.setColour (AmpPalette::hairline);
    g.drawLine (strip.getX(), strip.getY(), strip.getRight(), strip.getY(), 1.0f);

    auto stamped = plate();

    g.setColour (AmpPalette::recess);
    g.fillRoundedRectangle (stamped.toFloat(), 5.0f);

    g.setColour (AmpPalette::hairline);
    g.drawRoundedRectangle (stamped.toFloat().reduced (0.5f), 5.0f, 1.0f);

    stamped.reduce (16, 0);
    stamped.removeFromRight (buttonWidth + 12);

    g.setFont (AmpLookAndFeel::font (10.0f, true).withExtraKerningFactor (0.1f));
    g.setColour (AmpPalette::textFaint);
    g.drawText ("MODEL", stamped.removeFromTop (20).withTrimmedTop (7),
                juce::Justification::topLeft, false);

    // The capture's name is the loudest type on the page: it is what this amp actually is.
    g.setFont (AmpLookAndFeel::stencil (18.0f));
    g.setColour (showingError ? AmpPalette::bypassed : AmpPalette::text);
    g.drawText (modelName, stamped.withTrimmedBottom (7), juce::Justification::topLeft, true);
}

void AmpPage::resized()
{
    loadButton.setBounds (plate().removeFromRight (buttonWidth + 16).withTrimmedRight (16)
                                 .withSizeKeepingCentre (buttonWidth, 28));

    AmpKnob* controls[] { &gainKnob, &bassKnob, &midKnob, &trebleKnob, &masterKnob };

    auto strip = knobStrip().reduced (inset, stripPadding);
    const auto width = strip.getWidth() / (int) std::size (controls);

    for (auto* knob : controls)
        knob->setBounds (strip.removeFromLeft (width).reduced (6, 0));
}
