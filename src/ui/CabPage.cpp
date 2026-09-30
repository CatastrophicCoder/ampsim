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

    // The cab is a box like the amp is, so the two pages read as parts of one rig. The four mic
    // positions sit on its grille, which is where a microphone in front of a cabinet actually is.
    constexpr int cabWidth   = 470;
    constexpr int cabHeight  = 252;
    constexpr int shellInset = 14;

    constexpr int columnGap  = 22;
    constexpr int knobColumn = 256;
    constexpr int plateHeight = 162;

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
    const auto lift = shouldDrawButtonAsDown ? -0.03f : (shouldDrawButtonAsHighlighted ? 0.04f : 0.0f);

    // A filled corner and an empty one should not be something you have to read to tell apart.
    // A loaded one is a lit plate on the cloth; an empty one barely interrupts it.
    g.setColour (isLoaded ? juce::Colours::white.withAlpha (0.07f + lift)
                          : juce::Colours::black.withAlpha (0.34f - lift));
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (isLoaded ? AmpPalette::value.withAlpha (0.7f)
                          : juce::Colours::white.withAlpha (0.07f));
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
      axisKnob (p.getValueTreeState(), ParamID::micAxis, "AXIS"),
      distanceKnob (p.getValueTreeState(), ParamID::micDistance, "DISTANCE"),
      bypassAttachment (p.getValueTreeState(), ParamID::cabBypass, bypassButton)
{
    for (int slot = 0; slot < CabSim::numSlots; ++slot)
    {
        auto* button = slots.add (new CabinetSlotButton (positionNames[slot]));
        button->onClick = [this, slot] { showMenuFor ((CabSim::Slot) slot); };
        addAndMakeVisible (button);
    }

    for (auto* knob : { &axisKnob, &distanceKnob })
    {
        knob->setBodyColour (AmpMaterials::knobCap);
        knob->setEngravedOnMetal (true);
        addAndMakeVisible (*knob);
    }

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

/** The cab and the plate beside it, centred as one block. */
juce::Rectangle<int> CabPage::content() const
{
    return getLocalBounds().withSizeKeepingCentre (
               juce::jmin (getWidth(), cabWidth + columnGap + knobColumn),
               juce::jmin (getHeight(), captionHeight + cabHeight));
}

juce::Rectangle<int> CabPage::cabinet() const
{
    return content().withTrimmedTop (captionHeight).withWidth (cabWidth);
}

juce::Rectangle<int> CabPage::knobPlate() const
{
    return content().withTrimmedTop (captionHeight)
                    .removeFromRight (knobColumn)
                    .withSizeKeepingCentre (knobColumn, plateHeight);
}

void CabPage::paint (juce::Graphics& g)
{
    auto caption = content().removeFromTop (captionHeight).withWidth (cabWidth);

    g.setFont (AmpLookAndFeel::font (10.0f, true).withExtraKerningFactor (0.1f));
    g.setColour (AmpPalette::textFaint);
    g.drawText ("MIC POSITION", caption, juce::Justification::bottomLeft, false);

    g.setFont (AmpLookAndFeel::font (11.0f));
    g.drawText ("click a corner to load an IR", caption, juce::Justification::bottomRight, false);

    AmpMaterials::drawBox (g, cabinet().toFloat(), 0x3ab19);
    AmpMaterials::drawGrille (g, cabinet().reduced (shellInset).toFloat());
    AmpMaterials::drawPlate (g, knobPlate().toFloat());

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
    // Directly above the plate it belongs to, rather than alone at the top of the page.
    bypassButton.setBounds (knobPlate().withY (knobPlate().getY() - 28).withHeight (22)
                                       .withTrimmedLeft (4));

    auto plate = knobPlate().reduced (16, 16);
    axisKnob.setBounds (plate.removeFromLeft (plate.getWidth() / 2).reduced (6, 0));
    distanceKnob.setBounds (plate.reduced (6, 0));

    // Left to right is on axis to off axis; top to bottom is close to far, so the two knobs beside
    // the cab move along the axes the grille is laid out on.
    auto grille = cabinet().reduced (shellInset);

    const auto halfWidth = grille.getWidth() / 2;
    const auto halfHeight = grille.getHeight() / 2;

    for (int slot = 0; slot < CabSim::numSlots; ++slot)
        slots[slot]->setBounds (juce::Rectangle<int> (grille.getX() + (slot % 2) * halfWidth,
                                                      grille.getY() + (slot / 2) * halfHeight,
                                                      halfWidth, halfHeight).reduced (7));
}
