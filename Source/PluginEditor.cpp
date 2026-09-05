#include "PluginEditor.h"

namespace
{
constexpr float panelLeft = 28.0f;
constexpr float panelTop = 98.0f;
constexpr float panelWidth = 496.0f;
constexpr float panelHeight = 138.0f;
constexpr float knobCentreX = 622.0f;
constexpr float knobCentreY = 161.0f;
constexpr float knobRadius = 58.0f;
constexpr float floorDb = -60.0f;

float levelToHeight (float rms) noexcept
{
    const auto db = juce::Decibels::gainToDecibels (rms, floorDb);
    return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
}
}

juce::Font ClearRoomAudioProcessorEditor::titleFont()
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", 27.0f, juce::Font::plain));
}

juce::Font ClearRoomAudioProcessorEditor::labelFont()
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", 12.5f, juce::Font::plain));
}

juce::Font ClearRoomAudioProcessorEditor::readoutFont()
{
    return juce::Font (juce::FontOptions ("Helvetica Neue", 46.0f, juce::Font::plain));
}

juce::Rectangle<float> ClearRoomAudioProcessorEditor::envelopeBounds() const
{
    return { panelLeft, panelTop, panelWidth, panelHeight };
}

void ClearRoomAudioProcessorEditor::ClearRoomLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height, float position,
    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                            .withSizeKeepingCentre (knobRadius * 2.0f, knobRadius * 2.0f);
    const auto centre = bounds.getCentre();
    const float radius = knobRadius - 8.0f;
    const float angle = startAngle + position * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (Palette::rule));
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (position > 0.0f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        // A wider, very transparent pass under the stroke reads as a glow without a blur.
        g.setColour (juce::Colour (Palette::signal).withAlpha (0.16f));
        g.strokePath (value, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (Palette::signal));
        g.strokePath (value, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    g.setColour (juce::Colour (Palette::textBright));
    g.setFont (juce::Font (juce::FontOptions ("Helvetica Neue", 23.0f, juce::Font::plain)));
    g.drawText (juce::String (juce::roundToInt (slider.getValue())) + "%",
                bounds.reduced (18.0f), juce::Justification::centred, false);
}

ClearRoomAudioProcessorEditor::ClearRoomAudioProcessorEditor (ClearRoomAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mixKnob.setLookAndFeel (&lookAndFeel);
    mixKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                 juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (mixKnob);
    mixAttachment = std::make_unique<Attachment> (audioProcessor.parameters, "mix", mixKnob);

    setSize (720, 360);
    startTimerHz (30);
}

ClearRoomAudioProcessorEditor::~ClearRoomAudioProcessorEditor()
{
    mixKnob.setLookAndFeel (nullptr);
}

void ClearRoomAudioProcessorEditor::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour (juce::Colour (Palette::rule));
    g.drawRect (area, 1.0f);

    // Reference lines every 15 dB, drawn faint: they scale the tail without competing.
    for (int db = -15; db > -60; db -= 15)
    {
        const float t = (static_cast<float> (db) - floorDb) / -floorDb;
        const float lineY = area.getBottom() - t * area.getHeight();
        g.setColour (juce::Colour (Palette::rule).withAlpha (0.75f));
        g.fillRect (area.getX() + 1.0f, lineY, area.getWidth() - 2.0f, 1.0f);
    }

    const int count = ClearRoomAudioProcessor::envelopePoints;
    const float step = (area.getWidth() - 2.0f) / static_cast<float> (count - 1);
    const auto pointAt = [&] (const std::array<float, ClearRoomAudioProcessor::envelopePoints>& src, int i)
    {
        return juce::Point<float> (area.getX() + 1.0f + step * static_cast<float> (i),
                                   area.getBottom() - levelToHeight (src[(size_t) i]) * (area.getHeight() - 2.0f));
    };
    const auto buildFill = [&] (const std::array<float, ClearRoomAudioProcessor::envelopePoints>& src)
    {
        juce::Path path;
        path.startNewSubPath (area.getX() + 1.0f, area.getBottom());
        for (int i = 0; i < count; ++i)
            path.lineTo (pointAt (src, i));
        path.lineTo (area.getRight() - 1.0f, area.getBottom());
        path.closeSubPath();
        return path;
    };

    // The dry tail sits behind as a dull mass; the processed signal is drawn over it, so
    // the exposed area between them is exactly the reverb energy being removed.
    g.setColour (juce::Colour (Palette::residue));
    g.fillPath (buildFill (dryPoints));

    auto wetFill = buildFill (wetPoints);
    g.setGradientFill (juce::ColourGradient (juce::Colour (Palette::signal).withAlpha (0.34f), area.getCentreX(), area.getY(),
                                             juce::Colour (Palette::signalDeep).withAlpha (0.10f), area.getCentreX(), area.getBottom(), false));
    g.fillPath (wetFill);

    juce::Path wetLine;
    for (int i = 0; i < count; ++i)
    {
        const auto point = pointAt (wetPoints, i);
        if (i == 0) wetLine.startNewSubPath (point);
        else        wetLine.lineTo (point);
    }
    g.setColour (juce::Colour (Palette::signal));
    g.strokePath (wetLine, juce::PathStrokeType (1.6f));
}

// Level difference between the aligned dry and processed curves over the visible window,
// ignoring points where the input is essentially silent so pauses cannot skew it.
float ClearRoomAudioProcessorEditor::measuredRemovalDb (bool& hasSignal) const
{
    double dryEnergy = 0.0, wetEnergy = 0.0;
    int counted = 0;
    for (size_t i = 0; i < dryPoints.size(); ++i)
    {
        const float dry = dryPoints[i];
        if (dry < 1.0e-4f) continue;
        dryEnergy += static_cast<double> (dry) * dry;
        wetEnergy += static_cast<double> (wetPoints[i]) * wetPoints[i];
        ++counted;
    }
    hasSignal = counted > 4 && dryEnergy > 0.0;
    if (! hasSignal) return 0.0f;
    return juce::Decibels::gainToDecibels (static_cast<float> (std::sqrt (wetEnergy / dryEnergy)), -60.0f);
}

void ClearRoomAudioProcessorEditor::paint (juce::Graphics& g)
{
    audioProcessor.copyEnvelopes (dryPoints.data(), wetPoints.data());
    bool hasSignal = false;
    const float removal = measuredRemovalDb (hasSignal);
    displayedReduction += 0.25f * (removal - displayedReduction);
    g.fillAll (juce::Colour (Palette::ground));

    g.setColour (juce::Colour (Palette::textBright));
    g.setFont (titleFont());
    g.drawText ("Dereverb 2", 28, 20, 400, 32, juce::Justification::centredLeft);

    g.setColour (juce::Colour (Palette::textDim));
    g.setFont (labelFont());
    g.drawText ("Neural room removal for voice", 29, 52, 400, 18, juce::Justification::centredLeft);

    paintEnvelope (g, envelopeBounds());

    g.setColour (juce::Colour (Palette::textDim));
    g.setFont (labelFont());
    g.drawText ("Room tail", panelLeft + 1.0f, panelTop - 22.0f, 200.0f, 18.0f,
                juce::Justification::centredLeft);
    g.drawText ("1.7 s", panelLeft + panelWidth - 201.0f, panelTop - 22.0f, 200.0f, 18.0f,
                juce::Justification::centredRight);

    // Headline number, measured from the two curves actually on screen rather than from
    // the model's internal ratio: that ignores Mix, so it read 69 dB with the knob at 55%.
    g.setColour (juce::Colour (Palette::signal));
    g.setFont (readoutFont());
    const auto reduction = hasSignal ? juce::String (std::abs (displayedReduction), 1)
                                     : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
    g.drawText (reduction, 28, 250, 200, 52, juce::Justification::centredLeft);
    const auto reductionWidth = juce::GlyphArrangement::getStringWidth (readoutFont(), reduction);
    g.setColour (juce::Colour (Palette::textDim));
    g.setFont (labelFont());
    g.drawText ("dB removed", 32.0f + reductionWidth, 250.0f, 200.0f, 52.0f,
                juce::Justification::centredLeft);

    g.setFont (labelFont());
    g.setColour (juce::Colour (Palette::textDim));
    g.drawText ("Mix", knobCentreX - 60.0f, knobCentreY + knobRadius - 2.0f, 120.0f, 18.0f,
                juce::Justification::centred);

    const bool active = audioProcessor.isEngineActive();
    const auto stateColour = juce::Colour (active ? Palette::signal : Palette::alert);
    g.setColour (stateColour.withAlpha (0.22f));
    g.fillEllipse (28.0f, 325.0f, 9.0f, 9.0f);
    g.setColour (stateColour);
    g.fillEllipse (30.0f, 327.0f, 5.0f, 5.0f);
    g.setColour (active ? juce::Colour (Palette::textDim) : stateColour);
    g.setFont (labelFont());
    g.drawText (audioProcessor.getStatusText(), 46, 322, getWidth() - 74, 16,
                juce::Justification::centredLeft);
}

void ClearRoomAudioProcessorEditor::resized()
{
    mixKnob.setBounds (juce::Rectangle<int> ((int) (knobCentreX - knobRadius),
                                             (int) (knobCentreY - knobRadius),
                                             (int) (knobRadius * 2.0f), (int) (knobRadius * 2.0f)));
}

void ClearRoomAudioProcessorEditor::timerCallback()
{
    repaint();
}
