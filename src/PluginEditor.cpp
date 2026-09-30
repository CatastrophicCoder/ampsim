/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PluginEditor.h"

namespace
{
    constexpr int barHeight   = 52;
    constexpr int shelfHeight = 46;
    constexpr int tabHeight   = 34;
    constexpr int margin      = 16;
    constexpr int tabWidth    = 104;

    constexpr int wordmarkWidth = 96;
    constexpr int presetWidth   = 244;

    const int scaleOptions[] { 75, 100, 125, 150 };
}

TabButton::TabButton (const juce::String& text) : juce::Button (text)
{
    setClickingTogglesState (true);
    setRadioGroupId (0x7ab);
}

void TabButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool)
{
    const auto on = getToggleState();
    auto bounds = getLocalBounds();

    // The selected tab is the one the page below belongs to, so it shares the page's surface and
    // the bar under it is the join rather than a decoration.
    if (on)
    {
        g.setColour (AmpPalette::surface);
        g.fillRect (bounds);
    }

    auto underline = bounds.removeFromBottom (2);

    if (on)
    {
        g.setColour (AmpPalette::value);
        g.fillRect (underline);
    }

    g.setFont (AmpLookAndFeel::font (11.5f, true).withExtraKerningFactor (0.12f));
    g.setColour (on ? AmpPalette::text
                    : (shouldDrawButtonAsHighlighted ? AmpPalette::textDim : AmpPalette::textFaint));
    g.drawText (getButtonText(), bounds, juce::Justification::centred, false);
}

//==============================================================================
AmpSimAudioProcessorEditor::AmpSimAudioProcessorEditor (AmpSimAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      presetRow (p.getPresets()),
      tunerAttachment (p.getValueTreeState(), ParamID::tunerOn, tunerButton),
      bypassAttachment (p.getValueTreeState(), ParamID::bypass, bypassButton),
      transposeAttachment (p.getValueTreeState(), ParamID::transposeOn, transposeButton),
      semitonesAttachment (p.getValueTreeState(), ParamID::transposeSemitones, semitonesSlider),
      ampPage (p),
      pedalsPage (p.getValueTreeState()),
      cabPage (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (panel);
    panel.addAndMakeVisible (presetRow);

    // Green means "doing something to your signal", red means "switched out of it". The tuner is
    // the first and the bypass is the second, which is the whole of the panel's colour language.
    tunerButton.setColour (juce::ToggleButton::tickColourId, AmpPalette::engaged);
    tunerButton.onClick = [this] { presetRow.setVisible (! processorRef.isTunerEngaged()); panel.repaint(); };
    panel.addAndMakeVisible (tunerButton);

    bypassButton.setColour (juce::ToggleButton::tickColourId, AmpPalette::bypassed);
    panel.addAndMakeVisible (bypassButton);

    scaleButton.onClick = [this] { showScaleMenu(); };
    panel.addAndMakeVisible (scaleButton);

    // Green, because a transpose is doing something to your signal rather than switching
    // something out of it — the same green a pedal's LED uses.
    transposeButton.setColour (juce::ToggleButton::tickColourId, AmpPalette::engaged);
    panel.addAndMakeVisible (transposeButton);

    // Two buttons and a reading rather than a knob: an interval is a count, and a count is
    // easier to step than to aim at.
    semitonesSlider.setSliderStyle (juce::Slider::IncDecButtons);
    semitonesSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 46, 22);
    semitonesSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    semitonesSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    semitonesSlider.setColour (juce::Slider::textBoxBackgroundColourId, AmpPalette::recess);
    semitonesSlider.setColour (juce::Slider::textBoxTextColourId, AmpPalette::text);
    semitonesSlider.onContextMenu = [this] (const juce::String& id, juce::Component& source)
    {
        showParameterMenu (id, source);
    };
    panel.addAndMakeVisible (semitonesSlider);

    TabButton* tabs[] { &ampTab, &pedalsTab, &cabTab };

    for (int i = 0; i < (int) std::size (tabs); ++i)
    {
        tabs[i]->onClick = [this, i] { showPage (i); };
        panel.addAndMakeVisible (*tabs[i]);
    }

    panel.addChildComponent (ampPage);
    panel.addChildComponent (pedalsPage);
    panel.addChildComponent (cabPage);

    // Every control offers the same right-click menu, so a mapping table needs no panel space.
    const auto contextMenu = [this] (const juce::String& parameterID, juce::Component& source)
    {
        showParameterMenu (parameterID, source);
    };

    ampPage.setContextMenuHandler (contextMenu);
    pedalsPage.setContextMenuHandler (contextMenu);
    cabPage.setContextMenuHandler (contextMenu);

    processorRef.getPresets().onChanged = [this]
    {
        presetRow.updateContents();
        updateLoadedFileDisplay();
    };

    // The load finishes on a background thread, so the editor is told rather than polling.
    processorRef.onLoadStateChanged = [this] { updateLoadedFileDisplay(); };
    updateLoadedFileDisplay();

    showPage (0);
    presetRow.setVisible (! processorRef.isTunerEngaged());

    // Fast enough for a tuner to feel responsive while a string is still ringing.
    startTimerHz (25);

    const auto stored = (int) processorRef.getValueTreeState().state
                                          .getProperty (StateID::panelScale, 100);
    applyScale (stored);
}

AmpSimAudioProcessorEditor::~AmpSimAudioProcessorEditor()
{
    stopTimer();
    processorRef.onLoadStateChanged = nullptr;
    processorRef.getPresets().onChanged = nullptr;
    setLookAndFeel (nullptr);
}

void AmpSimAudioProcessorEditor::showPage (int index)
{
    currentPage = index;

    ampTab.setToggleState (index == 0, juce::dontSendNotification);
    pedalsTab.setToggleState (index == 1, juce::dontSendNotification);
    cabTab.setToggleState (index == 2, juce::dontSendNotification);

    ampPage.setVisible (index == 0);
    pedalsPage.setVisible (index == 1);
    cabPage.setVisible (index == 2);
}

void AmpSimAudioProcessorEditor::showScaleMenu()
{
    juce::PopupMenu menu;

    for (const auto percent : scaleOptions)
        menu.addItem (percent, juce::String (percent) + " %", true, percent == scalePercent);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (scaleButton),
                        [this] (int result) { if (result > 0) applyScale (result); });
}

void AmpSimAudioProcessorEditor::applyScale (int percent)
{
    scalePercent = juce::jlimit (scaleOptions[0], scaleOptions[std::size (scaleOptions) - 1], percent);
    scaleButton.setButtonText (juce::String (scalePercent) + " %");

    processorRef.getValueTreeState().state.setProperty (StateID::panelScale, scalePercent, nullptr);

    // The layout is in logical points and the panel carries the scale, so nothing below this line
    // ever has to know what the window's actual size is.
    const auto factor = (float) scalePercent / 100.0f;

    setSize (juce::roundToInt (panelWidth * factor), juce::roundToInt (panelHeight * factor));
}

void AmpSimAudioProcessorEditor::timerCallback()
{
    if (! processorRef.isTunerEngaged())
        return;

    // The analysis itself, on the message thread. See Tuner.
    processorRef.getTuner().analyse();
    panel.repaint (panel.getLocalBounds().removeFromTop (barHeight));
}

/** The reading, drawn where the preset field normally sits: a note name, a bar that says how far
    off it is and which way, and the figure. In tune is the bar sitting on the centre mark.

    It takes the field's space rather than having a row of its own, because the tuner is the only
    thing you are looking at while it is on, and a row for it would be dead panel the rest of the
    time. */
void AmpSimAudioProcessorEditor::paintTuner (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto reading = processorRef.getTuner().getReading();

    if (! reading.valid)
    {
        g.setFont (AmpLookAndFeel::font (12.5f));
        g.setColour (AmpPalette::textFaint);
        g.drawText ("play a string", area, juce::Justification::centredLeft, false);
        return;
    }

    // Held readings stay on screen, dimmed: a tuner that blanks between plucks is unusable, and a
    // note decays long before you have finished turning the peg.
    const auto alpha = reading.live ? 1.0f : 0.45f;

    g.setFont (AmpLookAndFeel::font (22.0f, true));
    g.setColour (AmpPalette::text.withAlpha (alpha));
    g.drawText (Tuner::noteName (reading.midiNote), area.removeFromLeft (54),
                juce::Justification::centredLeft, false);

    g.setFont (AmpLookAndFeel::font (12.0f));
    g.setColour (AmpPalette::textDim.withAlpha (alpha));
    g.drawText (juce::String (juce::roundToInt (reading.cents)) + " cents",
                area.removeFromRight (64), juce::Justification::centredRight, false);

    // The meter: ±50 cents across the strip, with the centre marked.
    const auto meter = area.withTrimmedRight (10).reduced (0, 20).toFloat();

    g.setColour (AmpPalette::recess);
    g.fillRoundedRectangle (meter, 2.0f);

    const auto inTune = std::abs (reading.cents) < 3.0f;
    const auto offset = juce::jlimit (-0.5f, 0.5f, reading.cents / 100.0f) * meter.getWidth();

    const juce::Rectangle<float> needle { meter.getCentreX() + juce::jmin (offset, 0.0f),
                                          meter.getY(), std::abs (offset), meter.getHeight() };

    g.setColour ((inTune ? AmpPalette::engaged : AmpPalette::value).withAlpha (alpha));
    g.fillRect (inTune ? meter.withSizeKeepingCentre (4.0f, meter.getHeight()) : needle);

    g.setColour (AmpPalette::textFaint.withAlpha (alpha));
    g.drawLine (meter.getCentreX(), meter.getY() - 4.0f, meter.getCentreX(), meter.getBottom() + 4.0f, 1.0f);
}

void AmpSimAudioProcessorEditor::showParameterMenu (const juce::String& parameterID,
                                                    juce::Component& source)
{
    auto& midiLearn = processorRef.getMidiLearn();

    const auto mapped = midiLearn.getControllerFor (parameterID);
    const auto learningThis = midiLearn.isLearning() && midiLearn.getLearningParameter() == parameterID;

    juce::PopupMenu menu;

    if (learningThis)
        menu.addItem (2, "Stop listening for a controller");
    else
        menu.addItem (1, "Learn a MIDI controller");

    if (mapped >= 0)
        menu.addItem (3, "Forget CC " + juce::String (mapped));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (source),
                        [this, parameterID] (int result)
                        {
                            auto& learn = processorRef.getMidiLearn();

                            if (result == 1) learn.startLearning (parameterID);
                            if (result == 2) learn.stopLearning();
                            if (result == 3) learn.clearMapping (parameterID);
                        });
}

void AmpSimAudioProcessorEditor::updateLoadedFileDisplay()
{
    ampPage.updateContents();
    cabPage.updateContents();
}

void AmpSimAudioProcessorEditor::paintPanel (juce::Graphics& g)
{
    auto area = panel.getLocalBounds();

    g.fillAll (AmpPalette::background);

    auto bar = area.removeFromTop (barHeight);
    auto tabs = area.removeFromTop (tabHeight);
    auto shelf = area.removeFromBottom (shelfHeight);

    g.setColour (AmpPalette::bar);
    g.fillRect (bar);

    // The shelf keeps the background's colour rather than the top bar's, with a hairline over it:
    // two matching bars would frame the pages and make the window look like a picture.
    g.setColour (AmpPalette::background);
    g.fillRect (shelf);

    g.setColour (AmpPalette::hairline);
    g.drawLine ((float) shelf.getX(), (float) shelf.getY(),
                (float) shelf.getRight(), (float) shelf.getY(), 1.0f);

    g.setFont (AmpLookAndFeel::font (10.0f, true).withExtraKerningFactor (0.1f));
    g.setColour (AmpPalette::textFaint);
    g.drawText ("SEMITONES", shelf.withTrimmedLeft (margin + 214).withWidth (110),
                juce::Justification::centredLeft, false);

    g.setColour (AmpPalette::surface);
    g.fillRect (area);

    // The tab strip sits on the bar's colour, so the selected tab's own fill is what joins it to
    // the page. A line under the unselected ones would fight that, so there isn't one.
    g.setColour (AmpPalette::bar);
    g.fillRect (tabs);

    g.setColour (AmpPalette::hairline);
    g.drawLine ((float) area.getX(), (float) area.getY(), (float) area.getRight(), (float) area.getY(), 1.0f);

    // The name, set once and left alone — the panel's one piece of display type.
    g.setFont (AmpLookAndFeel::stencil (19.0f).withExtraKerningFactor (0.14f));
    g.setColour (AmpPalette::text);
    g.drawText ("AMPSIM", bar.withTrimmedLeft (margin).withWidth (wordmarkWidth),
                juce::Justification::centredLeft, false);

    if (processorRef.isTunerEngaged())
        paintTuner (g, bar.withTrimmedLeft (margin + wordmarkWidth)
                        .withWidth (presetWidth + 130));
}

void AmpSimAudioProcessorEditor::layOutPanel()
{
    auto area = panel.getLocalBounds();

    auto bar = area.removeFromTop (barHeight);

    bar.removeFromRight (margin);
    bypassButton.setBounds (bar.removeFromRight (94).reduced (0, 16));
    bar.removeFromRight (14);
    tunerButton.setBounds (bar.removeFromRight (66).reduced (0, 16));


    presetRow.setBounds (bar.withTrimmedLeft (margin + wordmarkWidth)
                            .withWidth (presetWidth)
                            .withSizeKeepingCentre (presetWidth, 28));

    auto shelf = area.removeFromBottom (shelfHeight).reduced (margin, 0);

    transposeButton.setBounds (shelf.removeFromLeft (104).withSizeKeepingCentre (104, 22));
    shelf.removeFromLeft (6);
    semitonesSlider.setBounds (shelf.removeFromLeft (92).withSizeKeepingCentre (92, 24));
    scaleButton.setBounds (shelf.removeFromRight (58).withSizeKeepingCentre (58, 24));

    auto tabs = area.removeFromTop (tabHeight).withTrimmedLeft (margin);
    ampTab.setBounds (tabs.removeFromLeft (tabWidth));
    pedalsTab.setBounds (tabs.removeFromLeft (tabWidth));
    cabTab.setBounds (tabs.removeFromLeft (tabWidth));

    const auto page = area.reduced (margin);

    juce::Component* pages[] { &ampPage, &pedalsPage, &cabPage };

    for (auto* component : pages)
        component->setBounds (page);
}

void AmpSimAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Nothing of the panel reaches here, but a fractional scale can leave a sliver at the edge.
    g.fillAll (AmpPalette::background);
}

void AmpSimAudioProcessorEditor::resized()
{
    panel.setTransform (juce::AffineTransform::scale ((float) scalePercent / 100.0f));
    panel.setBounds (0, 0, panelWidth, panelHeight);
}
