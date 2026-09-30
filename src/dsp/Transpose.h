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

    Two read pointers running through a delay line faster or slower than it is written, each
    faded in and out by a raised cosine so that one takes over as the other runs out of room.
    That is how a pitch shifter in a box does it, and it is the right choice here for two
    reasons a phase vocoder would get wrong:

      - **It is polyphonic without being told.** A guitar plays chords, and this does not care —
        it is resampling a window of audio, not tracking a pitch.
      - **It does not smear a pick attack.** An FFT-based shifter has to spread a transient over
        its whole window, which on a guitar is the one thing you would notice. This one's
        artefact is a periodic roughness instead, which is what players already know pitch
        shifters to sound like.

    **The window is sized from the interval, and that is not a detail.** What decides whether the
    result is in tune is how many cycles of the note fit inside one grain: each grain plays back
    at exactly the right rate, but the joins between them are phase discontinuities, and if the
    grains are short enough the joins are most of what there is. At twenty milliseconds an octave
    down holds barely two cycles of a low note, and the result measures — and sounds — a couple of
    hundred cents sharp of where it should be. A semitone or two down moves the read pointer five
    times more slowly, so the same window holds forty cycles and is fine.

    So the window is proportional to how far the pointer has to move, clamped at both ends. A drop
    tuning keeps the short window and its small delay; an octave takes the long one and pays for
    it. That is the honest trade for this kind of shifter: a large interval costs delay.

    At zero semitones, or switched off, it is bypassed outright rather than run at a ratio of one:
    the read pointers would still sit behind the write pointer and delay the signal for nothing.
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

    /** What the window is scaled from, and the ends it is held between. The product of the middle
        figure and how far the read pointer moves per sample is what keeps a grain long enough to
        hold the note it is carrying. */
    static constexpr double shortestWindowSeconds = 0.020;
    static constexpr double longestWindowSeconds = 0.100;
    static constexpr double windowPerUnitRate = 0.200;

    /** The window a given interval needs, in seconds. */
    static double windowSecondsFor (int semitones);

private:
    /** Takes a new interval, with the window and the grain it implies. */
    void adoptInterval (int semitones);

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 4096 };

    double preparedRate = 48000.0;

    float windowSamples = 1024.0f;
    float phase = 0.0f;          // 0–1 through the window, and where the first tap reads
    float phaseStep = 0.0f;      // per sample; its sign is the direction of the shift

    /** Changing the interval changes the window with it, so the grains restart. Fade across it
        rather than cutting, the way the pedal slots do when what is in them changes. */
    juce::SmoothedValue<float> intervalFade;
    bool changing = false;

    std::atomic<int> pendingSemitones { 0 };
    int currentSemitones = 0;

    BypassCrossfade bypass;
};
