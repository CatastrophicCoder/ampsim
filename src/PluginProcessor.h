#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

/** Parameter IDs. Kept in one place because the editor, the APVTS layout and any future
    host automation all have to agree on them, and a typo silently creates a dead knob.

    Never change an existing ID or its version hint: a saved session looks parameters up by
    ID, so a rename loses the user's settings. Add new ones instead.
*/
namespace ParamID
{
    inline constexpr const char* inputGain  = "inputGain";
    inline constexpr const char* outputGain = "outputGain";
    inline constexpr const char* bypass     = "bypass";
}

/** Milestone 1: input gain → (chain) → output gain, with a click-free bypass.

    The chain itself is still empty; the plumbing here — APVTS, smoothing, dry/wet crossfade,
    state save/reload — is what the later milestones hang their DSP off.
*/
class AmpSimAudioProcessor final : public juce::AudioProcessor
{
public:
    AmpSimAudioProcessor();
    ~AmpSimAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                      { return true; }

    const juce::String getName() const override          { return JucePlugin_Name; }
    bool acceptsMidi() const override                    { return false; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 0.0; }

    /** Letting the host drive the bypass parameter keeps its own bypass button in sync with
        ours, and stops the wrapper falling back to processBlockBypassed(). */
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Cached: looking a parameter up by string on the audio thread is a map lookup per block.
    juce::AudioParameterFloat* inputGainParam  = nullptr;
    juce::AudioParameterFloat* outputGainParam = nullptr;
    juce::AudioParameterBool*  bypassParam     = nullptr;

    juce::dsp::Gain<float> inputGain, outputGain;

    // 1 = fully bypassed. Ramped so toggling bypass cannot click.
    juce::SmoothedValue<float> bypassMix;

    // Pre-allocated dry copy for the crossfade; never resized on the audio thread.
    juce::AudioBuffer<float> dryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessor)
};
