/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "BundledAssets.h"
#include "AssetPack.h"

#include <BinaryData.h>

namespace BundledAssets
{
    namespace
    {
        struct Asset
        {
            const char* resourceName;
            const char* unpackedName;
        };

        constexpr Asset assets[]
        {
            { "MARS2204.ampsimpack",  "MARS2204.nam" },
            { "V30 SM57.ampsimpack",  "V30 SM57.wav" }
        };

        juce::File unpackedFile (const Asset& asset)
        {
            return directory().getChildFile (asset.unpackedName);
        }

        juce::String writeOut (const Asset& asset)
        {
            const auto target = unpackedFile (asset);

            int packedSize = 0;
            const auto* packed = BinaryData::getNamedResource (
                juce::String (asset.resourceName).replaceCharacter (' ', '_')
                                                 .replaceCharacter ('.', '_').toRawUTF8(),
                packedSize);

            if (packed == nullptr || packedSize <= 0)
                return juce::String ("Missing built-in asset: ") + asset.unpackedName;

            const auto unpacked = AssetPack::unpack (packed, (size_t) packedSize);

            if (! unpacked.valid)
                return juce::String ("Could not read built-in asset: ") + asset.unpackedName;

            // Already there and the right size: leave it alone, so a user who has since edited or
            // replaced one is not overwritten on every launch.
            if (target.existsAsFile() && (size_t) target.getSize() == unpacked.data.getSize())
                return {};

            if (! target.replaceWithData (unpacked.data.getData(), unpacked.data.getSize()))
                return "Could not write to " + directory().getFullPathName();

            return {};
        }
    }

    juce::File directory()
    {
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif

        return base.getChildFile ("AmpSim").getChildFile ("Bundled");
    }

    juce::String install()
    {
        const auto folder = directory();

        if (! folder.createDirectory())
            return "Could not create " + folder.getFullPathName();

        for (const auto& asset : assets)
            if (const auto error = writeOut (asset); error.isNotEmpty())
                return error;

        return {};
    }

    juce::File ampModel()
    {
        const auto file = unpackedFile (assets[0]);
        return file.existsAsFile() ? file : juce::File();
    }

    juce::File cabinetImpulseResponse()
    {
        const auto file = unpackedFile (assets[1]);
        return file.existsAsFile() ? file : juce::File();
    }
}
