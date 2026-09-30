/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "DrivePedal.h"

namespace
{
    /** Asymmetric soft clipping: the positive half rounds over sooner than the negative one, so
        the pedal makes even harmonics as well as odd ones. A symmetric shaper sounds sterile in
        front of an amp — the amp is already making odd harmonics of its own.
    */
    inline float shape (float x) noexcept
    {
        const auto bias = x > 0.0f ? 1.0f : 0.7f;
        return std::tanh (x * bias) / bias;
    }
}

void DrivePedal::prepare (double sampleRate, int maxBlockSize)
{
    oversampling.initProcessing ((size_t) maxBlockSize);
    oversampling.reset();

    oversampledRate = sampleRate * (1 << oversampleFactor);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
    const juce::dsp::ProcessSpec overSpec { oversampledRate,
                                            (juce::uint32) (maxBlockSize << oversampleFactor), 1 };

    boostFilter.prepare (overSpec);
    *boostFilter.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (
        oversampledRate, boostCornerHz);

    toneFilter.prepare (overSpec);

    level.prepare (spec);
    level.setRampDurationSeconds (0.05);

    toneAmount.reset (oversampledRate, 0.05);
    boostGain.reset (oversampledRate, 0.05);

    bypass.prepare (sampleRate, maxBlockSize);
}

void DrivePedal::reset()
{
    oversampling.reset();
    level.reset();
    boostFilter.reset();
    toneFilter.reset();
}

int DrivePedal::getLatencySamples() const
{
    return (int) std::ceil (oversampling.getLatencyInSamples());
}

void DrivePedal::setParameters (float drive, float tone, float levelDb)
{
    // Up to 36 dB of boost into the shaper: the difference between a transparent lift and a fuzz.
    // Minus the unity the signal already arrives with, so the knob at zero adds nothing and the
    // pedal starts exactly where it always did.
    boostGain.setTargetValue (juce::Decibels::decibelsToGain (juce::jmap (drive, 0.0f, 1.0f, 0.0f, 36.0f)) - 1.0f);
    toneAmount.setTargetValue (tone);
    level.setGainDecibels (levelDb);
}

void DrivePedal::snapParameters()
{
    level.reset();
    toneAmount.setCurrentAndTargetValue (toneAmount.getTargetValue());
    boostGain.setCurrentAndTargetValue (boostGain.getTargetValue());
}

void DrivePedal::process (float* samples, int numSamples, bool bypassed)
{
    const auto action = bypass.beginBlock (bypassed, samples, numSamples);

    // The oversampler still has to run when the pedal is off, or its latency would appear and
    // disappear with the footswitch.
    const auto engaged = action != BypassCrossfade::Action::skip;

    juce::dsp::AudioBlock<float> block (&samples, 1, (size_t) numSamples);
    auto upsampled = oversampling.processSamplesUp (block);

    if (engaged)
    {
        // A one-pole low pass swept across the useful range: dark at 0, open at 1.
        const auto cutoff = juce::jmap (toneAmount.getNextValue(), 0.0f, 1.0f, 900.0f, 7000.0f);
        toneAmount.skip ((int) upsampled.getNumSamples() - 1);

        *toneFilter.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass (
            oversampledRate, cutoff);

        auto* data = upsampled.getChannelPointer (0);

        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
        {
            // Only what is above the corner is driven hard; the rest arrives at the shaper as it
            // was. Clipping the whole range equally is what turns a palm-muted low string to mush,
            // and it is the one thing a pedal in front of an amp is there not to do.
            const auto boosted = data[i] + boostGain.getNextValue() * boostFilter.processSample (data[i]);

            data[i] = toneFilter.processSample (shape (boosted));
        }
    }

    oversampling.processSamplesDown (block);

    if (engaged)
    {
        juce::dsp::ProcessContextReplacing<float> context (block);
        level.process (context);
    }

    if (action == BypassCrossfade::Action::crossfade)
        bypass.finishBlock (samples, numSamples);
}
