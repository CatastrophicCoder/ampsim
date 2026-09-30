/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "TestHelpers.h"
#include "dsp/PedalChain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sr = 48000.0;

    struct Signal
    {
        static std::vector<float> sine (double frequency, int numSamples, float amplitude = 0.3f)
        {
            std::vector<float> out ((size_t) numSamples);

            for (int i = 0; i < numSamples; ++i)
                out[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi
                                                                * frequency * i / sr);

            return out;
        }

        static float rms (const std::vector<float>& x, int from = 0)
        {
            double sum = 0.0;

            for (size_t i = (size_t) from; i < x.size(); ++i)
                sum += (double) x[i] * x[i];

            return (float) std::sqrt (sum / (double) (x.size() - (size_t) from));
        }

        static float peak (const std::vector<float>& x, int from = 0)
        {
            float result = 0.0f;

            for (size_t i = (size_t) from; i < x.size(); ++i)
                result = juce::jmax (result, std::abs (x[i]));

            return result;
        }
    };

    /** Runs a signal through one side of the chain, block by block. */
    std::vector<float> runThrough (PedalChain& chain, const std::vector<float>& input, bool beforeAmp)
    {
        auto output = input;

        for (size_t pos = 0; pos + (size_t) test::blockSize <= output.size(); pos += (size_t) test::blockSize)
        {
            if (beforeAmp)
                chain.processBeforeAmp (output.data() + pos, test::blockSize);
            else
                chain.processAfterAmp (output.data() + pos, test::blockSize);
        }

        return output;
    }

    /** Drives both halves in step, which is what the gate needs: it measures the key in front of
        the amp and applies the result behind it, so the two calls have to line up block by block.
        @returns what came out of the post-amp half. */
    std::vector<float> runKeyed (PedalChain& chain, const std::vector<float>& key,
                                 const std::vector<float>& afterAmp)
    {
        auto front = key;
        auto back = afterAmp;

        for (size_t pos = 0; pos + (size_t) test::blockSize <= back.size(); pos += (size_t) test::blockSize)
        {
            chain.processBeforeAmp (front.data() + pos, test::blockSize);
            chain.processAfterAmp (back.data() + pos, test::blockSize);
        }

        return back;
    }

    std::vector<float> noise (int numSamples, float amplitude = 0.25f)
    {
        juce::Random random (0x51a7e);
        std::vector<float> out ((size_t) numSamples);

        for (auto& sample : out)
            sample = amplitude * (random.nextFloat() * 2.0f - 1.0f);

        return out;
    }

    std::unique_ptr<PedalChain> makeChain (const PedalChain::Settings& settings)
    {
        auto chain = std::make_unique<PedalChain>();
        chain->prepare (sr, test::blockSize);
        chain->setSettings (settings);
        chain->snapToSettings();
        return chain;
    }
}

TEST_CASE ("With every pedal off the chain leaves the signal alone", "[pedals]")
{
    auto chain = makeChain ({});

    const auto input = Signal::sine (440.0, test::blockSize * 8);

    auto before = runThrough (*chain, input, true);
    auto after = runThrough (*chain, input, false);

    // The drive pedal's oversampler always runs, so "alone" means within its filters' ripple and
    // its five samples of latency — compared well past both.
    REQUIRE_THAT (Signal::rms (before, test::blockSize), WithinAbs (Signal::rms (input, test::blockSize), 1.0e-3));
    REQUIRE_THAT (Signal::rms (after, test::blockSize), WithinAbs (Signal::rms (input, test::blockSize), 1.0e-6));
}

TEST_CASE ("The gate closes on a signal below its threshold", "[pedals]")
{
    PedalChain::Settings settings;
    settings.gateEngaged = true;
    settings.gateThresholdDb = -40.0f;

    // A quiet hiss, well under the threshold.
    auto quiet = Signal::sine (440.0, test::blockSize * 20, 0.002f);
    auto chain = makeChain (settings);
    const auto gated = runKeyed (*chain, quiet, quiet);

    // And a note well over it.
    auto loud = Signal::sine (440.0, test::blockSize * 20, 0.3f);
    auto openChain = makeChain (settings);
    const auto passed = runKeyed (*openChain, loud, loud);

    const auto measureFrom = test::blockSize * 10;

    REQUIRE (Signal::rms (gated, measureFrom) < 0.25f * Signal::rms (quiet, measureFrom));
    REQUIRE (Signal::rms (passed, measureFrom) > 0.9f * Signal::rms (loud, measureFrom));
}

TEST_CASE ("The gate closes on noise the amp made, not only on noise at the input", "[pedals]")
{
    // The reason the gate is keyed. Hiss from a high-gain capture is generated past the point a
    // pedal in front of the amp can reach, so a gate that only looks at its own input cannot
    // remove it. Here the guitar is silent and the noise appears after the amp.
    PedalChain::Settings settings;
    settings.gateEngaged = true;
    settings.gateThresholdDb = -40.0f;

    const std::vector<float> silentGuitar ((size_t) (test::blockSize * 20), 0.0f);
    const auto ampHiss = noise (test::blockSize * 20, 0.01f);

    auto chain = makeChain (settings);
    const auto gated = runKeyed (*chain, silentGuitar, ampHiss);

    const auto measureFrom = test::blockSize * 10;
    REQUIRE (Signal::rms (gated, measureFrom) < 0.1f * Signal::rms (ampHiss, measureFrom));

    // And it opens for that same hiss the moment the guitar is played, because the key is what
    // decides — a gate that stayed shut while you played would be worse than none.
    auto playing = makeChain (settings);
    const auto note = Signal::sine (110.0, test::blockSize * 20, 0.3f);
    const auto open = runKeyed (*playing, note, ampHiss);

    REQUIRE (Signal::rms (open, measureFrom) > 0.9f * Signal::rms (ampHiss, measureFrom));
}

TEST_CASE ("The compressor narrows the range between quiet and loud", "[pedals]")
{
    PedalChain::Settings settings;
    settings.compressorEngaged = true;
    settings.compressorAmount = 1.0f;
    settings.compressorLevelDb = 0.0f;

    const auto outputFor = [&] (float amplitude)
    {
        auto chain = makeChain (settings);
        auto input = Signal::sine (220.0, test::blockSize * 30, amplitude);
        return Signal::rms (runThrough (*chain, input, true), test::blockSize * 20);
    };

    const auto quietOut = outputFor (0.05f);
    const auto loudOut = outputFor (0.5f);

    // 20 dB in should come out as appreciably less than 20 dB.
    const auto outputRangeDb = juce::Decibels::gainToDecibels (loudOut / quietOut);

    INFO ("input range 20 dB became " << outputRangeDb << " dB");
    REQUIRE (outputRangeDb < 14.0f);
    REQUIRE (outputRangeDb > 0.0f);
}

TEST_CASE ("The drive pedal adds harmonics rather than level alone", "[pedals]")
{
    PedalChain::Settings settings;
    settings.driveEngaged = true;
    settings.driveAmount = 0.9f;
    settings.driveTone = 1.0f;
    settings.driveLevelDb = 0.0f;

    auto chain = makeChain (settings);

    constexpr double frequency = 220.0;
    auto input = Signal::sine (frequency, test::blockSize * 16, 0.3f);
    const auto driven = runThrough (*chain, input, true);

    // Energy at the fundamental against everything else, by correlating with the input tone.
    const auto fundamentalEnergy = [&] (const std::vector<float>& x)
    {
        double real = 0.0, imaginary = 0.0;
        const auto from = (size_t) test::blockSize * 4;

        for (size_t i = from; i < x.size(); ++i)
        {
            const auto phase = juce::MathConstants<double>::twoPi * frequency * (double) i / sr;
            real += x[i] * std::cos (phase);
            imaginary += x[i] * std::sin (phase);
        }

        const auto count = (double) (x.size() - from);
        return 2.0 * std::sqrt (real * real + imaginary * imaginary) / count;
    };

    const auto total = Signal::rms (driven, test::blockSize * 4) * std::sqrt (2.0f);
    const auto fundamental = (float) fundamentalEnergy (driven);

    REQUIRE (fundamental > 0.0f);

    // A clean gain stage would put everything at the fundamental; a clipper does not.
    const auto harmonicShare = 1.0f - (fundamental * fundamental) / (total * total);

    INFO ("harmonic share " << harmonicShare);
    REQUIRE (harmonicShare > 0.02f);
}

TEST_CASE ("The delay repeats the signal at the time it was given", "[pedals]")
{
    PedalChain::Settings settings;
    settings.delayEngaged = true;
    settings.delayTimeSeconds = 0.1f;
    settings.delayFeedback = 0.0f;
    settings.delayMix = 1.0f;          // wet only, so the repeat is unmistakable

    auto chain = makeChain (settings);

    // One short burst, then silence.
    std::vector<float> input ((size_t) test::blockSize * 40, 0.0f);
    for (int i = 0; i < 64; ++i)
        input[(size_t) i] = 0.5f;

    const auto out = runThrough (*chain, input, false);

    const auto expectedAt = (int) (0.1 * sr);

    // The burst should be gone from the start and present around the delay time.
    const auto atStart = Signal::peak ({ out.begin() + 128, out.begin() + 1000 });
    const auto atDelay = Signal::peak ({ out.begin() + expectedAt - 32, out.begin() + expectedAt + 128 });

    INFO ("start " << atStart << ", at delay " << atDelay);
    REQUIRE (atDelay > 0.3f);
    REQUIRE (atStart < 0.05f);
}

TEST_CASE ("The reverb keeps sounding after the signal stops", "[pedals]")
{
    PedalChain::Settings settings;
    settings.reverbEngaged = true;
    settings.reverbSize = 0.8f;
    settings.reverbMix = 0.6f;

    auto chain = makeChain (settings);

    std::vector<float> input ((size_t) test::blockSize * 40, 0.0f);
    const auto burst = Signal::sine (440.0, test::blockSize * 4, 0.5f);
    std::copy (burst.begin(), burst.end(), input.begin());

    const auto out = runThrough (*chain, input, false);

    const auto tail = Signal::rms ({ out.begin() + test::blockSize * 8, out.begin() + test::blockSize * 12 });

    REQUIRE (tail > 0.001f);
}

TEST_CASE ("The chorus modulates the signal over time", "[pedals]")
{
    PedalChain::Settings settings;
    settings.chorusEngaged = true;
    settings.chorusRateHz = 4.0f;
    settings.chorusDepth = 1.0f;
    settings.chorusMix = 0.5f;

    auto chain = makeChain (settings);

    auto input = Signal::sine (440.0, test::blockSize * 40, 0.3f);
    const auto out = runThrough (*chain, input, false);

    // A modulated signal's level wanders as the delayed copy drifts in and out of phase; a static
    // one does not.
    float quietest = 1.0f, loudest = 0.0f;

    for (int b = 10; b < 38; ++b)
    {
        const std::vector<float> block { out.begin() + b * test::blockSize,
                                         out.begin() + (b + 1) * test::blockSize };
        const auto level = Signal::rms (block);
        quietest = juce::jmin (quietest, level);
        loudest = juce::jmax (loudest, level);
    }

    INFO ("level wandered between " << quietest << " and " << loudest);
    REQUIRE (loudest > quietest * 1.05f);
}

TEST_CASE ("Each pedal can be switched in and out without a click", "[pedals]")
{
    // A drive pedal's own output is nearly a square wave, whose sample-to-sample slope is far
    // larger than any switching artefact — so a fixed threshold would measure the pedal rather
    // than the switch. Measure each pedal running continuously, then compare a run that switches
    // it in halfway.
    struct Case { const char* name; void (*engage) (PedalChain::Settings&); bool beforeAmp; };

    const Case cases[]
    {
        { "gate",       [] (PedalChain::Settings& s) { s.gateEngaged = true; s.gateThresholdDb = -30.0f; }, true },
        { "compressor", [] (PedalChain::Settings& s) { s.compressorEngaged = true; s.compressorLevelDb = 9.0f; }, true },
        { "drive",      [] (PedalChain::Settings& s) { s.driveEngaged = true; s.driveAmount = 1.0f; }, true },
        { "chorus",     [] (PedalChain::Settings& s) { s.chorusEngaged = true; }, false },
        { "delay",      [] (PedalChain::Settings& s) { s.delayEngaged = true; s.delayMix = 1.0f; }, false },
        { "reverb",     [] (PedalChain::Settings& s) { s.reverbEngaged = true; s.reverbMix = 1.0f; }, false },
    };

    for (const auto& testCase : cases)
    {
        PedalChain::Settings engaged;
        testCase.engage (engaged);

        const auto worstJumpOver = [&] (bool switchMidway)
        {
            auto chain = makeChain (switchMidway ? PedalChain::Settings {} : engaged);

            std::vector<float> block ((size_t) test::blockSize);
            double phase = 0.0;
            const auto step = juce::MathConstants<double>::twoPi * 110.0 / sr;

            float worstJump = 0.0f, previous = 0.0f;

            for (int b = 0; b < 30; ++b)
            {
                if (switchMidway && b == 10)
                    chain->setSettings (engaged);

                for (int i = 0; i < test::blockSize; ++i)
                {
                    block[(size_t) i] = 0.3f * (float) std::sin (phase);
                    phase += step;
                }

                if (testCase.beforeAmp)
                    chain->processBeforeAmp (block.data(), test::blockSize);
                else
                    chain->processAfterAmp (block.data(), test::blockSize);

                for (int i = 0; i < test::blockSize; ++i)
                {
                    if (b > 0)
                        worstJump = juce::jmax (worstJump, std::abs (block[(size_t) i] - previous));

                    previous = block[(size_t) i];
                }
            }

            return worstJump;
        };

        const auto running = worstJumpOver (false);
        const auto switched = worstJumpOver (true);

        INFO (testCase.name << ": running " << running << ", switched " << switched);

        // Switching must not introduce a discontinuity beyond what the pedal already makes.
        REQUIRE (switched <= running * 1.15f + 0.005f);
    }
}

TEST_CASE ("The pedal groups stay on their own side of the amp", "[pedals][processor]")
{
    // The placement is the design, so it is worth a test that fails if someone merges the two
    // groups into one list: a drive pedal in front of the amp changes what the amp distorts, and
    // after it would only add fizz to an already-distorted signal.
    auto chain = makeChain ({});

    PedalChain::Settings driveOnly;
    driveOnly.driveEngaged = true;
    driveOnly.driveAmount = 1.0f;
    chain->setSettings (driveOnly);
    chain->snapToSettings();

    auto input = Signal::sine (220.0, test::blockSize * 8, 0.3f);

    const auto throughFront = runThrough (*chain, input, true);

    auto rearChain = makeChain (driveOnly);
    const auto throughRear = runThrough (*rearChain, input, false);

    // Drive belongs to the front group, so only that side changes the signal.
    const auto untouched = Signal::rms (input, test::blockSize * 2);
    REQUIRE (std::abs (Signal::rms (throughFront, test::blockSize * 2) - untouched) > 0.02f * untouched);
    REQUIRE_THAT (Signal::rms (throughRear, test::blockSize * 2),
                  WithinAbs (Signal::rms (input, test::blockSize * 2), 1.0e-6));
}

TEST_CASE ("The gate fades what is under the threshold rather than muting it", "[pedals]")
{
    // A downward expander takes (ratio - 1) times however far a signal sits below the threshold.
    // At 10:1 a note tail 10 dB under loses 90 dB and simply disappears, which is what makes a
    // gate feel like a guillotine. The ratio here is chosen so that the same tail fades.
    const auto reductionAt = [] (float thresholdDb, float signalDb)
    {
        PedalChain::Settings settings;
        settings.gateEngaged = true;
        settings.gateThresholdDb = thresholdDb;

        auto chain = makeChain (settings);

        const auto amplitude = juce::Decibels::decibelsToGain (signalDb) * std::sqrt (2.0f);
        auto input = Signal::sine (220.0, test::blockSize * 60, amplitude);

        const auto output = runKeyed (*chain, input, input);

        const auto measureFrom = test::blockSize * 40;
        const auto before = Signal::rms (input, measureFrom);
        const auto after = Signal::rms (output, measureFrom);

        return juce::Decibels::gainToDecibels (after / juce::jmax (1.0e-9f, before));
    };

    // Ten decibels under: audibly reduced, still clearly there.
    const auto justUnder = reductionAt (-50.0f, -60.0f);
    INFO ("10 dB under the threshold: " << justUnder << " dB");
    REQUIRE (justUnder < -8.0f);
    REQUIRE (justUnder > -40.0f);

    // Well under, which is where hiss lives: gone.
    const auto wellUnder = reductionAt (-50.0f, -85.0f);
    INFO ("35 dB under the threshold: " << wellUnder << " dB");
    REQUIRE (wellUnder < -50.0f);

    // Above it, untouched.
    const auto above = reductionAt (-50.0f, -30.0f);
    INFO ("20 dB over the threshold: " << above << " dB");
    REQUIRE (above > -0.5f);
}

TEST_CASE ("The reverb's mix adds to the dry signal rather than replacing it", "[pedals]")
{
    // A reverb pedal adds: the dry signal is untouched at every setting and the wet is mixed on
    // top of it. Crossfading to fully wet instead takes the note's attack away with the dry, which
    // sounds like lost level and added latency rather than like more reverb.
    //
    // The dry gain is measured rather than read: against noise, the wet path is decorrelated at
    // zero lag, so projecting the output onto the input recovers what is left of the dry.
    const auto dryGainAt = [] (float mix)
    {
        PedalChain::Settings settings;
        settings.reverbEngaged = true;
        settings.reverbMix = mix;
        settings.reverbSize = 0.5f;

        auto chain = makeChain (settings);

        const auto input = noise (test::blockSize * 40);
        const auto output = runThrough (*chain, input, false);

        double dot = 0.0, energy = 0.0;

        for (size_t i = (size_t) (test::blockSize * 8); i < input.size(); ++i)
        {
            dot += (double) output[i] * input[i];
            energy += (double) input[i] * input[i];
        }

        return (float) (dot / energy);
    };

    REQUIRE_THAT (dryGainAt (0.0f), WithinAbs (1.0, 0.02));
    REQUIRE_THAT (dryGainAt (0.5f), WithinAbs (1.0, 0.08));
    REQUIRE_THAT (dryGainAt (1.0f), WithinAbs (1.0, 0.08));
}

TEST_CASE ("Turning the reverb up does not turn the signal down", "[pedals]")
{
    // The symptom that says the dry has been crossfaded away: more reverb, less sound. Whatever
    // the mix is set to, the level should hold — a player reaches for this control to change the
    // space, not the volume.
    const auto levelAt = [] (float mix)
    {
        PedalChain::Settings settings;
        settings.reverbEngaged = true;
        settings.reverbMix = mix;
        settings.reverbSize = 0.5f;

        auto chain = makeChain (settings);

        const auto input = Signal::sine (220.0, test::blockSize * 40);
        return Signal::rms (runThrough (*chain, input, false), test::blockSize * 8);
    };

    const auto dry = levelAt (0.0f);

    for (const auto mix : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto level = juce::Decibels::gainToDecibels (levelAt (mix) / dry);

        INFO ("mix " << mix << ": " << level << " dB against the dry signal");
        REQUIRE (level > -0.5f);
        REQUIRE (level < 6.0f);
    }
}

TEST_CASE ("A reverb at zero mix is inaudible rather than six decibels loud", "[pedals]")
{
    PedalChain::Settings settings;
    settings.reverbEngaged = true;
    settings.reverbMix = 0.0f;

    auto chain = makeChain (settings);

    const auto input = Signal::sine (440.0, test::blockSize * 8);
    const auto output = runThrough (*chain, input, false);

    // Exactly, not nearly: with no wet signal in the sum there is nothing left to be approximate
    // about, and the old dry scaling made this twice the input.
    REQUIRE_THAT (Signal::rms (output, test::blockSize), WithinAbs (Signal::rms (input, test::blockSize), 1.0e-6));
}

TEST_CASE ("The drive pedal boosts into the clipper above a corner, not across the band", "[pedals]")
{
    // What a drive in front of an amp is for. Clipping the whole range equally flattens a
    // palm-muted low string along with everything else; a screamer boosts only what is above a
    // few hundred hertz into its clipping stage and lets the rest through at the level it
    // arrived, which is what keeps the low end defined under distortion.
    const auto gainAt = [] (double frequency, float drive)
    {
        PedalChain::Settings settings;
        settings.driveEngaged = true;
        settings.driveAmount = drive;
        settings.driveTone = 1.0f;      // wide open, so the tone control is not what is measured
        settings.driveLevelDb = 0.0f;

        auto chain = makeChain (settings);

        // Small enough that the shaper is still near enough linear to read a gain off it.
        const auto input = Signal::sine (frequency, test::blockSize * 40, 0.0005f);
        const auto output = runThrough (*chain, input, true);

        const auto measureFrom = test::blockSize * 20;
        return juce::Decibels::gainToDecibels (Signal::rms (output, measureFrom)
                                                   / Signal::rms (input, measureFrom));
    };

    const auto lowAtFull = gainAt (80.0, 1.0f);
    const auto midAtFull = gainAt (1500.0, 1.0f);

    INFO ("at full drive: 80 Hz " << lowAtFull << " dB, 1.5 kHz " << midAtFull << " dB");

    // The band above the corner is driven far harder than the band below it. The gap is the
    // first-order slope's, which is what the pedal this stands in for has: about 18 dB, so the
    // low end is still driven, just nothing like as hard.
    REQUIRE (midAtFull - lowAtFull > 15.0f);

    // And with the knob down the pedal is flat, so the corner is something the drive control
    // brings in rather than a filter sitting in the signal path whatever you do.
    const auto lowAtZero = gainAt (80.0, 0.0f);
    const auto midAtZero = gainAt (1500.0, 0.0f);

    INFO ("at zero drive: 80 Hz " << lowAtZero << " dB, 1.5 kHz " << midAtZero << " dB");
    REQUIRE (std::abs (midAtZero - lowAtZero) < 1.0f);
}

namespace
{
    /** A host that reports one tempo and nothing else, which is all the delay asks for. */
    struct FixedTempo final : juce::AudioPlayHead
    {
        explicit FixedTempo (double beatsPerMinute) : bpm (beatsPerMinute) {}

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            return info;
        }

        double bpm;
    };

    /** Where the delay's single repeat lands, in samples, measured through the whole processor. */
    int measureDelaySamples (AmpSimAudioProcessor& processor, int searchBlocks = 60)
    {
        juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), test::blockSize);
        juce::MidiBuffer midi;

        // The delay's time is smoothed over a quarter of a second on purpose, so a sweep bends
        // pitch rather than jumping. Let it arrive before asking where the repeat is.
        for (int b = 0; b < test::blocksForRamp (0.3); ++b)
        {
            buffer.clear();
            processor.processBlock (buffer, midi);
        }

        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        processor.processBlock (buffer, midi);

        int position = -1;
        float loudest = 0.0f;

        for (int b = 0; b < searchBlocks; ++b)
        {
            if (b > 0)
            {
                buffer.clear();
                processor.processBlock (buffer, midi);
            }

            for (int i = 0; i < test::blockSize; ++i)
            {
                // Past the dry impulse itself, which the mix at 1 should have removed anyway.
                if (b == 0 && i < 64)
                    continue;

                if (const auto level = std::abs (buffer.getSample (0, i)); level > loudest)
                {
                    loudest = level;
                    position = b * test::blockSize + i;
                }
            }
        }

        return position;
    }
}

TEST_CASE ("The delay follows the host's tempo when it is synced", "[pedals]")
{
    FixedTempo tempo { 120.0 };

    const auto repeatAt = [&tempo] (int division)
    {
        auto processor = test::makePreparedProcessor();
        processor->setPlayHead (&tempo);

        auto& state = processor->getValueTreeState();
        test::setParam (state, ParamID::delayOn, 1.0f);
        test::setParam (state, ParamID::delayMix, 1.0f);
        test::setParam (state, ParamID::delayFeedback, 0.0f);

        // A choice parameter is set by its index, the way the host sees it.
        state.getParameter (ParamID::delayDivision)
             ->setValueNotifyingHost ((float) division / 8.0f);

        return measureDelaySamples (*processor);
    };

    // At 120 BPM a quarter note is half a second, and an eighth is half of that. Within a
    // millisecond: the search is sample-accurate, the smoother is not quite.
    const auto quarter = repeatAt (3);
    const auto eighth = repeatAt (6);

    INFO ("quarter " << quarter << " samples, eighth " << eighth);

    REQUIRE (std::abs (quarter - 24000) < 48);
    REQUIRE (std::abs (eighth - 12000) < 48);
}

TEST_CASE ("The delay ignores the tempo while it is set to Free", "[pedals]")
{
    FixedTempo tempo { 120.0 };

    auto processor = test::makePreparedProcessor();
    processor->setPlayHead (&tempo);

    auto& state = processor->getValueTreeState();
    test::setParam (state, ParamID::delayOn, 1.0f);
    test::setParam (state, ParamID::delayMix, 1.0f);
    test::setParam (state, ParamID::delayFeedback, 0.0f);
    test::setParam (state, ParamID::delayTime, 0.2f);

    // Division 0 is Free, which is where a session saved before any of this existed lands.
    REQUIRE (std::abs (measureDelaySamples (*processor) - 9600) < 48);
}
