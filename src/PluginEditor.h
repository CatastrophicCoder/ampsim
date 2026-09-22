#pragma once

#include "PluginProcessor.h"
#include "ui/AmpLookAndFeel.h"
#include "ui/PedalTile.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** One control on the plate: engraved name above, the knob, its value below.

    The binding goes through an attachment, so the editor never writes to the processor directly.
*/
class LabelledKnob final : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  const juce::String& labelText);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String name;
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabelledKnob)
};

/** A loaded file, shown the way a chassis carries a stamped nameplate. */
class NameplateRow final : public juce::Component
{
public:
    NameplateRow (const juce::String& rowName, const juce::String& browseText);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setContents (const juce::String& text, bool isError);

    juce::TextButton browseButton;
    std::function<void()> onBrowse;

private:
    juce::String name, contents { "—" };
    bool showingError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NameplateRow)
};

class AmpSimAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit AmpSimAudioProcessorEditor (AmpSimAudioProcessor&);
    ~AmpSimAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintTuner (juce::Graphics&, juce::Rectangle<int>);
    void updateLoadedFileDisplay();
    void chooseFile (const juce::String& title, const juce::File& startingFile,
                     const juce::String& pattern, std::function<void (const juce::File&)> onChosen);

    AmpSimAudioProcessor& processorRef;
    AmpLookAndFeel lookAndFeel;

    // Gain, the three tone bands, then Master — the order they sit in the chain and on the panel.
    LabelledKnob gainKnob, bassKnob, midKnob, trebleKnob, masterKnob;

    juce::ToggleButton bypassButton { "bypassed" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    juce::ToggleButton tunerButton { "tuner" };
    juce::AudioProcessorValueTreeState::ButtonAttachment tunerAttachment;

    // Two rows, so the panel says which side of the amp each pedal is on — which is the whole
    // design of the pedal section, not a detail of it.
    PedalTile gatePedal, compressorPedal, drivePedal;
    PedalTile chorusPedal, delayPedal, reverbPedal;

    NameplateRow ampRow { "amp", "Load model" };
    NameplateRow cabRow { "cab", "Load IR" };

    // Named for what it does to the signal, in the same words as the plugin's own bypass.
    juce::ToggleButton cabBypassButton { "bypassed" };
    juce::AudioProcessorValueTreeState::ButtonAttachment cabBypassAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessorEditor)
};
