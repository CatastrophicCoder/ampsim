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
      bypassAttachment (p.getValueTreeState(), ParamID::bypass, bypassButton),
      cabBypassAttachment (p.getValueTreeState(), ParamID::cabBypass, cabBypassButton)
{
    addAndMakeVisible (inputKnob);
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (bypassButton);

    // Held as a member: the chooser has to outlive the click handler, since it runs asynchronously.
    const auto chooseFile = [this] (const juce::String& title,
                                    const juce::File& startingFile,
                                    const juce::String& pattern,
                                    std::function<void (const juce::File&)> onChosen)
    {
        fileChooser = std::make_unique<juce::FileChooser> (title, startingFile, pattern);

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [onChosen] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();

                                      if (file != juce::File())
                                          onChosen (file);
                                  });
    };

    loadModelButton.onClick = [this, chooseFile]
    {
        chooseFile ("Load a NAM model", processorRef.getModelFile(), "*.nam",
                    [this] (const juce::File& file) { processorRef.loadModel (file); });
    };
    addAndMakeVisible (loadModelButton);

    loadIRButton.onClick = [this, chooseFile]
    {
        chooseFile ("Load a cabinet impulse response", processorRef.getImpulseResponseFile(), "*.wav;*.aiff;*.aif",
                    [this] (const juce::File& file) { processorRef.loadImpulseResponse (file); });
    };
    addAndMakeVisible (loadIRButton);

    modelLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (modelLabel);

    irLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (irLabel);

    addAndMakeVisible (cabBypassButton);

    // The load finishes on a background thread, so the editor is told rather than polling.
    processorRef.onLoadStateChanged = [this] { updateModelDisplay(); };
    updateModelDisplay();

    setSize (420, 400);
}

AmpSimAudioProcessorEditor::~AmpSimAudioProcessorEditor()
{
    processorRef.onLoadStateChanged = nullptr;
}

void AmpSimAudioProcessorEditor::updateModelDisplay()
{
    const auto show = [] (juce::Label& label, const juce::String& error,
                          const juce::File& file, const juce::String& emptyText)
    {
        const auto failed = error.isNotEmpty();

        label.setText (failed ? error
                              : (file == juce::File() ? emptyText : file.getFileNameWithoutExtension()),
                       juce::dontSendNotification);

        label.setColour (juce::Label::textColourId,
                         failed ? juce::Colours::orangered : juce::Colours::whitesmoke);
    };

    show (modelLabel, processorRef.getModelError(), processorRef.getModelFile(), "No model loaded");
    show (irLabel, processorRef.getImpulseResponseError(), processorRef.getImpulseResponseFile(), "No cab IR loaded");
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

    irLabel.setBounds (area.removeFromBottom (labelHeight));

    auto irRow = area.removeFromBottom (labelHeight + margin / 2);
    cabBypassButton.setBounds (irRow.removeFromRight (120).withSizeKeepingCentre (120, labelHeight));
    loadIRButton.setBounds (irRow.withSizeKeepingCentre (150, labelHeight));

    modelLabel.setBounds (area.removeFromBottom (labelHeight));
    loadModelButton.setBounds (area.removeFromBottom (labelHeight + margin / 2)
                                   .withSizeKeepingCentre (150, labelHeight));

    const auto knobWidth = area.getWidth() / 2;
    inputKnob .setBounds (area.removeFromLeft (knobWidth).reduced (margin / 2, 0));
    outputKnob.setBounds (area.reduced (margin / 2, 0));
}
