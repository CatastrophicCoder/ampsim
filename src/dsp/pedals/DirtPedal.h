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

/** The slot in front of the amp that makes dirt, holding one of three pedals.

    They are three pedals rather than one with a wide knob because they differ in more than how
    much: what they clip with, what they let reach the clipper, and what they do to the tone
    afterwards. Those are the things that decide whether a pedal tightens an amp or swamps it.

      - **Overdrive** — the screamer. A high pass in front of the clipping stage means only what is
        above a few hundred hertz is driven hard, so the low end keeps its shape, and a soft
        asymmetric shaper makes even harmonics as well as odd ones. This is the pedal that pushes
        an amp rather than replacing it.
      - **Distortion** — more gain, a harder knee, and the whole range going into it. It makes its
        own sound instead of leaning on the amp's, which is why it can sit in front of a clean
        capture and still be a distorted guitar.
      - **Clean boost** — no clipping at all, just level and a tilt. What it drives is the amp.

    Oversampled 4x whichever is selected, because a waveshaper folds harmonics above Nyquist back
    down as aliasing. The oversampler runs even for the boost, which does not need it, and even
    while the slot is switched out — so the latency it reports never changes under the host.
*/
class DirtPedal
{
public:
    enum class Type { overdrive, distortion, cleanBoost };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    /** Jump to the current settings instead of ramping into them on every playback start. */
    void snapParameters();

    /** Audio thread. A change of type fades across, since the three sound nothing alike. */
    void setType (Type);

    /** @param amount 0–1.  @param tone 0–1, dark to bright.  @param levelDb the output trim. */
    void setParameters (float amount, float tone, float levelDb);

    void process (float* samples, int numSamples, bool bypassed);

    /** Constant, and reported to the host, because the oversampler always runs. */
    int getLatencySamples() const;

    /** The corner above which the overdrive boosts into its clipper. Below it the signal reaches
        the shaper as it arrived, which is what keeps a palm-muted low string defined. */
    static constexpr double boostCornerHz = 700.0;

    /** The distortion's own high pass, far lower: it is meant to pass the low end into the
        clipper, not to hold it back, and this only stops the very bottom turning to mud. */
    static constexpr double distortionCornerHz = 90.0;

    /** Where the tone control tilts about. */
    static constexpr double tiltCornerHz = 720.0;

private:
    void updateFilters();

    static constexpr int oversampleFactor = 2;   // 2^2 = 4x

    juce::dsp::Oversampling<float> oversampling { 1, oversampleFactor,
                                                  juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    juce::dsp::Gain<float> level;
    juce::dsp::IIR::Filter<float> boostFilter, toneFilter;
    juce::SmoothedValue<float> toneAmount, driveGain;

    std::atomic<Type> pendingType { Type::distortion };
    Type currentType = Type::distortion;

    /** Dips to silence and back when the type changes, so a swap cannot click. */
    juce::SmoothedValue<float> typeFade;
    bool swapping = false;

    double oversampledRate = 192000.0;
    BypassCrossfade bypass;
};
