#pragma once

#include "ModelLoader.h"
#include "dsp/AmpModel.h"
#include "dsp/CabSim.h"
#include "dsp/ToneStack.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

/** Parameter IDs. Kept in one place because the editor, the APVTS layout and any future
    host automation all have to agree on them, and a typo silently creates a dead knob.

    Never change an existing ID or its version hint: a saved session looks parameters up by
    ID, so a rename loses the user's settings. Add new ones instead.
*/
namespace ParamID
{
    // "inputGain" and "outputGain" are the amp's Gain and Master. The IDs keep their original
    // spelling because a saved session looks parameters up by ID.
    inline constexpr const char* inputGain  = "inputGain";
    inline constexpr const char* outputGain = "outputGain";
    inline constexpr const char* bass       = "bass";
    inline constexpr const char* mid        = "mid";
    inline constexpr const char* treble     = "treble";
    inline constexpr const char* bypass     = "bypass";
    inline constexpr const char* cabBypass  = "cabBypass";
}

/** Non-automatable state, stored as properties on the APVTS tree rather than as parameters:
    a file path is not something a host can sweep. */
namespace StateID
{
    inline constexpr const char* modelPath = "modelPath";
    inline constexpr const char* irPath    = "irPath";
}

/** Milestone 2: input gain → NAM amp model → output gain, with a click-free bypass.

    The model runs at the rate it was trained at, so the plugin reports the resampling latency to
    the host. Cab and pedals follow in later milestones.
*/
class AmpSimAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
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

    //==============================================================================
    /** Message thread. Starts a background load; the model arrives in the audio thread later. */
    void loadModel (const juce::File& file);

    /** The model file currently loaded or being loaded, or a non-existent File if none. */
    juce::File getModelFile() const;

    bool isModelLoaded() const noexcept { return ampModel.hasModel(); }

    /** Last load error, empty if the last load succeeded or none has been attempted. */
    juce::String getModelError() const { return modelLoader.getLastError(); }

    /** Message thread. Loads a `.wav` impulse response for the cab. */
    void loadImpulseResponse (const juce::File& file);

    /** The IR currently loaded, or a non-existent File if none. */
    juce::File getImpulseResponseFile() const;

    bool isImpulseResponseLoaded() const { return cabSim.hasImpulseResponse(); }

    /** Last IR load error, empty if the last one succeeded or none has been attempted. */
    juce::String getImpulseResponseError() const { return irError; }

    /** Called on the message thread whenever the loaded model or IR, or their error state,
        changes. */
    std::function<void()> onLoadStateChanged;

private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Cached: looking a parameter up by string on the audio thread is a map lookup per block.
    juce::AudioParameterFloat* inputGainParam  = nullptr;   // "Gain", before the model
    juce::AudioParameterFloat* outputGainParam = nullptr;   // "Master", after the tone stack
    juce::AudioParameterFloat* bassParam       = nullptr;
    juce::AudioParameterFloat* midParam        = nullptr;
    juce::AudioParameterFloat* trebleParam     = nullptr;
    juce::AudioParameterBool*  bypassParam     = nullptr;
    juce::AudioParameterBool*  cabBypassParam  = nullptr;

    juce::dsp::Gain<float> inputGain, outputGain;

    // 1 = fully bypassed. Ramped so toggling bypass cannot click.
    juce::SmoothedValue<float> bypassMix;

    // Pre-allocated scratch; never resized on the audio thread.
    juce::AudioBuffer<float> dryBuffer;    // dry copy for the bypass crossfade
    juce::AudioBuffer<float> monoBuffer;   // the mono signal the amp model sees

    AmpModel ampModel;
    ModelLoader modelLoader { ampModel };
    ToneStack toneStack;
    CabSim cabSim;
    juce::String irError;
    int reportedLatency = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessor)
};
