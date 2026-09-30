/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "PluginProcessor.h"
#include "ui/AmpLookAndFeel.h"
#include "ui/CabinetRow.h"
#include "ui/PedalTile.h"
#include "ui/PresetRow.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** One control on the plate: engraved name above, the knob, its value below.

    The binding goes through an attachment, so the editor never writes to the processor directly.
*/
class LabelledKnob final : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  const juce::String& labelText);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Right-click behaviour, wired up by the editor. */
    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
    {
        slider.onContextMenu = std::move (handler);
    }

private:
    juce::String name;
    ParameterSlider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabelledKnob)
};

/** A loaded file, shown the way a chassis carries a stamped nameplate. */
class NameplateRow final : public juce::Component
{
public:
    NameplateRow (const juce::String& rowName, const juce::String& browseText);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setContents (const juce::String& text, bool isError);

    juce::TextButton browseButton;
    std::function<void()> onBrowse;

private:
    juce::String name, contents { "—" };
    bool showingError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NameplateRow)
};

class AmpSimAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit AmpSimAudioProcessorEditor (AmpSimAudioProcessor&);
    ~AmpSimAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintTuner (juce::Graphics&, juce::Rectangle<int>);
    void updateLoadedFileDisplay();
    void showParameterMenu (const juce::String& parameterID, juce::Component& source);
    void chooseFile (const juce::String& title, const juce::File& startingFile,
                     const juce::String& pattern, std::function<void (const juce::File&)> onChosen);

    AmpSimAudioProcessor& processorRef;
    AmpLookAndFeel lookAndFeel;

    // Gain, the three tone bands, then Master — the order they sit in the chain and on the panel.
    LabelledKnob gainKnob, bassKnob, midKnob, trebleKnob, masterKnob;

    juce::ToggleButton bypassButton { "bypassed" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    juce::ToggleButton tunerButton { "tuner" };
    juce::AudioProcessorValueTreeState::ButtonAttachment tunerAttachment;

    // Two rows, so the panel says which side of the amp each pedal is on — which is the whole
    // design of the pedal section, not a detail of it.
    PedalTile gatePedal, compressorPedal, drivePedal;
    PedalTile chorusPedal, delayPedal, reverbPedal;

    PresetRow presetRow;

    NameplateRow ampRow { "amp", "Load model" };

    // The cab has its own row on the deck now that it has four corners and two controls.
    CabinetRow cabinetRow;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessorEditor)
};
