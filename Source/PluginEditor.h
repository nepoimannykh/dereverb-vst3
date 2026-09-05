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
    // Black ground with a single light-green signal colour, as specified. Everything that
    // is not the signal stays near-invisible so the envelope reads as the only bright
    // thing on the panel.
    struct Palette
    {
        static constexpr juce::uint32 ground     = 0xff000000;
        static constexpr juce::uint32 signal     = 0xff90ee90;   // light green
        static constexpr juce::uint32 signalDeep = 0xff2f7a3c;
        static constexpr juce::uint32 residue    = 0xff14331a;   // dry tail behind the wet
        static constexpr juce::uint32 rule       = 0xff112114;
        static constexpr juce::uint32 textBright = 0xffe4fbe6;
        static constexpr juce::uint32 textDim    = 0xff6f8f74;
        static constexpr juce::uint32 alert      = 0xffff7a6b;
    };

    ClearRoomAudioProcessor& audioProcessor;
    juce::Slider mixKnob;
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> mixAttachment;

    class ClearRoomLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics&, int, int, int, int, float,
                               float, float, juce::Slider&) override;
    } lookAndFeel;

    void timerCallback() override;
    void paintEnvelope (juce::Graphics&, juce::Rectangle<float>) const;
    float measuredRemovalDb (bool& hasSignal) const;

    juce::Rectangle<float> envelopeBounds() const;
    static juce::Font titleFont();
    static juce::Font labelFont();
    static juce::Font readoutFont();

    std::array<float, ClearRoomAudioProcessor::envelopePoints> dryPoints {};
    std::array<float, ClearRoomAudioProcessor::envelopePoints> wetPoints {};
    float displayedReduction = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClearRoomAudioProcessorEditor)
};
