/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "../BypassCrossfade.h"

#include <juce_dsp/juce_dsp.h>

/** First in the chain, so it works on the raw guitar level — before a compressor or a drive
    pedal lifts the noise floor along with everything else.

    One control. Ratio, attack and release are fixed at values that suit a guitar: fast enough to
    stop a high-gain amp hissing between phrases, slow enough not to chop note tails.

    The ratio is what decides whether it sounds like a gate or a guillotine. A downward expander
    takes (ratio - 1) times the amount a signal sits below the threshold, so at 10:1 a note tail
    10 dB under loses 90 dB — it does not fade, it vanishes. At 3:1 the same tail loses 20 dB and
    hiss 30 dB under still loses 60, which is the job.
*/
class NoiseGatePedal
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        gate.prepare (spec);
        gate.setRatio (3.0f);
        gate.setAttack (1.0f);
        gate.setRelease (250.0f);

        bypass.prepare (sampleRate, maxBlockSize);
    }

    void reset()                       { gate.reset(); }
    void snapBypass (bool bypassed)    { bypass.snap (bypassed); }

    void setThresholdDb (float db)     { gate.setThreshold (db); }

    void process (float* samples, int numSamples, bool bypassed)
    {
        const auto action = bypass.beginBlock (bypassed, samples, numSamples);

        if (action == BypassCrossfade::Action::skip)
            return;

        juce::dsp::AudioBlock<float> block (&samples, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        gate.process (context);

        if (action == BypassCrossfade::Action::crossfade)
            bypass.finishBlock (samples, numSamples);
    }

private:
    juce::dsp::NoiseGate<float> gate;
    BypassCrossfade bypass;
};
