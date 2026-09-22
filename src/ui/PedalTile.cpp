#include "PedalTile.h"

namespace
{
    constexpr int tileNameHeight = 20;
    constexpr int knobNameHeight = 14;
}

CompactKnob::CompactKnob (juce::AudioProcessorValueTreeState& state,
                          const juce::String& parameterID,
                          const juce::String& labelText)
    : name (labelText), slider (parameterID), attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);
}

void CompactKnob::parentHierarchyChanged()
{
    // The value popup is drawn inside whatever component it is given, and a compact knob is about
    // 60 px across — parented to itself, the bubble was clipped away to nothing. It belongs to the
    // whole editor, which is also the only component here big enough to hold it.
    slider.setPopupDisplayEnabled (true, true, getTopLevelComponent());
}

void CompactKnob::paint (juce::Graphics& g)
{
    AmpLookAndFeel::drawRailText (g, name, getLocalBounds().removeFromBottom (knobNameHeight),
                                  juce::Justification::centred,
                                  AmpLookAndFeel::panelFont (11.0f),
                                  AmpPalette::enamel.withAlpha (0.6f));
}

void CompactKnob::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedBottom (knobNameHeight));
}

//==============================================================================
PedalTile::PedalTile (juce::AudioProcessorValueTreeState& state,
                      const juce::String& pedalName,
                      const char* engageParameterID,
                      std::initializer_list<Knob> knobsToAdd)
    : name (pedalName), engageAttachment (state, engageParameterID, engageButton)
{
    // Blue, not red: on a pedal the lamp means "in your signal", the opposite of what the red
    // bypass lamps on the amp mean.
    engageButton.setColour (juce::ToggleButton::tickColourId, AmpPalette::reading);
    addAndMakeVisible (engageButton);

    for (const auto& knob : knobsToAdd)
        addAndMakeVisible (knobs.add (new CompactKnob (state, knob.parameterID, knob.label)));
}

void PedalTile::setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
{
    for (auto* knob : knobs)
        knob->setContextMenuHandler (handler);
}

void PedalTile::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (juce::Colour (0xff2e332b));
    g.fillRect (bounds);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (bounds.getX(), bounds.getY(), bounds.getRight(), bounds.getY(), 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRect (bounds, 1.0f);

    AmpLookAndFeel::drawRailText (g, name,
                                  getLocalBounds().removeFromTop (tileNameHeight).reduced (10, 0),
                                  juce::Justification::centredLeft,
                                  AmpLookAndFeel::panelFont (13.0f, true),
                                  AmpPalette::enamel.withAlpha (0.9f));
}

void PedalTile::resized()
{
    auto area = getLocalBounds();

    // The footswitch lamp sits with the name, at the top of the enclosure.
    auto nameRow = area.removeFromTop (tileNameHeight);
    engageButton.setBounds (nameRow.removeFromRight (34).withTrimmedRight (8));

    area = area.reduced (6, 4);

    if (knobs.isEmpty())
        return;

    const auto width = area.getWidth() / knobs.size();

    for (auto* knob : knobs)
        knob->setBounds (area.removeFromLeft (width).reduced (2, 0));
}
