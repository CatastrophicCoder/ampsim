/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    /** Where the clicks fall, in samples from the start, found as the peaks in a silent run. */
    std::vector<int> clickPositions (AmpSimAudioProcessor& processor, int blocks = 200)
    {
        juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), test::blockSize);
        juce::MidiBuffer midi;

        std::vector<int> positions;
        auto quietSince = 10000;

        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);

            for (int i = 0; i < test::blockSize; ++i)
            {
                const auto level = std::abs (buffer.getSample (0, i));

                // The leading edge of a click, rather than every sample of one.
                if (level > 0.02f)
                {
                    if (quietSince > 200)
                        positions.push_back (b * test::blockSize + i);

                    quietSince = 0;
                }
                else
                {
                    ++quietSince;
                }
            }
        }

        return positions;
    }

    void setUp (AmpSimAudioProcessor& processor, float tempo, int barChoice = 2)
    {
        auto& state = processor.getValueTreeState();

        test::setParam (state, ParamID::metronomeOn, 1.0f);
        test::setParam (state, ParamID::metronomeTempo, tempo);
        test::setParam (state, ParamID::metronomeLevel, 0.0f);
        state.getParameter (ParamID::metronomeBeats)
             ->setValueNotifyingHost ((float) barChoice / 5.0f);
    }
}

TEST_CASE ("The metronome clicks at the tempo it is given", "[metronome]")
{
    auto processor = test::makePreparedProcessor();
    setUp (*processor, 120.0f);

    const auto clicks = clickPositions (*processor);
    REQUIRE (clicks.size() > 4);

    // 120 BPM is a click every half second, which at this rate is 24000 samples.
    for (size_t i = 1; i < clicks.size(); ++i)
    {
        INFO ("gap " << i << " is " << (clicks[i] - clicks[i - 1]) << " samples");
        REQUIRE (std::abs (clicks[i] - clicks[i - 1] - 24000) < 64);
    }
}

TEST_CASE ("The metronome's tempo control moves it", "[metronome]")
{
    const auto gapAt = [] (float tempo)
    {
        auto processor = test::makePreparedProcessor();
        setUp (*processor, tempo);

        const auto clicks = clickPositions (*processor, 400);
        REQUIRE (clicks.size() > 3);

        return clicks[2] - clicks[1];
    };

    REQUIRE (std::abs (gapAt (60.0f) - 48000) < 64);
    REQUIRE (std::abs (gapAt (180.0f) - 16000) < 64);
}

TEST_CASE ("The accent falls once a bar, where the time signature puts it", "[metronome]")
{
    // The accented beat is the same sound a fifth up, so what marks the bar is its pitch rather
    // than its level — but it is also the loudest, which is what this can measure.
    const auto accentEvery = [] (int barChoice)
    {
        auto processor = test::makePreparedProcessor();
        setUp (*processor, 180.0f, barChoice);

        juce::AudioBuffer<float> buffer (processor->getTotalNumOutputChannels(), test::blockSize);
        juce::MidiBuffer midi;

        std::vector<float> peaks;
        float peak = 0.0f;
        int quiet = 0;

        // Long enough for two accents even in the longest bar on offer: at 180 BPM a seven-beat
        // bar takes well over two seconds, and one accent gives no gap to measure.
        for (int b = 0; b < 800; ++b)
        {
            buffer.clear();
            processor->processBlock (buffer, midi);

            for (int i = 0; i < test::blockSize; ++i)
            {
                const auto level = std::abs (buffer.getSample (0, i));

                if (level > 0.02f)
                {
                    peak = juce::jmax (peak, level);
                    quiet = 0;
                }
                else if (++quiet == 200 && peak > 0.1f)
                {
                    peaks.push_back (peak);
                    peak = 0.0f;
                }
            }
        }

        REQUIRE (peaks.size() > 20);

        // Halfway between the loudest click and the quietest, which separates the two cleanly:
        // where exactly a sine's own peak lands inside a fast decay varies a little, so a
        // threshold just under the maximum does not.
        const auto loudest = *std::max_element (peaks.begin(), peaks.end());
        const auto quietest = *std::min_element (peaks.begin(), peaks.end());
        const auto threshold = 0.5f * (loudest + quietest);

        std::vector<int> accents;

        for (size_t i = 0; i < peaks.size(); ++i)
            if (peaks[i] > threshold)
                accents.push_back ((int) i);

        INFO ("bar choice " << barChoice << ": " << accents.size() << " accents in "
              << peaks.size() << " clicks");

        REQUIRE (accents.size() > 1);
        return accents[1] - accents[0];
    };

    REQUIRE (accentEvery (0) == 2);   // 2/4
    REQUIRE (accentEvery (2) == 4);   // 4/4
    REQUIRE (accentEvery (4) == 6);   // 6/4
}

TEST_CASE ("The metronome keeps going through everything that silences the amp", "[metronome]")
{
    // It is a practice tool rather than part of the amp, so switching the amp off, muting it to
    // tune, or taking the whole plugin out of circuit all leave the click going.
    for (const auto* silencer : { ParamID::power, ParamID::tunerOn, ParamID::bypass })
    {
        auto processor = test::makePreparedProcessor();
        setUp (*processor, 120.0f);

        test::setParam (processor->getValueTreeState(), silencer,
                        juce::String (silencer) == ParamID::power ? 0.0f : 1.0f);

        INFO ("with " << silencer << " set against it");
        REQUIRE (clickPositions (*processor, 160).size() > 2);
    }
}

TEST_CASE ("The metronome is silent in an offline render", "[metronome]")
{
    // A click printed into a bounce is the one way this could do real damage.
    auto processor = test::makePreparedProcessor();
    setUp (*processor, 120.0f);

    REQUIRE (clickPositions (*processor, 160).size() > 2);

    processor->setNonRealtime (true);
    REQUIRE (clickPositions (*processor, 160).empty());
}
