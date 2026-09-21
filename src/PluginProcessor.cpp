#include "PluginProcessor.h"
#include "PluginEditor.h"

AmpSimAudioProcessor::AmpSimAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AmpSimAudioProcessor::prepareToPlay (double, int)
{
}

void AmpSimAudioProcessor::releaseResources()
{
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

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Pass-through until milestone 1.
}

juce::AudioProcessorEditor* AmpSimAudioProcessor::createEditor()
{
    return new AmpSimAudioProcessorEditor (*this);
}

void AmpSimAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
    // No parameters yet; the APVTS arrives in milestone 1.
}

void AmpSimAudioProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmpSimAudioProcessor();
}
