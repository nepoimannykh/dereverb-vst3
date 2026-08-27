#include "OnlineWpe.h"

size_t OnlineWpe::matrixIndex (int bin, int row, int column) const noexcept
{
    return static_cast<size_t> ((bin * taps + row) * taps + column);
}

void OnlineWpe::prepare (double, int channels)
{
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (
            juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (fftSize));

    channelStates.resize (static_cast<size_t> (channels));
    for (auto& state : channelStates)
    {
        state.history.resize (static_cast<size_t> (historyFrames * bins));
        state.inverseCovariance.resize (static_cast<size_t> (bins * taps * taps));
        state.filter.resize (static_cast<size_t> (bins * taps));
    }
    reset();
}

void OnlineWpe::reset()
{
    for (auto& state : channelStates)
    {
        state.inputRing.fill (0.0f);
        state.outputRing.fill (0.0f);
        state.fftData.fill (0.0f);
        std::fill (state.history.begin(), state.history.end(), Complex {});
        std::fill (state.inverseCovariance.begin(), state.inverseCovariance.end(), Complex {});
        std::fill (state.filter.begin(), state.filter.end(), Complex {});
        state.power.fill (1.0e-3f);
        for (int bin = 0; bin < bins; ++bin)
            for (int tap = 0; tap < taps; ++tap)
                state.inverseCovariance[matrixIndex (bin, tap, tap)] = Complex { 10.0f, 0.0f };
        state.position = state.hopCounter = state.historyPosition = state.framesSeen = 0;
    }
    reductionDb.store (0.0f);
}

void OnlineWpe::setParameters (float reduction, float roomTailMs)
{
    strength = juce::jlimit (0.0f, 0.92f, reduction);
    // Longer rooms need a slower recursive covariance estimate.
    forgetting = juce::jmap (juce::jlimit (80.0f, 1200.0f, roomTailMs),
                             80.0f, 1200.0f, 0.992f, 0.9993f);
}

float OnlineWpe::processSample (int channel, float input)
{
    auto& state = channelStates[static_cast<size_t> (channel)];
    const float delayedDry = state.inputRing[static_cast<size_t> (state.position)];
    const float wet = state.outputRing[static_cast<size_t> (state.position)];
    state.outputRing[static_cast<size_t> (state.position)] = 0.0f;
    state.inputRing[static_cast<size_t> (state.position)] = input;
    state.position = (state.position + 1) % fftSize;

    if (++state.hopCounter == hopSize)
    {
        state.hopCounter = 0;
        processFrame (state);
    }

    return delayedDry + strength * (wet - delayedDry);
}

void OnlineWpe::processFrame (Channel& state)
{
    for (int i = 0; i < fftSize; ++i)
    {
        const int source = (state.position + i) % fftSize;
        state.fftData[static_cast<size_t> (i)] = state.inputRing[static_cast<size_t> (source)]
                                               * window[static_cast<size_t> (i)];
    }
    std::fill (state.fftData.begin() + fftSize, state.fftData.end(), 0.0f);
    fft.performRealOnlyForwardTransform (state.fftData.data());

    std::array<Complex, taps> x {};
    std::array<Complex, taps> px {};
    double inputEnergy = 1.0e-12;
    double outputEnergy = 1.0e-12;

    for (int bin = 0; bin < bins; ++bin)
    {
        const Complex observed { state.fftData[static_cast<size_t> (2 * bin)],
                                 state.fftData[static_cast<size_t> (2 * bin + 1)] };
        Complex prediction {};
        const bool ready = state.framesSeen >= taps + delayFrames;

        if (ready)
        {
            for (int tap = 0; tap < taps; ++tap)
            {
                const int back = delayFrames + tap;
                const int frame = (state.historyPosition - back + historyFrames) % historyFrames;
                x[static_cast<size_t> (tap)] = state.history[static_cast<size_t> (frame * bins + bin)];
                prediction += std::conj (state.filter[static_cast<size_t> (bin * taps + tap)])
                              * x[static_cast<size_t> (tap)];
            }
        }

        const Complex residual = observed - prediction;
        const Complex processed = observed + strength * (residual - observed);
        state.fftData[static_cast<size_t> (2 * bin)] = residual.real();
        state.fftData[static_cast<size_t> (2 * bin + 1)] = residual.imag();
        inputEnergy += std::norm (observed);
        outputEnergy += std::norm (processed);

        if (ready)
        {
            for (int row = 0; row < taps; ++row)
            {
                px[static_cast<size_t> (row)] = {};
                for (int column = 0; column < taps; ++column)
                    px[static_cast<size_t> (row)] += state.inverseCovariance[matrixIndex (bin, row, column)]
                                                   * x[static_cast<size_t> (column)];
            }

            float denominator = forgetting * state.power[static_cast<size_t> (bin)];
            for (int tap = 0; tap < taps; ++tap)
                denominator += std::real (std::conj (x[static_cast<size_t> (tap)]) * px[static_cast<size_t> (tap)]);
            denominator = juce::jmax (denominator, 1.0e-6f);

            bool stable = std::isfinite (denominator);
            for (const auto& value : px)
                stable = stable && std::isfinite (value.real()) && std::isfinite (value.imag());

            for (int row = 0; stable && row < taps; ++row)
            {
                Complex gain = px[static_cast<size_t> (row)] / denominator;
                const float gainMagnitude = std::abs (gain);
                if (gainMagnitude > 0.25f)
                    gain *= 0.25f / gainMagnitude;
                state.filter[static_cast<size_t> (bin * taps + row)] += gain * std::conj (residual);
                auto& coefficient = state.filter[static_cast<size_t> (bin * taps + row)];
                const float coefficientMagnitude = std::abs (coefficient);
                if (coefficientMagnitude > 2.0f)
                    coefficient *= 2.0f / coefficientMagnitude;
                for (int column = 0; column < taps; ++column)
                {
                    auto& covariance = state.inverseCovariance[matrixIndex (bin, row, column)];
                    covariance =
                        (state.inverseCovariance[matrixIndex (bin, row, column)]
                         - gain * std::conj (px[static_cast<size_t> (column)])) / forgetting;
                    const float magnitude = std::abs (covariance);
                    if (! std::isfinite (magnitude) || magnitude > 1.0e5f)
                        stable = false;
                }
            }
            if (! stable)
            {
                for (int row = 0; row < taps; ++row)
                {
                    state.filter[static_cast<size_t> (bin * taps + row)] = {};
                    for (int column = 0; column < taps; ++column)
                        state.inverseCovariance[matrixIndex (bin, row, column)] =
                            row == column ? Complex { 10.0f, 0.0f } : Complex {};
                }
            }
            state.power[static_cast<size_t> (bin)] = forgetting * state.power[static_cast<size_t> (bin)]
                + (1.0f - forgetting) * juce::jmax (std::norm (residual), 1.0e-9f);
        }

        state.history[static_cast<size_t> (state.historyPosition * bins + bin)] = observed;
    }

    state.historyPosition = (state.historyPosition + 1) % historyFrames;
    ++state.framesSeen;
    fft.performRealOnlyInverseTransform (state.fftData.data());
    constexpr float overlapScale = 2.0f / 3.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        const int destination = (state.position + i) % fftSize;
        state.outputRing[static_cast<size_t> (destination)] += state.fftData[static_cast<size_t> (i)]
            * window[static_cast<size_t> (i)] * overlapScale;
    }

    const float frameReduction = juce::jmin (0.0f, juce::Decibels::gainToDecibels (
        static_cast<float> (std::sqrt (outputEnergy / inputEnergy))));
    reductionDb.store (0.92f * reductionDb.load() + 0.08f * frameReduction);
}
