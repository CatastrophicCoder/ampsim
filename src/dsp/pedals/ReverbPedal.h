#pragma once

#include "../BypassCrossfade.h"

#include <juce_dsp/juce_dsp.h>

/** Last in the chain before the cab, so the tail runs through the speaker response the way it
    would coming out of a real cabinet.
*/
class ReverbPedal
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        reverb.prepare (spec);
        bypass.prepare (sampleRate, maxBlockSize);
    }

    void reset()                    { reverb.reset(); }
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    void setParameters (float size, float mix)
    {
        juce::dsp::Reverb::Parameters parameters;
        parameters.roomSize = juce::jlimit (0.0f, 1.0f, size);
        parameters.damping = 0.45f;
        parameters.width = 1.0f;
        parameters.wetLevel = juce::jlimit (0.0f, 1.0f, mix);
        parameters.dryLevel = 1.0f - parameters.wetLevel * 0.5f;

        reverb.setParameters (parameters);
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
        reverb.process (context);

        if (action == BypassCrossfade::Action::crossfade)
            bypass.finishBlock (samples, numSamples);
    }

private:
    juce::dsp::Reverb reverb;
    BypassCrossfade bypass;
};
