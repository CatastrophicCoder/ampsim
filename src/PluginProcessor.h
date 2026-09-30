/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "MidiLearn.h"
#include "PresetManager.h"
#include "ModelLoader.h"
#include "dsp/AmpModel.h"
#include "dsp/CabSim.h"
#include "dsp/PedalChain.h"
#include "dsp/ToneStack.h"
#include "dsp/Tuner.h"

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
    // Named after the power-amp controls they sit where, not after the mechanism: see ToneStack.
    inline constexpr const char* presence   = "presence";
    inline constexpr const char* depth      = "depth";
    inline constexpr const char* bypass     = "bypass";
    inline constexpr const char* cabBypass  = "cabBypass";
    inline constexpr const char* tunerOn    = "tunerOn";
    // The amp's own power switch. An amp with no power makes no sound, so this mutes rather than
    // passing the signal through — which is what `bypass` is for.
    inline constexpr const char* power      = "power";
    inline constexpr const char* micAxis     = "micAxis";
    inline constexpr const char* micDistance = "micDistance";
    inline constexpr const char* cabLowCut   = "cabLowCut";
    inline constexpr const char* cabHighCut  = "cabHighCut";

    // The pedals. "Engaged" rather than "bypass": a pedal is off until you step on it, and the
    // parameter reads the way the footswitch does.
    inline constexpr const char* gateOn          = "gateOn";
    inline constexpr const char* gateThreshold   = "gateThreshold";

    inline constexpr const char* compOn          = "compOn";
    inline constexpr const char* compAmount      = "compAmount";
    inline constexpr const char* compLevel       = "compLevel";

    // The dirt slot. "drive" in these IDs is what it was called when it held one pedal; the names
    // stay so that a saved session still finds its settings.
    inline constexpr const char* dirtType        = "dirtType";
    inline constexpr const char* driveOn         = "driveOn";
    inline constexpr const char* driveAmount     = "driveAmount";
    inline constexpr const char* driveTone       = "driveTone";
    inline constexpr const char* driveLevel      = "driveLevel";

    // The overdrive and the clean boost, which the dirt slot can hold instead.
    inline constexpr const char* odAmount         = "odAmount";
    inline constexpr const char* odTone           = "odTone";
    inline constexpr const char* odLevel          = "odLevel";

    inline constexpr const char* boostLevel       = "boostLevel";
    inline constexpr const char* boostTone        = "boostTone";

    // The modulation slot, whose IDs likewise keep the chorus's name.
    inline constexpr const char* modulationType   = "modulationType";
    inline constexpr const char* chorusOn        = "chorusOn";
    inline constexpr const char* chorusRate      = "chorusRate";
    inline constexpr const char* chorusDepth     = "chorusDepth";
    inline constexpr const char* chorusMix       = "chorusMix";

    inline constexpr const char* flangerRate      = "flangerRate";
    inline constexpr const char* flangerDepth     = "flangerDepth";
    inline constexpr const char* flangerFeedback  = "flangerFeedback";
    inline constexpr const char* flangerMix       = "flangerMix";

    inline constexpr const char* phaserRate       = "phaserRate";
    inline constexpr const char* phaserDepth      = "phaserDepth";
    inline constexpr const char* phaserFeedback   = "phaserFeedback";
    inline constexpr const char* phaserMix        = "phaserMix";

    inline constexpr const char* tremoloRate      = "tremoloRate";
    inline constexpr const char* tremoloDepth     = "tremoloDepth";
    inline constexpr const char* tremoloShape     = "tremoloShape";

    inline constexpr const char* delayOn         = "delayOn";
    inline constexpr const char* delayTime       = "delayTime";
    // "Free" plus a set of note lengths. A choice rather than a switch and a second control: one
    // knob that reads Free at one end is the whole of what a tempo sync needs to say.
    inline constexpr const char* delayDivision   = "delayDivision";
    inline constexpr const char* delayFeedback   = "delayFeedback";
    inline constexpr const char* delayMix        = "delayMix";

    inline constexpr const char* reverbOn        = "reverbOn";
    inline constexpr const char* reverbSize      = "reverbSize";
    inline constexpr const char* reverbMix       = "reverbMix";
}

/** Non-automatable state, stored as properties on the APVTS tree rather than as parameters:
    a file path is not something a host can sweep. */
namespace StateID
{
    inline constexpr const char* modelPath = "modelPath";
    // One path per mic-position corner. "irPath" keeps its name: it was the only slot before
    // there were four, and sessions saved then still look it up.
    inline constexpr const char* irPath    = "irPath";
    inline constexpr const char* irPathEdgeClose = "irPathEdgeClose";
    inline constexpr const char* irPathCentreFar = "irPathCentreFar";
    inline constexpr const char* irPathEdgeFar   = "irPathEdgeFar";
    inline constexpr const char* presetName      = "presetName";
    // The panel's size, as a percentage. Not a parameter and not part of a preset: it is how big
    // you like the window, not how the amp sounds.
    inline constexpr const char* panelScale      = "panelScale";
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
    bool acceptsMidi() const override                    { return true; }   // CC control
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

    /** Whether a new instance writes out the built-in amp and cab and loads them. On in the
        plugin, so it makes a sound the first time it is opened; off in the tests, which want a
        bare chain and set their own inputs. */
    static inline bool loadBundledAssetsOnCreation = true;

    //==============================================================================
    /** Message thread. Starts a background load; the model arrives in the audio thread later. */
    void loadModel (const juce::File& file);

    /** The model file currently loaded or being loaded, or a non-existent File if none. */
    juce::File getModelFile() const;

    bool isModelLoaded() const noexcept { return ampModel.hasModel(); }

    /** Last load error, empty if the last load succeeded or none has been attempted. */
    juce::String getModelError() const { return modelLoader.getLastError(); }

    /** Message thread. Loads a `.wav` impulse response into one corner of the mic-position grid. */
    void loadImpulseResponse (CabSim::Slot, const juce::File& file);
    void clearImpulseResponse (CabSim::Slot);

    /** The IR in a slot, or a non-existent File if that corner is empty. */
    juce::File getImpulseResponseFile (CabSim::Slot = CabSim::Slot::centreClose) const;

    bool isImpulseResponseLoaded() const { return cabSim.hasImpulseResponse(); }
    bool isSlotLoaded (CabSim::Slot slot) const { return cabSim.isSlotLoaded (slot); }

    /** The state key each slot's path is saved under. */
    static const char* slotStateID (CabSim::Slot);

    /** The tuner taps the signal before the pedals and the amp, so it reads the guitar rather
        than what the drive pedal and the model have made of it. Analysis runs on the message
        thread — see Tuner. */
    Tuner& getTuner() { return tuner; }

    /** For tests, which have to drive the message thread's work by hand. */
    AmpModel& getAmpModelForTesting() { return ampModel; }
    MidiLearn& getMidiLearn() { return midiLearn; }

    //==============================================================================
    /** The whole state, as a preset file stores it. */
    juce::ValueTree getPresetState() { return apvts.copyState(); }

    /** Applies a preset. Anything the preset leaves empty — a model path, a cab IR — keeps
        whatever is currently loaded, so a preset that only sets the controls does exactly that. */
    void applyPresetState (const juce::ValueTree&);

    juce::String getCurrentPresetName() const;
    void setCurrentPresetName (const juce::String&);

    PresetManager& getPresets() { return presets; }
    bool isTunerEngaged() const { return tunerParam->get(); }
    bool isPowered() const      { return powerParam->get(); }

    /** Whether the end of the chain should be silent. Both reasons are the amp not reaching the
        speaker, so they share one ramp. */
    bool mutedNow() const       { return tunerParam->get() || ! powerParam->get(); }

    /** Last IR load error, empty if the last one succeeded or none has been attempted. */
    juce::String getImpulseResponseError() const { return irError; }

    /** Called on the message thread whenever the loaded model or IR, or their error state,
        changes. */
    std::function<void()> onLoadStateChanged;

private:
    void timerCallback() override;

    /** Shared by setStateInformation and applyPresetState. */
    void applyState (juce::ValueTree, bool keepLoadedFilesWhenEmpty);

    /** Reads the pedal parameters into one settings object, once per block. */
    PedalChain::Settings currentPedalSettings() const;

    /** The delay's time in seconds: the knob, or the host's tempo cut into whatever note length
        the division control is set to. */
    float currentDelaySeconds() const;

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
    juce::AudioParameterBool*  tunerParam      = nullptr;
    juce::AudioParameterBool*  powerParam      = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* cabLowCutParam = nullptr;
    juce::AudioParameterFloat* cabHighCutParam = nullptr;
    juce::AudioParameterFloat* micAxisParam    = nullptr;
    juce::AudioParameterFloat* micDistanceParam = nullptr;

    juce::dsp::Gain<float> inputGain, outputGain;

    // 1 = fully bypassed. Ramped so toggling bypass cannot click.
    juce::SmoothedValue<float> bypassMix;

    // Pre-allocated scratch; never resized on the audio thread.
    juce::AudioBuffer<float> dryBuffer;    // dry copy for the bypass crossfade
    juce::AudioBuffer<float> monoBuffer;   // the mono signal the amp model sees

    AmpModel ampModel;
    ModelLoader modelLoader { ampModel };
    MidiLearn midiLearn { apvts };
    ToneStack toneStack;
    Tuner tuner;

    // A tuner pedal mutes while you use it, so you can tune without the room hearing it.
    /** Silence at the end of the chain, from the tuner or from the power switch. One smoother for
        both, because they want the same thing and two ramps would fight over the same samples. */
    juce::SmoothedValue<float> outputMute;
    PedalChain pedals;
    CabSim cabSim;
    PresetManager presets { *this };
    juce::String irError;
    int reportedLatency = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessor)
};
