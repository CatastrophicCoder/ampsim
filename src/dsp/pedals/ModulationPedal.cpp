/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "ModulationPedal.h"

void ModulationPedal::prepare (double sampleRate, int maxBlockSize)
{
    preparedRate = sampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    chorus.prepare (spec);
    phaser.prepare (spec);

    tremoloDepth.reset (sampleRate, 0.05);
    tremoloShape.reset (sampleRate, 0.05);

    typeFade.reset (sampleRate, 0.01);
    typeFade.setCurrentAndTargetValue (1.0f);

    currentType = pendingType.load();
    updateSettings();

    bypass.prepare (sampleRate, maxBlockSize);
}

void ModulationPedal::reset()
{
    chorus.reset();
    phaser.reset();
    tremoloPhase = 0.0f;
}

void ModulationPedal::setType (Type type)
{
    pendingType.store (type);
}

void ModulationPedal::setParameters (float rateHz, float newDepth, float newFeedback, float newMix)
{
    rate = rateHz;
    depth = newDepth;
    feedback = newFeedback;
    mix = newMix;

    updateSettings();
}

void ModulationPedal::snapParameters()
{
    currentType = pendingType.load();
    typeFade.setCurrentAndTargetValue (1.0f);
    swapping = false;

    tremoloDepth.setCurrentAndTargetValue (tremoloDepth.getTargetValue());
    tremoloShape.setCurrentAndTargetValue (tremoloShape.getTargetValue());

    updateSettings();
}

void ModulationPedal::updateSettings()
{
    tremoloStep = (float) (rate / preparedRate);
    tremoloDepth.setTargetValue (depth);
    tremoloShape.setTargetValue (feedback);

    switch (currentType)
    {
        case Type::chorus:
            // Long enough that the delayed copy is heard as a second voice rather than as a comb.
            chorus.setCentreDelay (8.0f);
            chorus.setFeedback (0.0f);
            break;

        case Type::flanger:
            // Short enough for the comb's teeth to fall in the audible range, with feedback to
            // sharpen them. This is the whole difference between the two.
            chorus.setCentreDelay (2.0f);
            chorus.setFeedback (juce::jlimit (0.0f, 0.9f, feedback) * 0.95f);
            break;

        case Type::phaser:
        case Type::tremolo:
            // Neither uses the chorus, so its delay and feedback are left where they are.
            break;
    }

    chorus.setRate (juce::jlimit (0.01f, 20.0f, rate));
    chorus.setDepth (juce::jlimit (0.0f, 1.0f, depth));
    chorus.setMix (juce::jlimit (0.0f, 1.0f, mix));

    phaser.setRate (juce::jlimit (0.01f, 20.0f, rate));
    phaser.setDepth (juce::jlimit (0.0f, 1.0f, depth));
    phaser.setFeedback (juce::jlimit (0.0f, 0.9f, feedback));
    phaser.setMix (juce::jlimit (0.0f, 1.0f, mix));
    phaser.setCentreFrequency (600.0f);
}

void ModulationPedal::process (float* samples, int numSamples, bool bypassed)
{
    if (! swapping && pendingType.load() != currentType)
    {
        swapping = true;
        typeFade.setTargetValue (0.0f);
    }

    if (swapping && ! typeFade.isSmoothing() && typeFade.getCurrentValue() <= 0.0f)
    {
        currentType = pendingType.load();
        chorus.reset();
        phaser.reset();
        updateSettings();

        swapping = false;
        typeFade.setTargetValue (1.0f);
    }

    const auto action = bypass.beginBlock (bypassed, samples, numSamples);

    // Bypassed, but still fed: see BypassCrossfade::scratchFor. A sweep that started from the top
    // every time it was switched in would announce itself.
    auto* target = action == BypassCrossfade::Action::skip
                       ? bypass.scratchFor (samples, numSamples)
                       : samples;

    if (currentType == Type::tremolo)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // A sine at one end of the shape control and something close to a square at the
            // other, which is the difference between a valve tremolo and a chopper.
            const auto sine = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * tremoloPhase);
            const auto shaped = juce::jmap (tremoloShape.getNextValue(), sine,
                                            sine > 0.5f ? 1.0f : 0.0f);

            target[i] *= 1.0f - tremoloDepth.getNextValue() * (1.0f - shaped);

            tremoloPhase += tremoloStep;

            if (tremoloPhase >= 1.0f)
                tremoloPhase -= 1.0f;
        }
    }
    else
    {
        juce::dsp::AudioBlock<float> block (&target, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);

        if (currentType == Type::phaser)
            phaser.process (context);
        else
            chorus.process (context);
    }

    if (action == BypassCrossfade::Action::crossfade)
        bypass.finishBlock (samples, numSamples);

    if (typeFade.isSmoothing() || typeFade.getCurrentValue() < 1.0f)
    {
        const auto start = typeFade.getCurrentValue();
        typeFade.skip (numSamples);

        juce::AudioBuffer<float> view (&samples, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, start, typeFade.getCurrentValue());
    }
}
