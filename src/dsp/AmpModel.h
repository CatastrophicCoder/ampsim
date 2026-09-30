/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "ModelResampler.h"

#include <atomic>
#include <memory>

namespace nam { class DSP; }

/** A model and the rate conversion it needs, prepared together.

    They belong together because the conversion depends on the model's own sample rate: swapping
    a 48 kHz model into a chain configured for a 44.1 kHz one would need the resampler rebuilt,
    which allocates, which the audio thread may not do. So the loader thread builds both, and the
    audio thread swaps the pair with one pointer.
*/
struct LoadedModel
{
    std::unique_ptr<nam::DSP> dsp;
    ModelResampler resampler;

    // What this was sized for. The loader thread reads the host's settings when it runs, which
    // may be before prepareToPlay has supplied them, so the audio thread checks before swapping.
    double preparedHostRate = 0.0;
    int preparedMaxBlockSize = 0;

    /** Linear gain that brings this capture to the reference loudness, or 1 if the file does not
        say how loud it is. Worked out on the loader thread, because it is a property of the file
        rather than of the block being processed. */
    float normalisation = 1.0f;

    ~LoadedModel();
};

/** Holds the Neural Amp Modeler model and runs it at the rate it was trained at.

    Three threads touch this class, and which one may do what is the whole design:

      - a **loader thread** parses the `.nam` file and calls `prepareForLoading()`, which allocates
        and prewarms, then hands the result over with `setPendingModel()`;
      - the **audio thread** picks it up in `process()`, swapping under a short mute so the change
        cannot click, and hands the old one back;
      - the **message thread** calls `collectRetiredModel()` to delete it.

    The audio thread therefore never allocates, never blocks and never destroys anything. It also
    refuses to start a swap while a retired model is still waiting to be collected, which is what
    keeps the hand-back slot a single pointer rather than a queue.
*/
class AmpModel
{
public:
    AmpModel();
    ~AmpModel();

    /** Message thread. Also re-prepares an already-loaded model for the new rate. */
    void prepare (double hostSampleRate, int maxBlockSize);
    void reset();

    /** Audio thread. Processes one mono block in place.
        @returns true if a model was in the chain; false leaves the samples untouched. */
    bool process (float* samples, int numSamples);

    /** Loader thread. Wraps a freshly parsed model in a resampler and prewarms it, ready to be
        handed to the audio thread. Allocates. */
    std::unique_ptr<LoadedModel> prepareForLoading (std::unique_ptr<nam::DSP> dsp) const;

    /** Any thread. Takes ownership.
        @returns false if a swap is already queued, in which case the model is not taken. */
    bool setPendingModel (std::unique_ptr<LoadedModel> model);

    /** Message thread. Deletes the model the audio thread swapped out, if any. */
    void collectRetiredModel();

    /** The level correction applied to the capture in use, in dB, or 0 when there is none —
        either because no model is loaded or because the file does not carry a loudness. */
    float getNormalisationDb() const;

    /** Every capture is brought to this loudness, the figure NAM's own plugin normalises to. */
    static constexpr double referenceLoudnessDb = -18.0;

    /** Message thread. Re-sizes a model the audio thread refused because it was prepared for
        different host settings — a load that finished before prepareToPlay, most often at
        startup, when a session restores a model path before the device is open.
        @returns true if one was re-prepared. */
    bool repreparePendingModelIfNeeded();

    bool hasModel() const noexcept          { return modelIsLoaded.load(); }

    /** Whether prepareToPlay has supplied a sample rate and block size yet. Until it has, there is
        nothing to size a model against. */
    bool hasHostSettings() const noexcept   { return hostRate.load() > 0.0 && maxBlockSize.load() > 0; }
    int getLatencySamples() const noexcept  { return latencySamples.load(); }

    /** The sample rate the loaded model expects, or 0 if there is no model. */
    double getModelSampleRate() const noexcept { return loadedModelRate.load(); }

private:
    void refreshPublishedState();

    std::unique_ptr<LoadedModel> currentModel;          // audio thread
    std::atomic<LoadedModel*> pendingModel { nullptr }; // loader → audio
    std::atomic<LoadedModel*> retiredModel { nullptr }; // audio → message

    juce::SmoothedValue<float> swapFade;
    bool swapInProgress = false;

    std::atomic<bool> pendingNeedsPreparing { false };
    std::atomic<bool> modelIsLoaded { false };
    std::atomic<int> latencySamples { 0 };
    std::atomic<float> publishedNormalisationDb { 0.0f };
    std::atomic<double> loadedModelRate { 0.0 };

    // Read by the loader thread while the audio thread runs.
    std::atomic<double> hostRate { 0.0 };
    std::atomic<int> maxBlockSize { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpModel)
};
