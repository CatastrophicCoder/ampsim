#pragma once

#include <juce_dsp/juce_dsp.h>

/** Bass, Mid and Treble as three independent parametric bands.

    Chosen over a modelled passive stack: each control does one thing, centred is flat, and every
    band's response can be asserted directly in a test. What it gives up is the interaction of a
    real passive network — where the controls load each other and all-at-noon is mid-scooped rather
    than flat — so this colours the model's output rather than behaving like an amp's own stack.
    That trade is recorded in ampsim_plan.md.

    The bands sit after the model and before the cab, which is where a real amp's stack sits
    relative to the speaker.
*/
class ToneStack
{
public:
    ToneStack() = default;

    /** Band centres, chosen for guitar: below the low E's fundamental region, in the middle of the
        range a speaker emphasises, and above the cab's roll-off. */
    static constexpr float bassFrequency   = 100.0f;
    static constexpr float midFrequency    = 800.0f;
    static constexpr float trebleFrequency = 3200.0f;
    static constexpr float midQ            = 0.7f;
    static constexpr float shelfQ          = 0.7f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Audio thread. Sets the targets the smoothing ramps towards. */
    void setBandGains (float bassDb, float midDb, float trebleDb);

    /** Message thread. Jumps the bands to their targets instead of ramping, so prepareToPlay
        does not sweep the EQ in from flat on every playback start. */
    void snapToTargets();

    /** Audio thread. Processes one mono block in place. */
    void process (float* samples, int numSamples);

private:
    /** Coefficients are recomputed this often, rather than once per block: a block can be 20 ms
        long, and stepping a 24 dB swing in that few jumps is audible. */
    static constexpr int updateInterval = 32;

    void updateCoefficients();

    juce::dsp::IIR::Filter<float> bassFilter, midFilter, trebleFilter;
    juce::SmoothedValue<float> bassDb, midDb, trebleDb;

    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneStack)
};
