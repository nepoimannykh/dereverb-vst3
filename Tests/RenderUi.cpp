// Renders the editor to a PNG so the interface can be reviewed without loading the
// plug-in into a host. Feeds the processor a reverberant burst first, so the envelope
// display shows real captured data rather than an empty panel.
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File output { juce::String (argc > 1 ? argv[1] : "ui.png") };
    const float mixPercent = argc > 2 ? juce::String (argv[2]).getFloatValue() : 100.0f;

    ClearRoomAudioProcessor processor;
    auto* parameter = processor.parameters.getParameter ("mix");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (mixPercent));
    processor.prepareToPlay (48000.0, 512);

    // Syllables with a decaying modal tail: what the display is meant to show.
    const int total = 48000 * 2;
    juce::AudioBuffer<float> buffer (2, total);
    for (int i = 0; i < total; ++i)
    {
        const double phase = juce::MathConstants<double>::twoPi * 145.0 * i / 48000.0;
        const float syllable = (i % 12000) < 7000 ? 1.0f : 0.0f;
        auto value = syllable * (float) (0.16 * std::sin (phase) + 0.06 * std::sin (2.03 * phase));
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value);
    }
    for (int i = 1; i < total; ++i)
    {
        const float fed = 0.6f * buffer.getSample (0, i - 1)
                        + (i > 2400 ? 0.34f * buffer.getSample (0, i - 2400) : 0.0f);
        buffer.setSample (0, i, buffer.getSample (0, i) + fed * 0.45f);
        buffer.setSample (1, i, buffer.getSample (0, i));
    }
    juce::MidiBuffer midi;
    for (int start = 0; start + 512 <= total; start += 512)
    {
        juce::AudioBuffer<float> block (2, 512);
        for (int c = 0; c < 2; ++c)
            block.copyFrom (c, 0, buffer, c, start, 512);
        processor.processBlock (block, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr) { std::cout << "no editor\n"; return 1; }
    // Run the real message loop so the editor's own timer fires; the editor inherits
    // juce::Timer privately, so it cannot be driven directly from here.
    editor->setBounds (0, 0, editor->getWidth(), editor->getHeight());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (700);

    juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    { juce::Graphics g (image); editor->paintEntireComponent (g, true); }

    output.deleteFile();
    juce::FileOutputStream stream (output);
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, stream)) { std::cout << "write failed\n"; return 1; }
    std::cout << "wrote " << output.getFullPathName() << " ("
              << editor->getWidth() << "x" << editor->getHeight() << ")\n";
    return 0;
}
