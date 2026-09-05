#pragma once

#include <JuceHeader.h>
#include <onnxruntime_cxx_api.h>

class NeuralEnhancer
{
public:
    NeuralEnhancer();
    ~NeuralEnhancer();
    // Why the model is or is not running. Both failure modes used to be silent: the
    // plug-in simply passed audio through and looked identical to a working one.
    enum class Status { active, unsupportedSampleRate, modelLoadFailed };

    bool prepare (double sampleRate, int channels);
    void reset();
    // Read from the editor while prepare() may be running on another thread, so these are
    // atomic and the error string is copied under a lock. Handing out a juce::String by
    // value while another thread assigns it races on its reference count.
    Status getStatus() const noexcept { return status.load (std::memory_order_relaxed); }
    double getPreparedSampleRate() const noexcept { return preparedSampleRate.load (std::memory_order_relaxed); }
    // Empty unless the ONNX Runtime threw while creating the session. Previously the
    // exception text was caught and discarded, leaving no way to tell why the model
    // failed inside a sandboxed host such as the App Store build of DaVinci Resolve.
    juce::String getLastError() const
    {
        const juce::ScopedLock lock (errorLock);
        return lastError;   // copied under the lock, then returned by value
    }
    // mix is applied per sample so the host can ramp it without zipper noise. The dry
    // signal used in the blend is the latency-matched ring-buffer tap, not the live
    // input, so dry and wet stay time-aligned at every mix setting.
    float processSample (int channel, float input, float mix);
    int getLatencySamples() const noexcept { return fftSize; }
    float getReductionDb() const noexcept { return reductionDb.load(); }
    bool isReady() const noexcept { return ready; }

private:
    static constexpr int fftSize = 960;
    static constexpr int hopSize = 480;
    static constexpr int bins = 481;
    static constexpr int stateSize = 56436;

    struct Channel
    {
        std::array<float, fftSize> inputRing {};
        std::array<float, fftSize> outputRing {};
        std::array<float, fftSize> fftReal {};
        std::array<float, fftSize> fftImag {};
        std::array<float, bins * 2> spectrum {};
        std::array<float, bins * 2> enhanced {};
        std::vector<float> state;
        std::vector<float> nextState;
        int position = 0;
        int hopCounter = 0;
    };

    void processFrame (Channel&);
    void writeDiagnostics() const;
    void setStatus (Status) noexcept;
    void setLastError (const juce::String&);
    void initialiseState (std::vector<float>&);

    Ort::Env environment { ORT_LOGGING_LEVEL_WARNING, "JenyaDereverb2" };
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    std::vector<Channel> channelStates;
    std::array<float, fftSize> window {};
    void* forwardSetup = nullptr;
    void* inverseSetup = nullptr;
    std::atomic<float> reductionDb { 0.0f };
    std::atomic<Status> status { Status::modelLoadFailed };
    juce::CriticalSection errorLock;
    juce::String lastError;
    std::atomic<double> preparedSampleRate { 0.0 };
    bool ready = false;
};
