/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "DelayPedal.h"

void DelayPedal::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    delayLine.setMaximumDelayInSamples ((int) std::ceil (maxDelaySeconds * sampleRate) + 4);
    delayLine.prepare (spec);

    repeatFilter.prepare (spec);
    *repeatFilter.coefficients =
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass (sampleRate, 2600.0);

    // Delay time is smoothed slowly on purpose: swept quickly it should bend pitch, the way a
    // real delay does, rather than jump.
    delaySamples.reset (sampleRate, 0.25);
    feedbackAmount.reset (sampleRate, 0.05);
    wetMix.reset (sampleRate, 0.05);

    bypass.prepare (sampleRate, maxBlockSize);
}

void DelayPedal::reset()
{
    delayLine.reset();
    repeatFilter.reset();
}

void DelayPedal::setParameters (float timeSeconds, float feedback, float mix)
{
    delaySamples.setTargetValue (juce::jlimit (1.0f, (float) (maxDelaySeconds * sampleRate),
                                               timeSeconds * (float) sampleRate));
    feedbackAmount.setTargetValue (juce::jlimit (0.0f, 0.95f, feedback));
    wetMix.setTargetValue (juce::jlimit (0.0f, 1.0f, mix));
}

void DelayPedal::snapParameters()
{
    for (auto* smoothed : { &delaySamples, &feedbackAmount, &wetMix })
        smoothed->setCurrentAndTargetValue (smoothed->getTargetValue());
}

void DelayPedal::process (float* samples, int numSamples, bool bypassed)
{
    const auto action = bypass.beginBlock (bypassed, samples, numSamples);

    // Bypassed, but still fed, so engaging the pedal picks up repeats already in flight rather
    // than starting from an empty line — what a pedal with trails does.
    auto* target = action == BypassCrossfade::Action::skip
                       ? bypass.scratchFor (samples, numSamples)
                       : samples;

    for (int i = 0; i < numSamples; ++i)
    {
        delayLine.setDelay (delaySamples.getNextValue());

        const auto dry = target[i];
        const auto delayed = delayLine.popSample (0);

        delayLine.pushSample (0, dry + repeatFilter.processSample (delayed) * feedbackAmount.getNextValue());

        const auto mix = wetMix.getNextValue();
        target[i] = dry * (1.0f - mix) + delayed * mix;
    }

    if (action == BypassCrossfade::Action::crossfade)
        bypass.finishBlock (samples, numSamples);
}
