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

/** The modulation slot after the amp, holding one of four pedals.

    All four move something with a slow oscillator, and what they move is the whole difference:

      - **Chorus** — a delayed copy whose delay wanders, mixed back in. Two instruments almost in
        tune with each other.
      - **Flanger** — the same idea with a far shorter delay and the output fed back, so the
        comb it makes is deep enough to hear as a sweep rather than as a thickening.
      - **Phaser** — no delay at all: a chain of all-pass filters whose corner moves, notching the
        spectrum where their phase shift meets the dry signal.
      - **Tremolo** — nothing moves but the volume. The oldest of the four and the only one that
        is not a comb.

    Whichever is selected keeps running while the slot is switched out, so engaging it picks up an
    oscillator that is already going rather than one starting from nothing — a modulation that
    began at the top of its sweep every time would announce itself.
*/
class ModulationPedal
{
public:
    enum class Type { chorus, flanger, phaser, tremolo };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }
    void snapParameters();

    /** Audio thread. A change of type fades across, since the four sound nothing alike. */
    void setType (Type);

    /** Every type takes four numbers; what the third and fourth mean depends on which it is.
        @param rateHz the oscillator.  @param depth how far it moves.
        @param feedback the flanger's and phaser's resonance, or the tremolo's waveform shape.
        @param mix how much of it is heard, which the tremolo has no use for. */
    void setParameters (float rateHz, float depth, float feedback, float mix);

    void process (float* samples, int numSamples, bool bypassed);

private:
    void updateSettings();

    juce::dsp::Chorus<float> chorus;     // and, with a shorter delay and feedback, the flanger
    juce::dsp::Phaser<float> phaser;

    // The tremolo, which is small enough not to need a class of its own.
    float tremoloPhase = 0.0f;
    float tremoloStep = 0.0f;

    juce::SmoothedValue<float> tremoloDepth, tremoloShape;

    float rate = 1.2f, depth = 0.35f, feedback = 0.0f, mix = 0.4f;

    std::atomic<Type> pendingType { Type::chorus };
    Type currentType = Type::chorus;

    juce::SmoothedValue<float> typeFade;
    bool swapping = false;

    double preparedRate = 48000.0;
    BypassCrossfade bypass;
};
