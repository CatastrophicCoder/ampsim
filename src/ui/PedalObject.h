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
#include "ParameterSlider.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

/** A knob on a pedal: small, with its name underneath and its value on a popup while dragged.

    No permanent read-out. Six pedals' worth of numbers would crowd the board, and a pedal is read
    by where its knobs point.
*/
class PedalKnob final : public juce::Component
{
public:
    PedalKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
               const juce::String& labelText);

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
    {
        slider.onContextMenu = std::move (handler);
    }

private:
    juce::String name;
    ParameterSlider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalKnob)
};

/** The footswitch: what a pedal is switched on and off by.

    A real one is the whole point of the object — you do not toggle a pedal with a tick box. It is
    a separate component so that only the switch is clickable, and the enclosure around it is not.
*/
class Footswitch final : public juce::Button
{
public:
    Footswitch();

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footswitch)
};

/** One slot on the board, drawn as a pedal: a coloured enclosure with a brushed face, its controls
    as knobs, an LED that lights when it is in your signal, its name printed across the bottom, and
    a footswitch under that.

    A slot that can hold more than one pedal also carries a selector across the top. Choosing a
    different one changes the whole object — colour, name and knobs — because that is what has
    happened: a different pedal is in the slot. The footswitch and the LED stay where they are,
    since whatever is in the slot is switched the same way.

    Straight on rather than in perspective. The photoreal boards this takes its anatomy from are
    3D renders; drawn in code this reads as a well-drawn pedal, which is the trade made in
    docs/roadmap.md in exchange for needing no artwork and scaling freely.
*/
class PedalObject final : public juce::Component
{
public:
    struct Knob
    {
        const char* parameterID;
        const char* label;
    };

    /** One pedal a slot can hold: what it is called, what colour its box is, and its controls. */
    struct Variant
    {
        juce::String name;
        juce::Colour colour;
        std::vector<Knob> knobs;
    };

    /** A slot with one pedal in it and no selector. */
    PedalObject (juce::AudioProcessorValueTreeState&, const juce::String& pedalName,
                 const char* engageParameterID, juce::Colour bodyColour,
                 std::initializer_list<Knob> knobs);

    /** A slot that can hold several, chosen by a parameter. */
    PedalObject (juce::AudioProcessorValueTreeState&, const char* engageParameterID,
                 const char* typeParameterID, std::vector<Variant> variants);

    ~PedalObject() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)>);

private:
    /** Steps the type parameter, and with it everything the slot shows. */
    class SelectorButton final : public juce::Button
    {
    public:
        explicit SelectorButton (int stepToTake);

        void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        int step;
    };

    void showVariant (int index);
    void stepVariant (int by);
    juce::Rectangle<int> selectorRow() const;

    juce::AudioProcessorValueTreeState& state;
    std::vector<Variant> variants;
    int current = 0;

    juce::RangedAudioParameter* typeParameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> typeAttachment;

    SelectorButton previousType { -1 }, nextType { 1 };

    juce::OwnedArray<PedalKnob> knobs;
    std::function<void (const juce::String&, juce::Component&)> contextMenu;

    // Filled in by resized() so that paint() puts the name and the LED exactly where the layout
    // left room for them, rather than both files agreeing by hand.
    juce::Rectangle<int> nameArea;
    juce::Point<float> ledCentre;

    /** What the LED was last drawn as, so hovering the switch does not repaint the pedal. */
    bool lampLit = false;

    Footswitch footswitch;
    juce::AudioProcessorValueTreeState::ButtonAttachment engageAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalObject)
};
