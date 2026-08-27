#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr const char* amountId = "amount";
constexpr const char* tailId = "tail";
constexpr const char* focusId = "focus";
constexpr const char* mixId = "mix";
}

ClearRoomAudioProcessor::ClearRoomAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ClearRoomAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> values;
    values.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { amountId, 1 }, "Reduction",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, "%"));
    values.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { tailId, 1 }, "Room Tail",
        juce::NormalisableRange<float> { 80.0f, 1200.0f, 1.0f, 0.45f }, 1200.0f, " ms"));
    values.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { focusId, 1 }, "Voice Protect",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 0.0f, "%"));
    values.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { mixId, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, "%"));
    return { values.begin(), values.end() };
}

void ClearRoomAudioProcessor::prepareToPlay (double sampleRate, int)
{
    neural.prepare (sampleRate, juce::jmin (getTotalNumInputChannels(), maximumChannels));
    setLatencySamples (neural.isReady() ? neural.getLatencySamples() : 0);
}

bool ClearRoomAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    return ! input.isDisabled()
        && input == layouts.getMainOutputChannelSet()
        && input.size() <= maximumChannels;
}

void ClearRoomAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto channels = juce::jmin (buffer.getNumChannels(), maximumChannels);
    const float mix = parameters.getRawParameterValue (mixId)->load() * 0.01f;
    // The neural enhancer and hall removal are intentionally fixed at maximum;
    // Mix is the only user-facing control and blends against the latency-matched dry signal.
    neural.setStrength (mix);

    for (int channel = 0; channel < channels; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            samples[i] = neural.processSample (channel, samples[i]);
    }

    for (int channel = channels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

void ClearRoomAudioProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, data);
}

void ClearRoomAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml != nullptr)
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ClearRoomAudioProcessor::createEditor()
{
    return new ClearRoomAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClearRoomAudioProcessor();
}
