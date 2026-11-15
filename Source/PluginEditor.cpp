#include "PluginEditor.h"

void FilthLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider)
{
    juce::ignoreUnused(slider);

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(12.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Background track arc
    juce::Path bgArc;
    bgArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff2a2a2e));
    g.strokePath(bgArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Active value arc (shifts from amber to hot crimson as FILTH rises)
    const auto glowColour = juce::Colour(0xffff9f1c).interpolatedWith(juce::Colour(0xffff2a3c), sliderPos);
    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(glowColour);
    g.strokePath(valueArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Inner matte dial body
    const float dialRadius = radius - 12.0f;
    g.setColour(juce::Colour(0xff1b1b1f));
    g.fillEllipse(centre.x - dialRadius, centre.y - dialRadius, dialRadius * 2.0f, dialRadius * 2.0f);
    g.setColour(juce::Colour(0xff383840));
    g.drawEllipse(centre.x - dialRadius, centre.y - dialRadius, dialRadius * 2.0f, dialRadius * 2.0f, 1.5f);

    // Pointer indicator line
    juce::Path pointer;
    pointer.addRoundedRectangle(-2.0f, -dialRadius + 6.0f, 4.0f, dialRadius * 0.42f, 2.0f);
    g.setColour(glowColour);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

FilthAudioProcessorEditor::FilthAudioProcessorEditor(FilthAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    juce::ignoreUnused(processorRef);

    filthKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    filthKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 24);
    filthKnob.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd4d4d8));
    filthKnob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    filthKnob.setLookAndFeel(&filthLookAndFeel);
    addAndMakeVisible(filthKnob);

    filthLabel.setText("FILTH", juce::dontSendNotification);
    filthLabel.setJustificationType(juce::Justification::centred);
    filthLabel.setFont(juce::Font(juce::FontOptions(28.0f).withStyle("Bold")));
    filthLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff4f4f6));
    addAndMakeVisible(filthLabel);

    filthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "filth", filthKnob);

    setSize(280, 320);
}

FilthAudioProcessorEditor::~FilthAudioProcessorEditor()
{
    filthKnob.setLookAndFeel(nullptr);
}

void FilthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111114));

    g.setColour(juce::Colour(0xff27272c));
    g.drawRect(getLocalBounds(), 1);
}

void FilthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    filthLabel.setBounds(area.removeFromTop(42));
    filthKnob.setBounds(area);
}
