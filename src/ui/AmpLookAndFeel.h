/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The plugin's visual identity.

    The panel is not a pastiche of a guitar amp's tolex-and-gold-lettering front. This amp is a
    file — a neural capture loaded from disk — so the panel is drawn as a piece of measuring
    equipment instead: a pale enamelled steel plate, engraved lettering, graphite knobs with a
    single saturated blue arc reading out the value. Red is reserved for the one thing that means
    "this is switched out of your signal".

    Colours live here rather than in the editor so a second panel or window inherits them.
*/
struct AmpPalette
{
    static const juce::Colour enamel;        // the plate itself
    static const juce::Colour enamelShade;   // grain and vignette
    static const juce::Colour rail;          // the darker strips top and bottom
    static const juce::Colour railRecess;    // the inset a nameplate sits in
    static const juce::Colour engraved;      // lettering
    static const juce::Colour engravedSoft;  // secondary lettering
    static const juce::Colour graphite;      // knob body
    static const juce::Colour graphiteRim;
    static const juce::Colour reading;       // the value arc
    static const juce::Colour attention;     // bypassed
    static const juce::Colour hairline;
};

class AmpLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AmpLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    /** Lettering stamped into the pale plate: a light impression below, dark type above. */
    static void drawEngravedText (juce::Graphics&, const juce::String& text,
                                  juce::Rectangle<int> bounds, juce::Justification,
                                  const juce::Font&, juce::Colour colour);

    /** The same idea on the dark rails, where the light has to come from the other side: a dark
        impression above, light type below. Engraving dark-on-dark just smears. */
    static void drawRailText (juce::Graphics&, const juce::String& text,
                              juce::Rectangle<int> bounds, juce::Justification,
                              const juce::Font&, juce::Colour colour);

    static juce::Font panelFont (float height, bool medium = false);
};
