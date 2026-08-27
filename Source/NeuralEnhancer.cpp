#include <Accelerate/Accelerate.h>
#include "NeuralEnhancer.h"
#include "BinaryData.h"

NeuralEnhancer::NeuralEnhancer()
{
    sessionOptions.SetIntraOpNumThreads (1);
    sessionOptions.SetInterOpNumThreads (1);
    sessionOptions.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
}

NeuralEnhancer::~NeuralEnhancer()
{
    if (forwardSetup != nullptr) vDSP_DFT_DestroySetup (static_cast<vDSP_DFT_Setup> (forwardSetup));
    if (inverseSetup != nullptr) vDSP_DFT_DestroySetup (static_cast<vDSP_DFT_Setup> (inverseSetup));
}

bool NeuralEnhancer::prepare (double sampleRate, int channels)
{
    ready = false;
    if (std::abs (sampleRate - 48000.0) > 1.0)
        return false;

    if (forwardSetup == nullptr)
        forwardSetup = vDSP_DFT_zop_CreateSetup (nullptr, fftSize, vDSP_DFT_FORWARD);
    if (inverseSetup == nullptr)
        inverseSetup = vDSP_DFT_zop_CreateSetup (nullptr, fftSize, vDSP_DFT_INVERSE);
    if (forwardSetup == nullptr || inverseSetup == nullptr)
        return false;

    try
    {
        session = std::make_unique<Ort::Session> (environment,
            BinaryData::dpdfnet2_48khz_hr_onnx,
            static_cast<size_t> (BinaryData::dpdfnet2_48khz_hr_onnxSize), sessionOptions);
        for (int i = 0; i < fftSize; ++i)
        {
            const float s = std::sin (0.5f * juce::MathConstants<float>::pi
                                      * (static_cast<float> (i) + 0.5f) / (fftSize * 0.5f));
            window[static_cast<size_t> (i)] = std::sin (0.5f * juce::MathConstants<float>::pi * s * s);
        }
        channelStates.resize (static_cast<size_t> (channels));
        reset();
        ready = true;
    }
    catch (const Ort::Exception&)
    {
        session.reset();
    }
    return ready;
}

void NeuralEnhancer::initialiseState (std::vector<float>& state)
{
    state.assign (stateSize, 0.0f);
    if (session == nullptr) return;
    auto metadata = session->GetModelMetadata();
    Ort::AllocatorWithDefaultOptions allocator;
    const auto fill = [&] (const char* key, int offset)
    {
        auto text = metadata.LookupCustomMetadataMapAllocated (key, allocator);
        if (! text) return;
        juce::StringArray values;
        values.addTokens (text.get(), ",", "");
        for (int i = 0; i < values.size() && offset + i < stateSize; ++i)
            state[static_cast<size_t> (offset + i)] = values[i].getFloatValue();
    };
    fill ("erb_norm_init", 0);
    fill ("spec_norm_init", 481);
}

void NeuralEnhancer::reset()
{
    for (auto& channel : channelStates)
    {
        channel.inputRing.fill (0.0f);
        channel.outputRing.fill (0.0f);
        channel.fftReal.fill (0.0f);
        channel.fftImag.fill (0.0f);
        channel.position = channel.hopCounter = 0;
        initialiseState (channel.state);
        channel.nextState.assign (stateSize, 0.0f);
    }
    reductionDb.store (0.0f);
}

float NeuralEnhancer::processSample (int channelIndex, float input)
{
    if (! ready) return input;
    auto& channel = channelStates[static_cast<size_t> (channelIndex)];
    const float delayed = channel.inputRing[static_cast<size_t> (channel.position)];
    const float enhanced = channel.outputRing[static_cast<size_t> (channel.position)];
    channel.outputRing[static_cast<size_t> (channel.position)] = 0.0f;
    channel.inputRing[static_cast<size_t> (channel.position)] = input;
    channel.position = (channel.position + 1) % fftSize;
    if (++channel.hopCounter == hopSize)
    {
        channel.hopCounter = 0;
        processFrame (channel);
    }
    return delayed + strength * (enhanced - delayed);
}

void NeuralEnhancer::processFrame (Channel& channel)
{
    for (int i = 0; i < fftSize; ++i)
    {
        channel.fftReal[static_cast<size_t> (i)] = channel.inputRing[static_cast<size_t> ((channel.position + i) % fftSize)]
                                                    * window[static_cast<size_t> (i)];
        channel.fftImag[static_cast<size_t> (i)] = 0.0f;
    }
    vDSP_DFT_Execute (static_cast<vDSP_DFT_Setup> (forwardSetup), channel.fftReal.data(), channel.fftImag.data(),
                      channel.fftReal.data(), channel.fftImag.data());
    double before = 1.0e-12;
    for (int bin = 0; bin < bins; ++bin)
    {
        channel.spectrum[static_cast<size_t> (bin * 2)] = channel.fftReal[static_cast<size_t> (bin)];
        channel.spectrum[static_cast<size_t> (bin * 2 + 1)] = channel.fftImag[static_cast<size_t> (bin)];
        before += static_cast<double> (channel.fftReal[static_cast<size_t> (bin)]) * channel.fftReal[static_cast<size_t> (bin)]
                + static_cast<double> (channel.fftImag[static_cast<size_t> (bin)]) * channel.fftImag[static_cast<size_t> (bin)];
    }

    std::array<int64_t, 4> specShape { 1, 1, bins, 2 };
    std::array<int64_t, 1> stateShape { stateSize };
    auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    auto specIn = Ort::Value::CreateTensor<float> (memory, channel.spectrum.data(), channel.spectrum.size(), specShape.data(), specShape.size());
    auto stateIn = Ort::Value::CreateTensor<float> (memory, channel.state.data(), channel.state.size(), stateShape.data(), stateShape.size());
    auto specOut = Ort::Value::CreateTensor<float> (memory, channel.enhanced.data(), channel.enhanced.size(), specShape.data(), specShape.size());
    auto stateOut = Ort::Value::CreateTensor<float> (memory, channel.nextState.data(), channel.nextState.size(), stateShape.data(), stateShape.size());
    const char* inputNames[] { "spec", "state_in" };
    const char* outputNames[] { "spec_e", "state_out" };
    std::array<Ort::Value, 2> inputs { std::move (specIn), std::move (stateIn) };
    std::array<Ort::Value, 2> outputs { std::move (specOut), std::move (stateOut) };
    try { session->Run (Ort::RunOptions { nullptr }, inputNames, inputs.data(), 2, outputNames, outputs.data(), 2); }
    catch (const Ort::Exception&) { return; }
    channel.state.swap (channel.nextState);

    double after = 1.0e-12;
    for (int bin = 0; bin < bins; ++bin)
    {
        channel.fftReal[static_cast<size_t> (bin)] = channel.enhanced[static_cast<size_t> (bin * 2)];
        channel.fftImag[static_cast<size_t> (bin)] = channel.enhanced[static_cast<size_t> (bin * 2 + 1)];
        after += std::norm (std::complex<float> { channel.fftReal[static_cast<size_t> (bin)], channel.fftImag[static_cast<size_t> (bin)] });
    }
    for (int bin = bins; bin < fftSize; ++bin)
    {
        const int mirror = fftSize - bin;
        channel.fftReal[static_cast<size_t> (bin)] = channel.fftReal[static_cast<size_t> (mirror)];
        channel.fftImag[static_cast<size_t> (bin)] = -channel.fftImag[static_cast<size_t> (mirror)];
    }
    vDSP_DFT_Execute (static_cast<vDSP_DFT_Setup> (inverseSetup), channel.fftReal.data(), channel.fftImag.data(),
                      channel.fftReal.data(), channel.fftImag.data());
    const float scale = 1.0f / static_cast<float> (fftSize);
    for (int i = 0; i < fftSize; ++i)
        channel.outputRing[static_cast<size_t> ((channel.position + i) % fftSize)] +=
            channel.fftReal[static_cast<size_t> (i)] * window[static_cast<size_t> (i)] * scale;

    const float db = juce::jmin (0.0f, juce::Decibels::gainToDecibels (static_cast<float> (std::sqrt (after / before))));
    reductionDb.store (0.9f * reductionDb.load() + 0.1f * db);
}
