/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "AmpModel.h"

#include <NAM/dsp.h>

namespace
{
    // Long enough to be inaudible as a click, short enough not to sound like a gap.
    constexpr double swapFadeSeconds = 0.01;
}

LoadedModel::~LoadedModel() = default;

AmpModel::AmpModel() = default;

AmpModel::~AmpModel()
{
    // Whatever the audio thread handed back never got collected; this is the last chance.
    delete retiredModel.exchange (nullptr);
    delete pendingModel.exchange (nullptr);
}

void AmpModel::prepare (double hostSampleRate, int maxBlockSizeIn)
{
    hostRate.store (hostSampleRate);
    maxBlockSize.store (maxBlockSizeIn);

    swapFade.reset (hostSampleRate, swapFadeSeconds);
    swapFade.setCurrentAndTargetValue (1.0f);
    swapInProgress = false;

    // prepareToPlay is not the audio thread, so a model waiting in the wings can simply be
    // installed here rather than swapped in under a fade.
    if (auto* waiting = pendingModel.exchange (nullptr))
        currentModel.reset (waiting);

    // The rate or block size may have changed under an already-loaded model, and both the
    // resampler and NAM's own buffers are sized from them. Allocating here is fine.
    if (currentModel != nullptr && currentModel->dsp != nullptr)
    {
        const auto modelRate = currentModel->dsp->GetExpectedSampleRate();
        const auto effectiveRate = modelRate > 0.0 ? modelRate : hostSampleRate;

        currentModel->resampler.prepare (hostSampleRate, effectiveRate, maxBlockSizeIn);
        currentModel->dsp->Reset (effectiveRate, currentModel->resampler.getMaxModelBlockSize());
    }

    refreshPublishedState();
}

void AmpModel::refreshPublishedState()
{
    const auto haveModel = currentModel != nullptr && currentModel->dsp != nullptr;

    modelIsLoaded.store (haveModel);
    latencySamples.store (haveModel ? currentModel->resampler.getLatencyInHostSamples() : 0);

    if (haveModel)
    {
        const auto rate = currentModel->dsp->GetExpectedSampleRate();
        loadedModelRate.store (rate > 0.0 ? rate : hostRate.load());
    }
    else
    {
        loadedModelRate.store (0.0);
    }
}

void AmpModel::reset()
{
    if (currentModel != nullptr)
        currentModel->resampler.reset();

    swapFade.setCurrentAndTargetValue (1.0f);
    swapInProgress = false;
}

std::unique_ptr<LoadedModel> AmpModel::prepareForLoading (std::unique_ptr<nam::DSP> dsp) const
{
    if (dsp == nullptr)
        return nullptr;

    auto loaded = std::make_unique<LoadedModel>();

    const auto modelRate = dsp->GetExpectedSampleRate();
    const auto effectiveRate = modelRate > 0.0 ? modelRate : hostRate.load();

    loaded->resampler.prepare (hostRate.load(), effectiveRate, maxBlockSize.load());

    // Reset() sizes NAM's internal buffers and prewarms the network — the expensive part, and the
    // reason this must not happen on the audio thread.
    dsp->Reset (effectiveRate, loaded->resampler.getMaxModelBlockSize());

    loaded->dsp = std::move (dsp);
    return loaded;
}

bool AmpModel::setPendingModel (std::unique_ptr<LoadedModel> model)
{
    LoadedModel* expected = nullptr;

    if (! pendingModel.compare_exchange_strong (expected, model.get()))
        return false;   // a swap is already queued; the caller keeps the model

    model.release();
    return true;
}

void AmpModel::collectRetiredModel()
{
    delete retiredModel.exchange (nullptr);
}

bool AmpModel::process (float* samples, int numSamples)
{
    // Only start a swap once the previous model has been collected, so the hand-back slot is
    // always free and the audio thread never has to delete anything.
    const auto swapWaiting = pendingModel.load() != nullptr && retiredModel.load() == nullptr;

    if (swapWaiting && ! swapInProgress)
    {
        swapInProgress = true;
        swapFade.setTargetValue (0.0f);
    }

    if (swapInProgress && ! swapFade.isSmoothing() && swapFade.getCurrentValue() <= 0.0f)
    {
        if (auto* incoming = pendingModel.exchange (nullptr))
        {
            retiredModel.store (currentModel.release());
            currentModel.reset (incoming);
            currentModel->resampler.reset();

            refreshPublishedState();
        }

        swapInProgress = false;
        swapFade.setTargetValue (1.0f);
    }

    const auto haveModel = currentModel != nullptr && currentModel->dsp != nullptr;

    if (haveModel)
    {
        auto* dsp = currentModel->dsp.get();

        currentModel->resampler.process (samples, numSamples,
                                         [dsp] (float* modelSamples, int numModelSamples)
                                         {
                                             // NAM takes channel-indexed buffers; the chain is mono here.
                                             float* channels[1] { modelSamples };
                                             dsp->process (channels, channels, numModelSamples);
                                         });
    }

    // The fade runs whether or not a model is loaded, so the very first load fades in rather
    // than cutting from dry to modelled.
    if (swapFade.isSmoothing() || swapFade.getCurrentValue() < 1.0f)
    {
        const auto start = swapFade.getCurrentValue();
        swapFade.skip (numSamples);

        juce::AudioBuffer<float> view (&samples, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, start, swapFade.getCurrentValue());
    }

    return haveModel;
}
