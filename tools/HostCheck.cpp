/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

/*
    A host, not the plugin.

    Everything else here — the Catch2 suite, the benchmark — drives AmpSimAudioProcessor
    directly, which skips the AU and VST3 wrappers entirely. This loads the installed bundles the
    way Logic does, through juce::AudioUnitPluginFormat and juce::VST3PluginFormat, and reports
    what each one says about itself and what comes out of it.

    That layer is where a plugin can be wrong while every other test passes: the AU's type and
    category, the bus counts the host is offered, the latency the host is told to compensate, and
    whether audio survives the wrapper at all.

    Build with -DAMPSIM_BUILD_TOOLS=ON, then run it; it takes no arguments.
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>

namespace
{
    void measure (juce::AudioPluginInstance& plugin, double rate, int blockSize,
                  const juce::String& label)
    {
        plugin.setNonRealtime (false);
        plugin.prepareToPlay (rate, blockSize);

        const auto channels = juce::jmax (plugin.getTotalNumInputChannels(),
                                          plugin.getTotalNumOutputChannels(), 2);

        juce::AudioBuffer<float> buffer (channels, blockSize);
        juce::MidiBuffer midi;

        // Let the model's loader thread finish and the processor's 200 ms timer run.
        for (int i = 0; i < 120; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            buffer.clear();
            plugin.processBlock (buffer, midi);
        }

        double inSum = 0.0, outSum = 0.0;
        long n = 0;
        int index = 0;
        const auto blocks = (int) (rate * 3.0 / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();

            for (int ch = 0; ch < plugin.getTotalNumInputChannels(); ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                          * 220.0 * (index + i) / rate));

            if (b >= blocks / 2)
                for (int i = 0; i < blockSize; ++i) { inSum += std::pow (buffer.getSample (0, i), 2.0); ++n; }

            index += blockSize;
            plugin.processBlock (buffer, midi);

            if (b >= blocks / 2)
                for (int i = 0; i < blockSize; ++i) outSum += std::pow (buffer.getSample (0, i), 2.0);
        }

        const auto in  = std::sqrt (inSum / (double) n);
        const auto out = std::sqrt (outSum / (double) n);

        // What it costs in real time, through the wrapper and with the model prepared for this
        // rate — which is the only place the resampler is actually in the signal.
        std::vector<double> passes;

        for (int pass = 0; pass < 7; ++pass)
        {
            const auto start = std::chrono::steady_clock::now();

            for (int b = 0; b < blocks; ++b)
            {
                // Refilled every block. Left alone, the buffer carries the previous block's
                // output back in as input, which through a distorting amp is a feedback loop and
                // measures something other than the plugin.
                for (int ch = 0; ch < plugin.getTotalNumInputChannels(); ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (ch, i, 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                              * 220.0 * (b * blockSize + i) / rate));

                plugin.processBlock (buffer, midi);
            }

            const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            passes.push_back (elapsed.count() / ((double) (blocks * blockSize) / rate) * 100.0);
        }

        std::sort (passes.begin(), passes.end());

        std::cout << "    " << label << "  " << (int) rate << " Hz  " << blockSize << " spl"
                  << "  in=" << juce::String (in, 4) << "  out=" << juce::String (out, 4)
                  << "  (" << juce::String (juce::Decibels::gainToDecibels (out / juce::jmax (1.0e-9, in)), 1) << " dB)"
                  << "  latency=" << plugin.getLatencySamples()
                  << "  cpu=" << juce::String (passes[passes.size() / 2], 2) << "%"
                  << (out < 1.0e-4 ? "   <-- SILENT" : "")
                  << "\n";
        std::cout.flush();
    }

    void tryFormat (juce::AudioPluginFormat& format, const juce::String& path)
    {
        std::cout << "  " << format.getName() << ": " << path << "\n";

        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, path);

        if (found.isEmpty()) { std::cout << "    NOT FOUND by this format\n"; return; }

        for (auto* desc : found)
        {
            std::cout << "    \"" << desc->name << "\"  " << desc->pluginFormatName
                      << "  category=" << desc->category
                      << "  ins=" << desc->numInputChannels
                      << "  outs=" << desc->numOutputChannels
                      << "  midi=" << (desc->isInstrument ? "instrument" : "effect") << "\n";

            juce::String error;
            std::unique_ptr<juce::AudioPluginInstance> plugin (
                format.createInstanceFromDescription (*desc, 48000.0, 512, error));

            if (plugin == nullptr) { std::cout << "    COULD NOT INSTANTIATE: " << error << "\n"; continue; }

            // What the plugin says its controls are set to the moment a host loads it. A muted
            // power switch or an engaged tuner would be silence that nothing else here explains.
            // A hosted instance's parameters are the host's own wrappers, not ours, so they are
            // matched by name rather than by ID.
            std::cout << "    defaults:";
            for (auto* param : plugin->getParameters())
            {
                const auto name = param->getName (32);

                if (name == "Power" || name == "Bypass" || name == "Tuner" || name == "Master")
                    std::cout << "  " << name << "=" << param->getCurrentValueAsText();
            }
            std::cout << "\n";

            // The layouts a Logic track actually asks for. The default is stereo in and out; a
            // mono audio track is the case nothing here had tried through a wrapper.
            const std::pair<int, int> layouts[] { { 2, 2 }, { 1, 1 }, { 1, 2 } };

            for (const auto [ins, outs] : layouts)
            {
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses .add (ins  == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                layout.outputBuses.add (outs == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());

                const auto label = juce::String (ins) + "in/" + juce::String (outs) + "out";

                if (! plugin->setBusesLayout (layout))
                {
                    std::cout << "    " << label << "  REFUSED by the plugin\n";
                    continue;
                }

                for (const auto rate : { 44100.0, 48000.0 })
                {
                    for (const auto block : { 128, 512 })
                    {
                        // A fresh instance each time. Re-preparing one instance for a new rate
                        // leaves the model prepared for the old one, and the audio thread refuses
                        // a model that does not match what it was sized for.
                        juce::String e;
                        std::unique_ptr<juce::AudioPluginInstance> fresh (
                            format.createInstanceFromDescription (*desc, rate, block, e));

                        if (fresh == nullptr) { std::cout << "    could not re-instantiate: " << e << "\n"; continue; }

                        fresh->setBusesLayout (layout);
                        measure (*fresh, rate, block, label);
                    }
                }
            }
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::AudioUnitPluginFormat au;
    juce::VST3PluginFormat vst3;

    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    std::cout << "Through the real plugin wrappers, not the processor directly:\n\n";
    tryFormat (au, home.getChildFile ("Library/Audio/Plug-Ins/Components/AmpSim.component").getFullPathName());
    std::cout << "\n";
    tryFormat (vst3, home.getChildFile ("Library/Audio/Plug-Ins/VST3/AmpSim.vst3").getFullPathName());
    return 0;
}
