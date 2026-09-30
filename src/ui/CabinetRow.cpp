/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "CabinetRow.h"

namespace
{
    constexpr int tileNameHeight = 20;

    const char* positionNames[CabSim::numSlots]
    {
        "cap, close", "edge, close", "cap, far", "edge, far"
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

    // A filled corner and an empty one should not be something you have to read to tell apart.
    // Everything differs: the plate it is drawn on, the marker down its edge, the border, and the
    // weight of both lines of text.
    const auto lift = shouldDrawButtonAsDown ? -0.15f : (shouldDrawButtonAsHighlighted ? 0.25f : 0.0f);

    g.setColour (isLoaded ? AmpPalette::railRecess.brighter (0.35f + lift)
                          : AmpPalette::railRecess.darker (0.35f - lift));
    g.fillRect (bounds);

    if (isLoaded)
    {
        // The same blue that means "in your signal" everywhere else on the panel.
        g.setColour (AmpPalette::reading);
        g.fillRect (bounds.withWidth (3.0f));

        g.setColour (AmpPalette::reading.withAlpha (0.55f));
        g.drawRect (bounds, 1.0f);
    }
    else
    {
        g.setColour (AmpPalette::enamel.withAlpha (0.08f));
        g.drawRect (bounds, 1.0f);
    }

    auto text = getLocalBounds().reduced (10, 4);

    g.setFont (AmpLookAndFeel::panelFont (10.0f));
    g.setColour (AmpPalette::enamel.withAlpha (isLoaded ? 0.6f : 0.3f));
    g.drawText (position, text.removeFromTop (13), juce::Justification::centredLeft, false);

    g.setFont (AmpLookAndFeel::panelFont (12.0f, isLoaded));
    g.setColour (AmpPalette::enamel.withAlpha (isLoaded ? 1.0f : 0.25f));
    g.drawText (contents, text, juce::Justification::centredLeft, true);
}

//==============================================================================
CabinetRow::CabinetRow (AmpSimAudioProcessor& p)
    : processorRef (p),
      axisKnob (p.getValueTreeState(), ParamID::micAxis, "axis"),
      distanceKnob (p.getValueTreeState(), ParamID::micDistance, "distance"),
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

    bypassButton.setColour (juce::ToggleButton::tickColourId, AmpPalette::attention);
    addAndMakeVisible (bypassButton);

    updateContents();
}

void CabinetRow::updateContents()
{
    for (int slot = 0; slot < CabSim::numSlots; ++slot)
    {
        const auto file = processorRef.getImpulseResponseFile ((CabSim::Slot) slot);
        const auto loaded = file != juce::File();

        slots[slot]->setContents (loaded ? file.getFileNameWithoutExtension() : "empty", loaded);
    }
}

void CabinetRow::showMenuFor (CabSim::Slot slot)
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

void CabinetRow::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (juce::Colour (0xff2e332b));
    g.fillRect (bounds);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (bounds.getX(), bounds.getY(), bounds.getRight(), bounds.getY(), 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRect (bounds, 1.0f);

    auto nameRow = getLocalBounds().removeFromTop (tileNameHeight).reduced (10, 0);

    AmpLookAndFeel::drawRailText (g, "cabinet", nameRow.removeFromLeft (70),
                                  juce::Justification::centredLeft,
                                  AmpLookAndFeel::panelFont (13.0f, true),
                                  AmpPalette::enamel.withAlpha (0.9f));

    // The slots below already name what is loaded, so the heading carries nothing but the word —
    // except when a file could not be read, which has nowhere else to appear.
    if (const auto error = processorRef.getImpulseResponseError(); error.isNotEmpty())
        AmpLookAndFeel::drawRailText (g, error, nameRow, juce::Justification::centredLeft,
                                      AmpLookAndFeel::panelFont (11.0f),
                                      AmpPalette::attention.brighter (0.35f));
}

void CabinetRow::resized()
{
    auto area = getLocalBounds();

    auto nameRow = area.removeFromTop (tileNameHeight);
    bypassButton.setBounds (nameRow.removeFromRight (34).withTrimmedRight (8));

    area = area.reduced (8, 4);

    // The two position controls sit to the right of the corners they move between, with a margin
    // so the rightmost one's value popup has room: the bubble centres on the knob, and JUCE does
    // not pull it back inside the window.
    area.removeFromRight (6);

    auto knobArea = area.removeFromRight (128);
    axisKnob.setBounds (knobArea.removeFromLeft (64).reduced (2, 0));
    distanceKnob.setBounds (knobArea.reduced (2, 0));

    const auto slotWidth = area.getWidth() / CabSim::numSlots;

    for (auto* slot : slots)
        slot->setBounds (area.removeFromLeft (slotWidth).reduced (2, 2));
}
