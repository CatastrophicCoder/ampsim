/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "AmpLookAndFeel.h"
#include "../PresetManager.h"

/** The preset field: which one is loaded, a step either way, and a menu to load, save or delete.

    It lives in the persistent bar rather than on a page, because a preset is a saved state of the
    whole plugin — it belongs to all three pages at once.
*/
class PresetRow final : public juce::Component
{
public:
    explicit PresetRow (PresetManager&);

    void resized() override;

    void updateContents();

private:
    /** The name, and the whole field is the menu. A separate "Presets" button would be a second
        thing to aim at for the same job. */
    class NameButton final : public juce::Button
    {
    public:
        NameButton() : juce::Button ("preset") {}

        void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

        juce::String displayed { "no preset" };
        bool showingError = false;
    };

    void showMenu();
    void askForNameAndSave();
    void report (const juce::String& error);

    PresetManager& presets;

    juce::TextButton previousButton { juce::String::charToString ((juce::juce_wchar) 0x2039) },
                     nextButton     { juce::String::charToString ((juce::juce_wchar) 0x203a) };
    NameButton nameButton;

    std::unique_ptr<juce::AlertWindow> nameWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetRow)
};
