#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <chrono>

namespace
{
void setParameter (ClearRoomAudioProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

double rms (const juce::AudioBuffer<float>& buffer, int start, int length)
{
    double energy = 0.0;
    for (int i = start; i < start + length; ++i)
        energy += static_cast<double> (buffer.getSample (0, i)) * buffer.getSample (0, i);
    return std::sqrt (energy / static_cast<double> (length));
}

bool finiteAndBounded (const juce::AudioBuffer<float>& buffer, float bound)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (channel, i))
                || std::abs (buffer.getSample (channel, i)) > bound)
                return false;
    return true;
}
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int sampleCount = 96000;
    int failures = 0;

    ClearRoomAudioProcessor defaultsProcessor;
    const auto defaultValue = [&] (const char* id)
    {
        return defaultsProcessor.parameters.getParameter (id)->convertFrom0to1 (
            defaultsProcessor.parameters.getParameter (id)->getDefaultValue());
    };
    const bool defaultsPass = std::abs (defaultValue ("amount") - 100.0f) < 0.01f
                           && std::abs (defaultValue ("tail") - 1200.0f) < 0.01f
                           && std::abs (defaultValue ("focus")) < 0.01f
                           && std::abs (defaultValue ("mix") - 100.0f) < 0.01f;
    std::cout << "requested parameter defaults: " << (defaultsPass ? "PASS" : "FAIL") << '\n';
    failures += defaultsPass ? 0 : 1;

    // Fairlight tracks may be mono, stereo, 5.1, 7.1, or adaptive. The processor must
    // accept matching multichannel input/output and its editor must construct normally.
    ClearRoomAudioProcessor hostProcessor;
    juce::AudioProcessor::BusesLayout surroundLayout;
    surroundLayout.inputBuses.add (juce::AudioChannelSet::create5point1());
    surroundLayout.outputBuses.add (juce::AudioChannelSet::create5point1());
    const bool layoutPass = hostProcessor.checkBusesLayoutSupported (surroundLayout)
                         && hostProcessor.setBusesLayout (surroundLayout);
    std::unique_ptr<juce::AudioProcessorEditor> editor (hostProcessor.createEditor());
    const bool editorPass = editor != nullptr && editor->getWidth() == 720
                                           && editor->getHeight() == 360;
    std::cout << "5.1 Fairlight layout: " << (layoutPass ? "PASS" : "FAIL") << '\n'
              << "editor construction: " << (editorPass ? "PASS" : "FAIL") << '\n';
    failures += layoutPass ? 0 : 1;
    failures += editorPass ? 0 : 1;

    // Dry voiced bursts with natural pauses should retain their active-speech level.
    ClearRoomAudioProcessor dryProcessor;
    dryProcessor.prepareToPlay (sampleRate, sampleCount);
    setParameter (dryProcessor, "mix", 30.0f);
    juce::AudioBuffer<float> dry (2, sampleCount);
    for (int i = 0; i < sampleCount; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * 145.0
                           * static_cast<double> (i) / sampleRate;
        const float syllable = (i % 12000) < 8500 ? 1.0f : 0.0f;
        const float value = syllable * static_cast<float> (0.14 * std::sin (phase)
                          + 0.05 * std::sin (2.03 * phase) + 0.025 * std::sin (3.97 * phase));
        dry.setSample (0, i, value);
        dry.setSample (1, i, value);
    }
    const double dryInputRms = rms (dry, 48000, 48000);
    juce::MidiBuffer midi;
    dryProcessor.processBlock (dry, midi);
    const double dryRatio = rms (dry, 48000, 48000) / dryInputRms;
    // A neural speech model is expected to alter synthetic harmonic tones; this guards
    // against destructive collapse or gain runaway rather than claiming a speech score.
    const bool dryPass = dryRatio > 0.55 && dryRatio < 1.05;
    std::cout << "dry steady-state gain: " << juce::Decibels::gainToDecibels (dryRatio)
              << " dB " << (dryPass ? "PASS" : "FAIL") << '\n';
    failures += dryPass ? 0 : 1;

    // A direct impulse followed by a decaying modal room tail should retain the onset
    // while reducing energy later in the response.
    ClearRoomAudioProcessor roomProcessor;
    roomProcessor.prepareToPlay (sampleRate, sampleCount);
    setParameter (roomProcessor, "amount", 70.0f);
    setParameter (roomProcessor, "tail", 420.0f);
    setParameter (roomProcessor, "focus", 0.0f);
    setParameter (roomProcessor, "mix", 70.0f);
    juce::AudioBuffer<float> room (2, sampleCount);
    room.clear();
    room.setSample (0, 0, 0.8f);
    room.setSample (1, 0, 0.8f);
    for (int i = 960; i < sampleCount; ++i)
    {
        const double t = static_cast<double> (i - 960) / sampleRate;
        const float tail = static_cast<float> (0.30 * std::exp (-t / 0.36)
            * (0.62 * std::sin (juce::MathConstants<double>::twoPi * 311.0 * t)
               + 0.38 * std::sin (juce::MathConstants<double>::twoPi * 587.0 * t)));
        room.setSample (0, i, tail);
        room.setSample (1, i, tail);
    }
    const float onsetBefore = room.getSample (0, 0);
    const double tailBefore = rms (room, 9600, 24000);
    roomProcessor.processBlock (room, midi);
    const float onsetRatio = room.getSample (0, roomProcessor.getLatencySamples()) / onsetBefore;
    const double tailReductionDb = juce::Decibels::gainToDecibels (
        rms (room, 9600 + roomProcessor.getLatencySamples(), 24000) / tailBefore);
    const bool roomPass = onsetRatio > 0.25f && tailReductionDb < -2.0;
    std::cout << "direct-path blend floor: " << onsetRatio * 100.0f << "%\n"
              << "late-tail change: " << tailReductionDb << " dB "
              << (roomPass ? "PASS" : "FAIL") << '\n';
    failures += roomPass ? 0 : 1;

    const bool safetyPass = finiteAndBounded (dry, 1.0f) && finiteAndBounded (room, 1.0f);
    std::cout << "finite/bounded output: " << (safetyPass ? "PASS" : "FAIL") << '\n';
    failures += safetyPass ? 0 : 1;

    ClearRoomAudioProcessor speedProcessor;
    speedProcessor.prepareToPlay (sampleRate, sampleCount);
    juce::AudioBuffer<float> speed (2, sampleCount);
    speed.clear();
    const auto start = std::chrono::steady_clock::now();
    speedProcessor.processBlock (speed, midi);
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double realTimeFactor = elapsed / (sampleCount / sampleRate);
    const bool speedPass = realTimeFactor < 1.0;
    std::cout << "stereo real-time factor: " << realTimeFactor << " "
              << (speedPass ? "PASS" : "FAIL") << '\n';
    failures += speedPass ? 0 : 1;

    return failures == 0 ? 0 : 1;
}
