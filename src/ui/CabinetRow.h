#pragma once

#include "PedalTile.h"
#include "../PluginProcessor.h"

/** One corner of the mic-position grid: what position it is, and what is loaded there. */
class CabinetSlotButton final : public juce::Button
{
public:
    CabinetSlotButton (const juce::String& positionName);

    void setContents (const juce::String& text, bool loaded);

    void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

private:
    juce::String position, contents { "empty" };
    bool isLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabinetSlotButton)
};

/** The cabinet, laid out like the pedals because it is another box in the chain: four corners of
    mic position, the two controls that move between them, and a bypass lamp.

    The four corners are the feature, so they are the thing the row shows. With only one filled it
    reads as a plain IR loader, which is what it is.
*/
class CabinetRow final : public juce::Component
{
public:
    CabinetRow (AmpSimAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    void updateContents();

private:
    void showMenuFor (CabSim::Slot);

    AmpSimAudioProcessor& processorRef;

    juce::OwnedArray<CabinetSlotButton> slots;
    CompactKnob axisKnob, distanceKnob;

    juce::ToggleButton bypassButton { "" };
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabinetRow)
};
