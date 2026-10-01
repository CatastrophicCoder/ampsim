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

/** One of the amp's controls: its name above, the knob, its value below.

    The amp's knobs carry a permanent read-out where a pedal's do not — these are the controls you
    set deliberately and want to be able to report, and there are only five of them.
*/
class AmpKnob final : public juce::Component
{
public:
    AmpKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
             const juce::String& labelText);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
    {
        slider.onContextMenu = std::move (handler);
    }

    /** The knob's cap. Black on an amp's plate, raised grey on a page. */
    void setBodyColour (juce::Colour);

    /** Where the value ring starts, as a proportion of the travel. See ParameterSlider. */
    void setRingOrigin (float proportion)  { slider.ringOrigin = proportion; repaint(); }

    /** Marks the two ends of the travel instead of printing a value — what an amplifier's fascia
        does, and the reason the amp's controls are numbered 0 to 10 at all.

        The marks sit at the ends of the knob's own arc, which runs from the lower left round to
        the lower right, so they land where the pointer does when the control is at either stop.
        Calling this takes the read-out away, and with it the ability to type a value into the
        panel; the host's own editor still accepts one.
    */
    void setEndMarks (const juce::String& low, const juce::String& high);

    /** Names cut into a metal fascia rather than set on a dark page: light type with a dark
        impression above it, so the light reads as coming from in front and slightly above. */
    void setEngravedOnMetal (bool);

private:
    void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
                       juce::Justification) const;

    juce::String name;
    juce::String lowMark, highMark;
    bool marksEnds = false;
    bool engraved = false;
    ParameterSlider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpKnob)
};
