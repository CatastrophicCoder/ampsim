#include "CabSim.h"

namespace
{
    constexpr double bypassRampSeconds = 0.02;
}

void CabSim::prepare (double sampleRate, int maxBlockSize)
{
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    convolution.prepare (spec);

    bypassMix.reset (sampleRate, bypassRampSeconds);
    bypassMix.setCurrentAndTargetValue (0.0f);

    dryBuffer.setSize (1, maxBlockSize, false, false, true);
}

void CabSim::reset()
{
    convolution.reset();
    bypassMix.setCurrentAndTargetValue (bypassMix.getTargetValue());
}

void CabSim::loadImpulseResponse (const juce::File& file)
{
    // Stereo::no: the chain is mono until the cab, so a stereo IR is folded down rather than
    // silently widening the signal. Normalise::no keeps the IR's own level, so swapping IRs
    // changes tone rather than volume.
    convolution.loadImpulseResponse (file,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::yes,
                                     0,
                                     juce::dsp::Convolution::Normalise::no);

    irLoaded.store (true);
}

void CabSim::process (float* samples, int numSamples, bool bypassed)
{
    bypassMix.setTargetValue (bypassed ? 1.0f : 0.0f);

    const auto noIR = ! irLoaded.load();

    // Nothing loaded, or settled at bypassed: the signal is already what it should be.
    if (noIR || (! bypassMix.isSmoothing() && bypassMix.getCurrentValue() >= 1.0f))
        return;

    const auto needsCrossfade = bypassMix.isSmoothing() || bypassMix.getCurrentValue() > 0.0f;

    if (needsCrossfade)
    {
        jassert (dryBuffer.getNumSamples() >= numSamples);
        juce::FloatVectorOperations::copy (dryBuffer.getWritePointer (0), samples, numSamples);
    }

    juce::dsp::AudioBlock<float> block (&samples, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> context (block);
    convolution.process (context);

    if (needsCrossfade)
    {
        const auto startMix = bypassMix.getCurrentValue();
        bypassMix.skip (numSamples);
        const auto endMix = bypassMix.getCurrentValue();

        juce::AudioBuffer<float> view (&samples, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, 1.0f - startMix, 1.0f - endMix);
        view.addFromWithRamp (0, 0, dryBuffer.getReadPointer (0), numSamples, startMix, endMix);
    }
}
