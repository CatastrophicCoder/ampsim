/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "PedalObject.h"
#include "../PluginProcessor.h"

/** The board: six pedals in one left-to-right run, with the amp standing in the middle of it.

    Which side of the amp a pedal is on is the design of this section rather than a detail of it —
    a drive in front changes what the amp distorts, an echo after repeats the already-distorted
    tone — so the run is drawn as signal flow and the amp is drawn where it actually sits.
*/
class PedalsPage final : public juce::Component
{
public:
    explicit PedalsPage (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)>);

private:
    PedalObject gate, compressor, drive;
    PedalObject chorus, delay, reverb;

    /** Where the amp sits in the run, filled in by resized() and drawn by paint(). */
    juce::Rectangle<int> ampGap;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalsPage)
};
