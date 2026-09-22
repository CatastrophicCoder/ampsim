#pragma once

#include <juce_dsp/juce_dsp.h>

/** The cabinet: one impulse response, convolved with the signal.

    Deliberately the whole of it. A real 4x12 with a mic in front of it is a frequency response and
    a room, and an IR captures exactly that; multi-mic positioning and cab modelling are out of
    scope (see the Goal section of ampsim_plan.md).

    `juce::dsp::Convolution` does the hard parts already: it loads and resamples the file on its own
    background thread, and its default uniform-partitioned mode has no latency, so the plugin
    reports nothing extra for the cab.

    Bypass is a crossfade rather than a switch, because an IR changes the tone enough that cutting
    between them clicks.
*/
class CabSim
{
public:
    // Explicit because the non-copyable macro below declares a copy constructor, which suppresses
    // the implicit default one.
    CabSim() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Message thread. The file is read and resampled on the convolution's own thread, so the
        new IR becomes audible a moment later. The caller is expected to have checked the file. */
    void loadImpulseResponse (const juce::File& file);

    /** Audio thread. Processes one mono block in place. */
    void process (float* samples, int numSamples, bool bypassed);

    /** True once an IR has been handed over. Deliberately our own flag rather than
        `Convolution::getCurrentIRSize()`, which is already non-zero after prepare(): JUCE installs
        a default engine there, and running the signal through it is not a no-op. */
    bool hasImpulseResponse() const      { return irLoaded.load(); }
    int getLatencySamples() const        { return convolution.getLatency(); }

private:
    juce::dsp::Convolution convolution;
    std::atomic<bool> irLoaded { false };

    // 1 = fully bypassed, ramped so the switch cannot click.
    juce::SmoothedValue<float> bypassMix;
    juce::AudioBuffer<float> dryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabSim)
};
