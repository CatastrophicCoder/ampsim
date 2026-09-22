#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float gainRangeDb    = 24.0f;
    constexpr float toneRangeDb    = 12.0f;
    constexpr double gainRampSeconds   = 0.05;
    constexpr double bypassRampSeconds = 0.02;

    /** One decimal, and a unit where there is one. Without a formatter JUCE prints the raw float,
        so a mix knob's value popup read 0.3499999 rather than 0.3. */
    juce::AudioParameterFloatAttributes oneDecimal (const juce::String& unit = {})
    {
        return juce::AudioParameterFloatAttributes()
                   .withLabel (unit)
                   .withStringFromValueFunction ([unit] (float value, int)
                   {
                       return juce::String (value, 1)
                            + (unit.isEmpty() ? juce::String() : " " + unit);
                   });
    }

    juce::NormalisableRange<float> decibelRange (float limit)
    {
        juce::NormalisableRange<float> range { -limit, limit, 0.1f };
        range.setSkewForCentre (0.0f);   // 0 dB sits in the middle of the knob's travel
        return range;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AmpSimAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto dbAttributes = juce::AudioParameterFloatAttributes()
                                  .withLabel ("dB")
                                  .withStringFromValueFunction ([] (float v, int)
                                                                { return juce::String (v, 1) + " dB"; });

    // Gain drives the model: more level in means more saturation out, which is how a real
    // preamp gain control works too.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::inputGain, 1 }, "Gain",
        decibelRange (gainRangeDb), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::bass, 1 }, "Bass",
        decibelRange (toneRangeDb), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::mid, 1 }, "Mid",
        decibelRange (toneRangeDb), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::treble, 1 }, "Treble",
        decibelRange (toneRangeDb), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::outputGain, 1 }, "Master",
        decibelRange (gainRangeDb), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::bypass, 1 }, "Bypass", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::cabBypass, 1 }, "Cab Bypass", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::tunerOn, 1 }, "Tuner", false));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::micAxis, 1 }, "Mic Axis",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, oneDecimal()));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::micDistance, 1 }, "Mic Distance",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, oneDecimal()));

    // --- Pedals -------------------------------------------------------------------------------
    const auto addSwitch = [&layout] (const char* id, const juce::String& name)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 },
                                                                name, false));
    };

    const auto addKnob = [&layout] (const char* id, const juce::String& name,
                                    float minimum, float maximum, float defaultValue,
                                    const juce::String& unit = {})
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { minimum, maximum }, defaultValue,
            oneDecimal (unit)));
    };

    addSwitch (ParamID::gateOn, "Gate");
    addKnob (ParamID::gateThreshold, "Gate Threshold", -80.0f, -20.0f, -60.0f, "dB");

    addSwitch (ParamID::compOn, "Compressor");
    addKnob (ParamID::compAmount, "Comp Amount", 0.0f, 1.0f, 0.4f);
    addKnob (ParamID::compLevel, "Comp Level", -12.0f, 12.0f, 0.0f, "dB");

    addSwitch (ParamID::driveOn, "Drive");
    addKnob (ParamID::driveAmount, "Drive", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::driveTone, "Drive Tone", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::driveLevel, "Drive Level", -12.0f, 12.0f, 0.0f, "dB");

    addSwitch (ParamID::chorusOn, "Chorus");
    addKnob (ParamID::chorusRate, "Chorus Rate", 0.1f, 8.0f, 1.2f, "Hz");
    addKnob (ParamID::chorusDepth, "Chorus Depth", 0.0f, 1.0f, 0.35f);
    addKnob (ParamID::chorusMix, "Chorus Mix", 0.0f, 1.0f, 0.4f);

    addSwitch (ParamID::delayOn, "Delay");
    // Milliseconds, not seconds to one decimal: the shortest setting is 20 ms, which would read
    // "0.0 s" — a number that is wrong rather than merely coarse.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::delayTime, 1 }, "Delay Time",
        juce::NormalisableRange<float> { 0.02f, DelayPedal::maxDelaySeconds }, 0.35f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float seconds, int)
            {
                return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
            })));
    addKnob (ParamID::delayFeedback, "Delay Feedback", 0.0f, 0.95f, 0.35f);
    addKnob (ParamID::delayMix, "Delay Mix", 0.0f, 1.0f, 0.3f);

    addSwitch (ParamID::reverbOn, "Reverb");
    addKnob (ParamID::reverbSize, "Reverb Size", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::reverbMix, "Reverb Mix", 0.0f, 1.0f, 0.25f);

    return layout;
}

AmpSimAudioProcessor::AmpSimAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AmpSim", createParameterLayout())
{
    inputGainParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::inputGain));
    outputGainParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::outputGain));
    bypassParam     = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter (ParamID::bypass));
    cabBypassParam  = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter (ParamID::cabBypass));
    tunerParam      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter (ParamID::tunerOn));
    micAxisParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::micAxis));
    micDistanceParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::micDistance));
    bassParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::bass));
    midParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::mid));
    trebleParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::treble));

    jassert (inputGainParam != nullptr && outputGainParam != nullptr
             && bypassParam != nullptr && cabBypassParam != nullptr
             && bassParam != nullptr && midParam != nullptr && trebleParam != nullptr
             && tunerParam != nullptr);

    modelLoader.onFinished = [this] (ModelLoader::Result)
    {
        if (onLoadStateChanged != nullptr)
            onLoadStateChanged();
    };

    // Reaps models the audio thread has swapped out, and keeps the reported latency in step
    // with whatever model is now running.
    startTimer (200);
}

void AmpSimAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (samplesPerBlock),
                                        static_cast<juce::uint32> (getTotalNumOutputChannels()) };

    for (auto* gain : { &inputGain, &outputGain })
    {
        gain->prepare (spec);
        gain->setRampDurationSeconds (gainRampSeconds);
    }

    // A freshly constructed juce::dsp::Gain sits at 0, so setting the target alone would make
    // the plugin fade in over the ramp duration every time the host starts playback. Snap to
    // the parameter values instead: reset() pulls the smoother's current value up to its target.
    inputGain .setGainDecibels (inputGainParam->get());
    outputGain.setGainDecibels (outputGainParam->get());
    inputGain .reset();
    outputGain.reset();

    bypassMix.reset (sampleRate, bypassRampSeconds);
    bypassMix.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    dryBuffer.setSize (getTotalNumOutputChannels(), samplesPerBlock, false, false, true);
    monoBuffer.setSize (1, samplesPerBlock, false, false, true);

    ampModel.prepare (sampleRate, samplesPerBlock);
    ampModel.reset();

    tuner.prepare (sampleRate);
    tuner.reset();

    tunerMute.reset (sampleRate, 0.03);
    tunerMute.setCurrentAndTargetValue (tunerParam->get() ? 0.0f : 1.0f);

    pedals.prepare (sampleRate, samplesPerBlock);
    pedals.setSettings (currentPedalSettings());
    pedals.snapToSettings();
    pedals.reset();

    toneStack.prepare (sampleRate, samplesPerBlock);
    toneStack.setBandGains (bassParam->get(), midParam->get(), trebleParam->get());
    toneStack.snapToTargets();   // as with the gains: do not sweep in from flat on every start
    toneStack.reset();

    cabSim.prepare (sampleRate, samplesPerBlock);
    cabSim.reset();

    reportedLatency = ampModel.getLatencySamples() + cabSim.getLatencySamples()
                    + pedals.getLatencySamples();
    setLatencySamples (reportedLatency);
}

void AmpSimAudioProcessor::releaseResources()
{
    dryBuffer.setSize (0, 0);
    monoBuffer.setSize (0, 0);
}

PedalChain::Settings AmpSimAudioProcessor::currentPedalSettings() const
{
    const auto flag = [this] (const char* id)
    {
        return apvts.getRawParameterValue (id)->load() > 0.5f;
    };

    const auto value = [this] (const char* id)
    {
        return apvts.getRawParameterValue (id)->load();
    };

    PedalChain::Settings s;

    s.gateEngaged = flag (ParamID::gateOn);
    s.gateThresholdDb = value (ParamID::gateThreshold);

    s.compressorEngaged = flag (ParamID::compOn);
    s.compressorAmount = value (ParamID::compAmount);
    s.compressorLevelDb = value (ParamID::compLevel);

    s.driveEngaged = flag (ParamID::driveOn);
    s.driveAmount = value (ParamID::driveAmount);
    s.driveTone = value (ParamID::driveTone);
    s.driveLevelDb = value (ParamID::driveLevel);

    s.chorusEngaged = flag (ParamID::chorusOn);
    s.chorusRateHz = value (ParamID::chorusRate);
    s.chorusDepth = value (ParamID::chorusDepth);
    s.chorusMix = value (ParamID::chorusMix);

    s.delayEngaged = flag (ParamID::delayOn);
    s.delayTimeSeconds = value (ParamID::delayTime);
    s.delayFeedback = value (ParamID::delayFeedback);
    s.delayMix = value (ParamID::delayMix);

    s.reverbEngaged = flag (ParamID::reverbOn);
    s.reverbSize = value (ParamID::reverbSize);
    s.reverbMix = value (ParamID::reverbMix);

    return s;
}

void AmpSimAudioProcessor::timerCallback()
{
    ampModel.collectRetiredModel();

    // The audio thread captured a controller to learn; writing it into the state is this
    // thread's job, since a ValueTree may only be touched from one.
    midiLearn.commitPendingLearn();

    const auto latency = ampModel.getLatencySamples() + cabSim.getLatencySamples()
                       + pedals.getLatencySamples();

    if (latency != reportedLatency)
    {
        reportedLatency = latency;
        setLatencySamples (latency);
    }
}

void AmpSimAudioProcessor::loadModel (const juce::File& file)
{
    // Recorded now rather than on success, so the session remembers which model it wants even if
    // the file is missing on this machine — the user can then put it back.
    apvts.state.setProperty (StateID::modelPath, file.getFullPathName(), nullptr);

    modelLoader.loadAsync (file);
}

juce::File AmpSimAudioProcessor::getModelFile() const
{
    const auto path = apvts.state.getProperty (StateID::modelPath).toString();

    return path.isEmpty() ? juce::File() : juce::File (path);
}

const char* AmpSimAudioProcessor::slotStateID (CabSim::Slot slot)
{
    switch (slot)
    {
        case CabSim::Slot::edgeClose: return StateID::irPathEdgeClose;
        case CabSim::Slot::centreFar: return StateID::irPathCentreFar;
        case CabSim::Slot::edgeFar:   return StateID::irPathEdgeFar;
        case CabSim::Slot::centreClose:
        case CabSim::Slot::count:
        default:                      return StateID::irPath;
    }
}

void AmpSimAudioProcessor::clearImpulseResponse (CabSim::Slot slot)
{
    apvts.state.setProperty (slotStateID (slot), juce::String(), nullptr);
    cabSim.clearSlot (slot);

    if (onLoadStateChanged != nullptr)
        onLoadStateChanged();
}

void AmpSimAudioProcessor::loadImpulseResponse (CabSim::Slot slot, const juce::File& file)
{
    apvts.state.setProperty (slotStateID (slot), file.getFullPathName(), nullptr);

    // juce::dsp::Convolution ignores a file it cannot read, which would leave the UI claiming an
    // IR that is not there. Check it here so a bad file can be reported instead.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        irError = file.existsAsFile() ? "Not an audio file: " + file.getFileName()
                                      : "File not found: " + file.getFileName();
    }
    else
    {
        irError.clear();
        cabSim.loadImpulseResponse (slot, file);
    }

    if (onLoadStateChanged != nullptr)
        onLoadStateChanged();
}

juce::File AmpSimAudioProcessor::getImpulseResponseFile (CabSim::Slot slot) const
{
    const auto path = apvts.state.getProperty (slotStateID (slot)).toString();

    return path.isEmpty() ? juce::File() : juce::File (path);
}

bool AmpSimAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // The chain is mono until the cab; mono-in/stereo-out is allowed so a stereo
    // modulation stage can widen it later without a layout change.
    return layouts.getMainInputChannelSet() == out
        || (layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
            && out == juce::AudioChannelSet::stereo());
}

void AmpSimAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Before anything else, so a controller move takes effect on the block it arrived in.
    midiLearn.processMidi (midi);

    const auto numSamples  = buffer.getNumSamples();
    const auto numChannels = getTotalNumOutputChannels();

    for (auto ch = getTotalNumInputChannels(); ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    bypassMix.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    // Settled at fully bypassed: the dry signal is already in the buffer, so there is
    // nothing to do. Keep the gains' ramps in step for when bypass comes off again.
    if (! bypassMix.isSmoothing() && bypassMix.getCurrentValue() >= 1.0f)
    {
        inputGain .setGainDecibels (inputGainParam->get());
        outputGain.setGainDecibels (outputGainParam->get());
        return;
    }

    const bool needsCrossfade = bypassMix.isSmoothing() || bypassMix.getCurrentValue() > 0.0f;

    if (needsCrossfade)
    {
        jassert (dryBuffer.getNumSamples() >= numSamples);   // prepareToPlay sized it

        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    // The chain is mono: a guitar amp is, and NAM is. A stereo input is summed in, and the
    // result goes back out to every channel. Everything after this point works on that one signal.
    auto* mono = monoBuffer.getWritePointer (0);
    juce::FloatVectorOperations::copy (mono, buffer.getReadPointer (0), numSamples);

    for (int ch = 1; ch < getTotalNumInputChannels(); ++ch)
        juce::FloatVectorOperations::add (mono, buffer.getReadPointer (ch), numSamples);

    if (getTotalNumInputChannels() > 1)
        juce::FloatVectorOperations::multiply (mono, 1.0f / (float) getTotalNumInputChannels(), numSamples);

    juce::dsp::AudioBlock<float> monoBlock (&mono, 1, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> monoContext (monoBlock);

    inputGain .setGainDecibels (inputGainParam->get());
    outputGain.setGainDecibels (outputGainParam->get());

    // The tuner reads the guitar itself, so it taps in before anything shapes the signal.
    tuner.pushSamples (mono, numSamples);

    // Pedals in front of the amp, then the amp's own Gain — the order a real rig is plugged up
    // in, with the board going into the amp's input rather than the other way round.
    pedals.setSettings (currentPedalSettings());
    pedals.processBeforeAmp (mono, numSamples);

    // Gain, before the model, because driving the network harder is what makes it saturate.
    inputGain.process (monoContext);

    ampModel.process (mono, numSamples);

    // Bass / Mid / Treble after the model and before the cab, where an amp's tone stack sits
    // relative to its speaker.
    toneStack.setBandGains (bassParam->get(), midParam->get(), trebleParam->get());
    toneStack.process (mono, numSamples);

    // Master last in the amp, before the cab. Convolution is linear, so this is the same level
    // as applying it after the cab — it is here because that is where the control belongs.
    outputGain.process (monoContext);

    // Modulation and time effects after the amp but before the cab, so their tails run through
    // the speaker response the way they would coming out of a real cabinet.
    pedals.processAfterAmp (mono, numSamples);

    cabSim.setMicPosition (micAxisParam->get(), micDistanceParam->get());
    cabSim.process (mono, numSamples, cabBypassParam->get());

    // Muting for the tuner happens last, so everything upstream keeps running and the chain does
    // not have to settle again when you switch back.
    tunerMute.setTargetValue (tunerParam->get() ? 0.0f : 1.0f);

    if (tunerMute.isSmoothing() || tunerMute.getCurrentValue() < 1.0f)
    {
        const auto start = tunerMute.getCurrentValue();
        tunerMute.skip (numSamples);

        juce::AudioBuffer<float> view (&mono, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, start, tunerMute.getCurrentValue());
    }

    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), mono, numSamples);

    if (needsCrossfade)
    {
        // The smoothing is linear, so the ramp over this block is fully described by its
        // endpoints — let JUCE apply it per channel instead of stepping sample by sample.
        const auto startMix = bypassMix.getCurrentValue();
        bypassMix.skip (numSamples);
        const auto endMix = bypassMix.getCurrentValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.applyGainRamp (ch, 0, numSamples, 1.0f - startMix, 1.0f - endMix);
            buffer.addFromWithRamp (ch, 0, dryBuffer.getReadPointer (ch), numSamples, startMix, endMix);
        }
    }
}

juce::AudioProcessorEditor* AmpSimAudioProcessor::createEditor()
{
    return new AmpSimAudioProcessorEditor (*this);
}

void AmpSimAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpSimAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // A session restore is the whole picture, so empty means empty.
    applyState (juce::ValueTree::fromXml (*xml), false);
}

void AmpSimAudioProcessor::applyPresetState (const juce::ValueTree& preset)
{
    if (preset.isValid())
        applyState (preset.createCopy(), true);
}

void AmpSimAudioProcessor::applyState (juce::ValueTree newState, bool keepLoadedFilesWhenEmpty)
{
    if (keepLoadedFilesWhenEmpty)
    {
        // A preset saved on another machine cannot know where your models live, and a preset that
        // only sets the controls should not unload your amp. Carry the current paths across
        // wherever the incoming state has none.
        const auto carryOver = [this, &newState] (const char* id)
        {
            if (newState.getProperty (id).toString().isEmpty())
                newState.setProperty (id, apvts.state.getProperty (id), nullptr);
        };

        carryOver (StateID::modelPath);

        for (int slot = 0; slot < CabSim::numSlots; ++slot)
            carryOver (slotStateID ((CabSim::Slot) slot));
    }

    // Only the parameters are touched here. This runs on the message thread, so the DSP
    // objects are left alone — processBlock picks the new values up on its next call and
    // ramps to them, which is also what stops a preset change from clicking.
    apvts.replaceState (newState);

    midiLearn.rebuildFromState();

    if (const auto file = getModelFile(); file != juce::File())
        loadModel (file);

    for (int slot = 0; slot < CabSim::numSlots; ++slot)
    {
        const auto slotToLoad = (CabSim::Slot) slot;
        const auto ir = getImpulseResponseFile (slotToLoad);

        if (ir != juce::File())
            cabSim.loadImpulseResponse (slotToLoad, ir);
        else
            cabSim.clearSlot (slotToLoad);
    }

    if (onLoadStateChanged != nullptr)
        onLoadStateChanged();
}

juce::String AmpSimAudioProcessor::getCurrentPresetName() const
{
    return apvts.state.getProperty (StateID::presetName).toString();
}

void AmpSimAudioProcessor::setCurrentPresetName (const juce::String& name)
{
    apvts.state.setProperty (StateID::presetName, name, nullptr);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmpSimAudioProcessor();
}
