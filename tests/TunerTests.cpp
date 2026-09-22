/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "dsp/Tuner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    /** A plucked string is not a sine: it is a fundamental plus a stack of harmonics that decay
        at different rates. Feeding the tuner a pure sine would flatter it. */
    std::vector<float> guitarLikeTone (double frequency, int numSamples, float amplitude = 0.3f)
    {
        std::vector<float> out ((size_t) numSamples, 0.0f);

        const float harmonicLevels[] { 1.0f, 0.6f, 0.4f, 0.25f, 0.15f, 0.08f };

        for (int h = 0; h < (int) std::size (harmonicLevels); ++h)
        {
            const auto partial = frequency * (h + 1);

            if (partial > sr * 0.45)
                break;

            for (int i = 0; i < numSamples; ++i)
                out[(size_t) i] += amplitude * harmonicLevels[h]
                                 * (float) std::sin (juce::MathConstants<double>::twoPi * partial * i / sr
                                                     + 0.3 * h);
        }

        return out;
    }

    Tuner::Reading readingFor (const std::vector<float>& signal)
    {
        Tuner tuner;
        tuner.prepare (sr);

        // Feed it in blocks, the way the audio thread would.
        for (size_t pos = 0; pos + (size_t) test::blockSize <= signal.size(); pos += (size_t) test::blockSize)
        {
            tuner.pushSamples (signal.data() + pos, test::blockSize);

            if ((pos / (size_t) test::blockSize) % 4 == 3)
                tuner.analyse();
        }

        tuner.analyse();
        return tuner.getReading();
    }
}

TEST_CASE ("The tuner reads each open string of a guitar in standard tuning", "[tuner]")
{
    struct String { const char* name; double frequency; int midiNote; };

    const auto openString = GENERATE (String { "E2", 82.41, 40 },
                                      String { "A2", 110.00, 45 },
                                      String { "D3", 146.83, 50 },
                                      String { "G3", 196.00, 55 },
                                      String { "B3", 246.94, 59 },
                                      String { "E4", 329.63, 64 });

    const auto reading = readingFor (guitarLikeTone (openString.frequency, test::blockSize * 16));

    INFO (openString.name << ": read " << reading.frequencyHz << " Hz, note "
                          << Tuner::noteName (reading.midiNote) << ", " << reading.cents << " cents");

    REQUIRE (reading.valid);
    REQUIRE (reading.midiNote == openString.midiNote);
    REQUIRE (Tuner::noteName (reading.midiNote) == juce::String (openString.name));
    REQUIRE_THAT (reading.cents, WithinAbs (0.0, 2.0));
}

TEST_CASE ("The tuner says how far off the note is, and which way", "[tuner]")
{
    const auto offsetCents = GENERATE (-40.0, -12.0, 12.0, 40.0);

    // A2 at 110 Hz, detuned by a known number of cents.
    const auto frequency = 110.0 * std::pow (2.0, offsetCents / 1200.0);
    const auto reading = readingFor (guitarLikeTone (frequency, test::blockSize * 16));

    REQUIRE (reading.valid);
    REQUIRE (reading.midiNote == 45);
    REQUIRE_THAT (reading.cents, WithinAbs (offsetCents, 2.5));
}

TEST_CASE ("The tuner does not answer an octave out on a bright note", "[tuner]")
{
    // A tone whose second harmonic is louder than its fundamental, which is what makes naive
    // autocorrelation report the octave above.
    std::vector<float> signal ((size_t) test::blockSize * 16, 0.0f);

    for (size_t i = 0; i < signal.size(); ++i)
    {
        const auto t = (double) i / sr;
        signal[i] = 0.12f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                  + 0.30f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)
                  + 0.20f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * t);
    }

    const auto reading = readingFor (signal);

    INFO ("read " << reading.frequencyHz << " Hz");
    REQUIRE (reading.valid);
    REQUIRE (reading.midiNote == 45);      // A2, not A3
}

TEST_CASE ("The tuner reports nothing rather than guessing", "[tuner]")
{
    SECTION ("silence")
    {
        const std::vector<float> silence ((size_t) test::blockSize * 16, 0.0f);
        REQUIRE_FALSE (readingFor (silence).valid);
    }

    SECTION ("a note that has decayed away")
    {
        auto quiet = guitarLikeTone (110.0, test::blockSize * 16, 0.0002f);
        REQUIRE_FALSE (readingFor (quiet).valid);
    }

    SECTION ("noise with no pitch in it")
    {
        juce::Random random (0x7075ee);
        std::vector<float> noise ((size_t) test::blockSize * 16);

        for (auto& sample : noise)
            sample = 0.3f * (random.nextFloat() * 2.0f - 1.0f);

        REQUIRE_FALSE (readingFor (noise).valid);
    }
}

TEST_CASE ("The tuner keeps up rather than falling behind", "[tuner]")
{
    // The audio thread must never block, so the FIFO drops the oldest samples when analysis has
    // not run. What comes out afterwards should still be the current note.
    Tuner tuner;
    tuner.prepare (sr);

    const auto signal = guitarLikeTone (196.0, test::blockSize * 40);

    for (size_t pos = 0; pos + (size_t) test::blockSize <= signal.size(); pos += (size_t) test::blockSize)
        tuner.pushSamples (signal.data() + pos, test::blockSize);   // no analyse() at all

    tuner.analyse();

    const auto reading = tuner.getReading();

    REQUIRE (reading.valid);
    REQUIRE (reading.midiNote == 55);   // G3
}
