/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "AmpKnob.h"
#include "../PluginProcessor.h"

/** One corner of the mic-position grid: what position it is, and what is loaded there. */
class CabinetSlotButton final : public juce::Button
{
public:
    explicit CabinetSlotButton (const juce::String& positionName);

    void setContents (const juce::String& text, bool loaded);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::String position, contents { "empty" };
    bool isLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabinetSlotButton)
};

/** The cabinet: four corners of a mic position, drawn as the grid they actually are, and the two
    controls that move between them.

    Left to right is on axis to off axis, top to bottom is close to far, so the axis and distance
    knobs move the way the grid is laid out. With one corner filled it reads as a plain IR loader,
    which is what it then is.
*/
class CabPage final : public juce::Component
{
public:
    explicit CabPage (AmpSimAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void updateContents();

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
    {
        axisKnob.setContextMenuHandler (handler);
        distanceKnob.setContextMenuHandler (std::move (handler));
    }

private:
    void showMenuFor (CabSim::Slot);
    juce::Rectangle<int> content() const;

    AmpSimAudioProcessor& processorRef;

    juce::OwnedArray<CabinetSlotButton> slots;
    AmpKnob axisKnob, distanceKnob;

    juce::ToggleButton bypassButton { "cab bypassed" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabPage)
};
