/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/** Switches a mono block between processed and dry without a click.

    Every pedal needs this and none of them need their own version of it. A pedal calls
    `beginBlock()`, and gets back one of three answers: process nothing, process everything, or
    process into the buffer and let `finishBlock()` blend the dry copy back over the ramp.
*/
class BypassCrossfade
{
public:
    enum class Action
    {
        skip,       // settled at bypassed: the dry signal is already in the buffer
        processAll, // settled at engaged: no blending needed
        crossfade   // mid-ramp: process, then call finishBlock()
    };

    void prepare (double sampleRate, int maxBlockSize, double rampSeconds = 0.02)
    {
        mix.reset (sampleRate, rampSeconds);
        mix.setCurrentAndTargetValue (mix.getTargetValue());
        dry.setSize (1, maxBlockSize, false, false, true);
    }

    /** Jumps to the current setting rather than ramping into it — for prepareToPlay. */
    void snap (bool bypassed)
    {
        mix.setCurrentAndTargetValue (bypassed ? 1.0f : 0.0f);
    }

    Action beginBlock (bool bypassed, const float* samples, int numSamples)
    {
        mix.setTargetValue (bypassed ? 1.0f : 0.0f);

        if (! mix.isSmoothing())
            return mix.getCurrentValue() >= 1.0f ? Action::skip : Action::processAll;

        jassert (dry.getNumSamples() >= numSamples);
        juce::FloatVectorOperations::copy (dry.getWritePointer (0), samples, numSamples);

        return Action::crossfade;
    }

    /** For effects with a tail — chorus, delay, reverb — a scratch copy of the input to run
        through while the pedal is bypassed. Their delay lines have to stay fed: engaging one that
        has been sitting empty starts its delayed copy from silence, and that onset is a click
        however long the crossfade is. Whatever is written here is thrown away. */
    float* scratchFor (const float* samples, int numSamples)
    {
        jassert (dry.getNumSamples() >= numSamples);

        auto* scratch = dry.getWritePointer (0);
        juce::FloatVectorOperations::copy (scratch, samples, numSamples);

        return scratch;
    }

    void finishBlock (float* samples, int numSamples)
    {
        const auto start = mix.getCurrentValue();
        mix.skip (numSamples);
        const auto end = mix.getCurrentValue();

        juce::AudioBuffer<float> view (&samples, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, 1.0f - start, 1.0f - end);
        view.addFromWithRamp (0, 0, dry.getReadPointer (0), numSamples, start, end);
    }

private:
    juce::SmoothedValue<float> mix;
    juce::AudioBuffer<float> dry;
};
