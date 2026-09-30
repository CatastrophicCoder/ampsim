/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "CabPage.h"

namespace
{
    constexpr int captionHeight = 22;
    constexpr int gridWidth     = 462;
    constexpr int gridHeight    = 176;
    constexpr int knobColumn    = 228;
    constexpr int columnGap     = 28;

    const char* positionNames[CabSim::numSlots]
    {
        "on axis, close", "off axis, close", "on axis, far", "off axis, far"
    };
}

CabinetSlotButton::CabinetSlotButton (const juce::String& positionName)
    : juce::Button (positionName), position (positionName)
{
}

void CabinetSlotButton::setContents (const juce::String& text, bool loaded)
{
    contents = text;
    isLoaded = loaded;
    repaint();
}

void CabinetSlotButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                                     bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto corner = 4.0f;

    // A filled corner and an empty one should not be something you have to read to tell apart.
    // Everything differs: the plate, the marker down its edge, the border, and the weight of both
    // lines of text.
    const auto lift = shouldDrawButtonAsDown ? -0.12f : (shouldDrawButtonAsHighlighted ? 0.12f : 0.0f);

    g.setColour (isLoaded ? AmpPalette::raised.brighter (lift) : AmpPalette::recess.brighter (lift));
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (isLoaded ? AmpPalette::value.withAlpha (0.55f) : AmpPalette::hairline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

    if (isLoaded)
    {
        // The amber that marks where a control is set, marking which corners are in the blend.
        juce::Path marker;
        marker.addRoundedRectangle (bounds.getX(), bounds.getY(), 3.0f, bounds.getHeight(),
                                    corner, corner, true, false, true, false);
        g.setColour (AmpPalette::value);
        g.fillPath (marker);
    }

    auto text = getLocalBounds().reduced (14, 0).withSizeKeepingCentre (getWidth() - 28, 38);

    g.setFont (AmpLookAndFeel::font (10.0f, true).withExtraKerningFactor (0.08f));
    g.setColour (isLoaded ? AmpPalette::textDim : AmpPalette::textFaint);
    g.drawText (position.toUpperCase(), text.removeFromTop (15), juce::Justification::topLeft, false);

    g.setFont (AmpLookAndFeel::font (14.0f, isLoaded));
    g.setColour (isLoaded ? AmpPalette::text : AmpPalette::textFaint);
    g.drawText (contents, text, juce::Justification::topLeft, true);
}

//==============================================================================
CabPage::CabPage (AmpSimAudioProcessor& p)
    : processorRef (p),
      axisKnob (p.getValueTreeState(), ParamID::micAxis, "Axis"),
      distanceKnob (p.getValueTreeState(), ParamID::micDistance, "Distance"),
      bypassAttachment (p.getValueTreeState(), ParamID::cabBypass, bypassButton)
{
    for (int slot = 0; slot < CabSim::numSlots; ++slot)
    {
        auto* button = slots.add (new CabinetSlotButton (positionNames[slot]));
        button->onClick = [this, slot] { showMenuFor ((CabSim::Slot) slot); };
        addAndMakeVisible (button);
    }

    addAndMakeVisible (axisKnob);
    addAndMakeVisible (distanceKnob);

    bypassButton.setColour (juce::ToggleButton::tickColourId, AmpPalette::bypassed);
    addAndMakeVisible (bypassButton);

    updateContents();
}

void CabPage::updateContents()
{
    for (int slot = 0; slot < CabSim::numSlots; ++slot)
    {
        const auto file = processorRef.getImpulseResponseFile ((CabSim::Slot) slot);
        const auto loaded = file != juce::File();

        slots[slot]->setContents (loaded ? file.getFileNameWithoutExtension() : "empty", loaded);
    }

    repaint();
}

void CabPage::showMenuFor (CabSim::Slot slot)
{
    const auto loaded = processorRef.getImpulseResponseFile (slot) != juce::File();

    juce::PopupMenu menu;
    menu.addItem (1, "Load an impulse response...");
    menu.addItem (2, "Clear this position", loaded);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slots[(int) slot]),
                        [this, slot] (int result)
                        {
                            if (result == 2)
                            {
                                processorRef.clearImpulseResponse (slot);
                                return;
                            }

                            if (result != 1)
                                return;

                            fileChooser = std::make_unique<juce::FileChooser> (
                                "Load a cabinet impulse response",
                                processorRef.getImpulseResponseFile (slot),
                                "*.wav;*.aiff;*.aif");

                            fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                                          | juce::FileBrowserComponent::canSelectFiles,
                                                      [this, slot] (const juce::FileChooser& chooser)
                                                      {
                                                          const auto file = chooser.getResult();

                                                          if (file != juce::File())
                                                              processorRef.loadImpulseResponse (slot, file);
                                                      });
                        });
}

/** The grid and the two knobs, centred as one block with the caption on top of it. */
juce::Rectangle<int> CabPage::content() const
{
    return getLocalBounds().withSizeKeepingCentre (
               juce::jmin (getWidth(), gridWidth + columnGap + knobColumn),
               juce::jmin (getHeight(), captionHeight + gridHeight));
}

void CabPage::paint (juce::Graphics& g)
{
    auto caption = content().removeFromTop (captionHeight);
    caption.removeFromRight (knobColumn + columnGap);

    g.setFont (AmpLookAndFeel::font (10.0f, true).withExtraKerningFactor (0.1f));
    g.setColour (AmpPalette::textFaint);
    g.drawText ("MIC POSITION", caption, juce::Justification::bottomLeft, false);

    g.setFont (AmpLookAndFeel::font (11.0f));
    g.drawText ("click a corner to load an IR", caption, juce::Justification::bottomRight, false);

    // The only place a cab that would not load has to report itself.
    if (const auto error = processorRef.getImpulseResponseError(); error.isNotEmpty())
    {
        g.setFont (AmpLookAndFeel::font (11.5f));
        g.setColour (AmpPalette::bypassed);
        g.drawText (error, content().withTop (content().getBottom() + 6).withHeight (18),
                    juce::Justification::centredLeft, true);
    }
}

void CabPage::resized()
{
    auto area = content();

    auto caption = area.removeFromTop (captionHeight);
    bypassButton.setBounds (caption.removeFromRight (knobColumn).withHeight (captionHeight));

    auto knobArea = area.removeFromRight (knobColumn);
    axisKnob.setBounds (knobArea.removeFromLeft (knobColumn / 2).reduced (6, 12));
    distanceKnob.setBounds (knobArea.reduced (6, 12));

    auto grid = area.removeFromLeft (gridWidth);

    // Left to right is on axis to off axis; top to bottom is close to far, so the two knobs beside
    // the grid move along the axes the grid is laid out on.
    const auto halfWidth = grid.getWidth() / 2;
    const auto halfHeight = grid.getHeight() / 2;

    for (int slot = 0; slot < CabSim::numSlots; ++slot)
        slots[slot]->setBounds (juce::Rectangle<int> (grid.getX() + (slot % 2) * halfWidth,
                                                      grid.getY() + (slot / 2) * halfHeight,
                                                      halfWidth, halfHeight).reduced (4));
}
