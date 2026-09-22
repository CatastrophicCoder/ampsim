#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

namespace
{
    juce::MidiBuffer controllerMessage (int controller, int value, int channel = 1)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (channel, controller, value), 0);
        return midi;
    }

    void runBlockWith (AmpSimAudioProcessor& processor, juce::MidiBuffer& midi)
    {
        juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), test::blockSize);
        buffer.clear();
        processor.processBlock (buffer, midi);
    }

    /** Learning is finished by the message thread, which in the plugin is a timer. */
    void learn (AmpSimAudioProcessor& processor, const juce::String& parameterID, int controller)
    {
        processor.getMidiLearn().startLearning (parameterID);

        auto midi = controllerMessage (controller, 64);
        runBlockWith (processor, midi);

        REQUIRE (processor.getMidiLearn().commitPendingLearn());
    }
}

TEST_CASE ("The plugin accepts MIDI so a controller can reach it", "[midi]")
{
    AmpSimAudioProcessor processor;
    REQUIRE (processor.acceptsMidi());
}

TEST_CASE ("A learned controller moves its parameter", "[midi]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    learn (processor, ParamID::inputGain, 7);

    REQUIRE (processor.getMidiLearn().getControllerFor (ParamID::inputGain) == 7);

    auto full = controllerMessage (7, 127);
    runBlockWith (processor, full);

    REQUIRE_THAT (test::getParam (processor.getValueTreeState(), ParamID::inputGain),
                  WithinAbs (24.0, 0.1));

    auto none = controllerMessage (7, 0);
    runBlockWith (processor, none);

    REQUIRE_THAT (test::getParam (processor.getValueTreeState(), ParamID::inputGain),
                  WithinAbs (-24.0, 0.1));
}

TEST_CASE ("An unmapped controller is ignored", "[midi]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    test::setParam (processor.getValueTreeState(), ParamID::inputGain, 6.0f);

    auto midi = controllerMessage (42, 127);
    runBlockWith (processor, midi);

    REQUIRE_THAT (test::getParam (processor.getValueTreeState(), ParamID::inputGain),
                  WithinAbs (6.0, 0.05));
}

TEST_CASE ("One controller drives one parameter, and one parameter answers to one controller", "[midi]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    auto& midiLearn = processor.getMidiLearn();

    learn (processor, ParamID::bass, 20);
    learn (processor, ParamID::treble, 20);      // same controller, different parameter

    REQUIRE (midiLearn.getControllerFor (ParamID::bass) == -1);
    REQUIRE (midiLearn.getControllerFor (ParamID::treble) == 20);

    learn (processor, ParamID::treble, 21);      // same parameter, different controller

    REQUIRE (midiLearn.getControllerFor (ParamID::treble) == 21);

    // The controller it used to answer to must no longer move it.
    test::setParam (processor.getValueTreeState(), ParamID::treble, 0.0f);

    auto oldController = controllerMessage (20, 127);
    runBlockWith (processor, oldController);

    REQUIRE_THAT (test::getParam (processor.getValueTreeState(), ParamID::treble),
                  WithinAbs (0.0, 0.05));
}

TEST_CASE ("A mapping can be cleared", "[midi]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    learn (processor, ParamID::mid, 11);
    processor.getMidiLearn().clearMapping (ParamID::mid);

    REQUIRE (processor.getMidiLearn().getControllerFor (ParamID::mid) == -1);

    test::setParam (processor.getValueTreeState(), ParamID::mid, 0.0f);

    auto midi = controllerMessage (11, 127);
    runBlockWith (processor, midi);

    REQUIRE_THAT (test::getParam (processor.getValueTreeState(), ParamID::mid), WithinAbs (0.0, 0.05));
}

TEST_CASE ("Mappings are saved with the session", "[midi][state]")
{
    juce::MemoryBlock saved;

    {
        AmpSimAudioProcessor source;
        source.prepareToPlay (test::sampleRate, test::blockSize);
        learn (source, ParamID::outputGain, 91);
        source.getStateInformation (saved);
    }

    AmpSimAudioProcessor restored;
    restored.prepareToPlay (test::sampleRate, test::blockSize);

    REQUIRE (restored.getMidiLearn().getControllerFor (ParamID::outputGain) == -1);

    restored.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (restored.getMidiLearn().getControllerFor (ParamID::outputGain) == 91);

    auto midi = controllerMessage (91, 127);
    runBlockWith (restored, midi);

    REQUIRE_THAT (test::getParam (restored.getValueTreeState(), ParamID::outputGain),
                  WithinAbs (24.0, 0.1));
}

TEST_CASE ("Learning captures the next controller and then stops", "[midi]")
{
    AmpSimAudioProcessor processor;
    processor.prepareToPlay (test::sampleRate, test::blockSize);

    auto& midiLearn = processor.getMidiLearn();

    midiLearn.startLearning (ParamID::driveAmount);
    REQUIRE (midiLearn.isLearning());
    REQUIRE (midiLearn.getLearningParameter() == juce::String (ParamID::driveAmount));

    auto first = controllerMessage (30, 64);
    runBlockWith (processor, first);
    REQUIRE (midiLearn.commitPendingLearn());

    REQUIRE_FALSE (midiLearn.isLearning());
    REQUIRE (midiLearn.getControllerFor (ParamID::driveAmount) == 30);

    // A second controller arriving afterwards must not be swallowed as another mapping.
    auto second = controllerMessage (31, 64);
    runBlockWith (processor, second);

    REQUIRE_FALSE (midiLearn.commitPendingLearn());
    REQUIRE (midiLearn.getControllerFor (ParamID::driveAmount) == 30);
}
