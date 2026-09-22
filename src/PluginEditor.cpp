#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 46;
    constexpr int rowHeight    = 34;
    constexpr int gutter       = 18;
    constexpr int nameColumn   = 46;
    constexpr int pedalRowHeight = 86;
    constexpr int groupLabelHeight = 18;

    /** Both pedal rows, plus a margin at the bottom so the enclosures are not flush with the
        window edge. paint() and resized() both take this from the bottom, in the same order. */
    constexpr int deckHeight = 3 * (pedalRowHeight + groupLabelHeight) + 10;

    /** The faint vertical grain of a brushed enamel plate. Drawn once per repaint; cheap enough
        at this size, and it keeps the panel from reading as a flat rectangle of colour. */
    void paintPlate (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (AmpPalette::enamel);
        g.fillRect (area);

        juce::Random grain (0x5eed);

        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto strength = 0.05f + 0.07f * grain.nextFloat();

            if (grain.nextFloat() < 0.35f)
            {
                g.setColour (AmpPalette::enamelShade.withAlpha (strength));
                g.fillRect (x, area.getY(), 1, area.getHeight());
            }
        }

        // A soft vignette, so the middle of the plate sits forward of its edges.
        juce::ColourGradient vignette (juce::Colours::transparentBlack,
                                       area.getCentreX(), (float) area.getCentreY(),
                                       juce::Colours::black.withAlpha (0.10f),
                                       (float) area.getX(), (float) area.getY(), true);
        g.setGradientFill (vignette);
        g.fillRect (area);
    }

    void paintRecess (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (AmpPalette::railRecess);
        g.fillRect (area);

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine (area.getX(), area.getY(), area.getRight(), area.getY(), 1.0f);

        g.setColour (AmpPalette::enamel.withAlpha (0.10f));
        g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getBottom(), 1.0f);
    }
}

//==============================================================================
LabelledKnob::LabelledKnob (juce::AudioProcessorValueTreeState& state,
                            const juce::String& parameterID,
                            const juce::String& labelText)
    : name (labelText), attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);

    // The readout is engraving on the plate, not a text field: the frame and fill have to be
    // cleared on the slider itself, since that is where the text box takes its colours from.
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, AmpPalette::engravedSoft);
    slider.setColour (juce::Slider::textBoxHighlightColourId, AmpPalette::reading.withAlpha (0.25f));
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (slider);
}

void LabelledKnob::paint (juce::Graphics& g)
{
    AmpLookAndFeel::drawEngravedText (g, name, getLocalBounds().removeFromTop (17),
                                      juce::Justification::centred,
                                      AmpLookAndFeel::panelFont (13.0f, true),
                                      AmpPalette::engraved);
}

void LabelledKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (20);   // the engraved name

    // A square control with its readout directly under it, rather than a tall box that leaves
    // the value floating half a panel away from the knob it belongs to.
    const auto size = juce::jmin (area.getWidth(), area.getHeight() - 18);
    slider.setBounds (area.withSizeKeepingCentre (size, size + 18).withY (area.getY()));
}

//==============================================================================
NameplateRow::NameplateRow (const juce::String& rowName, const juce::String& browseText)
    : name (rowName)
{
    browseButton.setButtonText (browseText);
    browseButton.onClick = [this] { if (onBrowse != nullptr) onBrowse(); };
    addAndMakeVisible (browseButton);
}

void NameplateRow::setContents (const juce::String& text, bool isError)
{
    contents = text;
    showingError = isError;
    repaint();
}

void NameplateRow::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    AmpLookAndFeel::drawRailText (g, name, area.removeFromLeft (nameColumn),
                                  juce::Justification::centredLeft,
                                  AmpLookAndFeel::panelFont (13.0f, true),
                                  AmpPalette::enamel.withAlpha (0.5f));

    area.removeFromRight (browseButton.getWidth() + 10);

    paintRecess (g, area.toFloat().reduced (0.0f, 3.0f));

    g.setFont (AmpLookAndFeel::panelFont (14.0f));
    g.setColour (showingError ? AmpPalette::attention.brighter (0.35f)
                              : AmpPalette::enamel.withAlpha (0.92f));
    g.drawText (contents, area.reduced (12, 0), juce::Justification::centredLeft, true);
}

void NameplateRow::resized()
{
    browseButton.setBounds (getLocalBounds().removeFromRight (96).reduced (0, 4));
}

//==============================================================================
AmpSimAudioProcessorEditor::AmpSimAudioProcessorEditor (AmpSimAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      gainKnob   (p.getValueTreeState(), ParamID::inputGain,  "Gain"),
      bassKnob   (p.getValueTreeState(), ParamID::bass,       "Bass"),
      midKnob    (p.getValueTreeState(), ParamID::mid,        "Mid"),
      trebleKnob (p.getValueTreeState(), ParamID::treble,     "Treble"),
      masterKnob (p.getValueTreeState(), ParamID::outputGain, "Master"),
      bypassAttachment (p.getValueTreeState(), ParamID::bypass, bypassButton),
      tunerAttachment (p.getValueTreeState(), ParamID::tunerOn, tunerButton),
      gatePedal (p.getValueTreeState(), "gate", ParamID::gateOn,
                 { { ParamID::gateThreshold, "thresh" } }),
      compressorPedal (p.getValueTreeState(), "comp", ParamID::compOn,
                       { { ParamID::compAmount, "amount" }, { ParamID::compLevel, "level" } }),
      drivePedal (p.getValueTreeState(), "drive", ParamID::driveOn,
                  { { ParamID::driveAmount, "drive" }, { ParamID::driveTone, "tone" },
                    { ParamID::driveLevel, "level" } }),
      chorusPedal (p.getValueTreeState(), "chorus", ParamID::chorusOn,
                   { { ParamID::chorusRate, "rate" }, { ParamID::chorusDepth, "depth" },
                     { ParamID::chorusMix, "mix" } }),
      delayPedal (p.getValueTreeState(), "delay", ParamID::delayOn,
                  { { ParamID::delayTime, "time" }, { ParamID::delayFeedback, "repeats" },
                    { ParamID::delayMix, "mix" } }),
      reverbPedal (p.getValueTreeState(), "reverb", ParamID::reverbOn,
                   { { ParamID::reverbSize, "size" }, { ParamID::reverbMix, "mix" } }),
      cabinetRow (p)
{
    setLookAndFeel (&lookAndFeel);

    for (auto* knob : { &gainKnob, &bassKnob, &midKnob, &trebleKnob, &masterKnob })
        addAndMakeVisible (*knob);

    bypassButton.setColour (juce::ToggleButton::textColourId, AmpPalette::enamel);
    addAndMakeVisible (bypassButton);

    // Blue, like a pedal's footswitch: the tuner is doing something, not switching something out.
    tunerButton.setColour (juce::ToggleButton::tickColourId, AmpPalette::reading);
    tunerButton.onClick = [this] { repaint(); };
    addAndMakeVisible (tunerButton);

    // Fast enough for a tuner to feel responsive while a string is still ringing.
    startTimerHz (25);

    ampRow.onBrowse = [this]
    {
        chooseFile ("Load a NAM model", processorRef.getModelFile(), "*.nam",
                    [this] (const juce::File& file) { processorRef.loadModel (file); });
    };

    for (auto* pedal : { &gatePedal, &compressorPedal, &drivePedal,
                         &chorusPedal, &delayPedal, &reverbPedal })
        addAndMakeVisible (*pedal);

    addAndMakeVisible (ampRow);
    addAndMakeVisible (cabinetRow);

    // The load finishes on a background thread, so the editor is told rather than polling.
    processorRef.onLoadStateChanged = [this] { updateLoadedFileDisplay(); };
    updateLoadedFileDisplay();

    setSize (640, 310 - rowHeight + deckHeight);
}

AmpSimAudioProcessorEditor::~AmpSimAudioProcessorEditor()
{
    stopTimer();
    processorRef.onLoadStateChanged = nullptr;
    setLookAndFeel (nullptr);
}

void AmpSimAudioProcessorEditor::timerCallback()
{
    if (! processorRef.isTunerEngaged())
        return;

    // The analysis itself, on the message thread. See Tuner.
    processorRef.getTuner().analyse();
    repaint (getLocalBounds().removeFromTop (headerHeight));
}

/** The reading, drawn where the strapline normally sits: a note name, and a bar that says how
    far off it is and which way. In tune is the bar sitting on the centre mark. */
void AmpSimAudioProcessorEditor::paintTuner (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto reading = processorRef.getTuner().getReading();

    if (! reading.valid)
    {
        g.setFont (AmpLookAndFeel::panelFont (12.0f));
        g.setColour (AmpPalette::enamel.withAlpha (0.4f));
        g.drawText ("play a string", area, juce::Justification::centredLeft, false);
        return;
    }

    auto noteArea = area.removeFromLeft (52);

    g.setFont (AmpLookAndFeel::panelFont (20.0f, true));
    g.setColour (AmpPalette::enamel);
    g.drawText (Tuner::noteName (reading.midiNote), noteArea, juce::Justification::centredLeft, false);

    // The meter: ±50 cents across the strip, with the centre marked.
    auto meter = area.removeFromLeft (juce::jmax (80, area.getWidth() - 70))
                     .reduced (0, 15).toFloat();

    g.setColour (AmpPalette::railRecess);
    g.fillRect (meter);

    g.setColour (AmpPalette::enamel.withAlpha (0.25f));
    g.drawLine (meter.getCentreX(), meter.getY() - 3.0f, meter.getCentreX(), meter.getBottom() + 3.0f, 1.0f);

    const auto inTune = std::abs (reading.cents) < 3.0f;
    const auto offset = juce::jlimit (-0.5f, 0.5f, reading.cents / 100.0f) * meter.getWidth();

    const juce::Rectangle<float> needle { meter.getCentreX() + juce::jmin (offset, 0.0f),
                                          meter.getY(), std::abs (offset), meter.getHeight() };

    g.setColour (inTune ? AmpPalette::reading : AmpPalette::attention);
    g.fillRect (inTune ? meter.withSizeKeepingCentre (4.0f, meter.getHeight()) : needle);

    g.setFont (AmpLookAndFeel::panelFont (12.0f));
    g.setColour (AmpPalette::enamel.withAlpha (0.5f));
    g.drawText (juce::String (juce::roundToInt (reading.cents)) + " cents",
                area.withTrimmedLeft (10), juce::Justification::centredLeft, false);
}

void AmpSimAudioProcessorEditor::chooseFile (const juce::String& title, const juce::File& startingFile,
                                             const juce::String& pattern,
                                             std::function<void (const juce::File&)> onChosen)
{
    // Held as a member: the chooser has to outlive this call, since it runs asynchronously.
    fileChooser = std::make_unique<juce::FileChooser> (title, startingFile, pattern);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [onChosen] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (file != juce::File())
                                      onChosen (file);
                              });
}

void AmpSimAudioProcessorEditor::updateLoadedFileDisplay()
{
    const auto describe = [] (NameplateRow& row, const juce::String& error,
                              const juce::File& file, const juce::String& emptyText)
    {
        if (error.isNotEmpty())
            row.setContents (error, true);
        else
            row.setContents (file == juce::File() ? emptyText : file.getFileNameWithoutExtension(), false);
    };

    describe (ampRow, processorRef.getModelError(), processorRef.getModelFile(),
              "No model loaded");
    cabinetRow.updateContents();
    cabinetRow.repaint();
}

void AmpSimAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    // Same order as resized(), bottom upwards: the pedal deck sits under the amp's nameplates.
    auto header = area.removeFromTop (headerHeight);
    auto deck = area.removeFromBottom (deckHeight);
    auto footer = area.removeFromBottom (rowHeight + gutter);

    paintPlate (g, area);

    g.setColour (AmpPalette::rail);
    g.fillRect (header);
    g.fillRect (footer);

    // The pedal deck: a darker surface below the amp, with each group's position spelled out.
    g.setColour (AmpPalette::rail.darker (0.25f));
    g.fillRect (deck);

    const auto heading = [&] (juce::Rectangle<int> row, const juce::String& text)
    {
        AmpLookAndFeel::drawRailText (g, text, row.reduced (gutter, 0),
                                      juce::Justification::centredLeft,
                                      AmpLookAndFeel::panelFont (11.0f),
                                      AmpPalette::enamel.withAlpha (0.42f));
    };

    heading (deck.removeFromTop (groupLabelHeight), "into the amp");
    deck.removeFromTop (pedalRowHeight);
    heading (deck.removeFromTop (groupLabelHeight), "after the amp, before the cab");
    deck.removeFromTop (pedalRowHeight);
    heading (deck.removeFromTop (groupLabelHeight), "and out through the speaker");

    // The name, set once and left alone — the panel's one piece of display type.
    // Leave the right-hand end to the two switches, so nothing is drawn under them.
    auto nameArea = header.reduced (gutter, 0).withTrimmedRight (220);

    const auto nameFont = AmpLookAndFeel::panelFont (21.0f, true).withExtraKerningFactor (0.16f);
    const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (nameFont, "ampsim");

    g.setFont (nameFont);
    g.setColour (AmpPalette::enamel);
    g.drawText ("ampsim", nameArea.removeFromLeft (nameWidth + 18),
                juce::Justification::centredLeft, false);

    // While the tuner is on it takes over the strapline's space: it is the only thing you are
    // looking at, and a second row for it would be dead panel the rest of the time.
    if (processorRef.isTunerEngaged())
    {
        paintTuner (g, nameArea);
    }
    else
    {
        g.setFont (AmpLookAndFeel::panelFont (12.0f));
        g.setColour (AmpPalette::enamel.withAlpha (0.4f));
        g.drawText ("neural capture, played through a cabinet", nameArea,
                    juce::Justification::centredLeft, false);
    }

    // Hairlines where the plate meets the rails, so the plate reads as a separate piece of metal.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine ((float) area.getX(), (float) area.getY(), (float) area.getRight(), (float) area.getY(), 1.0f);
    g.drawLine ((float) area.getX(), (float) area.getBottom(), (float) area.getRight(), (float) area.getBottom(), 1.0f);
}

void AmpSimAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight);
    bypassButton.setBounds (header.removeFromRight (130).reduced (gutter, 14));
    tunerButton.setBounds (header.removeFromRight (80).reduced (0, 14));

    auto deck = area.removeFromBottom (deckHeight);

    const auto layOutRow = [] (juce::Rectangle<int> row, std::initializer_list<PedalTile*> tiles)
    {
        const auto width = row.getWidth() / (int) tiles.size();

        for (auto* tile : tiles)
            tile->setBounds (row.removeFromLeft (width).reduced (4, 0));
    };

    deck.removeFromTop (groupLabelHeight);
    layOutRow (deck.removeFromTop (pedalRowHeight).reduced (gutter - 4, 2),
               { &gatePedal, &compressorPedal, &drivePedal });

    deck.removeFromTop (groupLabelHeight);
    layOutRow (deck.removeFromTop (pedalRowHeight).reduced (gutter - 4, 2),
               { &chorusPedal, &delayPedal, &reverbPedal });

    deck.removeFromTop (groupLabelHeight);
    cabinetRow.setBounds (deck.removeFromTop (pedalRowHeight).reduced (gutter, 2));

    auto footer = area.removeFromBottom (rowHeight + gutter).reduced (gutter, gutter / 2);
    ampRow.setBounds (footer.removeFromTop (rowHeight));

    LabelledKnob* controls[] { &gainKnob, &bassKnob, &midKnob, &trebleKnob, &masterKnob };

    auto knobs = area.reduced (gutter, 0);
    const auto width = knobs.getWidth() / (int) std::size (controls);

    // One row, centred in the plate rather than pinned to its top edge.
    const auto rowHeightNeeded = juce::jmin (knobs.getHeight(), width + 24 + 18);
    knobs = knobs.withSizeKeepingCentre (knobs.getWidth(), rowHeightNeeded);

    for (auto* knob : controls)
        knob->setBounds (knobs.removeFromLeft (width).reduced (6, 0));
}
