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
    // Gate first, on the raw guitar level, before anything lifts the noise floor with it.
    gate.process (samples, numSamples, ! settings.gateEngaged);
    compressor.process (samples, numSamples, ! settings.compressorEngaged);
    drive.process (samples, numSamples, ! settings.driveEngaged);
}

void PedalChain::processAfterAmp (float* samples, int numSamples)
{
    chorus.process (samples, numSamples, ! settings.chorusEngaged);
    delay.process (samples, numSamples, ! settings.delayEngaged);
    reverb.process (samples, numSamples, ! settings.reverbEngaged);
}
