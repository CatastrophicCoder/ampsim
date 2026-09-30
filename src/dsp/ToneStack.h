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

/** Bass, Mid, Treble, Presence and Depth as five independent parametric bands.

    Chosen over a modelled passive stack: each control does one thing, centred is flat, and every
    band's response can be asserted directly in a test. What it gives up is the interaction of a
    real passive network — where the controls load each other and all-at-noon is mid-scooped rather
    than flat — so this colours the model's output rather than behaving like an amp's own stack.
    That trade is recorded in ampsim_plan.md.

    The bands sit after the model and before the cab, which is where a real amp's stack sits
    relative to the speaker.

    **Presence and Depth are shaped like the controls they are named after, and work differently.**
    On a real amplifier they are not tone controls at all: they remove negative feedback around the
    power amp at high or low frequencies, which raises the gain there *and* changes the distortion
    and the damping with it. A NAM capture already contains the power amp with its feedback loop at
    whatever position it was captured, so what these two can offer is the frequency response of
    those controls and none of the rest of their behaviour. That is worth having and worth being
    accurate about: they are extra bands placed where a presence and a depth control act, not a
    model of the mechanism.
*/
class ToneStack
{
public:
    ToneStack() = default;

    /** Band centres, chosen for guitar: below the low E's fundamental region, in the middle of the
        range a speaker emphasises, and above the cab's roll-off. */
    static constexpr float bassFrequency   = 100.0f;
    static constexpr float midFrequency    = 800.0f;
    static constexpr float trebleFrequency = 3200.0f;
    static constexpr float midQ            = 0.7f;
    static constexpr float shelfQ          = 0.7f;

    /** Above the treble shelf's corner, so the two have separate jobs: treble is the brightness of
        the whole top end, presence is the bite at the edge of it. */
    static constexpr float presenceFrequency = 5500.0f;

    /** A resonant lift where a 4x12 resonates, rather than a second bass shelf — the bass control
        already lifts everything below 100 Hz, and two shelves an octave apart would be one control
        with two knobs. */
    static constexpr float depthFrequency = 85.0f;
    static constexpr float depthQ         = 1.1f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Audio thread. Sets the targets the smoothing ramps towards. */
    void setBandGains (float bassDb, float midDb, float trebleDb, float presenceDb, float depthDb);

    /** Message thread. Jumps the bands to their targets instead of ramping, so prepareToPlay
        does not sweep the EQ in from flat on every playback start. */
    void snapToTargets();

    /** Audio thread. Processes one mono block in place. */
    void process (float* samples, int numSamples);

private:
    /** Coefficients are recomputed this often, rather than once per block: a block can be 20 ms
        long, and stepping a 24 dB swing in that few jumps is audible. */
    static constexpr int updateInterval = 32;

    void updateCoefficients();

    juce::dsp::IIR::Filter<float> bassFilter, midFilter, trebleFilter, presenceFilter, depthFilter;
    juce::SmoothedValue<float> bassDb, midDb, trebleDb, presenceDb, depthDb;

    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneStack)
};
