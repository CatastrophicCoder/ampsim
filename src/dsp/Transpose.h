/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "BypassCrossfade.h"

#include <juce_dsp/juce_dsp.h>

/** Retunes the instrument by a fixed interval, before anything else touches it.

    A read pointer runs through a delay line faster or slower than it is written, which resamples
    the signal and so moves its pitch. The pointer cannot run away for ever, so every so often it
    has to jump back by about a window's worth and carry on — and **where it jumps to is the whole
    problem.** Jump by a fixed amount and the join lands at an arbitrary point in the waveform: the
    two sides do not line up, they partly cancel through the crossfade, and what comes out has a
    tremolo on it at the rate the joins happen. Worse, the phase lost at each join accumulates, and
    a big interval ends up measurably out of tune.

    So the jump is not fixed. Before each one this looks back over the recent signal and picks the
    distance, within about twelve milliseconds either side, at which it most nearly repeats — the
    overlap-add trick that time-stretchers have used for decades. The join then lands on a matching
    part of the waveform, the two sides add rather than fight, and the crossfade can be short.

    That is what lets the window stay short as well, which is what keeps the delay down: a
    granular shifter's delay is its window, and it was only ever long here to hide joins that no
    longer need hiding.

    Two properties this keeps that an FFT-based shifter would not: it is polyphonic without being
    told, because it resamples audio rather than tracking a pitch, and it does not have to smear a
    pick attack across an analysis window.
*/
class Transpose
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Message thread, from prepareToPlay. */
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    /** Audio thread. Whole semitones only: this is a tuning, not a pitch bend. */
    void setSemitones (int semitones);

    /** Audio thread. Processes one mono block in place. */
    void process (float* samples, int numSamples, bool bypassed);

    /** How far it can be moved either way. An octave up or down is as far as this kind of shifter
        holds together, and further than anyone retunes a guitar. */
    static constexpr int maxSemitones = 12;

    /** How far the read pointer travels between jumps, and so how much delay this can add. */
    static constexpr double windowSeconds = 0.025;

    /** How far either side of a window the jump is allowed to land, which has to cover a period of
        the lowest note a guitar makes. */
    static constexpr double searchSeconds = 0.013;

    /** How much signal the two sides of a join are matched over, and how long they overlap for. */
    static constexpr double matchSeconds = 0.011;
    static constexpr double crossfadeSeconds = 0.004;

private:
    void adoptInterval (int semitones);
    float readAt (float delayInSamples);

    /** How far to jump, chosen so that the signal most nearly repeats across the join.
        @param direction -1 when the pointer has run to the far end and must come back, +1 when it
                         has run to the near end and must go further away. */
    float bestJumpFrom (float delay, float direction);

    /** The nearest the read pointer is allowed to get to the write head. Far enough that a jump
        the other way can always be searched for without reading past it. */
    float lowestDelay() const  { return (float) searchSamples; }

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 8192 };

    double preparedRate = 48000.0;

    float windowSamples = 1200.0f;
    int searchSamples = 600, matchSamples = 512, crossfadeSamples = 192;

    float readDelay = 0.0f;       // the live pointer, in samples behind the write head
    float outgoingDelay = 0.0f;   // the one being faded out across a join
    int crossfadeLeft = 0;
    float rate = 1.0f;            // how fast the read pointer moves, in samples per sample

    std::atomic<int> pendingSemitones { 0 };
    int currentSemitones = 0;

    juce::SmoothedValue<float> intervalFade;
    bool changing = false;

    BypassCrossfade bypass;
};
