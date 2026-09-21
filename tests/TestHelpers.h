#pragma once

#include "PluginProcessor.h"

namespace test
{
    inline constexpr double sampleRate = 48000.0;
    inline constexpr int    blockSize  = 512;

    /** Sets a parameter by its real-world value, the way a host or the editor would.

        Note the parameter ranges snap to their step (0.1 dB for the gains), so asking for
        -3.25 dB stores -3.2. Tests should expect the snapped value.
    */
    inline void setParam (juce::AudioProcessorValueTreeState& state,
                          const juce::String& parameterID,
                          float value)
    {
        auto* p = state.getParameter (parameterID);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    inline float getParam (juce::AudioProcessorValueTreeState& state,
                           const juce::String& parameterID)
    {
        auto* p = state.getParameter (parameterID);
        jassert (p != nullptr);
        return p->convertFrom0to1 (p->getValue());
    }

    /** Fills every channel with `level` and processes `numBlocks` blocks of DC.

        DC is the useful signal here: with only gain in the chain the output should be flat, so
        any deviation is the smoothing, and any sample-to-sample jump is a click.

        @returns the peak magnitude of the final block.
    */
    inline float runConstant (AmpSimAudioProcessor& processor,
                              int numBlocks,
                              float level = 0.5f)
    {
        juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), blockSize);
        juce::MidiBuffer midi;
        float peak = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), level, blockSize);

            processor.processBlock (buffer, midi);
            peak = buffer.getMagnitude (0, blockSize);
        }

        return peak;
    }

    /** A processor already prepared at the test sample rate and block size. */
    inline std::unique_ptr<AmpSimAudioProcessor> makePreparedProcessor()
    {
        auto processor = std::make_unique<AmpSimAudioProcessor>();
        processor->prepareToPlay (sampleRate, blockSize);
        return processor;
    }

    /** Blocks needed for a ramp of `seconds` to finish, rounded up, plus one for slack. */
    inline int blocksForRamp (double seconds)
    {
        return (int) std::ceil (seconds * sampleRate / blockSize) + 1;
    }
}
