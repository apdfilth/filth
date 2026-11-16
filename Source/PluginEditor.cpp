#include "PluginEditor.h"
#include <BinaryData.h>
#include <algorithm>
#include <cmath>

namespace
{
const juce::Rectangle<int> knobBounds { 100, 230, 216, 198 };
const juce::Rectangle<int> knobReadout { 327, 317, 64, 23 };
const juce::Rectangle<float> logoBounds { 140.0f, 38.0f, 136.0f, 170.0f };

// Reference center and radius of the knob removed from the source sunburst.
const juce::Point<float> sunburstPivot { 616.0f, 625.5f };
constexpr float sunburstRadius = 439.0f;

float knobRadius(juce::Rectangle<float> bounds)
{
    return 0.38f * std::min(bounds.getWidth(), bounds.getHeight());
}
}

class FilthDebugPanel final : public juce::Component, private juce::Timer
{
public:
    explicit FilthDebugPanel(FilthAudioProcessor& p) : processor(p)
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto& row = rows[i];
            const auto stage = static_cast<filth::Stage>(i);
            row.enabled.setButtonText(filth::stageNames[i]);
            row.enabled.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffe9e0cb));
            row.enabled.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffffb75d));
            row.enabled.onClick = [this, i, stage]
            {
                const auto bit = filth::stageBit(stage);
                setMask(rows[i].enabled.getToggleState() ? mask | bit : mask & ~bit);
            };
            row.solo.setButtonText("Solo");
            row.solo.setTitle(juce::String("Solo ") + filth::stageNames[i]);
            row.solo.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3128));
            row.solo.onClick = [this, stage]
            {
                const auto bit = filth::stageBit(stage);
                setMask(mask == bit ? filth::allStages : bit);
            };
            addAndMakeVisible(row.enabled);
            addAndMakeVisible(row.solo);
        }

        reset.onClick = [this] { setMask(filth::allStages); };
        bypass.onClick = [this] { setMask(0); };
        addAndMakeVisible(reset);
        addAndMakeVisible(bypass);
        setMask(filth::allStages);
    }

    ~FilthDebugPanel() override
    {
        stopTimer();
        processor.setDebugStages(filth::allStages);
    }

    void open()
    {
        setVisible(true);
        startTimerHz(15);
    }

    void close()
    {
        stopTimer();
        setMask(filth::allStages);
        setVisible(false);
    }

    void resized() override
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const int y = firstRowY + static_cast<int>(i) * rowHeight;
            rows[i].enabled.setBounds(12, y + 1, 150, rowHeight - 2);
            rows[i].solo.setBounds(getWidth() - 55, y + 5, 42, rowHeight - 10);
        }
        reset.setBounds(12, getHeight() - 38, 77, 26);
        bypass.setBounds(96, getHeight() - 38, 82, 26);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff211d17));
        g.setColour(juce::Colour(0xffffce8f));
        g.setFont(juce::FontOptions(17.0f).withStyle("Bold"));
        g.drawText("Audio path debug", 14, 9, 240, 26, juce::Justification::centredLeft);

        const auto live = processor.getDebugValues();
        const filth::Compression compression(live.compressorAmount);
        const filth::DriveStages drives(live.amount);
        const filth::SpectralSettings spectral(live.spectralAmounts[0], filth::codecPasses[0]);
        const filth::SpectralSettings secondPass(live.spectralAmounts[1], filth::codecPasses[1]);
        const filth::SpectralSettings thirdPass(live.spectralAmounts[2], filth::codecPasses[2]);
        const auto cutoff = [&](const filth::SpectralSettings& settings)
        {
            return juce::String(static_cast<float>(settings.cutoffBin(live.sampleRate))
                               * live.sampleRate / (1000.0f * filth::fftSize), 2) + " kHz";
        };
        const auto percent = [](float value) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; };
        const auto driveText = [&](float blend, float gain)
        {
            return percent(blend) + " blend  /  " + juce::String(gain, 2) + "× gain";
        };
        const auto passText = [&](const filth::SpectralSettings& settings)
        {
            return percent(settings.strength) + " strength / " + cutoff(settings)
                + " / " + percent(settings.quantStep()) + " step";
        };

        g.setColour(juce::Colour(0xffd7c7b0));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("FILTH " + percent(live.targetAmount) + "  /  live " + percent(live.amount),
                   14, 69, 185, 19, juce::Justification::centredLeft);
        g.drawText(juce::String(live.sampleRate / 1000.0f, 1) + " kHz  /  4× drives",
                   204, 69, 198, 19, juce::Justification::centredRight);

        const juce::String values[] {
            juce::String(compression.threshold(), 1) + " dB  /  " + juce::String(compression.ratio(), 1) + ":1  /  3/60 ms",
            driveText(drives.overdrive, drives.overdriveGain()),
            cutoff(spectral) + "  /  " + juce::String(filth::fftSize) + "-sample FFT",
            percent(spectral.bandGate()) + " band gate  /  " + percent(spectral.binGate()) + " bin gate",
            juce::String(spectral.quantStep() * 100.0f, 1) + "% of band peak per step",
            passText(secondPass),
            passText(thirdPass),
            driveText(drives.fold1, drives.fold1Gain()),
            driveText(drives.fold2, drives.fold2Gain()),
            juce::String(filth::dcCutoffHz, 0) + " Hz high-pass",
            juce::String(juce::Decibels::gainToDecibels(filth::outputGain(live.amount)), 2) + " dB gain",
            juce::String(juce::Decibels::gainToDecibels(filth::ceilingKnee), 2) + " dBFS knee  /  0 dBFS ceiling",
            mask == 0 ? juce::String("Latency-aligned dry reference")
                      : percent(stageEnabled(mask, filth::Stage::mix) ? filth::wetMix(live.amount) : 1.0f)
                          + " wet  /  latency-aligned dry"
        };
        static_assert(std::size(values) == stageCount);

        g.setFont(juce::FontOptions(12.0f));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const int y = firstRowY + static_cast<int>(i) * rowHeight;
            if (mask == filth::stageBit(static_cast<filth::Stage>(i)))
            {
                g.setColour(juce::Colour(0xff463521));
                g.fillRect(8, y, getWidth() - 16, rowHeight);
            }
            g.setColour(juce::Colour(0xff3e362b));
            g.drawHorizontalLine(y, 12.0f, static_cast<float>(getWidth() - 12));
            g.setColour(stageEnabled(mask, static_cast<filth::Stage>(i)) ? juce::Colour(0xffffdbac)
                                                                       : juce::Colour(0xffa99d8d));
            g.drawFittedText(values[i], 165, y, getWidth() - 225, rowHeight,
                             juce::Justification::centredLeft, 1, 0.85f);
        }
        g.setColour(juce::Colour(0xffc0b09a));
        g.drawText("Closing restores all stages", 184, getHeight() - 38, getWidth() - 198, 26,
                   juce::Justification::centredRight);
    }

private:
    static constexpr auto stageCount = static_cast<size_t>(filth::Stage::count);
    static constexpr int firstRowY = 91;
    static constexpr int rowHeight = 29;

    struct Row
    {
        juce::ToggleButton enabled;
        juce::TextButton solo;
    };

    void setMask(filth::StageMask newMask)
    {
        mask = newMask;
        processor.setDebugStages(mask);
        for (size_t i = 0; i < rows.size(); ++i)
            rows[i].enabled.setToggleState(stageEnabled(mask, static_cast<filth::Stage>(i)), juce::dontSendNotification);
        repaint();
    }

    void timerCallback() override { repaint(); }

    FilthAudioProcessor& processor;
    std::array<Row, stageCount> rows;
    juce::TextButton reset { "Enable all" };
    juce::TextButton bypass { "Bypass all" };
    filth::StageMask mask = filth::allStages;
};

FilthLookAndFeel::FilthLookAndFeel()
    : knobImage(juce::ImageCache::getFromMemory(BinaryData::FilthKnob_png, BinaryData::FilthKnob_pngSize))
{
}

void FilthLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = knobRadius(bounds);
    const auto body = juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(centre);
    const float indicatorAngle = std::lerp(rotaryStartAngle, rotaryEndAngle, sliderPos);

    const auto shadowCentre = centre.translated(3.0f, 7.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::black, shadowCentre,
        juce::Colours::transparentBlack, shadowCentre.translated(radius + 12.0f, 0.0f), true));
    g.fillEllipse(body.expanded(12.0f).withCentre(shadowCentre));
    const float diameter = radius * 2.15f; // Include the transparent margin around the photographed knob.
    const auto transform = juce::AffineTransform::scale(diameter / static_cast<float>(knobImage.getWidth()),
                                                        diameter / static_cast<float>(knobImage.getHeight()))
        .translated(-diameter * 0.5f, -diameter * 0.5f)
        .rotated(indicatorAngle)
        .translated(centre.x, centre.y);
    g.drawImageTransformed(knobImage, transform);
}

void FilthAudioProcessorEditor::DebugButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    if (!getToggleState())
        return;

    g.setColour(juce::Colour(highlighted || down ? 0xff584632 : 0xff3a3128));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(4.0f), 4.0f);
    g.setColour(juce::Colour(0xffffdcad));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("Close", getLocalBounds(), juce::Justification::centred);
}

FilthAudioProcessorEditor::FilthAudioProcessorEditor(FilthAudioProcessor& p)
    : AudioProcessorEditor(&p),
      faceplate(juce::ImageCache::getFromMemory(BinaryData::FilthPedal_jpg, BinaryData::FilthPedal_jpgSize)),
      scribble(juce::ImageCache::getFromMemory(BinaryData::FilthScribble_png, BinaryData::FilthScribble_pngSize)),
      sunburst(juce::ImageCache::getFromMemory(BinaryData::FilthSunburst_png, BinaryData::FilthSunburst_pngSize))
{
    debugPanel = std::make_unique<FilthDebugPanel>(p);
    addChildComponent(*debugPanel);

    filthKnob.setName("Filth");
    filthKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    filthKnob.setRotaryParameters(juce::MathConstants<float>::pi * 1.23f,
                                 juce::MathConstants<float>::pi * 2.77f, true);
    filthKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    filthKnob.setColour(juce::Slider::trackColourId, juce::Colour(0xffae7639));
    filthKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffc474));
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
        if (adjustingKnob)
            repaint(knobReadout);
    };
    addAndMakeVisible(filthKnob);

    filthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "filth", filthKnob);

    debugButton.setClickingTogglesState(true);
    debugButton.setTitle("Toggle audio path debug panel");
    debugButton.onClick = [this]
    {
        const bool showDebug = debugButton.getToggleState();
        if (showDebug)
            debugPanel->open();
        else
            debugPanel->close();
        filthKnob.setSliderStyle(showDebug ? juce::Slider::LinearHorizontal
                                          : juce::Slider::RotaryHorizontalVerticalDrag);
        resized();
    };
    addAndMakeVisible(debugButton);
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
    g.drawImage(scribble, logoBounds, juce::RectanglePlacement::centred);

    const auto centre = knobBounds.getCentre().toFloat();
    const float scale = knobRadius(knobBounds.toFloat()) / sunburstRadius;
    g.drawImageTransformed(sunburst, juce::AffineTransform::scale(scale)
        .translated(centre.x - sunburstPivot.x * scale, centre.y - sunburstPivot.y * scale));

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
    debugPanel->setBounds(getLocalBounds());
    debugButton.setBounds(getWidth() - 62, 0, 62, 43);
    if (debugButton.getToggleState())
        filthKnob.setBounds(12, 37, getWidth() - 24, 27);
    else
        filthKnob.setBounds(knobBounds);
}
