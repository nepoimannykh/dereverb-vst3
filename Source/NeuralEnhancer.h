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
    Status getStatus() const noexcept { return status; }
    double getPreparedSampleRate() const noexcept { return preparedSampleRate; }
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
    void initialiseState (std::vector<float>&);

    Ort::Env environment { ORT_LOGGING_LEVEL_WARNING, "JenyaDereverb2" };
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    std::vector<Channel> channelStates;
    std::array<float, fftSize> window {};
    void* forwardSetup = nullptr;
    void* inverseSetup = nullptr;
    std::atomic<float> reductionDb { 0.0f };
    Status status = Status::modelLoadFailed;
    double preparedSampleRate = 0.0;
    bool ready = false;
};
