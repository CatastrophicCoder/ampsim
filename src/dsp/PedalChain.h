#pragma once

#include "pedals/ChorusPedal.h"
#include "pedals/CompressorPedal.h"
#include "pedals/DelayPedal.h"
#include "pedals/DrivePedal.h"
#include "pedals/NoiseGatePedal.h"
#include "pedals/ReverbPedal.h"

/** The pedalboard, in two fixed groups either side of the amp.

    The placement is the point, and it is not user-reorderable. Gain-stage pedals go in front of
    the amp because an overdrive works by changing what the preamp distorts; modulation and time
    effects go after it, so the repeats and modulated copies are of the already-distorted tone.
    Running them the other way round is what a beginner's pedalboard sounds like.

    `processBeforeAmp` and `processAfterAmp` are separate calls rather than one list, so the amp
    physically cannot end up on the wrong side of a pedal.
*/
class PedalChain
{
public:
    /** Every pedal's state in one object, refreshed per block from the parameters. A single
        struct keeps the processor's per-block update to one call instead of eighteen. */
    struct Settings
    {
        bool gateEngaged = false;
        float gateThresholdDb = -60.0f;

        bool compressorEngaged = false;
        float compressorAmount = 0.4f, compressorLevelDb = 0.0f;

        bool driveEngaged = false;
        float driveAmount = 0.5f, driveTone = 0.5f, driveLevelDb = 0.0f;

        bool chorusEngaged = false;
        float chorusRateHz = 1.2f, chorusDepth = 0.35f, chorusMix = 0.4f;

        bool delayEngaged = false;
        float delayTimeSeconds = 0.35f, delayFeedback = 0.35f, delayMix = 0.3f;

        bool reverbEngaged = false;
        float reverbSize = 0.5f, reverbMix = 0.25f;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Audio thread. Cheap: setters only. */
    void setSettings (const Settings&);

    /** Message thread, from prepareToPlay: start at the current settings rather than ramping
        into them on every playback start. */
    void snapToSettings();

    void processBeforeAmp (float* samples, int numSamples);
    void processAfterAmp (float* samples, int numSamples);

    /** The drive pedal's oversampler, which runs whether or not the pedal is engaged so that this
        number never changes under the host. */
    int getLatencySamples() const;

private:
    NoiseGatePedal gate;
    CompressorPedal compressor;
    DrivePedal drive;

    ChorusPedal chorus;
    DelayPedal delay;
    ReverbPedal reverb;

    Settings settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalChain)

public:
    PedalChain() = default;
};
