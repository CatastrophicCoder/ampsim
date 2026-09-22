#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** A slider that knows which parameter it drives, so a right-click can offer to learn a MIDI
    controller for it.

    Putting the menu on the control itself is how every plugin does this, and it costs no panel
    space — a mapping table would need a page of its own that nobody opens twice.
*/
class ParameterSlider final : public juce::Slider
{
public:
    explicit ParameterSlider (juce::String parameterIDToUse)
        : parameterID (std::move (parameterIDToUse)) {}

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() && onContextMenu != nullptr)
        {
            onContextMenu (parameterID, *this);
            return;
        }

        juce::Slider::mouseDown (event);
    }

    const juce::String& getParameterID() const noexcept { return parameterID; }

    std::function<void (const juce::String& parameterID, juce::Component& source)> onContextMenu;

private:
    juce::String parameterID;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterSlider)
};
