#include "PluginEditor.h"

AmpSimAudioProcessorEditor::AmpSimAudioProcessorEditor (AmpSimAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (600, 300);
}

void AmpSimAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (22.0f));
    g.drawFittedText ("AmpSim — milestone 0", getLocalBounds(), juce::Justification::centred, 1);
}

void AmpSimAudioProcessorEditor::resized()
{
}
