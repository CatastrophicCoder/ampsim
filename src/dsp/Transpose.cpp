/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "Transpose.h"

namespace
{
    /** How much of a grain the two read pointers share, as a fraction of it.

        The obvious window — a raised cosine over the whole grain — has both pointers sounding all
        of the time, which means two copies of the note a third of a grain apart at every instant.
        That is where a granular shifter's chorused, hollow sound comes from, and on a guitar it is
        the difference between a retuned instrument and an effect. Handing over briefly instead
        leaves one copy playing for most of the grain.
    */
    constexpr float crossfadeFraction = 0.12f;

    /** The gain of a tap at its position through the grain. Two taps half a grain apart always
        add to one, so nothing is lost or doubled at the handover. */
    inline float gainAt (float position) noexcept
    {
        const auto rise = [] (float u)
        {
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u);
        };

        if (position < crossfadeFraction)
            return rise (position / crossfadeFraction);

        if (position < 0.5f)
            return 1.0f;

        if (position < 0.5f + crossfadeFraction)
            return 1.0f - rise ((position - 0.5f) / crossfadeFraction);

        return 0.0f;
    }
}

double Transpose::windowSecondsFor (int semitones)
{
    const auto ratio = std::pow (2.0, (double) semitones / 12.0);

    return juce::jlimit (shortestWindowSeconds, longestWindowSeconds,
                         windowPerUnitRate * std::abs (1.0 - ratio));
}

void Transpose::prepare (double sampleRate, int maxBlockSize)
{
    preparedRate = sampleRate;
    windowSamples = (float) (windowSecondsFor (pendingSemitones.load()) * sampleRate);

    // The taps read up to a whole window behind the write pointer, and linear interpolation reads
    // one sample either side of that. Sized for the longest window any interval can ask for, so
    // turning the dial never needs an allocation.
    line.setMaximumDelayInSamples ((int) std::ceil (longestWindowSeconds * sampleRate) + 4);
    line.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
    line.reset();

    phase = 0.0f;
    currentSemitones = pendingSemitones.load();

    intervalFade.reset (sampleRate, 0.01);
    intervalFade.setCurrentAndTargetValue (1.0f);
    changing = false;

    bypass.prepare (sampleRate, maxBlockSize);
}

void Transpose::reset()
{
    line.reset();
    phase = 0.0f;
}

void Transpose::setSemitones (int semitones)
{
    pendingSemitones.store (juce::jlimit (-maxSemitones, maxSemitones, semitones));
}

void Transpose::process (float* samples, int numSamples, bool bypassed)
{
    // A change of interval is also a change of window, so the grains restart: fade across it.
    if (! changing && pendingSemitones.load() != currentSemitones)
    {
        changing = true;
        intervalFade.setTargetValue (0.0f);
    }

    if (changing && ! intervalFade.isSmoothing() && intervalFade.getCurrentValue() <= 0.0f)
    {
        currentSemitones = pendingSemitones.load();
        windowSamples = (float) (windowSecondsFor (currentSemitones) * preparedRate);
        phase = 0.0f;

        changing = false;
        intervalFade.setTargetValue (1.0f);
    }

    // At unity the shifter would still hold the signal a window behind for no reason, so the
    // whole thing steps aside. This is also what makes the control's centre position honest.
    const auto action = bypass.beginBlock (bypassed || currentSemitones == 0, samples, numSamples);

    if (action == BypassCrossfade::Action::skip)
    {
        // Keep the line fed, and its read pointer moving with its write pointer, so that engaging
        // it does not start from a window of silence or from two pointers that have drifted apart.
        for (int i = 0; i < numSamples; ++i)
        {
            line.pushSample (0, samples[i]);
            line.popSample (0, 1.0f, true);
        }

        return;
    }

    // out(t) = in(t - D(t)), so the frequency multiplier is 1 - D'(t): to raise the pitch the
    // delay has to shrink, and to lower it, grow.
    const auto ratio = std::pow (2.0f, (float) currentSemitones / 12.0f);
    phaseStep = (1.0f - ratio) / windowSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        line.pushSample (0, samples[i]);

        auto sum = 0.0f;

        // Two taps half a window apart. Their raised cosines add to one, so a signal that is not
        // being shifted comes back unchanged and there is no hole where a tap runs out of room.
        //
        // Only the second read advances the line's own pointer, and it has to: JUCE's DelayLine
        // measures a delay from a read position that moves only when it is told to, so reading
        // twice without advancing once leaves the two pointers drifting a sample apart per sample.
        for (int tap = 0; tap < 2; ++tap)
        {
            auto position = phase + 0.5f * (float) tap;
            position -= std::floor (position);

            const auto gain = gainAt (position);
            const auto delay = juce::jlimit (1.0f, windowSamples, position * windowSamples);

            // The line's pointer is advanced by exactly one of the reads, whether or not that tap
            // is contributing anything.
            const auto read = line.popSample (0, delay, tap == 1);

            if (gain > 0.0f)
                sum += gain * read;
        }

        samples[i] = sum;

        phase += phaseStep;
        phase -= std::floor (phase);
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
