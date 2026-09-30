/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PresetRow.h"

void PresetRow::NameButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = 4.0f;

    g.setColour (shouldDrawButtonAsDown ? AmpPalette::background : AmpPalette::recess);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (shouldDrawButtonAsHighlighted ? AmpPalette::value.withAlpha (0.5f) : AmpPalette::hairline);
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    auto text = getLocalBounds().reduced (12, 0);
    auto chevron = text.removeFromRight (14);

    g.setFont (AmpLookAndFeel::font (13.0f, true));
    g.setColour (showingError ? AmpPalette::bypassed : AmpPalette::text);
    g.drawText (displayed, text, juce::Justification::centredLeft, true);

    juce::Path arrow;
    arrow.addTriangle (chevron.getCentreX() - 4.0f, (float) chevron.getCentreY() - 2.0f,
                       chevron.getCentreX() + 4.0f, (float) chevron.getCentreY() - 2.0f,
                       chevron.getCentreX(),        (float) chevron.getCentreY() + 3.0f);
    g.setColour (AmpPalette::textDim);
    g.fillPath (arrow);
}

//==============================================================================
PresetRow::PresetRow (PresetManager& presetsToUse)
    : presets (presetsToUse)
{
    previousButton.onClick = [this] { presets.step (-1); };
    nextButton.onClick = [this] { presets.step (1); };
    nameButton.onClick = [this] { showMenu(); };

    addAndMakeVisible (previousButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (nameButton);

    updateContents();
}

void PresetRow::updateContents()
{
    const auto name = presets.getCurrentName();

    nameButton.displayed = name.isEmpty() ? "no preset" : name;
    nameButton.showingError = false;
    nameButton.repaint();
}

void PresetRow::report (const juce::String& error)
{
    if (error.isEmpty())
        return;

    nameButton.displayed = error;
    nameButton.showingError = true;
    nameButton.repaint();
}

void PresetRow::showMenu()
{
    juce::PopupMenu menu;

    const auto names = presets.getNames();
    const auto current = presets.getCurrentName();

    if (names.isEmpty())
    {
        menu.addItem (-1, "No presets saved yet", false, false);
    }
    else
    {
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (100 + i, names[i], true, names[i] == current);
    }

    menu.addSeparator();
    menu.addItem (1, "Save as...");
    menu.addItem (2, "Save over " + current, current.isNotEmpty());
    menu.addItem (3, "Delete " + current, current.isNotEmpty());
    menu.addSeparator();
    menu.addItem (4, "Add the built-in presets");
    menu.addItem (5, "Open the presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nameButton),
                        [this, names] (int result)
                        {
                            if (result >= 100)
                            {
                                report (presets.load (names[result - 100]));
                            }
                            else if (result == 1)
                            {
                                askForNameAndSave();
                            }
                            else if (result == 2)
                            {
                                report (presets.save (presets.getCurrentName()));
                            }
                            else if (result == 3)
                            {
                                report (presets.remove (presets.getCurrentName()));
                            }
                            else if (result == 4)
                            {
                                presets.createFactoryPresetsIfMissing();
                            }
                            else if (result == 5)
                            {
                                presets.getDirectory().revealToUser();
                            }
                        });
}

void PresetRow::askForNameAndSave()
{
    nameWindow = std::make_unique<juce::AlertWindow> ("Save preset",
                                                      "What should this one be called?",
                                                      juce::MessageBoxIconType::NoIcon);

    nameWindow->addTextEditor ("name", presets.getCurrentName(), "Name");
    nameWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // Async: a plugin must never run a modal loop, which would block the host's message thread.
    nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        const auto name = result == 1 ? nameWindow->getTextEditorContents ("name") : juce::String();

        nameWindow.reset();

        if (name.isNotEmpty())
            report (presets.save (name));
    }), false);
}

void PresetRow::resized()
{
    auto area = getLocalBounds();

    previousButton.setBounds (area.removeFromLeft (26));
    area.removeFromLeft (4);
    nextButton.setBounds (area.removeFromRight (26));
    area.removeFromRight (4);

    nameButton.setBounds (area);
}
