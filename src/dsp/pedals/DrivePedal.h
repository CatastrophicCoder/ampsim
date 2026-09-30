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

/** Overdrive and distortion, in front of the amp.

    The pedal that earns its place in front rather than after: clipping here changes what the amp
    model is given to distort, which is the whole reason a player puts one there.

    Oversampled 4x, because a waveshaper folds harmonics above Nyquist back down as aliasing, and
    that is the difference between "distorted" and "broken". The oversampler runs whether or not
    the pedal is engaged, so the latency it adds does not change under the host's feet.
*/
class DrivePedal
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    /** Jump to the current settings instead of ramping into them on every playback start. */
    void snapParameters();

    /** The corner the boost into the clipper starts at. Below it the signal reaches the shaper at
        the level it arrived, so the low end stays defined instead of being flattened along with
        everything else — which is what a drive in front of an amp is for. A screamer does this
        with a high pass inside its gain stage; this is the same arrangement. */
    static constexpr double boostCornerHz = 700.0;

    /** @param drive 0–1, edge-of-breakup to fuzzy.  @param tone 0–1, dark to bright. */
    void setParameters (float drive, float tone, float levelDb);

    void process (float* samples, int numSamples, bool bypassed);

    /** Constant, and reported to the host, because the oversampler always runs. */
    int getLatencySamples() const;

private:
    static constexpr int oversampleFactor = 2;   // 2^2 = 4x

    juce::dsp::Oversampling<float> oversampling { 1, oversampleFactor,
                                                  juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    juce::dsp::Gain<float> level;
    juce::dsp::IIR::Filter<float> boostFilter, toneFilter;
    juce::SmoothedValue<float> toneAmount, boostGain;

    double oversampledRate = 192000.0;
    BypassCrossfade bypass;
};
