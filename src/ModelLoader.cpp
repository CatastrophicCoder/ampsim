/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "ModelLoader.h"

#include <NAM/get_dsp.h>

ModelLoader::ModelLoader (AmpModel& modelToLoadInto)
    : juce::Thread ("AmpSim model loader"), ampModel (modelToLoadInto)
{
}

ModelLoader::~ModelLoader()
{
    stopThread (2000);
}

juce::String ModelLoader::getLastError() const
{
    const juce::ScopedLock lock (resultLock);
    return lastError;
}

void ModelLoader::loadAsync (const juce::File& file)
{
    {
        const juce::ScopedLock lock (resultLock);
        lastError.clear();
    }

    {
        const juce::ScopedLock lock (requestLock);
        requestedFile = file;
    }

    if (! isThreadRunning())
        startThread (juce::Thread::Priority::normal);
    else
        notify();
}

void ModelLoader::run()
{
    while (! threadShouldExit())
    {
        juce::File file;

        {
            const juce::ScopedLock lock (requestLock);
            file = requestedFile;
            requestedFile = juce::File();
        }

        if (file == juce::File())
        {
            wait (-1);
            continue;
        }

        Result result;
        result.file = file;

        if (! file.existsAsFile())
        {
            result.message = "File not found: " + file.getFullPathName();
        }
        else
        {
            try
            {
                auto model = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));

                if (model == nullptr)
                {
                    result.message = "The file could not be read as a NAM model.";
                }
                else
                {
                    // Allocates the resampler and prewarms the network — the reason all of this
                    // happens here and not on the audio thread.
                    auto loaded = ampModel.prepareForLoading (std::move (model));

                    // The audio thread refuses a second swap until it has handed the previous
                    // model back, so retry briefly rather than dropping the load.
                    for (int attempt = 0; attempt < 200 && ! threadShouldExit(); ++attempt)
                    {
                        if (ampModel.setPendingModel (std::move (loaded)))
                        {
                            result.succeeded = true;
                            break;
                        }

                        wait (10);
                    }

                    if (! result.succeeded && result.message.isEmpty())
                        result.message = "Timed out waiting for the audio thread to take the model.";
                }
            }
            catch (const std::exception& e)
            {
                result.message = juce::String ("Could not load the model: ") + e.what();
            }
            catch (...)
            {
                result.message = "Could not load the model.";
            }
        }

        {
            const juce::ScopedLock lock (resultLock);
            lastError = result.message;
        }

        if (onFinished != nullptr)
            juce::MessageManager::callAsync ([callback = onFinished, result] { callback (result); });
    }
}
