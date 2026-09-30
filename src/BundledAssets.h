/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <juce_core/juce_core.h>

/** The amp model and cabinet impulse response built into the plugin, so it makes a sound the
    first time it is opened rather than passing audio through untouched.

    They travel inside the binary in the packed form described in AssetPack.h, and are written out
    as ordinary files on first run — NAM and the convolution both load from a path, and a file on
    disk is also something a preset can point at.
*/
namespace BundledAssets
{
    /** Where the unpacked files live: alongside the presets, under the plugin's own folder. */
    juce::File directory();

    /** Writes both out if they are not already there. Cheap to call repeatedly.
        @returns an error message, empty on success. */
    juce::String install();

    /** The unpacked files. Empty `juce::File`s if install() has not succeeded. */
    juce::File ampModel();
    juce::File cabinetImpulseResponse();
}
