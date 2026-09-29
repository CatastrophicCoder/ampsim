/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

/** The cabinet: up to four impulse responses at the corners of a mic-position space, blended.

    A single IR is one microphone in one place. Moving a mic across a speaker changes the response
    smoothly, so four captures — on and off axis, close and far — with a bilinear blend between
    them give two continuous controls over mic position.

    The blend is done on the **outputs of four convolutions**, not by mixing the IRs and reloading.
    Convolution is linear, so the two are mathematically identical, but mixing outputs needs no
    reload as the knobs move: no queued IR loads, no crossfades, nothing to click. It costs four
    convolutions instead of one, which measured cheap next to the amp model.

    With one IR loaded it behaves exactly as a single-IR loader, and runs only that one convolution.

    What this cannot do is tell you whether it sounds like moving a microphone. That depends
    entirely on the captures put in the corners; a grid of IRs of the same cab at known positions
    is source material this project does not ship.

    **Levels.** Captures are not made to a common level — a commercial pack can carry 15 dB of
    broadband gain — so the cab normalises. One factor is applied to the whole grid rather than one
    per corner, because a corner that really is quieter (a mic backed off, or off axis) should stay
    quieter; only the grid's overall level is brought to unity. The factor comes from the loudest
    loaded corner, so it does not depend on the order the corners were filled in.
*/
class CabSim
{
public:
    /** The corners of the mic-position space. Order matters: it is the bilinear layout. */
    enum class Slot
    {
        centreClose = 0,   // on axis, at the grille
        edgeClose,         // off axis, at the grille
        centreFar,         // on axis, backed off
        edgeFar,           // off axis, backed off
        count
    };

    static constexpr int numSlots = (int) Slot::count;

    CabSim() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Message thread. Each slot is read and resampled on its convolution's own thread. */
    void loadImpulseResponse (Slot, const juce::File&);
    void clearSlot (Slot);

    /** @param axis 0 = on the dust cap, 1 = at the cone's edge.
        @param distance 0 = against the grille, 1 = backed off. */
    void setMicPosition (float axis, float distance);

    /** Audio thread. Processes one mono block in place. */
    void process (float* samples, int numSamples, bool bypassed);

    bool hasImpulseResponse() const      { return loadedSlots.load() != 0; }

    /** The gain the normalisation is applying to the whole grid, in dB. 0 when nothing is loaded. */
    float getNormalisationDb() const;

    /** The length of the impulse response actually running in a slot.

        JUCE installs a one-sample default engine during prepare() and reads the file on its own
        thread, so a size of 1 means nothing has arrived yet. That is why hasImpulseResponse()
        reports what has been asked for rather than this. */
    int getLoadedSize (Slot slot) const  { return convolutions[(size_t) slot].getCurrentIRSize(); }
    bool isSlotLoaded (Slot slot) const  { return (loadedSlots.load() & (1 << (int) slot)) != 0; }
    int getLatencySamples() const        { return convolutions[0].getLatency(); }

private:
    void updateWeights();
    void updateNormalisation();

    /** The average magnitude response across the range a guitar occupies, which is what decides
        how loud an impulse response sounds. Total energy would count the deep sub-bass and the
        air above 6 kHz that a speaker cabinet rolls away, and under-read the level by half. */
    static float measureBandGain (const juce::File&);

    mutable std::array<juce::dsp::Convolution, numSlots> convolutions;
    std::array<juce::SmoothedValue<float>, numSlots> weights;
    std::array<juce::AudioBuffer<float>, numSlots> slotBuffers;

    std::atomic<int> loadedSlots { 0 };   // bit per slot
    float micAxis = 0.0f, micDistance = 0.0f;

    std::array<float, numSlots> slotBandGain { 1.0f, 1.0f, 1.0f, 1.0f };
    std::atomic<float> normalisationTarget { 1.0f };
    juce::SmoothedValue<float> normalisation;

    // 1 = fully bypassed, ramped so the switch cannot click.
    juce::SmoothedValue<float> bypassMix;
    juce::AudioBuffer<float> dryBuffer, mixBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabSim)
};
