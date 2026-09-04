#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <chrono>
#include <limits>
#include <complex>

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
    // Mix must be the only exposed parameter: the old amount/tail/focus controls were
    // never read by the DSP and are gone.
    const bool defaultsPass = std::abs (defaultValue ("mix") - 100.0f) < 0.01f
                           && defaultsProcessor.parameters.getParameter ("amount") == nullptr
                           && defaultsProcessor.parameters.getParameter ("tail") == nullptr
                           && defaultsProcessor.parameters.getParameter ("focus") == nullptr;
    std::cout << "parameter surface (mix only): " << (defaultsPass ? "PASS" : "FAIL") << '\n';
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

    // Mix = 0 must be a true bypass: the latency-matched dry tap, unaltered, including
    // above-0 dBFS material that a floating-point host bus can legitimately carry.
    ClearRoomAudioProcessor bypassProcessor;
    setParameter (bypassProcessor, "mix", 0.0f);   // set before prepare so the ramp starts settled
    bypassProcessor.prepareToPlay (sampleRate, sampleCount);
    const int latency = bypassProcessor.getLatencySamples();
    juce::AudioBuffer<float> hot (2, sampleCount), hotReference (2, sampleCount);
    for (int i = 0; i < sampleCount; ++i)
    {
        const float value = 1.8f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                             * static_cast<float> (i) / 48000.0f);
        hot.setSample (0, i, value);
        hot.setSample (1, i, value);
        hotReference.setSample (0, i, value);
        hotReference.setSample (1, i, value);
    }
    bypassProcessor.processBlock (hot, midi);
    float worstBypassError = 0.0f;
    for (int i = latency; i < sampleCount; ++i)
        worstBypassError = juce::jmax (worstBypassError,
            std::abs (hot.getSample (0, i) - hotReference.getSample (0, i - latency)));
    const bool bypassPass = worstBypassError < 1.0e-6f;
    std::cout << "mix=0 bypass transparency at +5 dBFS: " << worstBypassError << " peak error "
              << (bypassPass ? "PASS" : "FAIL") << '\n';
    failures += bypassPass ? 0 : 1;

    // A single non-finite input sample must not latch the recurrent model state off.
    // Compared differentially against a clean run of the same signal: a steady tone would
    // be suppressed by the speech model anyway, so absolute level proves nothing here.
    const auto renderSyllables = [&] (int nanIndex)
    {
        ClearRoomAudioProcessor processor;
        // 30% mix: the model suppresses synthetic tones almost completely at 100%, which
        // would leave nothing to compare. Here the dry tap dominates, so a poisoned wet
        // path still shows up plainly in the difference.
        setParameter (processor, "mix", 30.0f);
        processor.prepareToPlay (sampleRate, sampleCount);
        juce::AudioBuffer<float> buffer (2, sampleCount);
        for (int i = 0; i < sampleCount; ++i)
        {
            const double phase = juce::MathConstants<double>::twoPi * 145.0
                               * static_cast<double> (i) / sampleRate;
            const float syllable = (i % 12000) < 8500 ? 1.0f : 0.0f;
            const float value = syllable * static_cast<float> (0.14 * std::sin (phase)
                              + 0.05 * std::sin (2.03 * phase) + 0.025 * std::sin (3.97 * phase));
            buffer.setSample (0, i, i == nanIndex ? std::numeric_limits<float>::quiet_NaN() : value);
            buffer.setSample (1, i, value);
        }
        processor.processBlock (buffer, midi);
        return buffer;
    };
    const auto cleanRun = renderSyllables (-1);
    const auto poisonedRun = renderSyllables (1000);
    double divergence = 0.0;
    for (int i = sampleCount - 24000; i < sampleCount; ++i)
        divergence = juce::jmax (divergence,
            static_cast<double> (std::abs (poisonedRun.getSample (0, i) - cleanRun.getSample (0, i))));
    const double cleanTailRms = rms (cleanRun, sampleCount - 24000, 24000);
    const bool nanPass = std::isfinite (divergence) && divergence < 1.0e-4 && cleanTailRms > 1.0e-3;
    std::cout << "recovery after a NaN input sample: peak divergence from a clean run "
              << divergence << " (clean tail rms " << cleanTailRms << ") "
              << (nanPass ? "PASS" : "FAIL") << '\n';
    failures += nanPass ? 0 : 1;

    // Dry/wet phase coherence. Unlike a pure gain stage, the model rewrites the complex
    // spectrum, so the wet output can be phase-rotated against the latency-matched dry.
    // Where the two are more than 90 degrees apart, an intermediate Mix sums to LESS than
    // either extreme -- real cancellation, not comb filtering from a delay mismatch.
    const auto renderReverberant = [&] (float mixPercent)
    {
        ClearRoomAudioProcessor processor;
        setParameter (processor, "mix", mixPercent);
        processor.prepareToPlay (sampleRate, sampleCount);
        juce::AudioBuffer<float> buffer (2, sampleCount);
        for (int i = 0; i < sampleCount; ++i)
        {
            const double phase = juce::MathConstants<double>::twoPi * 145.0
                               * static_cast<double> (i) / sampleRate;
            const float syllable = (i % 12000) < 8500 ? 1.0f : 0.0f;
            float value = syllable * static_cast<float> (0.14 * std::sin (phase)
                        + 0.05 * std::sin (2.03 * phase) + 0.025 * std::sin (3.97 * phase));
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }
        // modal decaying tail so the model has reverb to actually remove
        for (int i = 1; i < sampleCount; ++i)
        {
            const float fed = 0.55f * buffer.getSample (0, i - 1)
                            + (i > 2400 ? 0.32f * buffer.getSample (0, i - 2400) : 0.0f);
            buffer.setSample (0, i, buffer.getSample (0, i) + fed * 0.45f);
            buffer.setSample (1, i, buffer.getSample (0, i));
        }
        processor.processBlock (buffer, midi);
        return buffer;
    };
    const auto dryRun = renderReverberant (0.0f);
    const auto wetRun = renderReverberant (100.0f);

    constexpr int fftOrder = 12, fftLength = 1 << fftOrder;
    juce::dsp::FFT phaseFft (fftOrder);
    int opposedBins = 0, countedBins = 0;
    double worstCosine = 1.0, worstDipDb = 0.0;
    for (int origin = 24000; origin + fftLength <= sampleCount; origin += fftLength)
    {
        std::vector<float> dryFft (2 * fftLength, 0.0f), wetFft (2 * fftLength, 0.0f);
        for (int i = 0; i < fftLength; ++i)
        {
            const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                * static_cast<float> (i) / static_cast<float> (fftLength));
            dryFft[static_cast<size_t> (i)] = dryRun.getSample (0, origin + i) * window;
            wetFft[static_cast<size_t> (i)] = wetRun.getSample (0, origin + i) * window;
        }
        phaseFft.performRealOnlyForwardTransform (dryFft.data());
        phaseFft.performRealOnlyForwardTransform (wetFft.data());
        for (int bin = 1; bin < fftLength / 2; ++bin)
        {
            const std::complex<double> d { dryFft[static_cast<size_t> (bin * 2)], dryFft[static_cast<size_t> (bin * 2 + 1)] };
            const std::complex<double> w { wetFft[static_cast<size_t> (bin * 2)], wetFft[static_cast<size_t> (bin * 2 + 1)] };
            const double dm = std::abs (d), wm = std::abs (w);
            if (dm < 1.0e-4 || wm < 1.0e-4) continue;   // ignore near-silent bins
            ++countedBins;
            const double cosine = (d.real() * w.real() + d.imag() * w.imag()) / (dm * wm);
            worstCosine = juce::jmin (worstCosine, cosine);
            if (cosine < 0.0) ++opposedBins;
            // minimum of |d + m(w-d)| over m in [0,1]
            const std::complex<double> delta = w - d;
            const double denominator = std::norm (delta);
            double best = juce::jmin (dm, wm);
            if (denominator > 1.0e-20)
            {
                const double m = juce::jlimit (0.0, 1.0,
                    -(d.real() * delta.real() + d.imag() * delta.imag()) / denominator);
                best = juce::jmin (best, std::abs (d + m * delta));
            }
            worstDipDb = juce::jmin (worstDipDb,
                20.0 * std::log10 (best / juce::jmin (dm, wm) + 1.0e-30));
        }
    }
    const double opposedPercent = 100.0 * opposedBins / juce::jmax (1, countedBins);
    std::cout << "dry/wet phase opposition: " << opposedPercent << "% of bins over 90 deg"
              << " (worst cos " << worstCosine << ", deepest partial-Mix dip "
              << worstDipDb << " dB)\n";
    // This is a characterisation, not a pass/fail: phase rotation is inherent to the model.
    // It fails only if the blend is outright broken (dry and wet systematically inverted).
    const bool phasePass = opposedPercent < 50.0 && countedBins > 1000;
    std::cout << "dry/wet blend sanity: " << (phasePass ? "PASS" : "FAIL") << '\n';
    failures += phasePass ? 0 : 1;

    // Benchmark on real signal: an all-zero buffer exercises none of the model path and
    // reports a real-time factor that says nothing about the plug-in's actual cost.
    ClearRoomAudioProcessor speedProcessor;
    speedProcessor.prepareToPlay (sampleRate, sampleCount);
    juce::AudioBuffer<float> speed (2, sampleCount);
    for (int i = 0; i < sampleCount; ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        const float value = static_cast<float> (0.3 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t)
                                              + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 3100.0 * t));
        speed.setSample (0, i, value);
        speed.setSample (1, i, value);
    }
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
