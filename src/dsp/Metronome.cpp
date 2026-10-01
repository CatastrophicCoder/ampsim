/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "Metronome.h"

namespace
{
    /** Each sound as a pitch, how long it takes to die away, and how much noise is in it. The
        accented beat is the same sound a fifth higher, which is how a metronome has always marked
        the bar — a different pitch rather than a different sound. */
    struct Voice { double frequency; double decaySeconds; float noise; };

    Voice voiceFor (Metronome::Sound sound, bool accented)
    {
        const auto lift = accented ? 1.5 : 1.0;

        switch (sound)
        {
            case Metronome::Sound::beep:  return { 1000.0 * lift, 0.045, 0.0f };
            case Metronome::Sound::wood:  return { 820.0 * lift, 0.014, 0.25f };
            case Metronome::Sound::click: return { 2400.0 * lift, 0.005, 0.8f };
        }

        return { 1000.0, 0.03, 0.0f };
    }
}

void Metronome::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);

    preparedRate = sampleRate;

    level.reset (sampleRate, 0.02);
    level.setCurrentAndTargetValue (level.getTargetValue());

    reset();
}

void Metronome::reset()
{
    envelope = 0.0f;
    phase = 0.0;
    freeRunningBeats = 0.0;
    lastBeat = -1;
}

void Metronome::setParameters (float tempoBpm, int beats, Sound newSound, float levelDb)
{
    tempo = juce::jlimit (slowestTempo, fastestTempo, tempoBpm);
    beatsPerBar = juce::jmax (1, beats);
    sound = newSound;
    level.setTargetValue (juce::Decibels::decibelsToGain (levelDb, -60.0f));
}

void Metronome::startBeat (int indexInBar)
{
    const auto voice = voiceFor (sound, indexInBar == 0);

    phase = 0.0;
    phaseStep = juce::MathConstants<double>::twoPi * voice.frequency / preparedRate;
    envelope = indexInBar == 0 ? 1.0f : unaccentedGain;
    noiseAmount = voice.noise;

    // An exponential fall, as a per-sample multiplier: the figure is how long it takes to become
    // inaudible rather than how long it takes to reach zero, which it never does.
    decay = (float) std::exp (-1.0 / (voice.decaySeconds * preparedRate) * 7.0);
}

float Metronome::nextClickSample()
{
    if (envelope < 1.0e-4f)
        return 0.0f;

    const auto tone = (float) std::sin (phase);
    const auto grit = noiseAmount * (noise.nextFloat() * 2.0f - 1.0f);

    phase += phaseStep;
    const auto value = envelope * ((1.0f - noiseAmount) * tone + grit);
    envelope *= decay;

    return value;
}

void Metronome::addTo (juce::AudioBuffer<float>& buffer, int numSamples, const double* hostQuarterNotes)
{
    const auto beatsPerSample = (double) tempo / 60.0 / preparedRate;

    for (int i = 0; i < numSamples; ++i)
    {
        // The host's grid while it is running, so the clicks land on its bar lines rather than
        // near them; a count of our own when there is nothing to follow.
        const auto beats = hostQuarterNotes != nullptr ? *hostQuarterNotes + (double) i * beatsPerSample
                                                       : freeRunningBeats;
        const auto beat = (int) std::floor (beats);

        if (beat != lastBeat && beats >= 0.0)
        {
            lastBeat = beat;
            startBeat (((beat % beatsPerBar) + beatsPerBar) % beatsPerBar);
        }

        if (hostQuarterNotes == nullptr)
            freeRunningBeats += beatsPerSample;

        const auto click = nextClickSample() * level.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, i, click);
    }
}
