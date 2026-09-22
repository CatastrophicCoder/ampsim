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
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (shouldDrawButtonAsDown ? AmpPalette::railRecess.darker (0.3f)
                                        : (shouldDrawButtonAsHighlighted ? AmpPalette::railRecess.brighter (0.25f)
                                                                         : AmpPalette::railRecess));
    g.fillRect (bounds);

    g.setColour (AmpPalette::enamel.withAlpha (isLoaded ? 0.28f : 0.12f));
    g.drawRect (bounds, 1.0f);

    auto text = getLocalBounds().reduced (7, 4);

    g.setFont (AmpLookAndFeel::panelFont (10.0f));
    g.setColour (AmpPalette::enamel.withAlpha (0.45f));
    g.drawText (position, text.removeFromTop (13), juce::Justification::centredLeft, false);

    g.setFont (AmpLookAndFeel::panelFont (12.0f));
    g.setColour (AmpPalette::enamel.withAlpha (isLoaded ? 0.92f : 0.3f));
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

    const auto error = processorRef.getImpulseResponseError();

    AmpLookAndFeel::drawRailText (g, error.isNotEmpty() ? error : "mic position",
                                  nameRow, juce::Justification::centredLeft,
                                  AmpLookAndFeel::panelFont (11.0f),
                                  error.isNotEmpty() ? AmpPalette::attention.brighter (0.35f)
                                                     : AmpPalette::enamel.withAlpha (0.35f));
}

void CabinetRow::resized()
{
    auto area = getLocalBounds();

    auto nameRow = area.removeFromTop (tileNameHeight);
    bypassButton.setBounds (nameRow.removeFromRight (34).withTrimmedRight (8));

    area = area.reduced (8, 4);

    // The two position controls sit to the right of the corners they move between.
    auto knobArea = area.removeFromRight (128);
    axisKnob.setBounds (knobArea.removeFromLeft (64).reduced (2, 0));
    distanceKnob.setBounds (knobArea.reduced (2, 0));

    const auto slotWidth = area.getWidth() / CabSim::numSlots;

    for (auto* slot : slots)
        slot->setBounds (area.removeFromLeft (slotWidth).reduced (2, 2));
}
