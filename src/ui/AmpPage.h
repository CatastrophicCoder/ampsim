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

/** The amp: which capture is loaded, and the five controls on its front panel.

    The model sits in a nameplate across the top rather than in a dropdown, because it is the one
    thing on this page that is a file — everything under it is a knob, and the difference should be
    visible before it is read.
*/
class AmpPage final : public juce::Component
{
public:
    explicit AmpPage (AmpSimAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called when the model finishes loading, or fails to. */
    void updateContents();

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)>);

private:
    juce::Rectangle<int> head() const;
    juce::Rectangle<int> plate() const;
    juce::Rectangle<int> knobStrip() const;

    AmpSimAudioProcessor& processorRef;

    juce::String modelName;
    bool showingError = false;

    juce::TextButton loadButton { "Load model" };

    // Gain, the three tone bands, then Master — the order they sit in the chain.
    AmpKnob gainKnob, bassKnob, midKnob, trebleKnob, masterKnob;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpPage)
};
