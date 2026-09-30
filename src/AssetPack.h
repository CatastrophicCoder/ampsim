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

/** A container for the amp model and cabinet impulse response that ship with the plugin.

    The payload is deflated and then masked with a keystream. **This is obfuscation, not
    encryption**, and it is not treated as a secret: the unpacking side is in this repository, so
    the method is published with it. What it buys is that the repository does not carry a playable
    `.nam` or `.wav` that can be lifted out of it by dragging, and that the files inside carry
    neutral names rather than a trademarked amplifier's.

    Both directions live here so there is one implementation of the format. The packing tool is a
    thin wrapper around `pack()`.
*/
namespace AssetPack
{
    inline constexpr const char* magic = "AMPSIMPK";
    inline constexpr int magicLength = 8;
    inline constexpr juce::uint32 formatVersion = 1;

    /** FNV-1a over the payload: enough to notice a truncated or corrupted pack, and it needs no
        module beyond juce_core. Not a security check — see the note above. */
    inline juce::uint32 checksumOf (const void* data, size_t size)
    {
        const auto* bytes = static_cast<const juce::uint8*> (data);
        juce::uint32 hash = 0x811c9dc5u;

        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 0x01000193u;
        }

        return hash;
    }

    /** Masks in place. Symmetric, so the same call packs and unpacks. */
    inline void applyMask (void* data, size_t size, juce::uint32 seed)
    {
        auto* bytes = static_cast<juce::uint8*> (data);
        auto state = seed != 0 ? seed : 0x9e3779b9u;

        for (size_t i = 0; i < size; ++i)
        {
            // xorshift32: cheap, deterministic, and the same on both sides.
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;

            bytes[i] = (juce::uint8) (bytes[i] ^ (juce::uint8) (state & 0xff));
        }
    }

    /** @returns the packed bytes, or an empty block if the source could not be read. */
    inline juce::MemoryBlock pack (const juce::MemoryBlock& source, const juce::String& originalName)
    {
        juce::MemoryBlock deflated;

        {
            juce::MemoryOutputStream deflatedStream (deflated, false);
            juce::GZIPCompressorOutputStream compressor (deflatedStream, 9);
            compressor.write (source.getData(), source.getSize());
            compressor.flush();
        }

        const auto nameBytes = originalName.toRawUTF8();
        const auto nameLength = (juce::uint32) std::strlen (nameBytes);
        const auto checksum = checksumOf (source.getData(), source.getSize());
        const auto seed = checksum ^ 0xa5a5a5a5u;

        applyMask (deflated.getData(), deflated.getSize(), seed);

        juce::MemoryBlock packed;
        juce::MemoryOutputStream out (packed, false);

        out.write (magic, magicLength);
        out.writeInt ((int) formatVersion);
        out.writeInt ((int) checksum);
        out.writeInt64 ((juce::int64) source.getSize());
        out.writeInt ((int) nameLength);
        out.write (nameBytes, nameLength);
        out.writeInt64 ((juce::int64) deflated.getSize());
        out.write (deflated.getData(), deflated.getSize());
        out.flush();

        return packed;
    }

    struct Unpacked
    {
        bool valid = false;
        juce::String originalName;
        juce::MemoryBlock data;
    };

    /** @returns the original bytes and filename, or `valid == false` if the container is not one
                of ours, is of a version this build does not know, or does not check out. */
    inline Unpacked unpack (const void* packedData, size_t packedSize)
    {
        Unpacked result;

        juce::MemoryInputStream in (packedData, packedSize, false);

        char readMagic[magicLength] {};

        if (in.read (readMagic, magicLength) != magicLength
            || std::memcmp (readMagic, magic, magicLength) != 0)
            return result;

        if ((juce::uint32) in.readInt() != formatVersion)
            return result;

        const auto checksum = (juce::uint32) in.readInt();
        const auto originalSize = (size_t) in.readInt64();
        const auto nameLength = (size_t) in.readInt();

        if (nameLength > 1024)
            return result;

        juce::MemoryBlock nameBytes (nameLength);

        if ((size_t) in.read (nameBytes.getData(), (int) nameLength) != nameLength)
            return result;

        result.originalName = juce::String::fromUTF8 ((const char*) nameBytes.getData(), (int) nameLength);

        const auto deflatedSize = (size_t) in.readInt64();
        juce::MemoryBlock deflated (deflatedSize);

        if ((size_t) in.read (deflated.getData(), (int) deflatedSize) != deflatedSize)
            return result;

        applyMask (deflated.getData(), deflated.getSize(), checksum ^ 0xa5a5a5a5u);

        {
            juce::MemoryInputStream deflatedStream (deflated, false);
            juce::GZIPDecompressorInputStream decompressor (deflatedStream);

            juce::MemoryOutputStream out (result.data, false);
            out.writeFromInputStream (decompressor, -1);
            out.flush();
        }

        if (result.data.getSize() != originalSize)
            return result;

        if (checksumOf (result.data.getData(), result.data.getSize()) != checksum)
            return result;

        result.valid = true;
        return result;
    }
}
