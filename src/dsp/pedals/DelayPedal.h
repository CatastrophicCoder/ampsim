#pragma once

#include "../BypassCrossfade.h"

#include <juce_dsp/juce_dsp.h>

/** An analogue-voiced delay: each repeat passes through a low-pass filter, so the echoes get
    darker as they fade instead of repeating a bright copy forever.
*/
class DelayPedal
{
public:
    static constexpr float maxDelaySeconds = 1.2f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void snapBypass (bool bypassed) { bypass.snap (bypassed); }

    /** Jump to the current settings instead of sweeping into them. Without this the delay time
        ramps up from zero on every playback start, and the first repeats land in the wrong place. */
    void snapParameters();

    /** @param timeSeconds delay time.  @param feedback 0–0.95.  @param mix 0–1 wet. */
    void setParameters (float timeSeconds, float feedback, float mix);

    void process (float* samples, int numSamples, bool bypassed);

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 1 };
    juce::dsp::IIR::Filter<float> repeatFilter;

    juce::SmoothedValue<float> delaySamples, feedbackAmount, wetMix;
    double sampleRate = 48000.0;

    BypassCrossfade bypass;
};
