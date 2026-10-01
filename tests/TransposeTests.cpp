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

    /** How strongly a stretch repeats at one period, from -1 to 1.

        Asked about a period rather than searched for one. A shifter like this joins grains of
        audio together, and the joins have a rate of their own that a search will happily report
        instead of the note — so the question put here is the one that matters: does what came out
        repeat at the interval that was asked for, and not at the one that went in.
    */
    double periodicityAt (const std::vector<float>& x, double frequency, size_t from)
    {
        const auto lag = (size_t) std::llround (sr / frequency);
        const auto count = (size_t) 7000;

        double product = 0.0, here = 0.0, there = 0.0;

        for (size_t i = 0; i < count; ++i)
        {
            const auto a = (double) x[from + i];
            const auto b = (double) x[from + i + lag];

            product += a * b;
            here += a * a;
            there += b * b;
        }

        return product / std::sqrt (juce::jmax (1.0e-12, here * there));
    }

    float levelOf (const std::vector<float>& x, size_t from)
    {
        double sum = 0.0;

        for (size_t i = from; i < x.size(); ++i)
            sum += (double) x[i] * x[i];

        return (float) std::sqrt (sum / (double) (x.size() - from));
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
    // Every whole step either way. What is asserted is periodicity rather than a detected pitch:
    // the output has to repeat at the note that was asked for and not at the one that was played.
    constexpr double source = 220.0;

    for (const auto semitones : { -12, -7, -5, -2, -1, 1, 2, 5, 7, 12 })
    {
        const auto shifted = throughTranspose (semitones, true, source);
        const auto from = (size_t) (test::blockSize * 40);

        const auto wanted = source * std::pow (2.0, semitones / 12.0);
        const auto atWanted = periodicityAt (shifted, wanted, from);
        const auto atSource = periodicityAt (shifted, source, from);

        INFO (semitones << " semitones: repeats at " << wanted << " Hz with " << atWanted
              << ", at the original " << source << " Hz with " << atSource);

        // Strongly periodic at the note asked for, and more so than at the note played. A
        // signal that had not been shifted could not manage the first of those: a 220 Hz tone
        // looked at over a 196 Hz period repeats with about 0.72, and over a 208 Hz one, 0.93.
        REQUIRE (atWanted > 0.95);
        REQUIRE (atWanted > atSource);

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


TEST_CASE ("Switching the transpose on shifts what comes out of the plugin", "[transpose]")
{
    // Through the whole processor, not the shifter on its own. The shifter was right and the
    // wiring around it was not: an interval changed while it was stepped aside left it waiting
    // on a fade that could never finish, so it never shifted again — and the only test that
    // touched the processor was looking at the tuner, which was unaffected.
    const auto pitchThroughPlugin = [] (bool engaged, int semitones)
    {
        auto processor = test::makePreparedProcessor();
        auto& state = processor->getValueTreeState();

        test::setParam (state, ParamID::transposeOn, engaged ? 1.0f : 0.0f);
        test::setParam (state, ParamID::transposeSemitones, (float) semitones);

        juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), test::blockSize);
        juce::MidiBuffer midi;

        std::vector<float> captured;
        int index = 0;

        for (int b = 0; b < 100; ++b)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < test::blockSize; ++i)
                    buffer.setSample (ch, i, 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                          * 220.0 * (index + i) / sr));

            index += test::blockSize;
            processor->processBlock (buffer, midi);

            if (b >= 40)
                for (int i = 0; i < test::blockSize; ++i)
                    captured.push_back (buffer.getSample (0, i));
        }

        return captured;
    };

    // Off, it is the note that was played and nothing else.
    const auto dry = pitchThroughPlugin (false, -12);
    REQUIRE (periodicityAt (dry, 220.0, 0) > 0.9);

    // On, it is the note that was asked for — and in both cases the interval was set while the
    // transpose was off, which is the order that used to leave it stuck.
    const auto down = pitchThroughPlugin (true, -12);
    INFO ("down an octave: 110 Hz " << periodicityAt (down, 110.0, 0)
          << ", 220 Hz " << periodicityAt (down, 220.0, 0));
    REQUIRE (periodicityAt (down, 110.0, 0) > 0.9);
    REQUIRE (periodicityAt (down, 110.0, 0) > periodicityAt (down, 220.0, 0));

    const auto up = pitchThroughPlugin (true, 7);
    INFO ("up a fifth: 329.6 Hz " << periodicityAt (up, 329.628, 0)
          << ", 220 Hz " << periodicityAt (up, 220.0, 0));
    REQUIRE (periodicityAt (up, 329.628, 0) > 0.9);
}

TEST_CASE ("The shifted signal does not wobble", "[transpose]")
{
    // The artefact this kind of shifter is known for, and the reason the join is matched rather
    // than taken at a fixed distance: two copies of the note crossing over at an arbitrary point
    // in the waveform partly cancel, and the level dips every time they do. On a steady note that
    // is a tremolo at a few hertz, which is what it sounded like before the join was aligned —
    // eight decibels deep at some intervals.
    for (const auto semitones : { -12, -5, -2, -1, 1, 2, 5, 12 })
    {
        const auto shifted = throughTranspose (semitones, true, 220.0, 200);
        const auto from = (size_t) (test::blockSize * 60);

        float loudest = 0.0f, quietest = 1.0e9f;

        for (size_t start = from; start + 480 < shifted.size(); start += 240)
        {
            double sum = 0.0;

            for (size_t i = start; i < start + 480; ++i)
                sum += (double) shifted[i] * shifted[i];

            const auto rms = (float) std::sqrt (sum / 480.0);
            loudest = juce::jmax (loudest, rms);
            quietest = juce::jmin (quietest, rms);
        }

        const auto swing = juce::Decibels::gainToDecibels (quietest / loudest);

        INFO (semitones << " semitones: the envelope swings " << swing << " dB");
        REQUIRE (swing > -1.5f);
    }
}

