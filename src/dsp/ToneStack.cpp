#include "ToneStack.h"

namespace
{
    constexpr double rampSeconds = 0.05;
}

void ToneStack::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

    for (auto* filter : { &bassFilter, &midFilter, &trebleFilter })
        filter->prepare (spec);

    for (auto* smoothed : { &bassDb, &midDb, &trebleDb })
        smoothed->reset (sampleRate, rampSeconds);

    // Also warms each Coefficients object's storage, so the updates in process() cannot allocate.
    updateCoefficients();
}

void ToneStack::reset()
{
    for (auto* filter : { &bassFilter, &midFilter, &trebleFilter })
        filter->reset();
}

void ToneStack::setBandGains (float bass, float mid, float treble)
{
    bassDb  .setTargetValue (bass);
    midDb   .setTargetValue (mid);
    trebleDb.setTargetValue (treble);
}

void ToneStack::snapToTargets()
{
    for (auto* smoothed : { &bassDb, &midDb, &trebleDb })
        smoothed->setCurrentAndTargetValue (smoothed->getTargetValue());

    updateCoefficients();
}

void ToneStack::updateCoefficients()
{
    // ArrayCoefficients returns a plain std::array, where the Coefficients::makeX factories each
    // allocate a new object. Assigning the array only rewrites the five normalised values in
    // storage that prepare() has already sized, so no allocation happens on the audio thread.
    using Array = juce::dsp::IIR::ArrayCoefficients<float>;

    const auto linear = [] (float db) { return juce::Decibels::decibelsToGain (db); };

    *bassFilter.coefficients = Array::makeLowShelf (sampleRate, bassFrequency, shelfQ,
                                                    linear (bassDb.getCurrentValue()));

    *midFilter.coefficients = Array::makePeakFilter (sampleRate, midFrequency, midQ,
                                                     linear (midDb.getCurrentValue()));

    *trebleFilter.coefficients = Array::makeHighShelf (sampleRate, trebleFrequency, shelfQ,
                                                       linear (trebleDb.getCurrentValue()));
}

void ToneStack::process (float* samples, int numSamples)
{
    for (int start = 0; start < numSamples; start += updateInterval)
    {
        const auto count = juce::jmin (updateInterval, numSamples - start);

        if (bassDb.isSmoothing() || midDb.isSmoothing() || trebleDb.isSmoothing())
        {
            bassDb  .skip (count);
            midDb   .skip (count);
            trebleDb.skip (count);

            updateCoefficients();
        }

        for (int i = start; i < start + count; ++i)
        {
            auto sample = bassFilter.processSample (samples[i]);
            sample = midFilter.processSample (sample);
            samples[i] = trebleFilter.processSample (sample);
        }
    }
}
