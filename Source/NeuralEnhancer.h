#pragma once

#include <JuceHeader.h>
#include <onnxruntime_cxx_api.h>

class NeuralEnhancer : private juce::Thread
{
public:
    NeuralEnhancer();
    ~NeuralEnhancer() override;
    bool prepare (double sampleRate, int channels);
    void reset();
    void setStrength (float value) noexcept { strength = juce::jlimit (0.0f, 1.0f, value); }
    float processSample (int channel, float input);
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
    void run() override;
    void applyCompletedFrames();
    void initialiseState (std::vector<float>&);

    struct Job { int channel = 0; int writePosition = 0; std::array<float, bins * 2> spectrum {}; };
    struct Result { int channel = 0; int writePosition = 0; std::array<float, bins * 2> enhanced {}; };
    juce::CriticalSection queueLock;
    juce::WaitableEvent queueEvent;
    std::deque<Job> jobs;
    std::deque<Result> results;
    std::vector<std::vector<float>> workerStates;

    Ort::Env environment { ORT_LOGGING_LEVEL_WARNING, "JenyaDereverb" };
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    std::vector<Channel> channelStates;
    std::array<float, fftSize> window {};
    void* forwardSetup = nullptr;
    void* inverseSetup = nullptr;
    float strength = 0.6f;
    std::atomic<float> reductionDb { 0.0f };
    bool ready = false;
};
