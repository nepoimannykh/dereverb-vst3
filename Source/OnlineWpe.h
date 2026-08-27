#pragma once

#include <JuceHeader.h>
#include <complex>

class OnlineWpe
{
public:
    void prepare (double sampleRate, int channels);
    void reset();
    void setParameters (float reduction, float roomTailMs);
    float processSample (int channel, float input);
    int getLatencySamples() const noexcept { return fftSize; }
    float getReductionDb() const noexcept { return reductionDb.load(); }

private:
    using Complex = std::complex<float>;
    static constexpr int fftOrder = 9;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;
    static constexpr int bins = fftSize / 2 + 1;
    static constexpr int taps = 8;
    static constexpr int delayFrames = 3;
    static constexpr int historyFrames = taps + delayFrames + 1;

    struct Channel
    {
        std::array<float, fftSize> inputRing {};
        std::array<float, fftSize> outputRing {};
        std::array<float, fftSize * 2> fftData {};
        std::vector<Complex> history;
        std::vector<Complex> inverseCovariance;
        std::vector<Complex> filter;
        std::array<float, bins> power {};
        int position = 0;
        int hopCounter = 0;
        int historyPosition = 0;
        int framesSeen = 0;
    };

    void processFrame (Channel&);
    size_t matrixIndex (int bin, int row, int column) const noexcept;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {};
    std::vector<Channel> channelStates;
    float strength = 0.55f;
    float forgetting = 0.997f;
    std::atomic<float> reductionDb { 0.0f };
};

