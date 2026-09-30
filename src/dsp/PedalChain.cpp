/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PedalChain.h"

void PedalChain::prepare (double sampleRate, int maxBlockSize)
{
    gate.prepare (sampleRate, maxBlockSize);
    compressor.prepare (sampleRate, maxBlockSize);
    drive.prepare (sampleRate, maxBlockSize);

    chorus.prepare (sampleRate, maxBlockSize);
    delay.prepare (sampleRate, maxBlockSize);
    reverb.prepare (sampleRate, maxBlockSize);
}

void PedalChain::reset()
{
    gate.reset();
    compressor.reset();
    drive.reset();

    chorus.reset();
    delay.reset();
    reverb.reset();
}

void PedalChain::setSettings (const Settings& newSettings)
{
    settings = newSettings;

    gate.setThresholdDb (settings.gateThresholdDb);
    compressor.setParameters (settings.compressorAmount, settings.compressorLevelDb);
    drive.setParameters (settings.driveAmount, settings.driveTone, settings.driveLevelDb);

    chorus.setParameters (settings.chorusRateHz, settings.chorusDepth, settings.chorusMix);
    delay.setParameters (settings.delayTimeSeconds, settings.delayFeedback, settings.delayMix);
    reverb.setParameters (settings.reverbSize, settings.reverbMix);
}

void PedalChain::snapToSettings()
{
    gate.snapBypass (! settings.gateEngaged);

    compressor.snapBypass (! settings.compressorEngaged);
    compressor.snapParameters();

    drive.snapBypass (! settings.driveEngaged);
    drive.snapParameters();

    chorus.snapBypass (! settings.chorusEngaged);

    delay.snapBypass (! settings.delayEngaged);
    delay.snapParameters();

    reverb.snapBypass (! settings.reverbEngaged);
}

int PedalChain::getLatencySamples() const
{
    return drive.getLatencySamples();
}

void PedalChain::processBeforeAmp (float* samples, int numSamples)
{
    // The gate listens here, on the raw guitar, before anything lifts the noise floor with it —
    // and closes on the other side of the amp. See NoiseGatePedal.
    gate.measureKey (samples, numSamples);

    compressor.process (samples, numSamples, ! settings.compressorEngaged);
    drive.process (samples, numSamples, ! settings.driveEngaged);
}

void PedalChain::processAfterAmp (float* samples, int numSamples)
{
    // Before the time effects, where a rack gate goes: a tail already in the delay line should
    // ring out rather than being cut off with the note that fed it.
    gate.apply (samples, numSamples, ! settings.gateEngaged);

    chorus.process (samples, numSamples, ! settings.chorusEngaged);
    delay.process (samples, numSamples, ! settings.delayEngaged);
    reverb.process (samples, numSamples, ! settings.reverbEngaged);
}
