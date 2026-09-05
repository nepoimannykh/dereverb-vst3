#include <Accelerate/Accelerate.h>
#include "NeuralEnhancer.h"
#include "BinaryData.h"
#include "Diagnostics.h"

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
    preparedSampleRate.store (sampleRate, std::memory_order_relaxed);
    setLastError ({});
    if (std::abs (sampleRate - 48000.0) > 1.0)
    {
        setStatus (Status::unsupportedSampleRate);
        writeDiagnostics();
        return false;
    }
    setStatus (Status::modelLoadFailed);

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
        setStatus (Status::active);
    }
    catch (const Ort::Exception& exception)
    {
        setLastError (juce::String (exception.what()));
        session.reset();
    }
    catch (const std::exception& exception)
    {
        setLastError (juce::String (exception.what()));
        session.reset();
    }
    writeDiagnostics();
    diagnostics::trace ("model prepare rate=" + juce::String (sampleRate, 1)
                        + " ready=" + (ready ? "yes" : "no")
                        + (getLastError().isEmpty() ? juce::String() : " error=" + getLastError()));
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

// Sandboxed hosts give a plug-in nowhere obvious to report from, and the editor may never
// be opened. Drop a line into the host's own writable application-data area on every
// prepare so the engine state can be inspected after the fact.
void NeuralEnhancer::setStatus (Status value) noexcept
{
    status.store (value, std::memory_order_relaxed);
}

void NeuralEnhancer::setLastError (const juce::String& value)
{
    const juce::ScopedLock lock (errorLock);
    lastError = value;
}

void NeuralEnhancer::writeDiagnostics() const
{
    const auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                               .getChildFile ("JenyaDereverb2");
    if (! directory.exists() && ! directory.createDirectory())
        return;
    const auto file = directory.getChildFile ("diagnostics.log");
    if (file.getSize() > 256 * 1024)
        file.deleteFile();
    juce::String line;
    line << juce::Time::getCurrentTime().toISO8601 (true)
         << "  rate=" << juce::String (getPreparedSampleRate(), 1)
         << "  channels=" << static_cast<int> (channelStates.size())
         << "  status=" << (getStatus() == Status::active ? "active"
                          : getStatus() == Status::unsupportedSampleRate ? "unsupported-sample-rate"
                          : "model-load-failed")
         << "  modelBytes=" << static_cast<int> (BinaryData::dpdfnet2_48khz_hr_onnxSize);
    const auto error = getLastError();
    if (error.isNotEmpty())
        line << "  error=" << error.replaceCharacters ("\r\n", "  ");
    file.appendText (line + "\n");
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

float NeuralEnhancer::processSample (int channelIndex, float input, float mix)
{
    if (! ready || channelIndex < 0 || channelIndex >= static_cast<int> (channelStates.size()))
        return input;
    // A single non-finite sample would otherwise persist in the overlap-add rings and in
    // the recurrent model state, silencing the channel until the next prepareToPlay.
    if (! std::isfinite (input))
        input = 0.0f;
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
    return delayed + mix * (enhanced - delayed);
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
    // The model is recurrent, so a diverged frame would feed itself forever. Spot-check
    // the returned spectrum and reinitialise the channel rather than latch a dead output.
    if (! std::isfinite (channel.enhanced[0]) || ! std::isfinite (channel.nextState[0]))
    {
        initialiseState (channel.state);
        channel.nextState.assign (static_cast<size_t> (stateSize), 0.0f);
        channel.enhanced.fill (0.0f);
        return;
    }
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
