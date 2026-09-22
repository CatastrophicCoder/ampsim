#pragma once

#include "../BypassCrossfade.h"

#include <juce_dsp/juce_dsp.h>

/** First in the chain, so it works on the raw guitar level — before a compressor or a drive
    pedal lifts the noise floor along with everything else.

    One control. Ratio, attack and release are fixed at values that suit a guitar: fast enough to
    stop a high-gain amp hissing between phrases, slow enough not to chop note tails.
*/
class NoiseGatePedal
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        gate.prepare (spec);
        gate.setRatio (10.0f);
        gate.setAttack (1.0f);
        gate.setRelease (120.0f);

        bypass.prepare (sampleRate, maxBlockSize);
    }

    void reset()                       { gate.reset(); }
    void snapBypass (bool bypassed)    { bypass.snap (bypassed); }

    void setThresholdDb (float db)     { gate.setThreshold (db); }

    void process (float* samples, int numSamples, bool bypassed)
    {
        const auto action = bypass.beginBlock (bypassed, samples, numSamples);

        if (action == BypassCrossfade::Action::skip)
            return;

        juce::dsp::AudioBlock<float> block (&samples, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        gate.process (context);

        if (action == BypassCrossfade::Action::crossfade)
            bypass.finishBlock (samples, numSamples);
    }

private:
    juce::dsp::NoiseGate<float> gate;
    BypassCrossfade bypass;
};
