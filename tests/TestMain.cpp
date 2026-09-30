/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "PluginProcessor.h"

#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

/** Catch2 runs with our own main so that JUCE's message manager and singletons exist for the
    whole session: constructing an AudioProcessor without them trips leak detectors on shutdown.
*/
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // A bare chain by default: most tests measure what one block does to a signal, and an amp
    // model in the way would mean measuring the model instead. The tests that care about the
    // built-in assets turn this back on for themselves.
    AmpSimAudioProcessor::loadBundledAssetsOnCreation = false;

    return Catch::Session().run (argc, argv);
}
