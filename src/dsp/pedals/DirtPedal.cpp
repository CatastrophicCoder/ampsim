/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "DirtPedal.h"

namespace
{
    /** Soft and asymmetric: the positive half rounds over sooner than the negative one, so the
        pedal makes even harmonics as well as odd ones. A symmetric shaper sounds sterile in front
        of an amp, which is already making odd harmonics of its own. */
    inline float shapeSoft (float x) noexcept
    {
        const auto bias = x > 0.0f ? 1.0f : 0.7f;
        return std::tanh (x * bias) / bias;
    }

    /** Symmetric, with a knee that gives way far more abruptly than tanh's and a hard ceiling
        past it — which is what a distortion pedal's diodes do and an overdrive's do not. */
    inline float shapeHard (float x) noexcept
    {
        x = juce::jlimit (-1.0f, 1.0f, x);
        return 1.5f * x - 0.5f * x * x * x;
    }

    /** How much gain the drive control puts into the shaper, per type. A distortion pedal is not
        an overdrive turned up: it starts further along and goes further. */
    inline float driveRangeDb (DirtPedal::Type type) noexcept
    {
        switch (type)
        {
            case DirtPedal::Type::overdrive:  return 36.0f;
            case DirtPedal::Type::distortion: return 48.0f;
            case DirtPedal::Type::cleanBoost: return 20.0f;
        }

        return 0.0f;
    }
}

void DirtPedal::prepare (double sampleRate, int maxBlockSize)
{
    oversampling.initProcessing ((size_t) maxBlockSize);
    oversampling.reset();

    oversampledRate = sampleRate * (1 << oversampleFactor);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
    const juce::dsp::ProcessSpec overSpec { oversampledRate,
                                            (juce::uint32) (maxBlockSize << oversampleFactor), 1 };

    boostFilter.prepare (overSpec);
    toneFilter.prepare (overSpec);

    level.prepare (spec);
    level.setRampDurationSeconds (0.05);

    toneAmount.reset (oversampledRate, 0.05);
    driveGain.reset (oversampledRate, 0.05);

    // Long enough to cover a swap, short enough not to sound like a gap — the same figure the
    // model swap uses, and for the same reason.
    typeFade.reset (sampleRate, 0.01);
    typeFade.setCurrentAndTargetValue (1.0f);

    currentType = pendingType.load();
    updateFilters();

    bypass.prepare (sampleRate, maxBlockSize);
}

void DirtPedal::reset()
{
    oversampling.reset();
    level.reset();
    boostFilter.reset();
    toneFilter.reset();
}

int DirtPedal::getLatencySamples() const
{
    return (int) std::ceil (oversampling.getLatencyInSamples());
}

void DirtPedal::setType (Type type)
{
    pendingType.store (type);
}

void DirtPedal::updateFilters()
{
    // The overdrive holds the low end out of its clipper; the distortion lets it in and only
    // stops the very bottom; the boost clips nothing, so its filter position does not matter.
    const auto corner = currentType == Type::overdrive ? boostCornerHz : distortionCornerHz;

    *boostFilter.coefficients =
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderHighPass (oversampledRate, (float) corner);

    *toneFilter.coefficients =
        juce::dsp::IIR::ArrayCoefficients<float>::makeFirstOrderLowPass (oversampledRate, tiltCornerHz);
}

void DirtPedal::setParameters (float amount, float tone, float levelDb)
{
    // Minus the unity the signal already arrives with, so the knob at zero adds nothing.
    driveGain.setTargetValue (
        juce::Decibels::decibelsToGain (juce::jmap (amount, 0.0f, 1.0f, 0.0f,
                                                    driveRangeDb (currentType))) - 1.0f);
    toneAmount.setTargetValue (tone);
    level.setGainDecibels (levelDb);
}

void DirtPedal::snapParameters()
{
    level.reset();
    toneAmount.setCurrentAndTargetValue (toneAmount.getTargetValue());
    driveGain.setCurrentAndTargetValue (driveGain.getTargetValue());

    currentType = pendingType.load();
    typeFade.setCurrentAndTargetValue (1.0f);
    swapping = false;
    updateFilters();
}

void DirtPedal::process (float* samples, int numSamples, bool bypassed)
{
    // The type change is handled before the bypass, because the fade has to run whether or not
    // the slot is in circuit — otherwise switching type while bypassed leaves the fade half done.
    if (! swapping && pendingType.load() != currentType)
    {
        swapping = true;
        typeFade.setTargetValue (0.0f);
    }

    if (swapping && ! typeFade.isSmoothing() && typeFade.getCurrentValue() <= 0.0f)
    {
        currentType = pendingType.load();
        updateFilters();
        boostFilter.reset();
        toneFilter.reset();

        swapping = false;
        typeFade.setTargetValue (1.0f);
    }

    const auto action = bypass.beginBlock (bypassed, samples, numSamples);

    // The oversampler still has to run when the slot is switched out, or its latency would appear
    // and disappear with the footswitch.
    const auto engaged = action != BypassCrossfade::Action::skip;

    juce::dsp::AudioBlock<float> block (&samples, 1, (size_t) numSamples);
    auto upsampled = oversampling.processSamplesUp (block);

    if (engaged)
    {
        auto* data = upsampled.getChannelPointer (0);
        const auto boostOnly = currentType == Type::cleanBoost;
        const auto soft = currentType == Type::overdrive;

        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
        {
            const auto gain = driveGain.getNextValue();
            const auto input = data[i];

            // Only the overdrive holds its low end out of the clipper; the distortion's corner is
            // far lower, and the boost is not clipping anything to hold anything out of.
            auto shaped = boostOnly ? input * (1.0f + gain)
                                    : input + gain * boostFilter.processSample (input);

            if (! boostOnly)
                shaped = soft ? shapeSoft (shaped) : shapeHard (shaped);

            // The tone control comes after the clipping, as it does in the box: the low pass is
            // its pivot, and what is under it and over it are mixed against each other, so the
            // middle of the knob is flat and either end tilts.
            const auto low = toneFilter.processSample (shaped);
            const auto high = shaped - low;
            const auto t = toneAmount.getNextValue();

            data[i] = 2.0f * ((1.0f - t) * low + t * high);
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

    // The swap fade last, over whatever the slot produced.
    if (typeFade.isSmoothing() || typeFade.getCurrentValue() < 1.0f)
    {
        const auto start = typeFade.getCurrentValue();
        typeFade.skip (numSamples);

        juce::AudioBuffer<float> view (&samples, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, start, typeFade.getCurrentValue());
    }
}
