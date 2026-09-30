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

/** The logo plate on the grille, which is also how a model is loaded.

    An amp wears its name on the front, and on this amp the name *is* the file — so the plate is
    the control rather than having a button next to it saying the same thing twice.
*/
class ModelBadge final : public juce::Button
{
public:
    ModelBadge();

    void setContents (const juce::String& text, bool isError);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::String contents;
    bool showingError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelBadge)
};

/** The power switch: a rocker and its pilot lamp, at the end of the control plate.

    It mutes rather than bypasses. An amp that is switched off makes no sound; it does not pass
    your guitar through to the speaker, which is what the panel's `bypassed` switch is for.
*/
class PowerSwitch final : public juce::Button
{
public:
    PowerSwitch();

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerSwitch)
};

/** The amp, drawn as a head: a covered box with a grille and its name on it, and a control plate
    across the bottom with the five knobs and the power switch mounted on it.

    Proportioned like the real thing — wide and shallow, roughly two and a half to one — because
    that is most of what makes a drawn box read as an amplifier rather than as a panel with a
    border around it.
*/
class AmpPage final : public juce::Component
{
public:
    explicit AmpPage (AmpSimAudioProcessor&);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    /** Called when the model finishes loading, or fails to. */
    void updateContents();

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)>);

private:
    juce::Rectangle<int> head() const;
    juce::Rectangle<int> grille() const;
    juce::Rectangle<int> plate() const;

    void paintShell (juce::Graphics&) const;
    void paintGrille (juce::Graphics&) const;
    void paintPlate (juce::Graphics&) const;

    AmpSimAudioProcessor& processorRef;

    /** What the head was last drawn as, so hovering the switch does not repaint it. */
    bool lit = true;

    ModelBadge badge;
    PowerSwitch powerSwitch;
    juce::AudioProcessorValueTreeState::ButtonAttachment powerAttachment;

    // Gain, the three tone bands, then Master — the order they sit in the chain.
    AmpKnob gainKnob, bassKnob, midKnob, trebleKnob, masterKnob;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpPage)
};
