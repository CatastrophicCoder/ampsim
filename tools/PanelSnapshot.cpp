/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

/*
    Renders the panel to PNGs, so it can be looked at rather than reasoned about. CLAUDE.md asks
    for that after any change to the panel. CI runs it on Windows and uploads the images, and at
    the guide's scale it writes the guide's own screenshots:

        ampsim_snapshot [output directory] [scale]
        ampsim_snapshot docs/images 2           # regenerates every screenshot in the user guide

    Each shot starts from the Default preset with the built-in amp and cab, as a fresh instance
    has them, sets what it needs, and gets an editor of its own, since the editor reads some
    state (whether the tuner is showing) only when it is made.
*/
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <functional>
#include <iostream>

namespace
{
    void runMessageLoop (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    void processSilence (AmpSimAudioProcessor& processor, int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int i = 0; i < blocks; ++i)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
        }
    }

    void waitForModel (AmpSimAudioProcessor& processor)
    {
        for (int i = 0; i < 400 && ! processor.isModelLoaded(); ++i)
        {
            runMessageLoop (25);
            processSilence (processor, 1);
        }
    }

    void findTabs (juce::Component& parent, juce::Array<TabButton*>& tabs)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* tab = dynamic_cast<TabButton*> (child))
                tabs.add (tab);

            findTabs (*child, tabs);
        }
    }

    void set (AmpSimAudioProcessor& processor, const char* id, float value)
    {
        if (auto* p = processor.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void choose (AmpSimAudioProcessor& processor, const char* id, const juce::String& choice)
    {
        if (auto* p = processor.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (p->getValueForText (choice));
    }

    /** A plucked open A string, nine cents sharp, fed through the processor while the editor's
        timer reads the tuner — which is where the reading on the tuner shot comes from. */
    void playA2 (AmpSimAudioProcessor& processor)
    {
        constexpr double rate = 48000.0;
        const auto frequency = 110.0 * std::pow (2.0, 9.0 / 1200.0);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int block = 0; block < 120; ++block)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto x = (float) (0.3 * std::sin (phase) + 0.1 * std::sin (2.0 * phase));
                phase += juce::MathConstants<double>::twoPi * frequency / rate;

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.setSample (ch, i, x);
            }

            processor.processBlock (buffer, midi);
            runMessageLoop (10);
        }
    }

    struct Shot
    {
        const char* name;
        int tab;
        std::function<void (AmpSimAudioProcessor&)> setUp;
        std::function<void (AmpSimAudioProcessor&)> whileOpen;
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto outputDirectory = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                          : juce::File::getCurrentWorkingDirectory();
    const auto scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 2.0f;

    if (! outputDirectory.createDirectory())
    {
        std::cerr << "Could not create " << outputDirectory.getFullPathName() << "\n";
        return 1;
    }

    AmpSimAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    waitForModel (processor);

    const Shot shots[]
    {
        { "amp",    0, {}, {} },
        { "pedals", 1, {}, {} },
        { "cab",    2, {}, {} },
        { "amp-off", 0, [] (auto& p) { set (p, ParamID::power, 0.0f); }, {} },
        { "pedals-variants", 1, [] (auto& p) { choose (p, ParamID::dirtType, "Overdrive");
                                               choose (p, ParamID::modulationType, "Phaser"); }, {} },
        { "tuner",  0, [] (auto& p) { set (p, ParamID::tunerOn, 1.0f); }, playA2 },
    };

    for (const auto& shot : shots)
    {
        if (const auto error = processor.getPresets().load ("Default"); error.isNotEmpty())
        {
            std::cerr << "Could not load the Default preset: " << error << "\n";
            return 1;
        }

        if (shot.setUp)
            shot.setUp (processor);

        processSilence (processor, 20);
        runMessageLoop (100);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        runMessageLoop (200);

        juce::Array<TabButton*> tabs;
        findTabs (*editor, tabs);

        if (! juce::isPositiveAndBelow (shot.tab, tabs.size()))
        {
            std::cerr << "No tab " << shot.tab << " on the editor\n";
            return 1;
        }

        tabs[shot.tab]->onClick();
        runMessageLoop (200);

        if (shot.whileOpen)
            shot.whileOpen (processor);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = outputDirectory.getChildFile (juce::String (shot.name) + ".png");

        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;

        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        {
            std::cerr << "Could not write " << file.getFullPathName() << "\n";
            return 1;
        }

        std::cout << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")\n";

        editor.reset();
        runMessageLoop (50);
    }

    return 0;
}
