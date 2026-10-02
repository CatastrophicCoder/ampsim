/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

/*
    Renders each page of the panel to a PNG, so the panel can be looked at rather than reasoned
    about. CLAUDE.md asks for that after any change to the panel. This is also how the Windows
    build's panel gets seen without a Windows machine: CI runs it and uploads the images.

        ampsim_snapshot [output directory] [scale]

    It drives the real editor with the built-in amp and cab loaded, as a fresh instance has them,
    and switches pages through the tab buttons' own click handlers.
*/
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <iostream>

namespace
{
    void runMessageLoop (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    void waitForModel (AmpSimAudioProcessor& processor)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        for (int i = 0; i < 400 && ! processor.isModelLoaded(); ++i)
        {
            runMessageLoop (25);
            buffer.clear();
            processor.processBlock (buffer, midi);
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

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    runMessageLoop (200);

    juce::Array<TabButton*> tabs;
    findTabs (*editor, tabs);

    if (tabs.isEmpty())
    {
        std::cerr << "No tabs found on the editor\n";
        return 1;
    }

    for (auto* tab : tabs)
    {
        tab->onClick();
        runMessageLoop (200);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = outputDirectory.getChildFile ("panel-" + tab->getButtonText().toLowerCase() + ".png");

        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;

        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        {
            std::cerr << "Could not write " << file.getFullPathName() << "\n";
            return 1;
        }

        std::cout << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")\n";
    }

    editor.reset();
    return 0;
}
