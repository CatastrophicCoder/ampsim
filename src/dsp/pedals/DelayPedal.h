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

/** An analogue-voiced delay: each repeat passes through a low-pass filter, so the echoes get
    darker as they fade instead of repeating a bright copy forever.
*/
class DelayPedal
{
public:
    /** What the line can hold. */
    static constexpr float maxDelaySeconds = 2.0f;

    /** What the time knob offers, which is deliberately less. It cannot be widened without moving
        every saved session's setting, since a stored parameter is a proportion of its range — so
        the line was made longer instead, for the one case that needs the room: a synced half note
        runs to two seconds at 60 BPM. */
    static constexpr float maxKnobSeconds = 1.2f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    /** Jump to the current settings instead of sweeping into them. Without this the delay time
        ramps up from zero on every playback start, and the first repeats land in the wrong place. */
    void snapParameters();

    /** @param timeSeconds delay time.  @param feedback 0–0.95.  @param mix 0–1 wet. */
    void setParameters (float timeSeconds, float feedback, float mix);

    void process (float* samples, int numSamples, bool bypassed);

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 1 };
    juce::dsp::IIR::Filter<float> repeatFilter;

    juce::SmoothedValue<float> delaySamples, feedbackAmount, wetMix;
    double sampleRate = 48000.0;

    BypassCrossfade bypass;
};
