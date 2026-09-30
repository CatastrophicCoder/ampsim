/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

/** Runs a mono processing callback at a fixed sample rate, whatever rate the host is using.

    A `.nam` model is trained at one rate (usually 48 kHz) and has to be fed at that rate or its
    frequency response — and so its tone — shifts with the session rate. This class converts host
    → model on the way in and model → host on the way out, using windowed-sinc interpolation.

    The two conversions do not line up sample for sample: a 512-sample host block at 44.1 kHz is
    557.4 samples at 48 kHz, so each block produces a whole number of model samples and carries the
    remainder. Both directions therefore run through small FIFOs, and the output side is primed with
    silence so it can always satisfy a full block. That priming, plus the interpolators' own filter
    delay, is what `getLatencyInHostSamples()` reports for the host to compensate.

    When the host already runs at the model's rate the whole mechanism is skipped and the callback
    sees the host buffer directly.
*/
class ModelResampler
{
public:
    /** Model-rate samples of silence held back so a block can always be filled. Also the
        headroom that absorbs the ±1 sample jitter of the two conversions. */
    static constexpr int primeSamples = 32;

    /** Spare samples kept in the input FIFO so the interpolator cannot read past its end. */
    static constexpr int inputGuard = 4;

    void prepare (double hostSampleRate, double modelSampleRate, int maxHostBlockSize)
    {
        hostRate  = hostSampleRate;
        modelRate = modelSampleRate;
        maxHostBlock = maxHostBlockSize;

        passthrough = (modelRate <= 0.0) || juce::approximatelyEqual (hostRate, modelRate);

        if (passthrough)
        {
            maxModelBlock = maxHostBlockSize;
            reset();
            return;
        }

        hostPerModel = hostRate / modelRate;   // input samples consumed per model sample
        modelPerHost = modelRate / hostRate;

        // Worst case: a full block plus the carried remainder.
        maxModelBlock = (int) std::ceil (maxHostBlock * modelPerHost) + 4;

        hostFifo .assign ((size_t) (maxHostBlock * 2 + inputGuard + 8), 0.0f);
        modelFifo.assign ((size_t) (maxModelBlock * 2 + primeSamples + 8), 0.0f);
        modelScratch.assign ((size_t) maxModelBlock, 0.0f);

        reset();
    }

    void reset()
    {
        upsampler.reset();
        downsampler.reset();

        std::fill (hostFifo.begin(), hostFifo.end(), 0.0f);
        std::fill (modelFifo.begin(), modelFifo.end(), 0.0f);

        hostFifoCount = 0;
        modelFifoCount = passthrough ? 0 : primeSamples;   // the priming silence
    }

    bool isPassthrough() const noexcept   { return passthrough; }

    /** The largest number of model-rate samples a single host block can turn into. The model
        must be Reset() with at least this buffer size, or it will allocate mid-process. */
    int getMaxModelBlockSize() const noexcept { return maxModelBlock; }

    int getLatencyInHostSamples() const noexcept
    {
        if (passthrough)
            return 0;

        // The input-side interpolator delays by its filter length in host samples; the
        // output-side one by the same in model samples, as does the priming silence.
        const auto base = (double) juce::Interpolators::WindowedSinc::getBaseLatency();

        return (int) std::round (base + (base + primeSamples) * hostPerModel);
    }

    /** Audio thread. Processes `numHostSamples` in place, calling
        `processAtModelRate (float* samples, int numSamples)` at the model's sample rate. */
    template <typename ProcessFn>
    void process (float* data, int numHostSamples, ProcessFn&& processAtModelRate)
    {
        if (passthrough)
        {
            processAtModelRate (data, numHostSamples);
            return;
        }

        // A guard, not an assertion: an assertion is compiled out of a release build, and the
        // failure this catches is a write past the end of the FIFOs rather than a wrong number.
        // Silence for a block is a glitch; the alternative is memory corruption.
        if (numHostSamples > maxHostBlock)
        {
            jassertfalse;
            juce::FloatVectorOperations::clear (data, numHostSamples);
            return;
        }

        // 1. Take the block in.
        std::copy (data, data + numHostSamples, hostFifo.begin() + hostFifoCount);
        hostFifoCount += numHostSamples;

        // 2. Convert as much as the input FIFO safely allows.
        const auto produce = juce::jlimit (0, maxModelBlock,
                                           (int) std::floor ((hostFifoCount - inputGuard) / hostPerModel));

        if (produce > 0)
        {
            const auto used = upsampler.process (hostPerModel, hostFifo.data(), modelScratch.data(), produce);
            consume (hostFifo, hostFifoCount, used);

            // 3. The model's turn.
            processAtModelRate (modelScratch.data(), produce);

            std::copy (modelScratch.begin(), modelScratch.begin() + produce,
                       modelFifo.begin() + modelFifoCount);
            modelFifoCount += produce;
        }

        // 4. Convert back, taking exactly one host block out. Producing N host samples consumes
        //    N * modelPerHost model samples — not hostPerModel, which is the other direction and
        //    starves this FIFO whenever the host runs faster than the model.
        const auto needed = (int) std::ceil (numHostSamples * modelPerHost) + inputGuard;

        if (modelFifoCount < needed)
        {
            // Only reachable if the priming was not enough — treat as a dropout rather than
            // reading uninitialised samples, and let the FIFO refill.
            juce::FloatVectorOperations::clear (data, numHostSamples);
            return;
        }

        const auto used = downsampler.process (modelPerHost, modelFifo.data(), data, numHostSamples);
        consume (modelFifo, modelFifoCount, used);
    }

private:
    static void consume (std::vector<float>& fifo, int& count, int numUsed)
    {
        jassert (numUsed <= count);

        const auto remaining = count - numUsed;

        if (remaining > 0)
            std::memmove (fifo.data(), fifo.data() + numUsed, (size_t) remaining * sizeof (float));

        count = remaining;
    }

    juce::Interpolators::WindowedSinc upsampler, downsampler;

    std::vector<float> hostFifo, modelFifo, modelScratch;
    int hostFifoCount = 0, modelFifoCount = 0;

    double hostRate = 0.0, modelRate = 0.0;
    double hostPerModel = 1.0, modelPerHost = 1.0;
    int maxHostBlock = 0, maxModelBlock = 0;
    bool passthrough = true;
};
