/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

/*
    What the chain costs, measured rather than guessed at.

    It runs the real processor — the same shared-code target the plugin builds ship — through two
    seconds of audio per pass and reports the time that took as a percentage of one core. Build it
    with -DAMPSIM_BUILD_TOOLS=ON, and **in Release**: a Debug figure is several times the real one
    and says nothing useful.

    Each row turns one thing on against the same baseline, so what a row costs is the difference
    from the row above the pedals. Seven passes and the median of them, because a single pass picks
    up whatever else the machine was doing.
*/
#include "PluginProcessor.h"
#include "BundledAssets.h"

#if defined(NAM_ENABLE_A2_FAST)
 #include "wavenet/a2_fast.h"
#endif

#include <chrono>

namespace
{
    double sampleRate = 48000.0;

    /** What this binary was built with, so that a table from one of several builds compared in
        the same CI job says which one it is. */
    juce::String buildDescription()
    {
        juce::StringArray parts;

       #if defined(__clang__)
        parts.add ("clang " __clang_version__);
       #elif defined(_MSC_VER)
        parts.add ("MSVC " + juce::String (_MSC_VER));
       #else
        parts.add ("unknown compiler");
       #endif

       #if defined(__AVX2__)
        parts.add ("AVX2");
       #endif
       #if defined(NAM_ENABLE_A2_FAST)
        parts.add ("NAM_ENABLE_A2_FAST");
       #endif
       #if defined(NAM_USE_INLINE_GEMM)
        parts.add ("NAM_USE_INLINE_GEMM");
       #endif

        return parts.joinIntoString (", ");
    }

    /** NAM takes its A2 fast path silently, and only for a model of exactly that shape, so a
        build with it enabled can measure the same as one without for either of two reasons. */
    juce::String a2FastPath()
    {
       #if defined(NAM_ENABLE_A2_FAST)
        try
        {
            const auto text = BundledAssets::ampModel().loadFileAsString().toStdString();
            const auto model = nlohmann::json::parse (text);

            // A slimmable capture is a container of WaveNets, and each is checked on its own.
            std::vector<nlohmann::json> wavenets;

            if (model.value ("architecture", std::string()) == "SlimmableContainer")
                for (const auto& sub : model.at ("config").at ("submodels"))
                    wavenets.push_back (sub.at ("model"));
            else
                wavenets.push_back (model);

            juce::StringArray verdicts;

            for (const auto& net : wavenets)
            {
                int channels = 0;

                if (net.value ("architecture", std::string()) != "WaveNet")
                    verdicts.add ("not a WaveNet");
                else if (nam::wavenet::a2_fast::is_a2_shape (net.at ("config"), &channels))
                    verdicts.add ("taken, " + juce::String (channels) + " channels");
                else
                    verdicts.add ("not taken, not A2-shaped");
            }

            return verdicts.joinIntoString ("; ");
        }
        catch (const std::exception& e)
        {
            return "unknown (" + juce::String (e.what()) + ")";
        }
       #else
        return "not compiled in";
       #endif
    }

    void set (juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
    {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    /** Fraction of one core, as the median of several passes: a single pass picks up whatever
        else the machine was doing. */
    double costOfOneCore (AmpSimAudioProcessor& processor, int blockSize,
                          const std::function<void (juce::AudioProcessorValueTreeState&)>& configure)
    {
        processor.prepareToPlay (sampleRate, blockSize);
        configure (processor.getValueTreeState());

        juce::AudioBuffer<float> buffer (juce::jmax (2, processor.getTotalNumOutputChannels()), blockSize);
        juce::MidiBuffer midi;

        const auto fill = [&buffer, blockSize] (int from)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (ch, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                          * 220.0 * (from + i) / sampleRate));
        };

        // Warm the caches, settle every smoother, and let the model's own prewarm be behind us.
        for (int b = 0; b < 200; ++b)
        {
            fill (b * blockSize);
            processor.processBlock (buffer, midi);
        }

        const auto blocks = (int) (sampleRate * 2.0 / blockSize);
        std::vector<double> passes;

        for (int pass = 0; pass < 7; ++pass)
        {
            const auto start = std::chrono::steady_clock::now();

            for (int b = 0; b < blocks; ++b)
            {
                fill (b * blockSize);
                processor.processBlock (buffer, midi);
            }

            const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            passes.push_back (elapsed.count() / ((double) (blocks * blockSize) / sampleRate));
        }

        std::sort (passes.begin(), passes.end());
        return passes[passes.size() / 2] * 100.0;
    }

    void waitForModel (AmpSimAudioProcessor& processor)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int i = 0; i < 400 && ! processor.isModelLoaded(); ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
            buffer.clear();
            processor.processBlock (buffer, midi);
        }
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    // The rate matters more than anything else here: the model's resampler is bypassed outright
    // when the host already runs at the model's own rate, so 48 kHz measures a chain with one
    // whole stage missing from it.
    if (argc > 1)
        sampleRate = juce::String (argv[1]).getDoubleValue();

    std::cout << "at " << (int) sampleRate << " Hz\n\n";

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (sampleRate, 512);
    waitForModel (processor);

    std::cout << "model loaded: " << (processor.isModelLoaded() ? "yes" : "no")
              << ", latency " << processor.getLatencySamples() << " samples\n"
              << "built with: " << buildDescription() << "\n"
              << "A2 fast path: " << a2FastPath() << "\n\n";

    struct Setup { const char* name; std::function<void (juce::AudioProcessorValueTreeState&)> apply; };

    const auto base = [] (juce::AudioProcessorValueTreeState& s)
    {
        for (const auto* id : { ParamID::gateOn, ParamID::compOn, ParamID::driveOn,
                                ParamID::chorusOn, ParamID::delayOn, ParamID::reverbOn,
                                ParamID::transposeOn, ParamID::metronomeOn, ParamID::bypass })
            set (s, id, 0.0f);

        set (s, ParamID::cabBypass, 0.0f);
    };

    const Setup setups[]
    {
        { "whole plugin bypassed",      [&] (auto& s) { base (s); set (s, ParamID::bypass, 1.0f); } },
        { "amp + cab (all pedals off)", [&] (auto& s) { base (s); } },
        { "  ... cab bypassed",         [&] (auto& s) { base (s); set (s, ParamID::cabBypass, 1.0f); } },
        { "+ gate",                     [&] (auto& s) { base (s); set (s, ParamID::gateOn, 1.0f); } },
        { "+ compressor",               [&] (auto& s) { base (s); set (s, ParamID::compOn, 1.0f); } },
        { "+ dirt (4x oversampled)",    [&] (auto& s) { base (s); set (s, ParamID::driveOn, 1.0f); } },
        { "+ modulation",               [&] (auto& s) { base (s); set (s, ParamID::chorusOn, 1.0f); } },
        { "+ delay",                    [&] (auto& s) { base (s); set (s, ParamID::delayOn, 1.0f); } },
        { "+ reverb",                   [&] (auto& s) { base (s); set (s, ParamID::reverbOn, 1.0f); } },
        { "+ metronome",                [&] (auto& s) { base (s); set (s, ParamID::metronomeOn, 1.0f); } },
        { "+ transpose (-5)",           [&] (auto& s) { base (s); set (s, ParamID::transposeOn, 1.0f);
                                                        set (s, ParamID::transposeSemitones, -5.0f); } },
        { "everything on",              [&] (auto& s)
            {
                base (s);
                for (const auto* id : { ParamID::gateOn, ParamID::compOn, ParamID::driveOn,
                                        ParamID::chorusOn, ParamID::delayOn, ParamID::reverbOn,
                                        ParamID::transposeOn, ParamID::metronomeOn })
                    set (s, id, 1.0f);

                set (s, ParamID::transposeSemitones, -5.0f);
            } },
    };

    const int blockSizes[] { 64, 128, 256, 512 };

    std::cout << juce::String ("configuration").paddedRight (' ', 30);
    for (const auto size : blockSizes)
        std::cout << juce::String (juce::String (size) + " spl").paddedLeft (' ', 10);
    std::cout << "\n" << juce::String().paddedRight ('-', 70) << "\n";

    for (const auto& setup : setups)
    {
        std::cout << juce::String (setup.name).paddedRight (' ', 30);

        for (const auto size : blockSizes)
            std::cout << juce::String (juce::String (costOfOneCore (processor, size, setup.apply), 2)
                                           + " %").paddedLeft (' ', 10);

        std::cout << "\n";
        std::cout.flush();
    }

    std::cout << "\nEach figure is the median of seven two-second passes, as a percentage of one\n"
                 "core at " << juce::String (sampleRate / 1000.0, 1) << " kHz. A plugin must stay"
                 " well under 100 % or it cannot keep up.\n";
    return 0;
}
