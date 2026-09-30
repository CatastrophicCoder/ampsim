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

/** The panel's colours.

    A cool near-black frame holding warm objects. The accent is amber rather than the cold blue or
    green a dark plugin usually reaches for: the amp being modelled has a brass front panel and
    valves behind it, and a warm value ring carries that without drawing a picture of it.

    Three colours carry meaning and are not interchangeable with the rest:
      - **amber** is a value — where a control is set;
      - **green** is a pedal in your signal;
      - **red** is something switched out of it.
*/
struct AmpPalette
{
    static const juce::Colour background;   // behind everything
    static const juce::Colour bar;          // the persistent top bar
    static const juce::Colour surface;      // a page
    static const juce::Colour raised;       // a control sitting on a page
    static const juce::Colour recess;       // a field sunk into one
    static const juce::Colour hairline;

    static const juce::Colour text;
    static const juce::Colour textDim;
    static const juce::Colour textFaint;

    static const juce::Colour value;        // amber: where a control is set
    static const juce::Colour engaged;      // green: in your signal
    static const juce::Colour bypassed;     // red: switched out of it
};

/** What the amp and the cabinet are made of.

    These are not the panel's signalling colours and carry no meaning — they are the materials an
    object is built from, the way a pedal's enclosure colour is. Both boxes share them so that the
    head on one page and the cab on another read as parts of the same rig.
*/
struct AmpMaterials
{
    static const juce::Colour tolex;      // the covering
    static const juce::Colour piping;     // the trim around its edge
    static const juce::Colour cloth;      // grille cloth
    static const juce::Colour metal;      // a control plate
    static const juce::Colour brassLit;
    static const juce::Colour brassDark;
    static const juce::Colour engraving;  // names cut into a plate
    static const juce::Colour knobCap;

    /** A covered box: tolex, its pebble grain, and piping round the edge. */
    static void drawBox (juce::Graphics&, juce::Rectangle<float>, int grainSeed);

    /** Grille cloth, woven and lit from above, sunk into whatever it is stretched across. */
    static void drawGrille (juce::Graphics&, juce::Rectangle<float>);

    /** A brushed fascia with a screw at each corner. */
    static void drawPlate (juce::Graphics&, juce::Rectangle<float>);
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

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    /** The interface's voice. Avenir Next: humanist, ships with macOS, and not the system font
        every other plugin defaults to. */
    static juce::Font font (float height, bool medium = false);

    /** For names printed on an object — a pedal's, the plugin's. Futura reads as something
        screen-printed onto metal, which is exactly what it is standing in for. */
    static juce::Font stencil (float height);

    /** Where a knob takes its body and track colours from, so the same drawing serves a control
        on a page and one mounted on an amp's metal plate. */
    static constexpr int knobBodyColourId  = juce::Slider::thumbColourId;
    static constexpr int knobTrackColourId = juce::Slider::rotarySliderOutlineColourId;

    /** A knob's travel: a gap at the bottom, like a real control's end stops. */
    static constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float rotaryEnd   = juce::MathConstants<float>::pi * 2.75f;

    /** Draws a control's ring and pointer at an arbitrary size, so a pedal's knobs and the amp's
        share one drawing and differ only in scale. */
    static void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float proportion,
                          float originProportion, juce::Colour bodyColour, juce::Colour trackColour,
                          float ringThickness);
};
