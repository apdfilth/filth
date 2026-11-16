#pragma once

#include "PluginProcessor.h"

class FilthDebugPanel;

class FilthLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FilthLookAndFeel();
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

private:
    juce::Image knobImage;
};

class FilthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FilthAudioProcessorEditor(FilthAudioProcessor&);
    ~FilthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct DebugButton final : juce::Button
    {
        DebugButton() : juce::Button("Toggle audio path debug panel") {}
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    FilthLookAndFeel filthLookAndFeel;
    juce::Image faceplate;
    juce::Image scribble;
    juce::Image sunburst;

    juce::Slider filthKnob;
    bool adjustingKnob = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filthAttachment;
    std::unique_ptr<FilthDebugPanel> debugPanel;
    DebugButton debugButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilthAudioProcessorEditor)
};
