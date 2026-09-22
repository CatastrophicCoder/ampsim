#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

/** Pitch detection for the tuner display.

    The audio thread only copies samples into a FIFO; the analysis — a YIN difference function
    over a 2048-sample window — runs on the message thread from the editor's timer. Pitch
    detection is far too expensive to do in processBlock, and nothing about a tuner needs to be
    sample-accurate.

    It taps the signal before the pedals and the amp, so what it reads is the guitar, not whatever
    the drive pedal and the model have made of it.
*/
class Tuner
{
public:
    struct Reading
    {
        bool valid = false;
        float frequencyHz = 0.0f;
        int midiNote = 0;        // 69 = A440
        float cents = 0.0f;      // how far off that note, -50 to +50
    };

    static constexpr int windowSize = 2048;

    /** The range a guitar needs: a detuned low B up to well past the 24th fret's high E. */
    static constexpr float lowestFrequency = 55.0f;
    static constexpr float highestFrequency = 1400.0f;

    void prepare (double sampleRate);
    void reset();

    /** Audio thread. Never blocks; the oldest samples are dropped if analysis falls behind. */
    void pushSamples (const float* samples, int numSamples);

    /** Message thread. Reads whatever has arrived and updates the published reading. */
    void analyse();

    Reading getReading() const;

    /** The note name for a MIDI note number, as a tuner would print it. */
    static juce::String noteName (int midiNote);

private:
    double sampleRate = 48000.0;

    juce::AbstractFifo fifo { windowSize * 4 };
    std::vector<float> ring = std::vector<float> ((size_t) windowSize * 4, 0.0f);

    // The analysis window, held between calls so a short block does not restart the search.
    std::vector<float> window = std::vector<float> ((size_t) windowSize, 0.0f);
    std::vector<float> difference;

    std::atomic<bool> readingValid { false };
    std::atomic<float> readingFrequency { 0.0f };
    std::atomic<int> readingNote { 0 };
    std::atomic<float> readingCents { 0.0f };
};
