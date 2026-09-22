/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "CabSim.h"

namespace
{
    constexpr double bypassRampSeconds = 0.02;
    constexpr double weightRampSeconds = 0.05;
}

void CabSim::prepare (double sampleRate, int maxBlockSize)
{
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    for (int slot = 0; slot < numSlots; ++slot)
    {
        convolutions[(size_t) slot].prepare (spec);
        weights[(size_t) slot].reset (sampleRate, weightRampSeconds);
        slotBuffers[(size_t) slot].setSize (1, maxBlockSize, false, false, true);
    }

    bypassMix.reset (sampleRate, bypassRampSeconds);
    bypassMix.setCurrentAndTargetValue (0.0f);

    dryBuffer.setSize (1, maxBlockSize, false, false, true);
    mixBuffer.setSize (1, maxBlockSize, false, false, true);

    updateWeights();

    for (auto& weight : weights)
        weight.setCurrentAndTargetValue (weight.getTargetValue());
}

void CabSim::reset()
{
    for (auto& convolution : convolutions)
        convolution.reset();

    bypassMix.setCurrentAndTargetValue (bypassMix.getTargetValue());
}

void CabSim::loadImpulseResponse (Slot slot, const juce::File& file)
{
    // Stereo::no: the chain is mono until the cab, so a stereo IR is folded down rather than
    // silently widening the signal. Normalise::no keeps each capture's own level, which is what
    // makes moving the mic sound like moving a mic rather than turning a volume knob.
    convolutions[(size_t) slot].loadImpulseResponse (file,
                                                     juce::dsp::Convolution::Stereo::no,
                                                     juce::dsp::Convolution::Trim::yes,
                                                     0,
                                                     juce::dsp::Convolution::Normalise::no);

    loadedSlots.fetch_or (1 << (int) slot);
    updateWeights();
}

void CabSim::clearSlot (Slot slot)
{
    loadedSlots.fetch_and (~(1 << (int) slot));
    updateWeights();
}

void CabSim::setMicPosition (float axis, float distance)
{
    micAxis = juce::jlimit (0.0f, 1.0f, axis);
    micDistance = juce::jlimit (0.0f, 1.0f, distance);

    updateWeights();
}

void CabSim::updateWeights()
{
    const float corners[numSlots]
    {
        (1.0f - micAxis) * (1.0f - micDistance),   // centreClose
        micAxis * (1.0f - micDistance),            // edgeClose
        (1.0f - micAxis) * micDistance,            // centreFar
        micAxis * micDistance                      // edgeFar
    };

    // Corners with nothing in them are left out and the rest renormalised, so an unfilled grid
    // changes the tone available rather than dropping the level as the knob crosses it.
    float total = 0.0f;

    for (int slot = 0; slot < numSlots; ++slot)
        if (isSlotLoaded ((Slot) slot))
            total += corners[slot];

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto loaded = isSlotLoaded ((Slot) slot);

        weights[(size_t) slot].setTargetValue (loaded && total > 1.0e-6f ? corners[slot] / total
                                                                         : 0.0f);
    }

    // Everything loaded is at a corner the position never reaches: fall back to an even blend
    // rather than silence.
    if (total <= 1.0e-6f && loadedSlots.load() != 0)
    {
        auto count = 0;

        for (int slot = 0; slot < numSlots; ++slot)
            count += isSlotLoaded ((Slot) slot) ? 1 : 0;

        for (int slot = 0; slot < numSlots; ++slot)
            weights[(size_t) slot].setTargetValue (isSlotLoaded ((Slot) slot) ? 1.0f / (float) count
                                                                              : 0.0f);
    }
}

void CabSim::process (float* samples, int numSamples, bool bypassed)
{
    bypassMix.setTargetValue (bypassed ? 1.0f : 0.0f);

    const auto slots = loadedSlots.load();

    // Nothing loaded, or settled at bypassed: the signal is already what it should be.
    if (slots == 0 || (! bypassMix.isSmoothing() && bypassMix.getCurrentValue() >= 1.0f))
        return;

    const auto needsCrossfade = bypassMix.isSmoothing() || bypassMix.getCurrentValue() > 0.0f;

    if (needsCrossfade)
    {
        jassert (dryBuffer.getNumSamples() >= numSamples);
        juce::FloatVectorOperations::copy (dryBuffer.getWritePointer (0), samples, numSamples);
    }

    auto* mixed = mixBuffer.getWritePointer (0);
    juce::FloatVectorOperations::clear (mixed, numSamples);

    for (int slot = 0; slot < numSlots; ++slot)
    {
        auto& weight = weights[(size_t) slot];

        if ((slots & (1 << slot)) == 0)
        {
            weight.skip (numSamples);
            continue;
        }

        if (! weight.isSmoothing() && weight.getCurrentValue() <= 0.0f)
        {
            // A juce::dsp::Convolution only installs a loaded impulse response while it is
            // processing. A corner the mic position has never been moved to would therefore still
            // be running JUCE's default engine, and the first sweep onto it would cross into that
            // before the real IR appeared. Keep feeding a corner until its IR is actually in —
            // getCurrentIRSize() is 1 until then — and leave it alone afterwards.
            if (convolutions[(size_t) slot].getCurrentIRSize() <= 1)
            {
                auto* priming = slotBuffers[(size_t) slot].getWritePointer (0);
                juce::FloatVectorOperations::copy (priming, samples, numSamples);

                juce::dsp::AudioBlock<float> block (&priming, 1, (size_t) numSamples);
                juce::dsp::ProcessContextReplacing<float> context (block);
                convolutions[(size_t) slot].process (context);
            }

            weight.skip (numSamples);
            continue;
        }

        auto* slotData = slotBuffers[(size_t) slot].getWritePointer (0);
        juce::FloatVectorOperations::copy (slotData, samples, numSamples);

        juce::dsp::AudioBlock<float> block (&slotData, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        convolutions[(size_t) slot].process (context);

        const auto start = weight.getCurrentValue();
        weight.skip (numSamples);

        juce::AudioBuffer<float> view (&mixed, 1, numSamples);
        view.addFromWithRamp (0, 0, slotData, numSamples, start, weight.getCurrentValue());
    }

    juce::FloatVectorOperations::copy (samples, mixed, numSamples);

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
