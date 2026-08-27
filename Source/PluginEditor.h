#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ClearRoomAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit ClearRoomAudioProcessorEditor (ClearRoomAudioProcessor&);
    ~ClearRoomAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ClearRoomAudioProcessor& audioProcessor;
    juce::Slider mixKnob;
    juce::Label mixLabel;
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> mixAttachment;

    class ClearRoomLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float,
                               float, float, juce::Slider&) override;
    } lookAndFeel;

    void timerCallback() override;
    float displayedReduction = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearRoomAudioProcessorEditor)
};
