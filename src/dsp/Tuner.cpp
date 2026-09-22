/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "Tuner.h"

namespace
{
    /** Below this, there is nothing worth calling a note — an open string decays past it. */
    constexpr float silenceRms = 0.0015f;

    /** YIN's absolute threshold. Lower is fussier about periodicity; 0.15 is the paper's value
        and behaves well on a guitar's harmonically rich tone. */
    constexpr float yinThreshold = 0.15f;
}

void Tuner::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    const auto maxLag = (int) std::ceil (sampleRate / lowestFrequency);
    difference.assign ((size_t) maxLag + 2, 0.0f);

    reset();
}

void Tuner::reset()
{
    fifo.reset();
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (window.begin(), window.end(), 0.0f);

    readingValid.store (false);
}

void Tuner::pushSamples (const float* samples, int numSamples)
{
    // If the message thread has not kept up, drop the oldest rather than block the audio thread.
    if (fifo.getFreeSpace() < numSamples)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (numSamples - fifo.getFreeSpace(), start1, size1, start2, size2);
        fifo.finishedRead (size1 + size2);
    }

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 > 0) juce::FloatVectorOperations::copy (ring.data() + start1, samples, size1);
    if (size2 > 0) juce::FloatVectorOperations::copy (ring.data() + start2, samples + size1, size2);

    fifo.finishedWrite (size1 + size2);
}

void Tuner::analyse()
{
    const auto available = fifo.getNumReady();

    if (available <= 0)
        return;

    // Slide whatever has arrived into the end of the window.
    const auto taking = juce::jmin (available, windowSize);

    if (taking < windowSize)
        std::memmove (window.data(), window.data() + taking,
                      (size_t) (windowSize - taking) * sizeof (float));

    int start1, size1, start2, size2;
    fifo.prepareToRead (taking, start1, size1, start2, size2);

    auto* destination = window.data() + (windowSize - taking);

    if (size1 > 0) juce::FloatVectorOperations::copy (destination, ring.data() + start1, size1);
    if (size2 > 0) juce::FloatVectorOperations::copy (destination + size1, ring.data() + start2, size2);

    fifo.finishedRead (size1 + size2);

    // Discard anything left over: a tuner wants the newest audio, not a backlog.
    fifo.finishedRead (fifo.getNumReady());

    double sumSquares = 0.0;

    for (auto sample : window)
        sumSquares += (double) sample * sample;

    if (std::sqrt (sumSquares / windowSize) < silenceRms)
    {
        readingValid.store (false);
        return;
    }

    const auto minLag = juce::jmax (2, (int) std::floor (sampleRate / highestFrequency));
    const auto maxLag = juce::jmin ((int) std::ceil (sampleRate / lowestFrequency), windowSize / 2 - 1);

    if (maxLag <= minLag)
    {
        readingValid.store (false);
        return;
    }

    // YIN step 1: the squared difference between the window and itself, lag by lag.
    const auto compareLength = windowSize - maxLag;

    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double sum = 0.0;

        for (int i = 0; i < compareLength; ++i)
        {
            const auto delta = (double) window[(size_t) i] - window[(size_t) (i + lag)];
            sum += delta * delta;
        }

        difference[(size_t) lag] = (float) sum;
    }

    // Step 2: cumulative mean normalisation, which is what stops it answering "zero lag".
    double runningSum = 0.0;
    difference[0] = 1.0f;

    for (int lag = 1; lag <= maxLag; ++lag)
    {
        runningSum += difference[(size_t) lag];
        difference[(size_t) lag] = runningSum > 0.0
                                     ? (float) (difference[(size_t) lag] * lag / runningSum)
                                     : 1.0f;
    }

    // Step 3: the first dip below the threshold, following it down to its local minimum. Taking
    // the first one rather than the smallest is what keeps it from answering an octave too high.
    int bestLag = -1;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (difference[(size_t) lag] < yinThreshold)
        {
            while (lag + 1 <= maxLag && difference[(size_t) (lag + 1)] < difference[(size_t) lag])
                ++lag;

            bestLag = lag;
            break;
        }
    }

    if (bestLag < 0)
    {
        readingValid.store (false);
        return;
    }

    // Step 4: parabolic interpolation, so the answer is not quantised to whole samples — at
    // 48 kHz a whole sample is about 6 cents up at the top of the range.
    auto refinedLag = (float) bestLag;

    if (bestLag > minLag && bestLag < maxLag)
    {
        const auto previous = difference[(size_t) (bestLag - 1)];
        const auto current = difference[(size_t) bestLag];
        const auto next = difference[(size_t) (bestLag + 1)];
        const auto denominator = 2.0f * (2.0f * current - previous - next);

        if (std::abs (denominator) > 1.0e-9f)
            refinedLag += (next - previous) / denominator;
    }

    const auto frequency = (float) (sampleRate / refinedLag);

    if (frequency < lowestFrequency || frequency > highestFrequency)
    {
        readingValid.store (false);
        return;
    }

    const auto exactNote = 69.0f + 12.0f * std::log2 (frequency / 440.0f);
    const auto nearestNote = (int) std::lround (exactNote);

    readingFrequency.store (frequency);
    readingNote.store (nearestNote);
    readingCents.store ((exactNote - (float) nearestNote) * 100.0f);
    readingValid.store (true);
}

Tuner::Reading Tuner::getReading() const
{
    Reading reading;

    reading.valid = readingValid.load();
    reading.frequencyHz = readingFrequency.load();
    reading.midiNote = readingNote.load();
    reading.cents = readingCents.load();

    return reading;
}

juce::String Tuner::noteName (int midiNote)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const auto index = ((midiNote % 12) + 12) % 12;
    const auto octave = midiNote / 12 - 1;

    return juce::String (names[index]) + juce::String (octave);
}
