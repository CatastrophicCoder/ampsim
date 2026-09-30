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
    /** Below this, there is nothing worth calling a note. Set low: a high string a few seconds
        into its decay is far quieter than a freshly plucked low one, and gating it off is what
        makes a tuner give up exactly when you are still turning the peg. */
    constexpr float silenceRms = 0.0012f;

    /** YIN's absolute threshold. Lower is fussier about periodicity; 0.15 is the paper's value
        and behaves well on a guitar's harmonically rich tone. */
    constexpr float yinThreshold = 0.15f;

    /** When nothing dips below the threshold, the best candidate is accepted up to here instead.
        A decaying high string, whose fundamental is weak to begin with, often never reaches 0.15
        — and refusing to answer at all is worse than answering with the best periodicity found. */
    constexpr float fallbackThreshold = 0.30f;

    /** How far a new candidate may sit from what is being shown before it counts as a different
        note rather than the same one wavering. Roughly a semitone. */
    constexpr float sameNoteTolerance = 0.03f;

    /** Agreeing frames needed before a genuinely different pitch is believed. At the analysis
        rate this is about a tenth of a second — slower than a bad frame, faster than a player. */
    constexpr int agreementsNeeded = 4;

    /** How firmly the cents reading is held back. A real string drifts sharp on the attack and
        settles, and the needle should not chase every frame of that. */
    constexpr float centsSmoothing = 0.4f;
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
    readingLive.store (false);

    historyCount = 0;
    historyWriteIndex = 0;
    haveSmoothedCents = false;
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
        publishNoDetection();
        return;
    }

    const auto minLag = juce::jmax (2, (int) std::floor (sampleRate / highestFrequency));
    const auto maxLag = juce::jmin ((int) std::ceil (sampleRate / lowestFrequency), windowSize / 2 - 1);

    if (maxLag <= minLag)
    {
        publishNoDetection();
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
        // Nothing crossed the threshold: take the most periodic lag there is, if it is credible.
        auto lowest = minLag;

        for (int lag = minLag; lag <= maxLag; ++lag)
            if (difference[(size_t) lag] < difference[(size_t) lowest])
                lowest = lag;

        if (difference[(size_t) lowest] >= fallbackThreshold)
        {
            publishNoDetection();
            return;
        }

        bestLag = lowest;
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
        publishNoDetection();
        return;
    }

    publish (frequency);
}

void Tuner::publish (float frequency)
{
    // A candidate far from what is already showing is either a new note or a bad frame, and the
    // two are told apart by whether it happens again. Accepting immediately is what puts an
    // octave error on the display; refusing outright would stop it following a real change.
    if (historyCount > 0)
    {
        const auto showing = readingFrequency.load();
        const auto differs = std::abs (frequency - showing) > showing * sameNoteTolerance;

        if (differs)
        {
            const auto agreesWithPending = pendingAgreements > 0
                                        && std::abs (frequency - pendingFrequency)
                                             <= pendingFrequency * sameNoteTolerance;

            pendingFrequency = frequency;
            pendingAgreements = agreesWithPending ? pendingAgreements + 1 : 1;

            if (pendingAgreements < agreementsNeeded)
                return;   // hold what is on screen until this settles

            // Believed: start afresh on the new pitch rather than dragging the old one along.
            historyCount = 0;
            historyWriteIndex = 0;
            haveSmoothedCents = false;
        }
    }

    pendingAgreements = 0;

    recentFrequencies[(size_t) historyWriteIndex] = frequency;
    historyWriteIndex = (historyWriteIndex + 1) % historySize;
    historyCount = juce::jmin (historyCount + 1, historySize);

    // The median, not the mean: one wrong frame should be discarded outright rather than
    // averaged in, and an octave error is exactly the kind of frame a mean would smear.
    std::array<float, historySize> sorted {};
    std::copy (recentFrequencies.begin(), recentFrequencies.begin() + historyCount, sorted.begin());
    std::sort (sorted.begin(), sorted.begin() + historyCount);

    const auto median = sorted[(size_t) (historyCount / 2)];

    const auto exactNote = 69.0f + 12.0f * std::log2 (median / 440.0f);
    const auto nearestNote = (int) std::lround (exactNote);
    const auto cents = (exactNote - (float) nearestNote) * 100.0f;

    // Jumping to a different note should not drag the needle across from the old one.
    if (! haveSmoothedCents || nearestNote != readingNote.load())
    {
        smoothedCents = cents;
        haveSmoothedCents = true;
    }
    else
    {
        smoothedCents += centsSmoothing * (cents - smoothedCents);
    }

    readingFrequency.store (median);
    readingNote.store (nearestNote);
    readingCents.store (smoothedCents);
    readingLive.store (true);
    readingValid.store (true);
}

void Tuner::publishNoDetection()
{
    // The last note stays on screen; only its liveness changes. Whatever history there was is
    // dropped, so the next note starts clean rather than blending into the one before it.
    historyCount = 0;
    historyWriteIndex = 0;
    haveSmoothedCents = false;
    pendingAgreements = 0;

    readingLive.store (false);
}

Tuner::Reading Tuner::getReading() const
{
    Reading reading;

    reading.valid = readingValid.load();
    reading.live = readingLive.load();
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
