/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "pedals/CompressorPedal.h"
#include "pedals/DelayPedal.h"
#include "pedals/DirtPedal.h"
#include "pedals/ModulationPedal.h"
#include "pedals/NoiseGatePedal.h"
#include "pedals/ReverbPedal.h"

/** The pedalboard, in two fixed groups either side of the amp.

    The placement is the point, and it is not user-reorderable. Gain-stage pedals go in front of
    the amp because an overdrive works by changing what the preamp distorts; modulation and time
    effects go after it, so the repeats and modulated copies are of the already-distorted tone.
    Running them the other way round is what a beginner's pedalboard sounds like.

    `processBeforeAmp` and `processAfterAmp` are separate calls rather than one list, so the amp
    physically cannot end up on the wrong side of a pedal.
*/
class PedalChain
{
public:
    /** Every pedal's state in one object, refreshed per block from the parameters. A single
        struct keeps the processor's per-block update to one call instead of eighteen. */
    struct Settings
    {
        bool gateEngaged = false;
        float gateThresholdDb = -60.0f;

        bool compressorEngaged = false;
        float compressorAmount = 0.4f, compressorLevelDb = 0.0f;

        // The dirt slot: which pedal is in it, and the settings of whichever that is. The
        // processor picks the right knobs for the type, so this stays four numbers rather than
        // one set per pedal.
        bool driveEngaged = false;
        DirtPedal::Type dirtType = DirtPedal::Type::distortion;
        float driveAmount = 0.5f, driveTone = 0.5f, driveLevelDb = 0.0f;

        // Likewise the modulation slot, where the third and fourth numbers mean different things
        // to different pedals. ModulationPedal::setParameters says which.
        bool chorusEngaged = false;
        ModulationPedal::Type modulationType = ModulationPedal::Type::chorus;
        float chorusRateHz = 1.2f, chorusDepth = 0.35f;
        float modulationFeedback = 0.0f, chorusMix = 0.4f;

        bool delayEngaged = false;
        float delayTimeSeconds = 0.35f, delayFeedback = 0.35f, delayMix = 0.3f;

        bool reverbEngaged = false;
        float reverbSize = 0.5f, reverbMix = 0.25f;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Audio thread. Cheap: setters only. */
    void setSettings (const Settings&);

    /** Message thread, from prepareToPlay: start at the current settings rather than ramping
        into them on every playback start. */
    void snapToSettings();

    void processBeforeAmp (float* samples, int numSamples);
    void processAfterAmp (float* samples, int numSamples);

    /** The dirt slot's oversampler, which runs whatever is in it and whether or not it is engaged,
        so that this number never changes under the host. */
    int getLatencySamples() const;

private:
    NoiseGatePedal gate;
    CompressorPedal compressor;
    DirtPedal dirt;

    ModulationPedal modulation;
    DelayPedal delay;
    ReverbPedal reverb;

    Settings settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalChain)

public:
    PedalChain() = default;
};
