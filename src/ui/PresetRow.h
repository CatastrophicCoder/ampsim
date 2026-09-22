#pragma once

#include "AmpLookAndFeel.h"
#include "../PresetManager.h"

/** The preset bar: which preset is loaded, a step either way, and a menu to load, save or delete.

    It sits with the amp's nameplate rather than in the header, because a preset is a saved state
    of the whole plugin — the same kind of thing as "which model is loaded", not a switch.
*/
class PresetRow final : public juce::Component
{
public:
    explicit PresetRow (PresetManager&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void updateContents();

private:
    void showMenu();
    void askForNameAndSave();
    void report (const juce::String& error);

    PresetManager& presets;

    juce::TextButton previousButton { "<" }, nextButton { ">" }, menuButton { "Presets" };
    juce::String displayed { "no preset" }, message;

    std::unique_ptr<juce::AlertWindow> nameWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetRow)
};
