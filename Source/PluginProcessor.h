#pragma once

#include <JuceHeader.h>
#include "NeuralEnhancer.h"

class ClearRoomAudioProcessor final : public juce::AudioProcessor
{
public:
    ClearRoomAudioProcessor();

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    float getReductionDb() const noexcept { return neural.getReductionDb(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int maximumChannels = 16;
    NeuralEnhancer neural;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoother;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearRoomAudioProcessor)
};
