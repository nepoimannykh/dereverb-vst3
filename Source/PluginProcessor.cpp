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
    currentSampleRate = sampleRate;
    hostToModelRatio = 48000.0 / juce::jmax (1.0, sampleRate);
    resamplePhase.fill (0.0);
    resampledOutput.fill (0.0f);
    for (auto& state : postStates)
        state = {};
    // DPDFNet is native 48 kHz; the adapter below keeps it usable at common host rates.
    neural.prepare (48000.0, juce::jmin (getTotalNumInputChannels(), maximumChannels));
    setLatencySamples (neural.isReady() ? juce::roundToInt (neural.getLatencySamples() / hostToModelRatio) : 0);
}

float ClearRoomAudioProcessor::processVoicePost (int channel, float sample) noexcept
{
    auto& state = postStates[static_cast<size_t> (channel)];
    // 70 Hz one-pole high-pass: removes HVAC/plosive rumble without thinning speech.
    const float hpCoeff = std::exp (-juce::MathConstants<float>::twoPi * 70.0f
                                    / static_cast<float> (juce::jmax (1.0, currentSampleRate)));
    const float hp = hpCoeff * (state.hpY + sample - state.hpX);
    state.hpX = sample;
    state.hpY = hp;

    // Fast speech-level compressor with a slower release. This keeps the restored voice
    // forward while avoiding the pumping behavior of a hard gate.
    const float detectorInput = std::abs (hp);
    const float detectorCoeff = detectorInput > state.detector ? 0.35f : 0.035f;
    state.detector += detectorCoeff * (detectorInput - state.detector);
    constexpr float threshold = 0.28f;
    float gain = 1.0f;
    if (state.detector > threshold)
    {
        const float compressed = threshold + (state.detector - threshold) / 3.0f;
        gain = compressed / juce::jmax (state.detector, 1.0e-6f);
    }
    const float compressed = hp * gain * 1.18f;
    // Soft safety limiter; unlike hard clipping it keeps consonant transients smooth.
    return std::tanh (compressed) * 0.92f;
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
        {
            float processed = samples[i];
            resamplePhase[static_cast<size_t> (channel)] += hostToModelRatio;
            while (resamplePhase[static_cast<size_t> (channel)] >= 1.0)
            {
                resamplePhase[static_cast<size_t> (channel)] -= 1.0;
                resampledOutput[static_cast<size_t> (channel)] = neural.processSample (channel, samples[i]);
            }
            processed = resampledOutput[static_cast<size_t> (channel)];
            const float wet = processVoicePost (channel, processed);
            const float mixNow = parameters.getRawParameterValue (mixId)->load() * 0.01f;
            samples[i] = processed + mixNow * (wet - processed);
        }
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
