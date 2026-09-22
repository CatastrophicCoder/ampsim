#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

/** A labelled rotary knob bound to one APVTS parameter.

    Deliberately plain: the amp-style LookAndFeel is milestone 5. What matters here is that
    the binding goes through an attachment, so the editor never writes to the processor directly.
*/
class LabelledKnob final : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  const juce::String& labelText);

    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabelledKnob)
};

class AmpSimAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AmpSimAudioProcessorEditor (AmpSimAudioProcessor&);
    ~AmpSimAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AmpSimAudioProcessor& processorRef;

    void updateModelDisplay();

    LabelledKnob inputKnob, outputKnob;
    juce::ToggleButton bypassButton { "Bypass" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    juce::TextButton loadModelButton { "Load model..." };
    juce::Label modelLabel;

    juce::TextButton loadIRButton { "Load cab IR..." };
    juce::Label irLabel;
    juce::ToggleButton cabBypassButton { "Cab bypass" };
    juce::AudioProcessorValueTreeState::ButtonAttachment cabBypassAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessorEditor)
};
