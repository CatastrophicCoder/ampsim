/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "dsp/Transpose.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    /** The pitch that came out, found by autocorrelation rather than by looking in one bin of a
        transform. A shifter of this kind modulates what it produces at the rate its grains repeat,
        which spreads the energy into sidebands either side of the note — a single bin then reads
        far too low and says nothing about whether the pitch is right. The period does not move. */
    /** The pitch that came out, as the period the whole stretch repeats at.

        Autocorrelation rather than a transform, because this kind of shifter joins grains of
        audio together and every join is a phase discontinuity — which spreads a long transform
        across so many sidebands that the note itself can disappear from it. The periodicity
        survives that; it is what the ear is following too.
    */
    double detectedFrequency (const std::vector<float>& x, size_t from)
    {
        const auto lowestLag = (size_t) (sr / 700.0);
        const auto highestLag = (size_t) (sr / 50.0);
        const auto count = (size_t) 7000;

        std::vector<double> correlation (highestLag + 1, 0.0);

        for (auto lag = lowestLag; lag <= highestLag; ++lag)
        {
            double product = 0.0, here = 0.0, there = 0.0;

            for (size_t i = 0; i < count; ++i)
            {
                const auto a = (double) x[from + i];
                const auto b = (double) x[from + i + lag];

                product += a * b;
                here += a * a;
                there += b * b;
            }

            correlation[lag] = product / std::sqrt (juce::jmax (1.0e-12, here * there));
        }

        auto bestLag = (size_t) std::distance (correlation.begin(),
                                               std::max_element (correlation.begin() + (long) lowestLag,
                                                                 correlation.end()));

        // A period's multiples correlate just as well as the period, so the best lag may be two or
        // three of them. Halve it while the half still correlates, which is the usual fix.
        while (bestLag / 2 >= lowestLag && correlation[bestLag / 2] > 0.95 * correlation[bestLag])
            bestLag /= 2;

        return bestLag > 0 ? sr / (double) bestLag : 0.0;
    }

    float levelOf (const std::vector<float>& x, size_t from)
    {
        double sum = 0.0;

        for (size_t i = from; i < x.size(); ++i)
            sum += (double) x[i] * x[i];

        return (float) std::sqrt (sum / (double) (x.size() - from));
    }

    /** How far apart two pitches are, in cents. */
    double centsBetween (double a, double b)
    {
        return 1200.0 * std::log2 (a / b);
    }

    std::vector<float> throughTranspose (int semitones, bool engaged, double frequency = 220.0,
                                         int numBlocks = 120)
    {
        Transpose transpose;
        transpose.prepare (sr, test::blockSize);
        transpose.setSemitones (semitones);
        transpose.snapBypass (! engaged);

        std::vector<float> signal ((size_t) (numBlocks * test::blockSize));

        for (size_t i = 0; i < signal.size(); ++i)
            signal[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                     * frequency * (double) i / sr);

        for (size_t pos = 0; pos + (size_t) test::blockSize <= signal.size(); pos += (size_t) test::blockSize)
            transpose.process (signal.data() + pos, test::blockSize, ! engaged);

        return signal;
    }
}

TEST_CASE ("The interval that comes out is the interval that was asked for", "[transpose]")
{
    // Every whole step either way, measured as a pitch rather than as a level: a shifter of this
    // kind modulates what it makes, so what matters is where the period lands.
    constexpr double source = 220.0;

    for (const auto semitones : { -12, -7, -5, -2, -1, 1, 2, 5, 7, 12 })
    {
        const auto shifted = throughTranspose (semitones, true, source);
        const auto from = (size_t) (test::blockSize * 40);

        const auto detected = detectedFrequency (shifted, from);
        const auto wanted = source * std::pow (2.0, semitones / 12.0);

        INFO (semitones << " semitones: wanted " << wanted << " Hz, got " << detected << " Hz");

        // Within a third of a semitone, which is as close as a whole-sample lag can say at the
        // top of this range: at 440 Hz one sample of period is sixteen cents.
        REQUIRE (std::abs (centsBetween (detected, wanted)) < 35.0);

        // And it is still a guitar rather than a whisper: the shifter must not cost level.
        INFO ("level " << levelOf (shifted, from) << " against an input of 0.354");
        REQUIRE (levelOf (shifted, from) > 0.2f);
    }
}

TEST_CASE ("Transpose at zero semitones is not in the signal at all", "[transpose]")
{
    // A ratio of one would still hold the signal a window behind for nothing, so it steps aside
    // instead. This is what makes the middle of the control honest.
    const auto passed = throughTranspose (0, true, 220.0, 20);

    std::vector<float> reference (passed.size());

    for (size_t i = 0; i < reference.size(); ++i)
        reference[i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) i / sr);

    for (size_t i = (size_t) (test::blockSize * 10); i < passed.size(); ++i)
        REQUIRE_THAT (passed[i], WithinAbs (reference[i], 1.0e-6));
}

TEST_CASE ("The tuner reads the strings, not the interval", "[transpose][tuner]")
{
    // Tuning to a transposed reading would put the guitar out, so the tuner taps in ahead of the
    // shifter. This is the test that keeps the two in that order.
    auto processor = test::makePreparedProcessor();
    auto& state = processor->getValueTreeState();

    test::setParam (state, ParamID::transposeOn, 1.0f);
    test::setParam (state, ParamID::transposeSemitones, 12.0f);
    test::setParam (state, ParamID::tunerOn, 1.0f);

    juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), test::blockSize);
    juce::MidiBuffer midi;

    // A2, well inside the range a guitar's low strings occupy.
    constexpr double frequency = 110.0;
    int index = 0;

    for (int b = 0; b < 60; ++b)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < test::blockSize; ++i)
                buffer.setSample (ch, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                      * frequency * (index + i) / sr));

        index += test::blockSize;
        processor->processBlock (buffer, midi);
        processor->getTuner().analyse();
    }

    const auto reading = processor->getTuner().getReading();
    REQUIRE (reading.valid);

    // A2 is MIDI 45. An octave up would read 57, which is what a tuner placed after the shifter
    // would say — and would have you tune the guitar an octave out.
    INFO ("read MIDI note " << reading.midiNote);
    REQUIRE (reading.midiNote == 45);
}

