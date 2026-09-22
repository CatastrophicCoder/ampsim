#include "AmpLookAndFeel.h"

const juce::Colour AmpPalette::enamel       { 0xffd7dad2 };
const juce::Colour AmpPalette::enamelShade  { 0xffc0c5bb };
const juce::Colour AmpPalette::rail         { 0xff23271f };
const juce::Colour AmpPalette::railRecess   { 0xff171a14 };
const juce::Colour AmpPalette::engraved     { 0xff1e221d };
const juce::Colour AmpPalette::engravedSoft { 0xff5c635a };
const juce::Colour AmpPalette::graphite     { 0xff2b2f2b };
const juce::Colour AmpPalette::graphiteRim  { 0xff585f57 };
const juce::Colour AmpPalette::reading      { 0xff1d4e9b };
const juce::Colour AmpPalette::attention    { 0xffb63a2c };
const juce::Colour AmpPalette::hairline     { 0xff9aa096 };

juce::Font AmpLookAndFeel::panelFont (float height, bool medium)
{
    // A plain grotesque, the way instrument panels are lettered. Helvetica Neue is present on
    // every Mac; the fallback keeps Windows and Linux builds sane later.
    auto options = juce::FontOptions ("Helvetica Neue", height, juce::Font::plain);

    if (medium)
        options = options.withStyle ("Medium");

    return juce::Font (options);
}

AmpLookAndFeel::AmpLookAndFeel()
{
    setColour (juce::Label::textColourId, AmpPalette::engraved);
    setColour (juce::Slider::textBoxTextColourId, AmpPalette::engravedSoft);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId, AmpPalette::rail);
    setColour (juce::TextButton::textColourOffId, AmpPalette::enamel.withAlpha (0.9f));
    setColour (juce::ToggleButton::textColourId, AmpPalette::enamel);
}

void AmpLookAndFeel::drawEngravedText (juce::Graphics& g, const juce::String& text,
                                       juce::Rectangle<int> bounds, juce::Justification justification,
                                       const juce::Font& font, juce::Colour colour)
{
    g.setFont (font);

    // The impression first, one pixel down, then the ink.
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawText (text, bounds.translated (0, 1), justification, false);

    g.setColour (colour);
    g.drawText (text, bounds, justification, false);
}

void AmpLookAndFeel::drawRailText (juce::Graphics& g, const juce::String& text,
                                   juce::Rectangle<int> bounds, juce::Justification justification,
                                   const juce::Font& font, juce::Colour colour)
{
    g.setFont (font);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText (text, bounds.translated (0, -1), justification, false);

    g.setColour (colour);
    g.drawText (text, bounds, justification, false);
}

void AmpLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto centre = bounds.getCentre();

    const auto outerRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto arcRadius = outerRadius - 2.0f;
    const auto knobRadius = arcRadius - 7.0f;   // close in, so the reading belongs to the control

    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // The scale the reading is taken against.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (AmpPalette::hairline.withAlpha (0.8f));
    g.strokePath (track, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // The reading itself, from the centre detent for a control that rests at zero.
    const auto isCentred = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto originAngle = isCentred ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
                                       : rotaryStartAngle;

    if (std::abs (angle - originAngle) > 1.0e-3f)
    {
        juce::Path reading;
        reading.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                               juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);

        g.setColour (AmpPalette::reading);
        g.strokePath (reading, juce::PathStrokeType (3.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    // No detent mark: the arc already grows out of the rest position, so a tick there is a
    // second way of saying the same thing — and at this size it reads as a speck of dirt.

    // The control: a machined graphite cap, lit from above.
    const auto knobBounds = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f)
                                .withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (knobBounds.translated (0.0f, 2.0f));

    g.setGradientFill (juce::ColourGradient (AmpPalette::graphiteRim, centre.x, knobBounds.getY(),
                                             AmpPalette::graphite.darker (0.4f), centre.x,
                                             knobBounds.getBottom(), false));
    g.fillEllipse (knobBounds);

    // A knurled rim: slightly lighter ring, then the flat top face inside it.
    g.setGradientFill (juce::ColourGradient (AmpPalette::graphite.brighter (0.12f), centre.x,
                                             knobBounds.getY() + knobRadius * 0.3f,
                                             AmpPalette::graphite.darker (0.15f), centre.x,
                                             knobBounds.getBottom(), false));
    g.fillEllipse (knobBounds.reduced (knobRadius * 0.16f));

    g.setColour (juce::Colours::white.withAlpha (0.13f));
    g.drawEllipse (knobBounds.reduced (0.8f), 1.3f);

    // The index line, the part the eye actually reads.
    const juce::Point<float> pointer { std::sin (angle), -std::cos (angle) };
    g.setColour (AmpPalette::enamel);
    g.drawLine ({ centre + pointer * (knobRadius * 0.30f),
                  centre + pointer * (knobRadius * 0.94f) }, 2.4f);
}

void AmpLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool)
{
    auto bounds = button.getLocalBounds();
    const auto lampSize = juce::jmin (10, bounds.getHeight());
    const auto lamp = bounds.removeFromLeft (lampSize + 8)
                          .withSizeKeepingCentre (lampSize, lampSize).toFloat();

    const auto on = button.getToggleState();

    if (on)
    {
        g.setColour (AmpPalette::attention.withAlpha (0.30f));
        g.fillEllipse (lamp.expanded (3.5f));
    }

    g.setColour (on ? AmpPalette::attention : juce::Colour (0xff3a4034));
    g.fillEllipse (lamp);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (lamp, 1.0f);

    drawRailText (g, button.getButtonText(), bounds, juce::Justification::centredLeft,
                  panelFont (13.0f),
                  AmpPalette::enamel.withAlpha (on ? 1.0f
                                                   : (shouldDrawButtonAsHighlighted ? 0.85f : 0.6f)));
}

void AmpLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (shouldDrawButtonAsDown ? AmpPalette::railRecess
                                        : (shouldDrawButtonAsHighlighted ? AmpPalette::rail.brighter (0.22f)
                                                                         : AmpPalette::rail.brighter (0.10f)));
    g.fillRect (bounds);

    g.setColour (AmpPalette::enamel.withAlpha (0.35f));
    g.drawRect (bounds, 1.0f);
}

juce::Font AmpLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return panelFont (13.0f);
}

juce::Font AmpLookAndFeel::getLabelFont (juce::Label& label)
{
    return panelFont (label.getFont().getHeight());
}

juce::Label* AmpLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    label->setFont (panelFont (12.0f));
    label->setJustificationType (juce::Justification::centred);
    label->setColour (juce::Label::textColourId, AmpPalette::engravedSoft);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineWhenEditingColourId, AmpPalette::reading);
    label->setColour (juce::Label::backgroundWhenEditingColourId, AmpPalette::enamel);
    label->setColour (juce::Label::textWhenEditingColourId, AmpPalette::engraved);

    return label;
}
