#pragma once

#include "dsp/AmpModel.h"

#include <juce_events/juce_events.h>
#include <functional>

/** Loads `.nam` files off the audio thread and hands them to an AmpModel.

    Parsing a model and calling Reset() on it allocates and prewarms — tens of milliseconds for a
    standard WaveNet — so none of it may happen in processBlock. Everything expensive is done here,
    on a background thread, and only a finished, ready-to-run model reaches the audio thread.
*/
class ModelLoader final : private juce::Thread
{
public:
    struct Result
    {
        bool succeeded = false;
        juce::File file;
        juce::String message;       // empty on success
    };

    explicit ModelLoader (AmpModel& modelToLoadInto);
    ~ModelLoader() override;

    /** Queues a file. A load already in flight is superseded rather than queued behind. */
    void loadAsync (const juce::File& file);

    /** Called on the message thread when a load finishes, successfully or not. */
    std::function<void (Result)> onFinished;

    /** Any thread. The last load's error message, empty if it succeeded or none has run yet.
        Kept here rather than behind the callback so that state is readable without waiting for
        the message thread to get round to it. */
    juce::String getLastError() const;

private:
    void run() override;

    AmpModel& ampModel;

    juce::CriticalSection requestLock;
    juce::File requestedFile;       // guarded by requestLock

    mutable juce::CriticalSection resultLock;
    juce::String lastError;         // guarded by resultLock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelLoader)
};
