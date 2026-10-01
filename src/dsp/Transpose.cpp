/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "Transpose.h"

double Transpose::windowSecondsFor (int semitones)
{
    const auto ratio = std::pow (2.0, (double) semitones / 12.0);

    return juce::jlimit (shortestWindowSeconds, longestWindowSeconds,
                         windowPerUnitRate * std::abs (1.0 - ratio));
}

void Transpose::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);

    preparedRate = sampleRate;

    windowSamples = (float) (windowSecondsFor (pendingSemitones.load()) * sampleRate);
    searchSamples = (int) std::round (searchSeconds * sampleRate);
    matchSamples = (int) std::round (matchSeconds * sampleRate);
    crossfadeSamples = (int) std::round (crossfadeSeconds * sampleRate);

    // The furthest back anything reads: a pointer a window behind, the jump it is looking at, and
    // the stretch the two sides are matched over. Rounded up to a power of two so that wrapping
    // the ring is a mask rather than a division.
    const auto needed = (int) std::ceil (longestWindowSeconds * sampleRate) * 2 + matchSamples + 8;
    auto size = 1;

    while (size < needed)
        size <<= 1;

    history.assign ((size_t) size, 0.0f);
    mask = size - 1;
    writeIndex = 0;

    adoptInterval (pendingSemitones.load());

    intervalFade.reset (sampleRate, 0.01);
    intervalFade.setCurrentAndTargetValue (1.0f);
    changing = false;

    bypass.prepare (sampleRate, maxBlockSize);
}

void Transpose::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    writeIndex = 0;
    readDelay = windowSamples * 0.5f;
    crossfadeLeft = 0;
}

void Transpose::push (float sample)
{
    history[(size_t) writeIndex] = sample;
    writeIndex = (writeIndex + 1) & mask;
}

float Transpose::atWholeSample (int delayInSamples) const
{
    return history[(size_t) ((writeIndex - 1 - delayInSamples) & mask)];
}

float Transpose::at (float delayInSamples) const
{
    const auto clamped = juce::jlimit (0.0f, (float) mask - 1.0f, delayInSamples);
    const auto whole = (int) clamped;
    const auto fraction = clamped - (float) whole;

    const auto nearer = atWholeSample (whole);
    const auto further = atWholeSample (whole + 1);

    return nearer + fraction * (further - nearer);
}

void Transpose::setSemitones (int semitones)
{
    pendingSemitones.store (juce::jlimit (-maxSemitones, maxSemitones, semitones));
}

void Transpose::adoptInterval (int semitones)
{
    currentSemitones = semitones;
    rate = std::pow (2.0f, (float) semitones / 12.0f);
    windowSamples = (float) (windowSecondsFor (semitones) * preparedRate);

    // Start in the middle of the travel, so there is room to move whichever way the pointer goes.
    readDelay = windowSamples * 0.5f;
    crossfadeLeft = 0;
}

float Transpose::bestJumpFrom (float delay, int direction) const
{
    // How far back the signal most nearly repeats, looked for just short of a window. Both
    // stretches being compared are already in the history, so this is an autocorrelation of what
    // has recently been played — no pitch detection, and nothing a chord confuses.
    //
    // Never longer than a window, so that whichever way the pointer jumps it lands back inside
    // its travel instead of running straight out of the other end.
    const auto shortest = juce::jmax (1, (int) windowSamples - searchSamples);
    const auto longest = (int) windowSamples;
    const auto from = (int) delay;

    auto bestJump = longest;
    auto bestScore = -1.0e30f;

    // Coarse then fine: a period is hundreds of samples across, so stepping four at a time finds
    // the right hill and a second pass finds its top. Whole samples throughout, and only every
    // fourth one compared — a join is being matched, not measured.
    for (auto pass = 0; pass < 2; ++pass)
    {
        const auto step = pass == 0 ? 4 : 1;
        const auto first = pass == 0 ? shortest : juce::jmax (shortest, bestJump - 4);
        const auto last = pass == 0 ? longest : juce::jmin (longest, bestJump + 4);

        for (auto jump = first; jump <= last; jump += step)
        {
            float product = 0.0f, energy = 0.0f;

            for (int i = 0; i < matchSamples; i += 4)
            {
                const auto here = atWholeSample (from + i);
                const auto there = atWholeSample (from + direction * jump + i);

                product += here * there;
                energy += there * there;
            }

            // Normalised, or the loudest place always wins rather than the best match.
            const auto score = product / std::sqrt (juce::jmax (1.0e-9f, energy));

            if (score > bestScore)
            {
                bestScore = score;
                bestJump = jump;
            }
        }
    }

    return (float) bestJump;
}

void Transpose::process (float* samples, int numSamples, bool bypassed)
{
    const auto wanted = pendingSemitones.load();

    // Starting or stopping a shift is a change between dry and shifted, and the bypass crossfade
    // already covers that — so those are taken at once, and only a change from one interval to
    // another gets a fade of its own, because both sides of that one are audible.
    if (currentSemitones == 0 || wanted == 0)
    {
        if (wanted != currentSemitones)
        {
            adoptInterval (wanted);
            intervalFade.setCurrentAndTargetValue (1.0f);
            changing = false;
        }
    }
    else if (! changing && wanted != currentSemitones)
    {
        changing = true;
        intervalFade.setTargetValue (0.0f);
    }

    if (changing && ! intervalFade.isSmoothing() && intervalFade.getCurrentValue() <= 0.0f)
    {
        adoptInterval (wanted);

        changing = false;
        intervalFade.setTargetValue (1.0f);
    }

    // At unity the pointer would still sit half a window behind and delay the signal for nothing,
    // so the whole thing steps aside. This is also what makes the middle of the control honest.
    const auto action = bypass.beginBlock (bypassed || currentSemitones == 0, samples, numSamples);

    if (action == BypassCrossfade::Action::skip)
    {
        // The fade has to keep moving even here, or an interval changed while the shifter is
        // stepped aside would leave it waiting for a fade that never finishes.
        intervalFade.skip (numSamples);

        for (int i = 0; i < numSamples; ++i)
            push (samples[i]);

        return;
    }

    // out(t) = in(t - D(t)), so the frequency multiplier is 1 - D'(t): to raise the pitch the
    // delay has to shrink, and to lower it, grow.
    const auto delayStep = 1.0f - rate;

    for (int i = 0; i < numSamples; ++i)
    {
        push (samples[i]);

        auto out = at (readDelay);

        if (crossfadeLeft > 0)
        {
            // Equal gain, because the two sides have been matched to each other and so add rather
            // than cancel. An equal-power fade would bulge where they agree.
            const auto through = 1.0f - (float) crossfadeLeft / (float) crossfadeSamples;

            out = through * out + (1.0f - through) * at (outgoingDelay);
            outgoingDelay += delayStep;
            --crossfadeLeft;
        }

        samples[i] = out;

        readDelay += delayStep;

        // The pointer has run out of room, so hand over to one a matched distance away — nearer
        // the write head if it has drifted too far from it, further if it has caught up.
        if (readDelay > windowSamples || readDelay < 0.0f)
        {
            const auto direction = readDelay < 0.0f ? 1 : -1;
            const auto jump = bestJumpFrom (juce::jlimit (0.0f, windowSamples, readDelay), direction);

            outgoingDelay = readDelay;
            readDelay += (float) direction * jump;
            crossfadeLeft = crossfadeSamples;
        }
    }

    if (action == BypassCrossfade::Action::crossfade)
        bypass.finishBlock (samples, numSamples);

    if (intervalFade.isSmoothing() || intervalFade.getCurrentValue() < 1.0f)
    {
        const auto start = intervalFade.getCurrentValue();
        intervalFade.skip (numSamples);

        juce::AudioBuffer<float> view (&samples, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, start, intervalFade.getCurrentValue());
    }
}
