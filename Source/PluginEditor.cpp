#include "PluginEditor.h"

void ClearRoomAudioProcessorEditor::ClearRoomLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height, float position,
    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto size = static_cast<float> (juce::jmin (width, height)) - 18.0f;
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                 static_cast<float> (width), static_cast<float> (height))
                            .withSizeKeepingCentre (size, size);
    const float angle = startAngle + position * (endAngle - startAngle);
    juce::Path track, value;
    track.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), bounds.getWidth() * 0.5f,
                         bounds.getHeight() * 0.5f, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), bounds.getWidth() * 0.5f,
                         bounds.getHeight() * 0.5f, 0.0f, startAngle, angle, true);
    g.setColour (juce::Colour (0xff263640));
    g.strokePath (track, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (value, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff0c1419));
    g.fillEllipse (bounds.reduced (12.0f));
    const auto centre = bounds.getCentre();
    const auto pointer = juce::Point<float> (0.0f, -bounds.getHeight() * 0.27f).rotatedAboutOrigin (angle);
    g.setColour (juce::Colour (0xfff4fbf9));
    g.drawLine ({ centre, centre + pointer }, 2.5f);
}

ClearRoomAudioProcessorEditor::ClearRoomAudioProcessorEditor (ClearRoomAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 22);
    mixKnob.setLookAndFeel (&lookAndFeel);
    mixKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff58e1bd));
    mixKnob.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xffdce8e8));
    mixKnob.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff101b21));
    mixKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (mixKnob);

    mixLabel.setText ("MIX", juce::dontSendNotification);
    mixLabel.setJustificationType (juce::Justification::centred);
    mixLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc8d4da));
    addAndMakeVisible (mixLabel);
    mixAttachment = std::make_unique<Attachment> (audioProcessor.parameters, "mix", mixKnob);

    setSize (720, 360);
    startTimerHz (30);
}

ClearRoomAudioProcessorEditor::~ClearRoomAudioProcessorEditor()
{
    mixKnob.setLookAndFeel (nullptr);
}

void ClearRoomAudioProcessorEditor::paint (juce::Graphics& g)
{
    juce::ColourGradient background (juce::Colour (0xff14232b), 0.0f, 0.0f,
                                      juce::Colour (0xff081015), 0.0f, static_cast<float> (getHeight()), false);
    g.setGradientFill (background);
    g.fillAll();
    g.setColour (juce::Colour (0x1858e1bd));
    g.fillEllipse (-90.0f, -150.0f, 440.0f, 330.0f);

    g.setColour (juce::Colour (0xfff3faf8));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("JENYA / DEREVERB 2", 28, 20, 380, 36, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8ca3ad));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("REAL-TIME NEURAL VOICE RESTORATION", 30, 54, 330, 20, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff101a20));
    g.fillRoundedRectangle (24.0f, 88.0f, static_cast<float> (getWidth() - 48), 196.0f, 14.0f);
    g.setColour (juce::Colour (0xff24353e));
    g.drawRoundedRectangle (24.0f, 88.0f, static_cast<float> (getWidth() - 48), 196.0f, 14.0f, 1.0f);

    g.setColour (juce::Colour (0xff263740));
    g.fillRoundedRectangle (28.0f, 307.0f, 420.0f, 8.0f, 4.0f);
    const float meterWidth = 420.0f * juce::jlimit (0.0f, 1.0f, -displayedReduction / 18.0f);
    juce::ColourGradient meter (juce::Colour (0xff58e1bd), 28.0f, 0.0f,
                                juce::Colour (0xffffc66d), 448.0f, 0.0f, false);
    g.setGradientFill (meter);
    g.fillRoundedRectangle (28.0f, 307.0f, meterWidth, 8.0f, 4.0f);
    g.setColour (juce::Colour (0xff8ca3ad));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("ROOM REDUCTION", 28, 321, 180, 18, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffdce9e6));
    g.drawText (juce::String (displayedReduction, 1) + " dB", 350, 321, 98, 18,
                juce::Justification::centredRight);
    // Engine status gets its own full-width row: the bypass messages are long, and a
    // silently inactive plug-in must not look identical to a working one.
    const bool engineActive = audioProcessor.isEngineActive();
    g.setColour (engineActive ? juce::Colour (0xff58e1bd) : juce::Colour (0xffff6b5c));
    g.fillEllipse (28.0f, 341.0f, 7.0f, 7.0f);
    g.setColour (engineActive ? juce::Colour (0xff8ca3ad) : juce::Colour (0xffffb0a6));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (audioProcessor.getStatusText(), 44, 334, getWidth() - 68, 20,
                juce::Justification::centredLeft);
}

void ClearRoomAudioProcessorEditor::resized()
{
    auto column = juce::Rectangle<int> (getWidth() / 2 - 90, 98, 180, 176);
    mixLabel.setBounds (column.removeFromTop (24));
    mixKnob.setBounds (column);
}

void ClearRoomAudioProcessorEditor::timerCallback()
{
    displayedReduction += 0.18f * (audioProcessor.getReductionDb() - displayedReduction);
    repaint (juce::Rectangle<int> (20, 298, getWidth() - 40, 62));
}
