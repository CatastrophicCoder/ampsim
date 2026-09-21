#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float gainRangeDb    = 24.0f;
    constexpr double gainRampSeconds   = 0.05;
    constexpr double bypassRampSeconds = 0.02;

    juce::NormalisableRange<float> decibelRange()
    {
        juce::NormalisableRange<float> range { -gainRangeDb, gainRangeDb, 0.1f };
        range.setSkewForCentre (0.0f);   // 0 dB sits in the middle of the knob's travel
        return range;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AmpSimAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto dbAttributes = juce::AudioParameterFloatAttributes()
                                  .withLabel ("dB")
                                  .withStringFromValueFunction ([] (float v, int)
                                                                { return juce::String (v, 1) + " dB"; });

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::inputGain, 1 }, "Input Gain",
        decibelRange(), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::outputGain, 1 }, "Output Gain",
        decibelRange(), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::bypass, 1 }, "Bypass", false));

    return layout;
}

AmpSimAudioProcessor::AmpSimAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AmpSim", createParameterLayout())
{
    inputGainParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::inputGain));
    outputGainParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::outputGain));
    bypassParam     = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter (ParamID::bypass));

    jassert (inputGainParam != nullptr && outputGainParam != nullptr && bypassParam != nullptr);
}

void AmpSimAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (samplesPerBlock),
                                        static_cast<juce::uint32> (getTotalNumOutputChannels()) };

    for (auto* gain : { &inputGain, &outputGain })
    {
        gain->prepare (spec);
        gain->setRampDurationSeconds (gainRampSeconds);
    }

    // A freshly constructed juce::dsp::Gain sits at 0, so setting the target alone would make
    // the plugin fade in over the ramp duration every time the host starts playback. Snap to
    // the parameter values instead: reset() pulls the smoother's current value up to its target.
    inputGain .setGainDecibels (inputGainParam->get());
    outputGain.setGainDecibels (outputGainParam->get());
    inputGain .reset();
    outputGain.reset();

    bypassMix.reset (sampleRate, bypassRampSeconds);
    bypassMix.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    dryBuffer.setSize (getTotalNumOutputChannels(), samplesPerBlock, false, false, true);
}

void AmpSimAudioProcessor::releaseResources()
{
    dryBuffer.setSize (0, 0);
}

bool AmpSimAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // The chain is mono until the cab; mono-in/stereo-out is allowed so a stereo
    // modulation stage can widen it later without a layout change.
    return layouts.getMainInputChannelSet() == out
        || (layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
            && out == juce::AudioChannelSet::stereo());
}

void AmpSimAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples  = buffer.getNumSamples();
    const auto numChannels = getTotalNumOutputChannels();

    for (auto ch = getTotalNumInputChannels(); ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    bypassMix.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    // Settled at fully bypassed: the dry signal is already in the buffer, so there is
    // nothing to do. Keep the gains' ramps in step for when bypass comes off again.
    if (! bypassMix.isSmoothing() && bypassMix.getCurrentValue() >= 1.0f)
    {
        inputGain .setGainDecibels (inputGainParam->get());
        outputGain.setGainDecibels (outputGainParam->get());
        return;
    }

    const bool needsCrossfade = bypassMix.isSmoothing() || bypassMix.getCurrentValue() > 0.0f;

    if (needsCrossfade)
    {
        jassert (dryBuffer.getNumSamples() >= numSamples);   // prepareToPlay sized it

        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    inputGain .setGainDecibels (inputGainParam->get());
    outputGain.setGainDecibels (outputGainParam->get());

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    inputGain.process (context);

    // Milestone 2 onwards: the amp, cab and pedal chain go here.

    outputGain.process (context);

    if (needsCrossfade)
    {
        // The smoothing is linear, so the ramp over this block is fully described by its
        // endpoints — let JUCE apply it per channel instead of stepping sample by sample.
        const auto startMix = bypassMix.getCurrentValue();
        bypassMix.skip (numSamples);
        const auto endMix = bypassMix.getCurrentValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.applyGainRamp (ch, 0, numSamples, 1.0f - startMix, 1.0f - endMix);
            buffer.addFromWithRamp (ch, 0, dryBuffer.getReadPointer (ch), numSamples, startMix, endMix);
        }
    }
}

juce::AudioProcessorEditor* AmpSimAudioProcessor::createEditor()
{
    return new AmpSimAudioProcessorEditor (*this);
}

void AmpSimAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpSimAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // Only the parameters are touched here. This runs on the message thread, so the DSP
    // objects are left alone — processBlock picks the new values up on its next call and
    // ramps to them, which is also what stops a preset change from clicking.
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmpSimAudioProcessor();
}
