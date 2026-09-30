/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "Transpose.h"

void Transpose::prepare (double sampleRate, int maxBlockSize)
{
    preparedRate = sampleRate;

    windowSamples = (float) (windowSeconds * sampleRate);
    searchSamples = (int) std::round (searchSeconds * sampleRate);
    matchSamples = (int) std::round (matchSeconds * sampleRate);
    crossfadeSamples = (int) std::round (crossfadeSeconds * sampleRate);

    // The furthest back anything reads: a pointer a window behind, plus the jump it is looking
    // ahead to, plus the stretch the two sides are matched over.
    line.setMaximumDelayInSamples ((int) std::ceil (windowSamples) + 2 * searchSamples + matchSamples + 8);
    line.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
    line.reset();

    adoptInterval (pendingSemitones.load());

    intervalFade.reset (sampleRate, 0.01);
    intervalFade.setCurrentAndTargetValue (1.0f);
    changing = false;

    bypass.prepare (sampleRate, maxBlockSize);
}

void Transpose::reset()
{
    line.reset();
    readDelay = lowestDelay() + windowSamples * 0.5f;
    crossfadeLeft = 0;
}

void Transpose::setSemitones (int semitones)
{
    pendingSemitones.store (juce::jlimit (-maxSemitones, maxSemitones, semitones));
}

void Transpose::adoptInterval (int semitones)
{
    currentSemitones = semitones;
    rate = std::pow (2.0f, (float) semitones / 12.0f);

    // Start in the middle of the travel, so there is room to move whichever way the pointer goes.
    readDelay = lowestDelay() + windowSamples * 0.5f;
    crossfadeLeft = 0;
}

float Transpose::readAt (float delayInSamples)
{
    const auto clamped = juce::jlimit (1.0f, (float) line.getMaximumDelayInSamples() - 2.0f,
                                       delayInSamples);

    return line.popSample (0, clamped, false);
}

float Transpose::bestJumpFrom (float delay, float direction)
{
    // How far back the signal most nearly repeats, looked for around a window's distance. Both
    // stretches being compared are already in the line, so this is an autocorrelation of what has
    // recently been played — no pitch detection, and nothing that a chord confuses.
    const auto lowest = juce::jmax (1.0f, windowSamples - (float) searchSamples);
    const auto highest = windowSamples + (float) searchSamples;

    auto bestJump = windowSamples;
    auto bestScore = -1.0e30f;

    // Coarse then fine: a period is hundreds of samples across, so stepping four at a time finds
    // the right hill and a second pass finds its top. Whole samples throughout — a join is being
    // matched, not measured.
    for (auto pass = 0; pass < 2; ++pass)
    {
        const auto step = pass == 0 ? 4.0f : 1.0f;
        const auto from = pass == 0 ? lowest : juce::jmax (lowest, bestJump - 4.0f);
        const auto to = pass == 0 ? highest : juce::jmin (highest, bestJump + 4.0f);

        for (auto jump = from; jump <= to; jump += step)
        {
            float product = 0.0f, energy = 0.0f;

            for (int i = 0; i < matchSamples; i += 2)
            {
                const auto here = readAt (delay + (float) i);
                const auto there = readAt (delay + direction * jump + (float) i);

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

    return bestJump;
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
        {
            line.pushSample (0, samples[i]);
            line.popSample (0, 1.0f, true);
        }

        return;
    }

    // out(t) = in(t - D(t)), so the frequency multiplier is 1 - D'(t): to raise the pitch the
    // delay has to shrink, and to lower it, grow.
    const auto delayStep = 1.0f - rate;
    const auto lower = lowestDelay();
    const auto upper = lower + windowSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        line.pushSample (0, samples[i]);

        auto out = readAt (readDelay);

        if (crossfadeLeft > 0)
        {
            // Equal gain, because the two sides have been matched to each other and so add rather
            // than cancel. An equal-power fade would bulge where they agree.
            const auto through = 1.0f - (float) crossfadeLeft / (float) crossfadeSamples;

            out = through * out + (1.0f - through) * readAt (outgoingDelay);
            outgoingDelay += delayStep;
            --crossfadeLeft;
        }

        samples[i] = out;

        readDelay += delayStep;

        // The pointer has run out of room, so hand over to one a matched distance away — nearer
        // the write head if it has drifted too far from it, further if it has caught up.
        if (readDelay > upper || readDelay < lower)
        {
            const auto direction = readDelay < lower ? 1.0f : -1.0f;
            const auto jump = bestJumpFrom (readDelay, direction);

            outgoingDelay = readDelay;
            readDelay += direction * jump;
            crossfadeLeft = crossfadeSamples;
        }

        line.popSample (0, 1.0f, true);
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
