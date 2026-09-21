#include "PluginEditor.h"

namespace
{
    constexpr int labelHeight = 22;
    constexpr int margin      = 20;
}

LabelledKnob::LabelledKnob (juce::AudioProcessorValueTreeState& state,
                            const juce::String& parameterID,
                            const juce::String& labelText)
    : attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, labelHeight);
    addAndMakeVisible (slider);

    label.setText (labelText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
}

void LabelledKnob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (labelHeight));
    slider.setBounds (area);
}

AmpSimAudioProcessorEditor::AmpSimAudioProcessorEditor (AmpSimAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      inputKnob  (p.getValueTreeState(), ParamID::inputGain,  "Input"),
      outputKnob (p.getValueTreeState(), ParamID::outputGain, "Output"),
      bypassAttachment (p.getValueTreeState(), ParamID::bypass, bypassButton)
{
    addAndMakeVisible (inputKnob);
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (bypassButton);

    setSize (420, 260);
}

void AmpSimAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    g.setColour (juce::Colours::whitesmoke);
    g.setFont (juce::FontOptions (18.0f));
    g.drawFittedText ("AmpSim",
                      getLocalBounds().removeFromTop (margin + labelHeight),
                      juce::Justification::centred, 1);
}

void AmpSimAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);
    area.removeFromTop (labelHeight);                         // title

    auto footer = area.removeFromBottom (labelHeight + margin);
    bypassButton.setBounds (footer.withSizeKeepingCentre (110, labelHeight));

    const auto knobWidth = area.getWidth() / 2;
    inputKnob .setBounds (area.removeFromLeft (knobWidth).reduced (margin / 2, 0));
    outputKnob.setBounds (area.reduced (margin / 2, 0));
}
