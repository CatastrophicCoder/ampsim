/*
    This file is part of AmpSim, a guitar amp simulator built on Neural Amp Modeler.
    Copyright (C) 2026 Kimmo Fonsell

    AmpSim is free software: you can redistribute it and/or modify it under the terms of the GNU
    Affero General Public License as published by the Free Software Foundation, either version 3
    of the License, or (at your option) any later version. See the LICENSE file, or
    <https://www.gnu.org/licenses/>.
*/

#include "MidiLearn.h"

namespace
{
    const juce::Identifier midiMapTag { "midiMap" };
    const juce::Identifier parameterTag { "parameter" };
    const juce::Identifier parameterIdProperty { "id" };
    const juce::Identifier controllerProperty { "cc" };
}

MidiLearn::MidiLearn (juce::AudioProcessorValueTreeState& stateToUse)
    : state (stateToUse)
{
    for (auto* parameter : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            parameters.add (ranged);

    for (auto& entry : controllerToParameter)
        entry.store (-1);

    rebuildFromState();
}

int MidiLearn::indexOf (const juce::String& parameterID) const
{
    for (int i = 0; i < parameters.size(); ++i)
        if (parameters[i]->paramID == parameterID)
            return i;

    return -1;
}

juce::ValueTree MidiLearn::mapTree()
{
    auto tree = state.state.getOrCreateChildWithName (midiMapTag, nullptr);
    return tree;
}

void MidiLearn::processMidi (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (! message.isController())
            continue;

        const auto controller = message.getControllerNumber();

        if (! juce::isPositiveAndBelow (controller, numControllers))
            continue;

        // Learning: hand the controller to the message thread rather than writing the map here.
        if (learningIndex.load() >= 0)
        {
            pendingController.store (controller);
            continue;
        }

        const auto parameterIndex = controllerToParameter[(size_t) controller].load();

        if (parameterIndex < 0 || parameterIndex >= parameters.size())
            continue;

        auto* parameter = parameters[parameterIndex];
        const auto value = (float) message.getControllerValue() / 127.0f;

        // Standard practice for MIDI-driven parameters, and what a host's own automation does:
        // the listeners an APVTS attaches post their updates asynchronously.
        parameter->setValueNotifyingHost (value);
    }
}

void MidiLearn::startLearning (const juce::String& parameterID)
{
    pendingController.store (-1);
    learningIndex.store (indexOf (parameterID));
}

void MidiLearn::stopLearning()
{
    learningIndex.store (-1);
    pendingController.store (-1);
}

juce::String MidiLearn::getLearningParameter() const
{
    const auto index = learningIndex.load();

    return juce::isPositiveAndBelow (index, parameters.size()) ? parameters[index]->paramID
                                                               : juce::String();
}

bool MidiLearn::commitPendingLearn()
{
    const auto controller = pendingController.load();
    const auto index = learningIndex.load();

    if (controller < 0 || index < 0)
        return false;

    const auto parameterID = parameters[index]->paramID;

    auto tree = mapTree();

    // One controller drives one parameter, and one parameter answers to one controller: drop any
    // entry that would break either.
    for (int i = tree.getNumChildren(); --i >= 0;)
    {
        const auto child = tree.getChild (i);

        if (child[parameterIdProperty].toString() == parameterID
            || (int) child[controllerProperty] == controller)
            tree.removeChild (i, nullptr);
    }

    juce::ValueTree entry { parameterTag };
    entry.setProperty (parameterIdProperty, parameterID, nullptr);
    entry.setProperty (controllerProperty, controller, nullptr);
    tree.appendChild (entry, nullptr);

    stopLearning();
    rebuildFromState();

    return true;
}

int MidiLearn::getControllerFor (const juce::String& parameterID) const
{
    const auto index = indexOf (parameterID);

    if (index < 0)
        return -1;

    for (int controller = 0; controller < numControllers; ++controller)
        if (controllerToParameter[(size_t) controller].load() == index)
            return controller;

    return -1;
}

void MidiLearn::clearMapping (const juce::String& parameterID)
{
    auto tree = mapTree();

    for (int i = tree.getNumChildren(); --i >= 0;)
        if (tree.getChild (i)[parameterIdProperty].toString() == parameterID)
            tree.removeChild (i, nullptr);

    rebuildFromState();
}

void MidiLearn::rebuildFromState()
{
    std::array<int, numControllers> rebuilt;
    rebuilt.fill (-1);

    const auto tree = state.state.getChildWithName (midiMapTag);

    for (const auto child : tree)
    {
        const auto controller = (int) child[controllerProperty];
        const auto index = indexOf (child[parameterIdProperty].toString());

        if (juce::isPositiveAndBelow (controller, numControllers) && index >= 0)
            rebuilt[(size_t) controller] = index;
    }

    for (int controller = 0; controller < numControllers; ++controller)
        controllerToParameter[(size_t) controller].store (rebuilt[(size_t) controller]);

    if (onMappingChanged != nullptr)
        onMappingChanged();
}
