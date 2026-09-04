#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
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
    // Mix is the only control. "amount", "tail" and "focus" existed in earlier builds but
    // were never read by the DSP; they are removed rather than left as dead automation.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> values;
    values.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { mixId, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f, "%"));
    return { values.begin(), values.end() };
}

void ClearRoomAudioProcessor::prepareToPlay (double sampleRate, int)
{
    neural.prepare (sampleRate, juce::jmin (getTotalNumInputChannels(), maximumChannels));
    setLatencySamples (neural.isReady() ? neural.getLatencySamples() : 0);
    // Ramp Mix over 20 ms. Without this the blend steps once per block, which ticks
    // audibly whenever the knob is dragged or the parameter is automated.
    mixSmoother.reset (sampleRate, 0.02);
    mixSmoother.setCurrentAndTargetValue (parameters.getRawParameterValue (mixId)->load() * 0.01f);
}

void ClearRoomAudioProcessor::reset()
{
    // Hosts call this on transport locate; drop the overlap-add rings and the recurrent
    // model state so a seek cannot smear the previous position's reverb into the new one.
    neural.reset();
    mixSmoother.setCurrentAndTargetValue (parameters.getRawParameterValue (mixId)->load() * 0.01f);
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
    const auto numSamples = buffer.getNumSamples();
    // Mix is the only user-facing control. It blends against the latency-matched dry tap
    // inside the model wrapper, so dry and wet stay time-aligned at every setting.
    mixSmoother.setTargetValue (juce::jlimit (0.0f, 1.0f,
        parameters.getRawParameterValue (mixId)->load() * 0.01f));

    std::array<float*, maximumChannels> writePointers {};
    for (int channel = 0; channel < channels; ++channel)
        writePointers[static_cast<size_t> (channel)] = buffer.getWritePointer (channel);

    // Sample-outer/channel-inner so every channel sees the identical Mix ramp.
    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = mixSmoother.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* sample = writePointers[static_cast<size_t> (channel)] + i;
            *sample = neural.processSample (channel, *sample, mix);
        }
    }

    for (int channel = channels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);
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
