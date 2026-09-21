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

    loadModelButton.onClick = [this]
    {
        // Held as a member: the chooser has to outlive this call, since it runs asynchronously.
        fileChooser = std::make_unique<juce::FileChooser> ("Load a NAM model",
                                                           processorRef.getModelFile(),
                                                           "*.nam");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();

                                      if (file != juce::File())
                                          processorRef.loadModel (file);
                                  });
    };
    addAndMakeVisible (loadModelButton);

    modelLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modelLabel);

    // The load finishes on a background thread, so the editor is told rather than polling.
    processorRef.onModelChanged = [this] { updateModelDisplay(); };
    updateModelDisplay();

    setSize (420, 320);
}

AmpSimAudioProcessorEditor::~AmpSimAudioProcessorEditor()
{
    processorRef.onModelChanged = nullptr;
}

void AmpSimAudioProcessorEditor::updateModelDisplay()
{
    const auto error = processorRef.getModelError();

    if (error.isNotEmpty())
    {
        modelLabel.setText (error, juce::dontSendNotification);
        modelLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
        return;
    }

    const auto file = processorRef.getModelFile();

    modelLabel.setText (file == juce::File() ? "No model loaded" : file.getFileNameWithoutExtension(),
                        juce::dontSendNotification);
    modelLabel.setColour (juce::Label::textColourId, juce::Colours::whitesmoke);
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

    modelLabel.setBounds (area.removeFromBottom (labelHeight));
    loadModelButton.setBounds (area.removeFromBottom (labelHeight + margin / 2)
                                   .withSizeKeepingCentre (150, labelHeight));

    const auto knobWidth = area.getWidth() / 2;
    inputKnob .setBounds (area.removeFromLeft (knobWidth).reduced (margin / 2, 0));
    outputKnob.setBounds (area.reduced (margin / 2, 0));
}
