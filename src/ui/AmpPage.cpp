/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "AmpPage.h"

namespace
{
    // A head is wide and shallow. These add up to roughly two and a half to one, which is what
    // stops the drawing reading as a bordered panel.
    constexpr int headHeight   = 316;
    constexpr int shellInset   = 16;
    constexpr int grilleHeight = 152;
    constexpr int shelfGap     = 12;
    constexpr int plateHeight  = headHeight - 2 * shellInset - grilleHeight - shelfGap;

    constexpr int badgeWidth  = 232;
    constexpr int badgeHeight = 58;

    constexpr int powerWidth = 92;
    constexpr int plateInsetX = 18;
    constexpr int plateInsetY = 15;

    /** Room above the pilot lamp for its glow, taken out of the plate's own inset rather than out
        of the switch, so the lamp, the rocker and the word stay level with the knobs beside them.
        `PowerSwitch::paintButton` skips it before laying anything out. */
    constexpr int lampHeadroom = 9;

    /** The pilot lamp's red. On the panel red means "switched out of your signal"; here it is a
        jewel on the front of an amplifier, which is a thing rather than a reading — the same
        licence a pedal's enclosure colour takes. Deeper and more saturated than
        `AmpPalette::bypassed`, which is the reading, so the two do not read as the same mark.
        Dark is off, and that is the whole message. */
    const juce::Colour jewel { 0xffe0241c };
}

//==============================================================================
ModelBadge::ModelBadge() : juce::Button ("model")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModelBadge::setContents (const juce::String& text, bool isError)
{
    contents = text;
    showingError = isError;
    repaint();
}

void ModelBadge::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    if (shouldDrawButtonAsDown)
        bounds.translate (0.0f, 1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), 4.0f);

    const auto lift = shouldDrawButtonAsHighlighted ? 0.12f : 0.0f;

    g.setGradientFill (juce::ColourGradient (AmpMaterials::brassLit.brighter (lift), bounds.getCentreX(), bounds.getY(),
                                             AmpMaterials::brassDark.brighter (lift), bounds.getCentreX(), bounds.getBottom(),
                                             false));
    g.fillRoundedRectangle (bounds, 4.0f);

    // Brushed along its length, as a stamped plate is.
    juce::Random grain (0x8ad9e);

    for (float y = bounds.getY() + 1.0f; y < bounds.getBottom() - 1.0f; y += 1.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.03f * grain.nextFloat()));
        g.fillRect (bounds.getX() + 1.0f, y, bounds.getWidth() - 2.0f, 1.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    auto text = bounds.toNearestInt().reduced (14, 8);

    g.setFont (AmpLookAndFeel::font (9.0f, true).withExtraKerningFactor (0.18f));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawText ("MODEL", text.removeFromTop (12), juce::Justification::centredTop, false);

    g.setFont (AmpLookAndFeel::stencil (19.0f).withExtraKerningFactor (0.04f));
    g.setColour (showingError ? juce::Colour (0xff5e1a12) : juce::Colour (0xff24190a));
    g.drawText (contents, text, juce::Justification::centred, true);
}

//==============================================================================
PowerSwitch::PowerSwitch() : juce::Button ("power")
{
    setClickingTogglesState (true);
}

void PowerSwitch::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown)
{
    const auto on = getToggleState();
    auto bounds = getLocalBounds();

    // The switch's own bounds reach above the control plate's inset to give the glow somewhere to
    // go. Everything below is laid out from here, so it sits where it did before the glow existed.
    bounds.removeFromTop (lampHeadroom);

    // The jewel above, the rocker below it, and the word under both.
    const auto lamp = bounds.removeFromTop (18).withSizeKeepingCentre (14, 14).toFloat();

    if (on)
    {
        // A glow falls off; it does not stop. A flat disc of one alpha has an edge, and an edge
        // that runs into the component's own is what makes it look cut rather than lit.
        const auto glow = lamp.expanded (9.0f);

        juce::ColourGradient halo (jewel.withAlpha (0.34f), lamp.getCentreX(), lamp.getCentreY(),
                                   jewel.withAlpha (0.0f), glow.getCentreX(), glow.getBottom(), true);
        halo.addColour (0.5, jewel.withAlpha (0.13f));

        g.setGradientFill (halo);
        g.fillEllipse (glow);
    }

    g.setColour (juce::Colour (0xff474d57));
    g.fillEllipse (lamp.expanded (2.0f));

    // Off is the same jewel unlit: dark, but still the colour of the glass rather than of a hole.
    g.setColour (on ? jewel : juce::Colour (0xff3b1512));
    g.fillEllipse (lamp);

    if (on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.fillEllipse (lamp.reduced (4.2f).translated (-0.8f, -1.2f));
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (lamp, 1.0f);

    bounds.removeFromTop (5);

    const auto rocker = bounds.removeFromTop (22).withSizeKeepingCentre (40, 22).toFloat();

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (rocker.expanded (2.0f), 4.0f);

    // A rocker is one piece that tips: the half you last pressed sits down, the other stands up.
    const auto pressedHalf = on ? rocker.withTrimmedBottom (rocker.getHeight() * 0.5f)
                                : rocker.withTrimmedTop (rocker.getHeight() * 0.5f);
    const auto raisedHalf  = on ? rocker.withTrimmedTop (rocker.getHeight() * 0.5f)
                                : rocker.withTrimmedBottom (rocker.getHeight() * 0.5f);

    g.setColour (juce::Colour (0xff171a1f).brighter (shouldDrawButtonAsDown ? 0.08f : 0.0f));
    g.fillRoundedRectangle (pressedHalf, 3.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffa8afb9), rocker.getCentreX(), raisedHalf.getY(),
                                             juce::Colour (0xff5b616a), rocker.getCentreX(), raisedHalf.getBottom(),
                                             false));
    g.fillRoundedRectangle (raisedHalf, 3.0f);

    // The pivot the two halves tip about.
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.fillRect (rocker.getX() + 1.0f, rocker.getCentreY() - 0.5f, rocker.getWidth() - 2.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (shouldDrawButtonAsHighlighted ? 0.4f : 0.2f));
    g.drawRoundedRectangle (rocker.reduced (0.5f), 4.0f, 1.0f);

    bounds.removeFromTop (6);

    g.setFont (AmpLookAndFeel::font (8.5f, true).withExtraKerningFactor (0.22f));
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText ("POWER", bounds.translated (0, 1), juce::Justification::centredTop, false);
    g.setColour (AmpMaterials::engraving.withAlpha (on ? 0.85f : 0.45f));
    g.drawText ("POWER", bounds, juce::Justification::centredTop, false);
}

//==============================================================================
AmpPage::AmpPage (AmpSimAudioProcessor& p)
    : processorRef (p),
      powerAttachment (p.getValueTreeState(), ParamID::power, powerSwitch),
      gainKnob   (p.getValueTreeState(), ParamID::inputGain,  "GAIN"),
      bassKnob   (p.getValueTreeState(), ParamID::bass,       "BASS"),
      midKnob    (p.getValueTreeState(), ParamID::mid,        "MIDDLE"),
      trebleKnob (p.getValueTreeState(), ParamID::treble,     "TREBLE"),
      presenceKnob (p.getValueTreeState(), ParamID::presence,  "PRESENCE"),
      depthKnob  (p.getValueTreeState(), ParamID::depth,       "DEPTH"),
      masterKnob (p.getValueTreeState(), ParamID::outputGain,  "MASTER")
{
    badge.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load a NAM model",
                                                           processorRef.getModelFile(), "*.nam");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();

                                      if (file != juce::File())
                                          processorRef.loadModel (file);
                                  });
    };

    addAndMakeVisible (badge);

    // The lamp is inside the switch, but the head going dark is not, so the page has to be told.
    powerSwitch.onStateChange = [this]
    {
        if (const auto on = powerSwitch.getToggleState(); on != lit)
        {
            lit = on;
            repaint();
        }
    };

    addAndMakeVisible (powerSwitch);

    for (auto* knob : { &gainKnob, &bassKnob, &midKnob, &trebleKnob, &presenceKnob, &depthKnob, &masterKnob })
    {
        // Black knobs on a metal plate, rather than the raised grey they wear on a page.
        knob->setBodyColour (AmpMaterials::knobCap);
        knob->setEngravedOnMetal (true);

        // The amp's controls are marked 0 to 10 and the two ends are printed on the plate, the way
        // an amplifier's are. The cab's knobs and the shelf's keep their read-outs, because what
        // they are set to is a measurement rather than a dial position.
        knob->setEndMarks ("0", "10");
        addAndMakeVisible (*knob);
    }

    updateContents();
}

void AmpPage::setContextMenuHandler (std::function<void (const juce::String&, juce::Component&)> handler)
{
    for (auto* knob : { &gainKnob, &bassKnob, &midKnob, &trebleKnob, &presenceKnob, &depthKnob, &masterKnob })
        knob->setContextMenuHandler (handler);
}

void AmpPage::updateContents()
{
    const auto error = processorRef.getModelError();
    const auto file = processorRef.getModelFile();

    badge.setContents (error.isNotEmpty() ? error
                                          : (file == juce::File() ? "No model loaded"
                                                                  : file.getFileNameWithoutExtension()),
                       error.isNotEmpty());
}

//==============================================================================
juce::Rectangle<int> AmpPage::head() const
{
    return getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (getHeight(), headHeight));
}

juce::Rectangle<int> AmpPage::grille() const
{
    return head().reduced (shellInset).withHeight (grilleHeight);
}

juce::Rectangle<int> AmpPage::plate() const
{
    return head().reduced (shellInset).withTrimmedTop (grilleHeight + shelfGap).withHeight (plateHeight);
}

void AmpPage::paintShell (juce::Graphics& g) const
{
    AmpMaterials::drawBox (g, head().toFloat(), 0x7012e5);
}

void AmpPage::paintGrille (juce::Graphics& g) const
{
    AmpMaterials::drawGrille (g, grille().toFloat());
}

void AmpPage::paintPlate (juce::Graphics& g) const
{
    AmpMaterials::drawPlate (g, plate().toFloat());
}

void AmpPage::paint (juce::Graphics& g)
{
    paintShell (g);
    paintGrille (g);
    paintPlate (g);
}

void AmpPage::paintOverChildren (juce::Graphics& g)
{
    if (powerSwitch.getToggleState())
        return;

    // Switched off, the front of the amp goes dark. Over the children, because the name plate is
    // one of them and a lamp-off amp with its badge still glowing would be the wrong picture.
    // The control plate keeps its light: you can still see what the amp is set to.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (grille().toFloat(), 4.0f);
}

void AmpPage::resized()
{
    badge.setBounds (grille().withSizeKeepingCentre (badgeWidth, badgeHeight));

    auto face = plate().reduced (plateInsetX, plateInsetY);

    // Expanded upward into the plate's inset, which is empty, so the glow above the lamp is not
    // clipped off by the switch's own top edge.
    powerSwitch.setBounds (face.removeFromRight (powerWidth).withTrimmedTop (-lampHeadroom));
    face.removeFromRight (10);

    AmpKnob* controls[] { &gainKnob, &bassKnob, &midKnob, &trebleKnob,
                          &presenceKnob, &depthKnob, &masterKnob };
    const auto width = face.getWidth() / (int) std::size (controls);

    for (auto* knob : controls)
        knob->setBounds (face.removeFromLeft (width).reduced (5, 0));
}
