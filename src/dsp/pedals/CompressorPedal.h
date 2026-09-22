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

/** A squash-and-level pedal, the shape most guitar compressors take.

    "Amount" runs the threshold down and the ratio up together, so one control goes from a gentle
    evening-out to the squashed, sustaining sound a compressor is usually bought for. "Level" makes
    up the gain it takes away, which is what makes A/B comparison honest.
*/
class CompressorPedal
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        compressor.prepare (spec);
        compressor.setAttack (8.0f);
        compressor.setRelease (180.0f);

        makeup.prepare (spec);
        makeup.setRampDurationSeconds (0.05);

        bypass.prepare (sampleRate, maxBlockSize);
    }

    void reset()
    {
        compressor.reset();
        makeup.reset();
    }

    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    /** @param amount 0–1, gentle to squashed.  @param levelDb makeup gain. */
    void setParameters (float amount, float levelDb)
    {
        compressor.setThreshold (juce::jmap (amount, 0.0f, 1.0f, -6.0f, -36.0f));
        compressor.setRatio (juce::jmap (amount, 0.0f, 1.0f, 1.5f, 10.0f));
        makeup.setGainDecibels (levelDb);
    }

    /** Jump to the current settings instead of ramping into them on every playback start. */
    void snapParameters() { makeup.reset(); }

    void process (float* samples, int numSamples, bool bypassed)
    {
        const auto action = bypass.beginBlock (bypassed, samples, numSamples);

        if (action == BypassCrossfade::Action::skip)
            return;

        juce::dsp::AudioBlock<float> block (&samples, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);

        compressor.process (context);
        makeup.process (context);

        if (action == BypassCrossfade::Action::crossfade)
            bypass.finishBlock (samples, numSamples);
    }

private:
    juce::dsp::Compressor<float> compressor;
    juce::dsp::Gain<float> makeup;
    BypassCrossfade bypass;
};
