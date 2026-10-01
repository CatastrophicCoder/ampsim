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

    /** Appended to, never reordered: a saved session stores the choice as an index.

        The first six are what the control offered when it was one number, so those sessions keep
        their meaning; cut time and the compound bars were added after, and 7/8 after them — which
        is why it sits on the end rather than beside its neighbours. Every one of them is a count of
        beats and a beat length, because a bar of 6/8 is not six of anything a bar of 6/4 is made of.
    */
    const Metronome::TimeSignature signatures[]
    {
        { "2/4",  2, 1.0 },
        { "3/4",  3, 1.0 },
        { "4/4",  4, 1.0 },
        { "5/4",  5, 1.0 },
        { "6/4",  6, 1.0 },
        { "7/4",  7, 1.0 },
        { "2/2",  2, 2.0 },
        { "3/8",  3, 0.5 },
        { "6/8",  6, 0.5 },
        { "9/8",  9, 0.5 },
        { "12/8", 12, 0.5 },
        { "7/8",  7, 0.5 }
    };

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

int Metronome::numTimeSignatures()
{
    return (int) std::size (signatures);
}

const Metronome::TimeSignature& Metronome::timeSignature (int index)
{
    return signatures[juce::jlimit (0, numTimeSignatures() - 1, index)];
}

juce::StringArray Metronome::timeSignatureNames()
{
    juce::StringArray names;

    for (const auto& s : signatures)
        names.add (s.name);

    return names;
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
    freeRunningQuarterNotes = 0.0;
    lastBeat = -1;
}

void Metronome::setParameters (float tempoBpm, int timeSignatureIndex, Sound newSound, float levelDb)
{
    const auto& signature = timeSignature (timeSignatureIndex);

    tempo = juce::jlimit (slowestTempo, fastestTempo, tempoBpm);
    beatsPerBar = signature.beatsPerBar;
    quarterNotesPerBeat = signature.quarterNotesPerBeat;
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
    // The tempo is the quarter note, which is the unit the host reports its position in, so the
    // two paths below count the same thing and the bar is the only thing that turns it into beats.
    const auto quarterNotesPerSample = (double) tempo / 60.0 / preparedRate;

    for (int i = 0; i < numSamples; ++i)
    {
        // The host's grid while it is running, so the clicks land on its bar lines rather than
        // near them; a count of our own when there is nothing to follow.
        const auto quarterNotes = hostQuarterNotes != nullptr
                                      ? *hostQuarterNotes + (double) i * quarterNotesPerSample
                                      : freeRunningQuarterNotes;

        const auto beats = quarterNotes / quarterNotesPerBeat;
        const auto beat = (int) std::floor (beats);

        if (beat != lastBeat && beats >= 0.0)
        {
            lastBeat = beat;
            startBeat (((beat % beatsPerBar) + beatsPerBar) % beatsPerBar);
        }

        if (hostQuarterNotes == nullptr)
            freeRunningQuarterNotes += quarterNotesPerSample;

        const auto click = nextClickSample() * level.getNextValue();

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addSample (ch, i, click);
    }
}
