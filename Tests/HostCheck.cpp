// Loads the built VST3 the same way a host does: scan the bundle, instantiate the
// component, prepare it, and run audio through it. This is the step that fails inside
// DaVinci Resolve when the bundle, its embedded dylib, or the factory is not sound.
#include <JuceHeader.h>
#include <iostream>
#include <thread>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "usage: ClearRoomHostCheck <path-to.vst3>\n";
        return 2;
    }
    std::cout << std::unitbuf;   // unbuffered: a hang must not swallow the last line
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File bundle { juce::String (argv[1]) };
    std::cout << "bundle: " << bundle.getFullPathName() << "\n"
              << "exists: " << (bundle.exists() ? "yes" : "NO") << "\n\n";
    if (! bundle.exists()) return 1;

    // Pick the format from the bundle extension so the same harness covers both the
    // VST3 and the Audio Unit that hosts actually load.
    std::unique_ptr<juce::AudioPluginFormat> formatOwner;
    if (bundle.hasFileExtension ("component"))
        formatOwner = std::make_unique<juce::AudioUnitPluginFormat>();
    else
        formatOwner = std::make_unique<juce::VST3PluginFormat>();
    auto& format = *formatOwner;
    std::cout << "format: " << format.getName() << "\n\n";

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

    // The model is 48 kHz only and falls back to passthrough elsewhere -- silently, from
    // the host's point of view. Reported latency is the tell: 960 when the engine runs,
    // 0 when it has bypassed itself.
    std::cout << "\nengine state by sample rate (latency 960 = model running, 0 = passthrough):\n";
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        instance->releaseResources();
        instance->prepareToPlay (rate, 512);
        juce::AudioBuffer<float> probe (2, 512);
        juce::MidiBuffer probeMidi;
        probe.clear();
        for (int i = 0; i < 512; ++i)
        {
            const auto value = 0.25f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                * static_cast<float> (i) / static_cast<float> (rate));
            probe.setSample (0, i, value);
            probe.setSample (1, i, value);
        }
        instance->processBlock (probe, probeMidi);
        const int reported = instance->getLatencySamples();
        std::cout << "  " << juce::String (rate / 1000.0, 1) << " kHz: latency " << reported
                  << (reported > 0 ? "   ENGINE ACTIVE" : "   PASSTHROUGH (model not running)") << "\n";
    }
    instance->releaseResources();

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

    // Editor creation through the plug-in wrapper -- the path a DAW takes when it opens
    // the window, and the one that shows nothing when it fails.
    std::cout << "\neditor:\n";
    instance->prepareToPlay (48000.0, 512);
    if (! instance->hasEditor())
    {
        std::cout << "  hasEditor() == false -- the host will never show a window\n";
        ++failures;
    }
    else
    {
        auto* editor = instance->createEditorIfNeeded();
        if (editor == nullptr)
        {
            std::cout << "  createEditorIfNeeded() returned NULL -- no window can appear\n";
            ++failures;
        }
        else
        {
            std::cout << "  created " << editor->getWidth() << "x" << editor->getHeight() << "\n";
            if (editor->getWidth() <= 0 || editor->getHeight() <= 0)
            {
                std::cout << "  ZERO-SIZED EDITOR -- host shows an empty or invisible window\n";
                ++failures;
            }
            // Force a full paint: a throw or crash here is what a blank window looks like.
            juce::Image surface (juce::Image::ARGB,
                                 juce::jmax (1, editor->getWidth()),
                                 juce::jmax (1, editor->getHeight()), true);
            {
                juce::Graphics g (surface);
                editor->paintEntireComponent (g, true);
            }
            std::cout << "  paint: OK\n";
            // Repaint while the audio thread re-prepares, which is when the editor reads
            // engine state that prepare() is concurrently writing.
            instance->editorBeingDeleted (editor);
            delete editor;
        }
    }

    std::cout << "\nparameters exposed to the host: " << instance->getParameters().size() << "\n";
    for (auto* parameter : instance->getParameters())
        std::cout << "  " << parameter->getName (64) << " = " << parameter->getValue() << "\n";

    std::cout << "\n" << (failures == 0 ? "HOST CHECK PASSED" : "HOST CHECK FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
