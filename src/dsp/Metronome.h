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

/** A click to play against, added at the very end of the chain.

    It is a practice tool rather than part of the amp, so nothing upstream touches it: the power
    switch, the tuner's mute and the plugin's own bypass all leave it clicking. Switching it on is
    a deliberate act and it keeps going until you switch it off, which is the only behaviour that
    does not surprise someone using it.

    **It is silent in an offline render.** A click printed into a bounce is the one way this could
    do real damage, and `AudioProcessor::isNonRealtime()` says exactly when that is happening.

    Timing comes from the host while its transport is running, so the clicks land on its bar lines
    rather than near them. With the transport stopped, or in the standalone where there is no
    transport at all, it free-runs at whatever tempo it has been given.
*/
class Metronome
{
public:
    /** What the click is made of. All three are a tone with a fast decay; what separates them is
        how fast, how high, and how much noise is in the attack. */
    enum class Sound { beep, wood, click };

    /** One of the bars on offer, and what a beat of it is worth in quarter notes.

        **The tempo is always the quarter note**, as it is in every host, so the bar decides how
        fast the clicks come rather than the other way round: an /8 bar clicks eighths and so
        clicks twice as often at the same BPM, and cut time clicks halves and so clicks half as
        often. Sharing the host's unit is what lets the free-running count and the host's grid mean
        the same thing.
    */
    struct TimeSignature
    {
        const char* name;
        int beatsPerBar;
        double quarterNotesPerBeat;
    };

    /** The table, which is the one place the list lives. The parameter's choices are built from
        it, so a name and a bar cannot drift apart the way a hand-written choice list would.

        **Append to it, never reorder it.** A saved session stores the choice as an index. */
    static int numTimeSignatures();
    static const TimeSignature& timeSignature (int index);
    static juce::StringArray timeSignatureNames();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Audio thread. @param timeSignatureIndex an index into the table above. */
    void setParameters (float tempoBpm, int timeSignatureIndex, Sound sound, float levelDb);

    /** Audio thread. Adds the click to every channel.

        @param hostQuarterNotes  the host's position in quarter notes while its transport is
                                 running, or nullptr when there is none to follow.
    */
    void addTo (juce::AudioBuffer<float>& buffer, int numSamples, const double* hostQuarterNotes);

    /** The range the tempo control covers when there is no host to follow. */
    static constexpr float slowestTempo = 40.0f;
    static constexpr float fastestTempo = 240.0f;

private:
    void startBeat (int indexInBar);
    float nextClickSample();

    double preparedRate = 48000.0;

    float tempo = 120.0f;
    int beatsPerBar = 4;
    double quarterNotesPerBeat = 1.0;
    Sound sound = Sound::wood;
    juce::SmoothedValue<float> level;

    // Where the click is in its own short life, and what it sounds like.
    double phase = 0.0, phaseStep = 0.0;
    float envelope = 0.0f, decay = 0.0f, noiseAmount = 0.0f;

    /** How much quieter an unaccented beat is than the one that marks the bar. A metronome marks
        it with both pitch and weight; either alone is easy to lose behind a guitar. */
    static constexpr float unaccentedGain = 0.7f;

    // Counted in quarter notes rather than in beats, so it is the same quantity the host supplies
    // and changing the bar mid-count does not move the position.
    double freeRunningQuarterNotes = 0.0;
    int lastBeat = -1;

    juce::Random noise;
};
