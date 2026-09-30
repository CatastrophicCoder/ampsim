/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PluginProcessor.h"
#include "BundledAssets.h"
#include "PluginEditor.h"

namespace
{
    constexpr float gainRangeDb    = 24.0f;
    constexpr float toneRangeDb    = 12.0f;
    constexpr double gainRampSeconds   = 0.05;
    constexpr double bypassRampSeconds = 0.02;

    /** One decimal, and a unit where there is one. Without a formatter JUCE prints the raw float,
        so a mix knob's value popup read 0.3499999 rather than 0.3. */
    /** A blend between two ends. "0.3" tells nobody anything about where a microphone is; a
        percentage at least says how far along it has been moved. */
    juce::AudioParameterFloatAttributes percentage()
    {
        return juce::AudioParameterFloatAttributes()
                   .withLabel ("%")
                   .withStringFromValueFunction ([] (float value, int)
                   {
                       return juce::String (juce::roundToInt (value * 100.0f)) + " %";
                   });
    }

    /** A frequency, to the nearest hertz below a kilohertz and to a tenth of one above: a cut at
        "8000 Hz" is a number nobody needs five digits of. */
    juce::AudioParameterFloatAttributes hertz()
    {
        return juce::AudioParameterFloatAttributes()
                   .withStringFromValueFunction ([] (float value, int)
                   {
                       if (value < 1000.0f)
                           return juce::String (juce::roundToInt (value)) + " Hz";

                       return juce::String (value / 1000.0f, 1) + " kHz";
                   });
    }

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

    /** A frequency control has to be logarithmic, or the whole useful part sits in the last
        eighth of the travel. */
    juce::NormalisableRange<float> frequencyRange (float low, float high, float centre)
    {
        juce::NormalisableRange<float> range { low, high };
        range.setSkewForCentre (centre);
        return range;
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
        juce::ParameterID { ParamID::presence, 1 }, "Presence",
        decibelRange (toneRangeDb), 0.0f, dbAttributes));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::depth, 1 }, "Depth",
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

    // On by default: a plugin that makes no sound until you find its power switch is a support
    // ticket. A session saved before this parameter existed also lands on the default.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::power, 1 }, "Power", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamID::transposeOn, 1 }, "Transpose", false));

    // Whole semitones, signed, so the reading says which way it has gone.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamID::transposeSemitones, 1 }, "Transpose Interval",
        -Transpose::maxSemitones, Transpose::maxSemitones, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int value, int)
        {
            if (value == 0)
                return juce::String ("0");

            return (value > 0 ? juce::String ("+") : juce::String ("-"))
                 + juce::String (std::abs (value));
        })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::micAxis, 1 }, "Mic Axis",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentage()));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::micDistance, 1 }, "Mic Distance",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f, percentage()));

    // Both default to the end of their travel, where they do nothing: a cabinet arrives as its
    // impulse response describes it, and these are there to be reached for.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::cabLowCut, 1 }, "Cab Low Cut",
        frequencyRange (CabSim::lowCutOffHz, 1000.0f, 120.0f), CabSim::lowCutOffHz, hertz()));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::cabHighCut, 1 }, "Cab High Cut",
        frequencyRange (1000.0f, CabSim::highCutOffHz, 6000.0f), CabSim::highCutOffHz, hertz()));

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
    // -20 dB is above anything a guitar produces once it has stopped being struck, so the old
    // top of the range was unusable and the useful part was squeezed into a third of the knob.
    addKnob (ParamID::gateThreshold, "Gate Threshold", -80.0f, -40.0f, -65.0f, "dB");

    addSwitch (ParamID::compOn, "Compressor");
    addKnob (ParamID::compAmount, "Comp Amount", 0.0f, 1.0f, 0.4f);
    addKnob (ParamID::compLevel, "Comp Level", -12.0f, 12.0f, 0.0f, "dB");

    addSwitch (ParamID::driveOn, "Dirt");

    // Distortion first, because that is what the slot held when it held one pedal — a session
    // saved before the slot existed lands on it and keeps its knob positions.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::dirtType, 1 }, "Dirt Pedal",
        juce::StringArray { "Distortion", "Overdrive", "Fuzz", "Clean Boost" }, 0));
    addKnob (ParamID::driveAmount, "Drive", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::driveTone, "Drive Tone", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::driveLevel, "Drive Level", -12.0f, 12.0f, 0.0f, "dB");

    addKnob (ParamID::odAmount, "Overdrive", 0.0f, 1.0f, 0.4f);
    addKnob (ParamID::odTone, "Overdrive Tone", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::odLevel, "Overdrive Level", -12.0f, 12.0f, 0.0f, "dB");

    addKnob (ParamID::fuzzAmount, "Fuzz", 0.0f, 1.0f, 0.6f);
    addKnob (ParamID::fuzzTone, "Fuzz Tone", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::fuzzLevel, "Fuzz Level", -12.0f, 12.0f, -6.0f, "dB");

    addKnob (ParamID::boostLevel, "Boost", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::boostTone, "Boost Tone", 0.0f, 1.0f, 0.5f);

    addSwitch (ParamID::chorusOn, "Modulation");

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::modulationType, 1 }, "Modulation Pedal",
        juce::StringArray { "Chorus", "Flanger", "Phaser", "Tremolo" }, 0));
    addKnob (ParamID::chorusRate, "Chorus Rate", 0.1f, 8.0f, 1.2f, "Hz");
    addKnob (ParamID::chorusDepth, "Chorus Depth", 0.0f, 1.0f, 0.35f);
    addKnob (ParamID::chorusMix, "Chorus Mix", 0.0f, 1.0f, 0.4f);

    addKnob (ParamID::flangerRate, "Flanger Rate", 0.05f, 6.0f, 0.4f, "Hz");
    addKnob (ParamID::flangerDepth, "Flanger Depth", 0.0f, 1.0f, 0.6f);
    addKnob (ParamID::flangerFeedback, "Flanger Feedback", 0.0f, 0.9f, 0.5f);
    addKnob (ParamID::flangerMix, "Flanger Mix", 0.0f, 1.0f, 0.5f);

    addKnob (ParamID::phaserRate, "Phaser Rate", 0.05f, 8.0f, 0.6f, "Hz");
    addKnob (ParamID::phaserDepth, "Phaser Depth", 0.0f, 1.0f, 0.7f);
    addKnob (ParamID::phaserFeedback, "Phaser Feedback", 0.0f, 0.9f, 0.4f);
    addKnob (ParamID::phaserMix, "Phaser Mix", 0.0f, 1.0f, 0.5f);

    addKnob (ParamID::tremoloRate, "Tremolo Rate", 0.5f, 14.0f, 5.0f, "Hz");
    addKnob (ParamID::tremoloDepth, "Tremolo Depth", 0.0f, 1.0f, 0.5f);
    addKnob (ParamID::tremoloShape, "Tremolo Shape", 0.0f, 1.0f, 0.0f);

    addSwitch (ParamID::delayOn, "Delay");
    // Milliseconds, not seconds to one decimal: the shortest setting is 20 ms, which would read
    // "0.0 s" — a number that is wrong rather than merely coarse.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamID::delayTime, 1 }, "Delay Time",
        juce::NormalisableRange<float> { 0.02f, DelayPedal::maxKnobSeconds }, 0.35f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float seconds, int)
            {
                return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";
            })));
    // "Free" first, so a session saved before this existed lands on it and the delay behaves
    // exactly as it did. The lengths run long to short, the way a delay's time knob does.
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamID::delayDivision, 1 }, "Delay Sync",
        juce::StringArray { "Free", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16" }, 0));

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
    powerParam      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter (ParamID::power));
    transposeParam  = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter (ParamID::transposeOn));
    semitonesParam  = dynamic_cast<juce::AudioParameterInt*>   (apvts.getParameter (ParamID::transposeSemitones));
    micAxisParam    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::micAxis));
    micDistanceParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::micDistance));
    presenceParam   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::presence));
    depthParam      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::depth));
    cabLowCutParam  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::cabLowCut));
    cabHighCutParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::cabHighCut));
    bassParam       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::bass));
    midParam        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::mid));
    trebleParam     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamID::treble));

    jassert (inputGainParam != nullptr && outputGainParam != nullptr
             && bypassParam != nullptr && cabBypassParam != nullptr
             && bassParam != nullptr && midParam != nullptr && trebleParam != nullptr
             && tunerParam != nullptr && powerParam != nullptr
             && transposeParam != nullptr && semitonesParam != nullptr);

    modelLoader.onFinished = [this] (ModelLoader::Result)
    {
        if (onLoadStateChanged != nullptr)
            onLoadStateChanged();
    };

    // The built-in amp and cab, written out on first run so a fresh instance makes a sound
    // rather than passing audio through untouched. A session that names its own files replaces
    // these when its state is restored.
    if (loadBundledAssetsOnCreation && BundledAssets::install().isEmpty())
    {
        // Land on the Default preset rather than merely on the files it names, so the panel says
        // which preset is loaded instead of showing nothing chosen.
        presets.createFactoryPresetsIfMissing();

        if (presets.load ("Default").isNotEmpty())
        {
            // No preset folder to write to: fall back to loading the files directly.
            if (const auto model = BundledAssets::ampModel(); model != juce::File())
                loadModel (model);

            if (const auto cab = BundledAssets::cabinetImpulseResponse(); cab != juce::File())
                loadImpulseResponse (CabSim::Slot::centreClose, cab);
        }
    }

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

    outputMute.reset (sampleRate, 0.03);
    outputMute.setCurrentAndTargetValue (mutedNow() ? 0.0f : 1.0f);

    pedals.prepare (sampleRate, samplesPerBlock);
    pedals.setSettings (currentPedalSettings());
    pedals.snapToSettings();
    pedals.reset();

    toneStack.prepare (sampleRate, samplesPerBlock);
    toneStack.setBandGains (bassParam->get(), midParam->get(), trebleParam->get(),
                            presenceParam->get(), depthParam->get());
    toneStack.snapToTargets();   // as with the gains: do not sweep in from flat on every start
    toneStack.reset();

    transpose.prepare (sampleRate, samplesPerBlock);
    transpose.setSemitones (semitonesParam->get());
    transpose.snapBypass (! transposeParam->get());

    cabSim.prepare (sampleRate, samplesPerBlock);
    cabSim.setCutoffs (cabLowCutParam->get(), cabHighCutParam->get());
    cabSim.snapCutoffs();       // as with the tone stack: do not sweep in from the ends on a start
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

    // Each slot reads the knobs of whatever is in it. Every type keeps its own parameters rather
    // than sharing a set of anonymous ones, so a host shows "Phaser Rate" instead of "Slot 2
    // Knob 1", a MIDI mapping stays with the pedal it was made for, and switching type does not
    // silently move the settings of the one you switched away from.
    s.driveEngaged = flag (ParamID::driveOn);
    s.dirtType = (DirtPedal::Type) (int) value (ParamID::dirtType);

    switch (s.dirtType)
    {
        case DirtPedal::Type::overdrive:
            s.driveAmount = value (ParamID::odAmount);
            s.driveTone = value (ParamID::odTone);
            s.driveLevelDb = value (ParamID::odLevel);
            break;

        case DirtPedal::Type::fuzz:
            s.driveAmount = value (ParamID::fuzzAmount);
            s.driveTone = value (ParamID::fuzzTone);
            s.driveLevelDb = value (ParamID::fuzzLevel);
            break;

        case DirtPedal::Type::cleanBoost:
            s.driveAmount = value (ParamID::boostLevel);
            s.driveTone = value (ParamID::boostTone);
            s.driveLevelDb = 0.0f;
            break;

        case DirtPedal::Type::distortion:
        default:
            s.driveAmount = value (ParamID::driveAmount);
            s.driveTone = value (ParamID::driveTone);
            s.driveLevelDb = value (ParamID::driveLevel);
            break;
    }

    s.chorusEngaged = flag (ParamID::chorusOn);
    s.modulationType = (ModulationPedal::Type) (int) value (ParamID::modulationType);

    switch (s.modulationType)
    {
        case ModulationPedal::Type::flanger:
            s.chorusRateHz = value (ParamID::flangerRate);
            s.chorusDepth = value (ParamID::flangerDepth);
            s.modulationFeedback = value (ParamID::flangerFeedback);
            s.chorusMix = value (ParamID::flangerMix);
            break;

        case ModulationPedal::Type::phaser:
            s.chorusRateHz = value (ParamID::phaserRate);
            s.chorusDepth = value (ParamID::phaserDepth);
            s.modulationFeedback = value (ParamID::phaserFeedback);
            s.chorusMix = value (ParamID::phaserMix);
            break;

        case ModulationPedal::Type::tremolo:
            s.chorusRateHz = value (ParamID::tremoloRate);
            s.chorusDepth = value (ParamID::tremoloDepth);
            s.modulationFeedback = value (ParamID::tremoloShape);
            s.chorusMix = 1.0f;
            break;

        case ModulationPedal::Type::chorus:
        default:
            s.chorusRateHz = value (ParamID::chorusRate);
            s.chorusDepth = value (ParamID::chorusDepth);
            s.modulationFeedback = 0.0f;
            s.chorusMix = value (ParamID::chorusMix);
            break;
    }

    s.delayEngaged = flag (ParamID::delayOn);
    s.delayTimeSeconds = currentDelaySeconds();
    s.delayFeedback = value (ParamID::delayFeedback);
    s.delayMix = value (ParamID::delayMix);

    s.reverbEngaged = flag (ParamID::reverbOn);
    s.reverbSize = value (ParamID::reverbSize);
    s.reverbMix = value (ParamID::reverbMix);

    return s;
}

float AmpSimAudioProcessor::currentDelaySeconds() const
{
    const auto division = (int) apvts.getRawParameterValue (ParamID::delayDivision)->load();

    if (division <= 0)
        return apvts.getRawParameterValue (ParamID::delayTime)->load();

    // Each note length as a multiple of a quarter, in the order the choices are listed.
    static constexpr float ofAQuarter[] { 0.0f, 2.0f, 1.5f, 1.0f, 2.0f / 3.0f,
                                          0.75f, 0.5f, 1.0f / 3.0f, 0.25f };

    if (division >= (int) std::size (ofAQuarter))
        return apvts.getRawParameterValue (ParamID::delayTime)->load();

    // A host that reports no tempo — a standalone, or one stopped before it has played anything —
    // still has to give a delay that sounds like something, so it gets the tempo most people
    // would have guessed.
    auto beatsPerMinute = 120.0;

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto hostBpm = position->getBpm())
                beatsPerMinute = *hostBpm;

    const auto quarterSeconds = 60.0 / juce::jmax (20.0, beatsPerMinute);

    // Long note lengths run past what the line can hold at slow tempos — a half note needs two
    // seconds at 60 BPM — so the clamp is what the delay actually does rather than a surprise.
    return juce::jlimit (0.02f, DelayPedal::maxDelaySeconds,
                         (float) (quarterSeconds * ofAQuarter[division]));
}

void AmpSimAudioProcessor::timerCallback()
{
    ampModel.collectRetiredModel();

    // A model whose load finished before prepareToPlay was sized for nothing; this re-sizes it.
    ampModel.repreparePendingModelIfNeeded();

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

    // The tuner reads the guitar itself, so it taps in before anything shapes the signal — and
    // before the transpose, so it goes on telling you about the strings rather than about the
    // interval you have asked for. Tuning to a transposed reading would put the guitar out.
    tuner.pushSamples (mono, numSamples);

    // Retuning happens first, so everything downstream — the gate's key, the amp, the cab — sees
    // the note you meant to play rather than the one the strings made.
    transpose.setSemitones (semitonesParam->get());
    transpose.process (mono, numSamples, ! transposeParam->get());

    // Pedals in front of the amp, then the amp's own Gain — the order a real rig is plugged up
    // in, with the board going into the amp's input rather than the other way round.
    pedals.setSettings (currentPedalSettings());
    pedals.processBeforeAmp (mono, numSamples);

    // Gain, before the model, because driving the network harder is what makes it saturate.
    inputGain.process (monoContext);

    ampModel.process (mono, numSamples);

    // Bass / Mid / Treble after the model and before the cab, where an amp's tone stack sits
    // relative to its speaker.
    toneStack.setBandGains (bassParam->get(), midParam->get(), trebleParam->get(),
                            presenceParam->get(), depthParam->get());
    toneStack.process (mono, numSamples);

    // Master last in the amp, before the cab. Convolution is linear, so this is the same level
    // as applying it after the cab — it is here because that is where the control belongs.
    outputGain.process (monoContext);

    // Modulation and time effects after the amp but before the cab, so their tails run through
    // the speaker response the way they would coming out of a real cabinet.
    pedals.processAfterAmp (mono, numSamples);

    cabSim.setMicPosition (micAxisParam->get(), micDistanceParam->get());
    cabSim.setCutoffs (cabLowCutParam->get(), cabHighCutParam->get());
    cabSim.process (mono, numSamples, cabBypassParam->get());

    // Muting happens last, so everything upstream keeps running and the chain does not have to
    // settle again when you switch back. The power switch mutes here rather than stopping the
    // chain for the same reason, and because a tail that was already in the air should die away
    // with the ramp instead of being cut.
    outputMute.setTargetValue (mutedNow() ? 0.0f : 1.0f);

    if (outputMute.isSmoothing() || outputMute.getCurrentValue() < 1.0f)
    {
        const auto start = outputMute.getCurrentValue();
        outputMute.skip (numSamples);

        juce::AudioBuffer<float> view (&mono, 1, numSamples);
        view.applyGainRamp (0, 0, numSamples, start, outputMute.getCurrentValue());
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
    // How big the window is, is not something a preset gets to change — and a session restore
    // carries its own, so only an absent one is filled in.
    if (! newState.hasProperty (StateID::panelScale) && apvts.state.hasProperty (StateID::panelScale))
        newState.setProperty (StateID::panelScale, apvts.state.getProperty (StateID::panelScale), nullptr);

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
