#include "PluginEditor.h"
#include <BinaryData.h>
#include <algorithm>
#include <cmath>

namespace
{
const juce::Rectangle<float> ledHousing { 116.0f, 421.0f, 184.0f, 50.0f };
const juce::Rectangle<int> knobReadout { 172, 379, 72, 25 };

juce::Point<float> pointOnDial(juce::Point<float> centre, float radius, float angle)
{
    return centre + juce::Point<float>(radius * std::sin(angle), -radius * std::cos(angle));
}

void drawSmdLed(juce::Graphics& g, juce::Point<float> centre, float level)
{
    const juce::Colour amber(0xffffa62b);
    const auto package = juce::Rectangle<float>(18.0f, 11.0f).withCentre(centre);
    g.setColour(juce::Colour(0xff858176));
    g.fillRoundedRectangle(package, 1.5f);
    g.setColour(juce::Colour(0xffb8a982));
    g.fillRect(package.withWidth(3.0f).reduced(0.0f, 2.0f));
    g.fillRect(package.withLeft(package.getRight() - 3.0f).reduced(0.0f, 2.0f));

    const auto lens = package.reduced(3.5f, 1.1f);
    g.setColour(juce::Colour(0xff69543c).interpolatedWith(amber, level));
    g.fillRoundedRectangle(lens, 1.2f);
    g.setColour(juce::Colour(0xff33251b).interpolatedWith(juce::Colour(0xffffd788), level));
    g.fillRoundedRectangle(lens.reduced(3.5f, 2.3f), 1.0f);

    if (level > 0.0f)
    {
        g.setGradientFill(juce::ColourGradient(amber.withAlpha(level * 0.64f), centre,
                            amber.withAlpha(0.0f), centre.translated(16.0f, 0.0f), true));
        g.fillEllipse(juce::Rectangle<float>(32.0f, 32.0f).withCentre(centre));
    }
}

void drawLedWindow(juce::Graphics& g, float amount)
{
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(ledHousing.expanded(2.0f).translated(0.0f, 3.0f), 12.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff81817a), ledHousing.getTopLeft(),
                                          juce::Colour(0xff262226), ledHousing.getBottomLeft(), false));
    g.fillRoundedRectangle(ledHousing, 11.0f);

    const auto cavity = ledHousing.reduced(3.0f);
    g.setColour(juce::Colour(0xff130f15));
    g.fillRoundedRectangle(cavity, 9.0f);
    const auto centre = cavity.getCentre();
    constexpr int ledCount = 5;
    for (int i = 0; i < ledCount; ++i)
    {
        const auto offset = static_cast<float>(i - ledCount / 2) * 31.0f;
        const auto level = std::clamp(amount * ledCount - static_cast<float>(i), 0.0f, 1.0f);
        drawSmdLed(g, centre.translated(offset, 0.0f), level);
    }

    // Tinted acrylic with a reflected light strip over the LED packages.
    juce::ColourGradient acrylic(juce::Colour(0x65544c3e), cavity.getTopLeft(),
                                 juce::Colour(0x5c17120d), cavity.getBottomLeft(), false);
    acrylic.addColour(0.42, juce::Colour(0x1f4b3d33));
    g.setGradientFill(acrylic);
    g.fillRoundedRectangle(cavity, 9.0f);
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.drawRoundedRectangle(cavity.reduced(2.0f), 7.0f, 0.8f);
    g.setColour(juce::Colours::white.withAlpha(0.14f));
    g.fillRoundedRectangle(cavity.reduced(12.0f, 6.0f).withHeight(3.0f), 1.5f);
}
}

void FilthLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = 0.38f * std::min(bounds.getWidth(), bounds.getHeight());
    const auto body = juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(centre);
    const float indicatorAngle = std::lerp(rotaryStartAngle, rotaryEndAngle, sliderPos);

    for (int tick = 0; tick <= 20; ++tick)
    {
        const auto angle = std::lerp(rotaryStartAngle, rotaryEndAngle, static_cast<float>(tick) * 0.05f);
        const bool major = tick % 5 == 0;
        const auto inner = pointOnDial(centre, radius + 12.0f, angle);
        const auto outer = pointOnDial(centre, radius + (major ? 21.0f : 17.0f), angle);
        g.setColour(juce::Colour(0xff211d20));
        g.drawLine({ inner, outer }, major ? 3.6f : 1.8f);
        g.setColour(juce::Colour(0xffe8e4b8).withAlpha(0.7f));
        g.drawLine({ inner.translated(0.0f, -0.7f), outer.translated(0.0f, -0.7f) }, major ? 1.6f : 0.7f);
    }

    const auto shadowCentre = centre.translated(3.0f, 7.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black, shadowCentre,
        juce::Colours::transparentBlack, shadowCentre.translated(radius + 12.0f, 0.0f), true));
    g.fillEllipse(body.expanded(12.0f).withCentre(shadowCentre));
    g.setColour(juce::Colour(0xff181317));
    g.fillEllipse(body);

    // Ribbed phenolic skirt. Its ridges rotate with the indicator.
    for (int rib = 0; rib < 60; ++rib)
    {
        const float angle = indicatorAngle + juce::MathConstants<float>::twoPi * static_cast<float>(rib) / 60.0f;
        const auto inner = pointOnDial(centre, radius - 10.0f, angle);
        const auto edge = pointOnDial(centre, radius - 1.0f, angle);
        const float sheen = 0.5f + 0.5f * std::cos(angle + 0.65f);
        g.setColour(juce::Colour(0xff292229).interpolatedWith(juce::Colour(0xff796b70), sheen));
        g.drawLine({ inner, edge }, 2.1f);
    }

    const auto cap = body.reduced(9.0f);
    juce::ColourGradient surface(juce::Colour(0xff615357), cap.getTopLeft(),
                                 juce::Colour(0xff1a151b), cap.getBottomRight(), false);
    surface.addColour(0.45, juce::Colour(0xff30272d));
    g.setGradientFill(surface);
    g.fillEllipse(cap);
    g.setColour(juce::Colour(0xff89767c).withAlpha(0.67f));
    g.drawEllipse(cap.reduced(1.0f), 1.0f);
    g.setColour(juce::Colour(0xff160f16));
    g.drawEllipse(cap.reduced(4.0f), 1.0f);

    const auto markerStart = pointOnDial(centre, radius * 0.45f, indicatorAngle);
    const auto markerEnd = pointOnDial(centre, radius - 19.0f, indicatorAngle);
    g.setColour(juce::Colour(0xff171219));
    g.drawLine({ markerStart.translated(1.0f, 1.0f), markerEnd.translated(1.0f, 1.0f) }, 6.5f);
    g.setColour(juce::Colour(0xfff1e9b9));
    g.drawLine({ markerStart, markerEnd }, 4.5f);
}

FilthAudioProcessorEditor::FilthAudioProcessorEditor(FilthAudioProcessor& p)
    : AudioProcessorEditor(&p),
      faceplate(juce::ImageCache::getFromMemory(BinaryData::FilthPedal_jpg, BinaryData::FilthPedal_jpgSize))
{
    filthKnob.setName("Filth");
    filthKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    filthKnob.setRotaryParameters(juce::MathConstants<float>::pi * 1.23f,
                                 juce::MathConstants<float>::pi * 2.77f, true);
    filthKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    filthKnob.setLookAndFeel(&filthLookAndFeel);
    filthKnob.onDragStart = [this]
    {
        adjustingKnob = true;
        repaint(knobReadout);
    };
    filthKnob.onDragEnd = [this]
    {
        adjustingKnob = false;
        repaint(knobReadout);
    };
    filthKnob.onValueChange = [this]
    {
        repaint(ledHousing.getSmallestIntegerContainer().expanded(8));
        if (adjustingKnob)
            repaint(knobReadout);
    };
    addAndMakeVisible(filthKnob);

    filthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "filth", filthKnob);
    setSize(416, 520);
}

FilthAudioProcessorEditor::~FilthAudioProcessorEditor()
{
    filthKnob.setLookAndFeel(nullptr);
}

void FilthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b1913));
    g.drawImage(faceplate, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    drawLedWindow(g, static_cast<float>(filthKnob.getValue()));

    if (adjustingKnob)
    {
        g.setColour(juce::Colour(0xf01b1913));
        g.fillRoundedRectangle(knobReadout.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xffffdfaa));
        g.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
        g.drawText(filthKnob.getTextFromValue(filthKnob.getValue()), knobReadout, juce::Justification::centred);
    }
}

void FilthAudioProcessorEditor::resized()
{
    filthKnob.setBounds(83, 149, 250, 226);
}
