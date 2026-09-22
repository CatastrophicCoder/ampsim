#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AmpSimAudioProcessor;

/** Named presets, saved as files on disk.

    A preset is the whole state: every parameter, the MIDI map, and the paths of the model and the
    cabinet IRs. Paths are the awkward part, because they only mean anything on the machine that
    saved them — so **a preset with no path for something leaves what is already loaded alone**.
    That is what makes a factory preset useful: it sets the knobs and keeps your amp.
*/
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".ampsim";

    /** @param directory where presets live; the default is the user's application data. Tests
                         pass a temporary directory rather than writing into it. */
    PresetManager (AmpSimAudioProcessor&, juce::File directory = {});

    juce::File getDirectory() const { return directory; }

    /** Names of every preset on disk, in the order they should be stepped through. */
    juce::StringArray getNames() const;

    /** @returns an error message, empty on success. */
    juce::String save (const juce::String& name);
    juce::String load (const juce::String& name);
    juce::String remove (const juce::String& name);

    /** Steps to the next or previous preset, wrapping. Does nothing if there are none. */
    void step (int delta);

    juce::String getCurrentName() const;

    /** Writes the built-in presets if they are not there. Each sets the controls only, so
        loading one keeps whatever model and cab are loaded. */
    void createFactoryPresetsIfMissing();

    /** Called on the message thread when the list or the current preset changes. */
    std::function<void()> onChanged;

private:
    juce::File fileFor (const juce::String& name) const;

    AmpSimAudioProcessor& processor;
    juce::File directory;
};
