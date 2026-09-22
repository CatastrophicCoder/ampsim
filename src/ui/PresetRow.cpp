#include "PresetRow.h"

namespace
{
    constexpr int nameColumn = 46;
}

PresetRow::PresetRow (PresetManager& presetsToUse)
    : presets (presetsToUse)
{
    previousButton.onClick = [this] { presets.step (-1); };
    nextButton.onClick = [this] { presets.step (1); };
    menuButton.onClick = [this] { showMenu(); };

    for (auto* button : { &previousButton, &nextButton, &menuButton })
        addAndMakeVisible (*button);

    updateContents();
}

void PresetRow::updateContents()
{
    const auto name = presets.getCurrentName();

    displayed = name.isEmpty() ? "no preset" : name;
    repaint();
}

void PresetRow::report (const juce::String& error)
{
    message = error;
    repaint();
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

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton),
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

void PresetRow::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    AmpLookAndFeel::drawRailText (g, "preset", area.removeFromLeft (nameColumn),
                                  juce::Justification::centredLeft,
                                  AmpLookAndFeel::panelFont (13.0f, true),
                                  AmpPalette::enamel.withAlpha (0.5f));

    area.removeFromRight (previousButton.getWidth() + nextButton.getWidth()
                          + menuButton.getWidth() + 16);

    const auto plate = area.toFloat().reduced (0.0f, 3.0f);

    g.setColour (AmpPalette::railRecess);
    g.fillRect (plate);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawLine (plate.getX(), plate.getY(), plate.getRight(), plate.getY(), 1.0f);

    g.setColour (AmpPalette::enamel.withAlpha (0.10f));
    g.drawLine (plate.getX(), plate.getBottom(), plate.getRight(), plate.getBottom(), 1.0f);

    const auto failed = message.isNotEmpty();

    g.setFont (AmpLookAndFeel::panelFont (14.0f));
    g.setColour (failed ? AmpPalette::attention.brighter (0.35f) : AmpPalette::enamel.withAlpha (0.92f));
    g.drawText (failed ? message : displayed, area.reduced (12, 0),
                juce::Justification::centredLeft, true);
}

void PresetRow::resized()
{
    auto area = getLocalBounds().withTrimmedTop (3).withTrimmedBottom (3);

    menuButton.setBounds (area.removeFromRight (86));
    area.removeFromRight (6);
    nextButton.setBounds (area.removeFromRight (30));
    previousButton.setBounds (area.removeFromRight (30));
}
