#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Diagnostics.h"

namespace
{
constexpr const char* mixId = "mix";
}

ClearRoomAudioProcessor::ClearRoomAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createLayout())
{
    diagnostics::trace ("processor constructed, version " + juce::String (JucePlugin_VersionString));
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
    for (auto& value : envelopeDry) value.store (0.0f, std::memory_order_relaxed);
    for (auto& value : envelopeWet) value.store (0.0f, std::memory_order_relaxed);
    envelopeDryEnergy = envelopeWetEnergy = 0.0;
    envelopeCount = 0;
    diagnostics::trace ("prepareToPlay rate=" + juce::String (sampleRate, 1)
                        + " channels=" + juce::String (getTotalNumInputChannels())
                        + " status=" + getStatusText());
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
            const float dry = *sample;
            const float wet = neural.processSample (channel, dry, mix);
            *sample = wet;
            if (channel == 0)
            {
                envelopeDryEnergy += static_cast<double> (dry) * dry;
                envelopeWetEnergy += static_cast<double> (wet) * wet;
                if (++envelopeCount >= envelopeWindow)
                {
                    const auto scale = 1.0 / static_cast<double> (envelopeWindow);
                    const int slot = envelopeIndex.load (std::memory_order_relaxed);
                    envelopeDry[static_cast<size_t> (slot)].store (
                        static_cast<float> (std::sqrt (envelopeDryEnergy * scale)), std::memory_order_relaxed);
                    envelopeWet[static_cast<size_t> (slot)].store (
                        static_cast<float> (std::sqrt (envelopeWetEnergy * scale)), std::memory_order_relaxed);
                    envelopeIndex.store ((slot + 1) % envelopePoints, std::memory_order_release);
                    envelopeDryEnergy = envelopeWetEnergy = 0.0;
                    envelopeCount = 0;
                }
            }
        }
    }

    for (int channel = channels; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);
}

void ClearRoomAudioProcessor::copyEnvelopes (float* dry, float* wet) const
{
    // Unroll the ring so index 0 is the oldest point and the display scrolls left. The
    // output trails the input by the model's latency, so delay the dry series to match;
    // otherwise the two curves are compared across different moments of the signal.
    const int newest = envelopeIndex.load (std::memory_order_acquire);
    const int latencyPoints = juce::roundToInt (static_cast<double> (neural.getLatencySamples())
                                                / static_cast<double> (envelopeWindow));
    for (int i = 0; i < envelopePoints; ++i)
    {
        const auto wetSlot = static_cast<size_t> ((newest + i) % envelopePoints);
        const auto drySlot = static_cast<size_t> ((newest + i - latencyPoints + envelopePoints * 2) % envelopePoints);
        dry[i] = envelopeDry[drySlot].load (std::memory_order_relaxed);
        wet[i] = envelopeWet[wetSlot].load (std::memory_order_relaxed);
    }
}

juce::String ClearRoomAudioProcessor::getStatusText() const
{
    switch (neural.getStatus())
    {
        case NeuralEnhancer::Status::active:
            return "DPDFNET AI  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) + "  20 ms";
        case NeuralEnhancer::Status::unsupportedSampleRate:
        {
            const auto rate = neural.getPreparedSampleRate();
            if (rate <= 0.0)
                return "INACTIVE  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) + "  NOT PREPARED";
            return "BYPASSED  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) + "  NEEDS 48 kHz, HOST IS "
                 + juce::String (rate / 1000.0, 1) + " kHz";
        }
        case NeuralEnhancer::Status::modelLoadFailed:
        default:
        {
            auto text = "BYPASSED  " + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2")) + "  MODEL FAILED TO LOAD";
            const auto detail = neural.getLastError();
            if (detail.isNotEmpty())
                text << ": " << detail.substring (0, 90);
            return text;
        }
    }
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
    diagnostics::trace ("createEditor called");
    auto* editor = new ClearRoomAudioProcessorEditor (*this);
    diagnostics::trace ("editor constructed " + juce::String (editor->getWidth())
                        + "x" + juce::String (editor->getHeight()));
    return editor;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClearRoomAudioProcessor();
}
