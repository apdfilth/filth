#pragma once

#include "PluginProcessor.h"

class FilthLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;
};

class FilthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FilthAudioProcessorEditor(FilthAudioProcessor&);
    ~FilthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FilthAudioProcessor& processorRef;
    FilthLookAndFeel filthLookAndFeel;

    juce::Slider filthKnob;
    juce::Label filthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filthAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilthAudioProcessorEditor)
};
