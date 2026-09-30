/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

/** A gate that listens at the input and closes at the output.

    It measures the clean guitar, before anything has touched it, and applies the result *after*
    the amp. That split is the whole point. A gate placed only in front of the amp cannot remove
    hiss the amp makes — and on a high-gain capture, the amp is where nearly all the hiss comes
    from. A gate placed only after the amp has no usable envelope to trigger on, because the
    distortion has flattened the dynamics it would need.

    Hardware solves this with a key input — a Decimator G String sits late in the chain with a
    cable back to the guitar — and this is the same arrangement. The threshold therefore still
    means what it always meant: a level of the raw guitar, not of the amplified signal.

    The detection law is JUCE's: an RMS ballistics filter into a peak one, then a downward
    expander below the threshold. Ratio, attack and release are fixed at values that suit a
    guitar. At 3:1 a note tail 10 dB under the threshold loses 20 dB and fades; at 10:1 it would
    lose 90 dB and vanish, which is the difference between a gate and a guillotine.
*/
class NoiseGatePedal
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        rmsFilter.prepare (spec);
        rmsFilter.setLevelCalculationType (juce::dsp::BallisticsFilterLevelCalculationType::RMS);
        rmsFilter.setAttackTime (0.0f);
        rmsFilter.setReleaseTime (50.0f);

        envelopeFilter.prepare (spec);
        envelopeFilter.setAttackTime (1.0f);
        envelopeFilter.setReleaseTime (250.0f);

        gains.assign ((size_t) maxBlockSize, 1.0f);

        engagement.reset (sampleRate, 0.01);
    }

    void reset()
    {
        rmsFilter.reset();
        envelopeFilter.reset();
        std::fill (gains.begin(), gains.end(), 1.0f);
    }

    void snapBypass (bool bypassed)
    {
        engagement.setCurrentAndTargetValue (bypassed ? 0.0f : 1.0f);
    }

    void setThresholdDb (float db)
    {
        threshold = juce::Decibels::decibelsToGain (db, -200.0f);
        thresholdInverse = 1.0f / threshold;
    }

    /** Audio thread, in front of the amp: works out how open the gate should be, from the guitar.

        This runs whether or not the pedal is switched in, so that engaging it mid-phrase picks up
        an envelope that has already settled rather than one starting from nothing — the same
        reason the chorus and the delay keep running while they are bypassed.
    */
    void measureKey (const float* key, int numSamples)
    {
        const auto count = juce::jmin ((size_t) numSamples, gains.size());

        for (size_t i = 0; i < count; ++i)
        {
            const auto envelope = envelopeFilter.processSample (0, rmsFilter.processSample (0, key[i]));

            gains[i] = envelope > threshold ? 1.0f
                                            : std::pow (envelope * thresholdInverse, ratio - 1.0f);
        }
    }

    /** Audio thread, after the amp: applies what the key said, ramping in and out of bypass. */
    void apply (float* samples, int numSamples, bool bypassed)
    {
        engagement.setTargetValue (bypassed ? 0.0f : 1.0f);

        // A block longer than prepare() was told about would read past the gains it measured.
        // Leaving the signal alone is the safe failure, and the processor guards this as well.
        if ((size_t) numSamples > gains.size())
        {
            engagement.skip (numSamples);
            return;
        }

        if (! engagement.isSmoothing() && engagement.getCurrentValue() <= 0.0f)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            // Between the gate's gain and unity, so switching it in or out is a ramp rather than
            // a step, and a bypassed gate is exactly unity rather than nearly so.
            const auto amount = engagement.getNextValue();
            samples[i] *= 1.0f + amount * (gains[(size_t) i] - 1.0f);
        }
    }

private:
    static constexpr float ratio = 3.0f;

    juce::dsp::BallisticsFilter<float> rmsFilter, envelopeFilter;
    std::vector<float> gains;
    juce::SmoothedValue<float> engagement;

    float threshold = 1.0f, thresholdInverse = 1.0f;
};
