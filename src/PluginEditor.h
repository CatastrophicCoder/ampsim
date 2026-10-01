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
#include "ui/AmpPage.h"
#include "ui/CabPage.h"
#include "ui/PedalsPage.h"
#include "ui/ParameterSlider.h"
#include "ui/PresetRow.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** One of the three section tabs. Drawn rather than configured: a tab is a word with a lit bar
    under it, and none of JUCE's button styles is that. */
class TabButton final : public juce::Button
{
public:
    explicit TabButton (const juce::String& text);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabButton)
};

/** The panel: a persistent bar, a tab bar, and one of three pages.

    Everything is laid out in fixed logical points inside one child component, and that child
    carries the scale as an AffineTransform. The layout code therefore never has to know what size
    the window is, a vector panel needs no second set of assets to be resizable, and the editor's
    own setScaleFactor is left alone for the host to use for display DPI.
*/
class AmpSimAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    /** The size everything is laid out at, before the scale factor is applied. */
    static constexpr int panelWidth = 780;
    static constexpr int panelHeight = 506;

    explicit AmpSimAudioProcessorEditor (AmpSimAudioProcessor&);
    ~AmpSimAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Everything, at its logical size. The editor holds nothing but this and the scale. */
    struct Panel final : juce::Component
    {
        explicit Panel (AmpSimAudioProcessorEditor& o) : owner (o) {}

        void paint (juce::Graphics& g) override  { owner.paintPanel (g); }
        void resized() override                  { owner.layOutPanel(); }

        AmpSimAudioProcessorEditor& owner;
    };

    /** A text button that reads a choice parameter and offers its choices in a menu.

        The panel draws everything on it, so a juce::ComboBox would be the one object on the
        window that came out of the box. This is the scale button's shape, pointed at a parameter.
    */
    class ChoiceButton final : public juce::TextButton
    {
    public:
        ChoiceButton (juce::AudioProcessorValueTreeState&, const juce::String& parameterID);

    private:
        void showMenu();

        juce::AudioParameterChoice& parameter;
        juce::ParameterAttachment attachment;
    };

    void paintPanel (juce::Graphics&);
    void layOutPanel();

    void timerCallback() override;
    void paintTuner (juce::Graphics&, juce::Rectangle<int>);
    void updateLoadedFileDisplay();
    void showParameterMenu (const juce::String& parameterID, juce::Component& source);
    void showPage (int index);
    void showScaleMenu();
    void applyScale (int percent);

    AmpSimAudioProcessor& processorRef;
    AmpLookAndFeel lookAndFeel;

    Panel panel { *this };

    PresetRow presetRow;

    juce::ToggleButton tunerButton { "tuner" };
    juce::AudioProcessorValueTreeState::ButtonAttachment tunerAttachment;

    juce::ToggleButton bypassButton { "bypassed" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    juce::TextButton scaleButton;

    // The bottom bar: what you play *against* rather than what you play through. The scale button
    // lives down here too — it is housekeeping, and it was the one thing in the top bar nobody
    // touches twice in a session.
    juce::ToggleButton transposeButton { "transpose" };
    juce::AudioProcessorValueTreeState::ButtonAttachment transposeAttachment;

    ParameterSlider semitonesSlider { ParamID::transposeSemitones };
    juce::AudioProcessorValueTreeState::SliderAttachment semitonesAttachment;

    juce::ToggleButton metronomeButton { "metronome" };
    juce::AudioProcessorValueTreeState::ButtonAttachment metronomeAttachment;

    ParameterSlider tempoSlider { ParamID::metronomeTempo };
    juce::AudioProcessorValueTreeState::SliderAttachment tempoAttachment;

    ChoiceButton beatsButton, soundButton;

    ParameterSlider metronomeLevelSlider { ParamID::metronomeLevel };
    juce::AudioProcessorValueTreeState::SliderAttachment metronomeLevelAttachment;

    TabButton ampTab { "AMP" }, pedalsTab { "PEDALS" }, cabTab { "CAB" };

    AmpPage ampPage;
    PedalsPage pedalsPage;
    CabPage cabPage;

    int currentPage = 0;
    int scalePercent = 100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessorEditor)
};
