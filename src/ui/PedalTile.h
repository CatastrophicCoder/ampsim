#pragma once

#include "AmpLookAndFeel.h"
#include "ParameterSlider.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** A small knob for the pedalboard: name underneath, value on a popup while you drag it.

    No permanent value read-out — six pedals' worth of numbers would crowd the deck, and a pedal's
    settings are read from the knob positions anyway.
*/
class CompactKnob final : public juce::Component
{
public:
    CompactKnob (juce::AudioProcessorValueTreeState& state,
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompactKnob)
};

/** One pedal: an enclosure with a name, a footswitch lamp and its controls.

    The tile does not say which side of the amp it is on — the deck it sits on does, which is why
    the two groups are laid out as separate rows with their own heading.
*/
class PedalTile final : public juce::Component
{
public:
    struct Knob
    {
        const char* parameterID;
        const char* label;
    };

    PedalTile (juce::AudioProcessorValueTreeState& state,
               const juce::String& pedalName,
               const char* engageParameterID,
               std::initializer_list<Knob> knobs);

    void paint (juce::Graphics&) override;
    void resized() override;

    void setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler);

private:
    juce::String name;
    juce::ToggleButton engageButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment engageAttachment;
    juce::OwnedArray<CompactKnob> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalTile)
};
