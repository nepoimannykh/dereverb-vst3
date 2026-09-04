// Loads the built VST3 the same way a host does: scan the bundle, instantiate the
// component, prepare it, and run audio through it. This is the step that fails inside
// DaVinci Resolve when the bundle, its embedded dylib, or the factory is not sound.
#include <JuceHeader.h>
#include <iostream>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage: ClearRoomHostCheck <path-to.vst3>\n";
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File bundle { juce::String (argv[1]) };
    std::cout << "bundle: " << bundle.getFullPathName() << "\n"
              << "exists: " << (bundle.exists() ? "yes" : "NO") << "\n\n";
    if (! bundle.exists()) return 1;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, bundle.getFullPathName());
    std::cout << "scan: found " << found.size() << " plug-in(s)\n";
    if (found.isEmpty())
    {
        std::cout << "SCAN FAILED - the host cannot enumerate any class in this bundle.\n";
        return 1;
    }
    for (auto* description : found)
        std::cout << "  name=" << description->name
                  << "  manufacturer=" << description->manufacturerName
                  << "  version=" << description->version
                  << "  uid=" << juce::String::toHexString (description->uniqueId)
                  << "  isInstrument=" << (description->isInstrument ? "yes" : "no") << "\n";

    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> instance (
        format.createInstanceFromDescription (*found[0], 48000.0, 512, error));
    if (instance == nullptr)
    {
        std::cout << "\nINSTANTIATION FAILED: " << error << "\n";
        return 1;
    }
    std::cout << "\ninstantiate: OK\n";

    // Resolve probes several layouts; mono, stereo and 5.1 must all survive prepare/process.
    const juce::AudioChannelSet layouts[] { juce::AudioChannelSet::mono(),
                                            juce::AudioChannelSet::stereo(),
                                            juce::AudioChannelSet::create5point1() };
    int failures = 0;
    for (const auto& set : layouts)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (set);
        layout.outputBuses.add (set);
        const bool accepted = instance->setBusesLayout (layout);
        if (! accepted)
        {
            std::cout << "  layout " << set.getDescription() << ": REJECTED\n";
            ++failures;
            continue;
        }
        instance->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (juce::jmax (1, set.size()), 512);
        juce::MidiBuffer midi;
        for (int block = 0; block < 8; ++block)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample (channel, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi
                                     * 220.0f * static_cast<float> (block * 512 + i) / 48000.0f));
            instance->processBlock (buffer, midi);
        }
        bool finite = true;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int i = 0; i < 512; ++i)
                finite = finite && std::isfinite (buffer.getSample (channel, i));
        std::cout << "  layout " << set.getDescription() << ": prepare+process "
                  << (finite ? "OK" : "PRODUCED NON-FINITE OUTPUT")
                  << "  (latency " << instance->getLatencySamples() << ")\n";
        failures += finite ? 0 : 1;
        instance->releaseResources();
    }

    std::cout << "\nparameters exposed to the host: " << instance->getParameters().size() << "\n";
    for (auto* parameter : instance->getParameters())
        std::cout << "  " << parameter->getName (64) << " = " << parameter->getValue() << "\n";

    std::cout << "\n" << (failures == 0 ? "HOST CHECK PASSED" : "HOST CHECK FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
