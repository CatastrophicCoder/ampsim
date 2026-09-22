#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

/** Maps MIDI continuous controllers onto parameters, one CC to one parameter.

    Two thread rules shape this:

      - the audio thread reads the map through a plain array of atomics, never a dictionary
        lookup and never a lock;
      - the audio thread never writes the map. Learning a CC sets an atomic, and the message
        thread commits it to the APVTS tree, because a ValueTree may only be touched from one
        thread and the state is saved from the message thread.

    The mapping lives in the state, so it is saved with the session and travels with a preset.
*/
class MidiLearn
{
public:
    static constexpr int numControllers = 128;

    explicit MidiLearn (juce::AudioProcessorValueTreeState&);

    /** Audio thread. Applies mapped controllers, and captures one while learning. */
    void processMidi (const juce::MidiBuffer&);

    /** Message thread. The next controller that arrives is mapped to this parameter. */
    void startLearning (const juce::String& parameterID);
    void stopLearning();

    bool isLearning() const                  { return learningIndex.load() >= 0; }
    juce::String getLearningParameter() const;

    /** Message thread. Commits a controller captured by the audio thread, if there is one.
        @returns true if the map changed. */
    bool commitPendingLearn();

    /** The controller mapped to a parameter, or -1. */
    int getControllerFor (const juce::String& parameterID) const;

    void clearMapping (const juce::String& parameterID);

    /** Message thread. Rebuilds the lookup from the state — call after loading a session. */
    void rebuildFromState();

    /** Called on the message thread when the map changes. */
    std::function<void()> onMappingChanged;

private:
    int indexOf (const juce::String& parameterID) const;
    juce::ValueTree mapTree();

    juce::AudioProcessorValueTreeState& state;
    juce::Array<juce::RangedAudioParameter*> parameters;

    // -1 means "nothing mapped". Read on the audio thread, written on the message thread.
    std::array<std::atomic<int>, numControllers> controllerToParameter;

    std::atomic<int> learningIndex { -1 };
    std::atomic<int> pendingController { -1 };
};
