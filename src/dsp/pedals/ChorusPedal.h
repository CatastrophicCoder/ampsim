#pragma once

#include "../BypassCrossfade.h"

#include <juce_dsp/juce_dsp.h>

/** Modulation, after the amp — where a chorus goes in a real rig, so it modulates the distorted
    tone rather than being distorted itself. In front of a high-gain amp it turns to mush.
*/
class ChorusPedal
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        chorus.prepare (spec);
        chorus.setCentreDelay (8.0f);
        chorus.setFeedback (0.0f);      // a chorus, not a flanger

        bypass.prepare (sampleRate, maxBlockSize);
    }

    void reset()                    { chorus.reset(); }
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    void setParameters (float rateHz, float depth, float mix)
    {
        chorus.setRate (rateHz);
        chorus.setDepth (depth);
        chorus.setMix (mix);
    }

    void process (float* samples, int numSamples, bool bypassed)
    {
        const auto action = bypass.beginBlock (bypassed, samples, numSamples);

        // Bypassed, but still fed: see BypassCrossfade::scratchFor.
        auto* target = action == BypassCrossfade::Action::skip
                           ? bypass.scratchFor (samples, numSamples)
                           : samples;

        juce::dsp::AudioBlock<float> block (&target, 1, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        chorus.process (context);

        if (action == BypassCrossfade::Action::crossfade)
            bypass.finishBlock (samples, numSamples);
    }

private:
    juce::dsp::Chorus<float> chorus;
    BypassCrossfade bypass;
};
