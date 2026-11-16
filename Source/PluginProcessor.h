#pragma once

#include "FilthResponse.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class FilthAudioProcessor final : public juce::AudioProcessor
{
public:
    FilthAudioProcessor();
    ~FilthAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName(int index) override { juce::ignoreUnused(index); return {}; }
    void changeProgramName(int index, const juce::String& newName) override { juce::ignoreUnused(index, newName); }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    struct DebugValues
    {
        float targetAmount;
        float amount;
        float compressorAmount;
        std::array<float, filth::codecPasses.size()> spectralAmounts;
        float sampleRate;
    };

    DebugValues getDebugValues() const noexcept;
    void setDebugStages(filth::StageMask mask) noexcept { debugStages.store(mask, std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kFftOrder = 10;
    static constexpr int kFftSize  = filth::fftSize;
    static_assert(kFftSize == (1 << kFftOrder));
    static constexpr int kHopSize  = kFftSize / 2;   // 50% overlap

    struct ChannelStft
    {
        std::array<float, kFftSize> inputFifo {};
        std::array<float, kFftSize> outputAccum {};
        std::array<float, kHopSize> readyOutput {};
        int pos = 0;
        float frameAmount = 0.0f;

        void clear() noexcept
        {
            inputFifo.fill(0.0f);
            outputAccum.fill(0.0f);
            readyOutput.fill(0.0f);
            pos = 0;
            frameAmount = 0.0f;
        }
    };

    using CodecChain = std::array<ChannelStft, filth::codecPasses.size()>;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void processAudio(juce::dsp::AudioBlock<float> block, filth::StageMask mask) noexcept;
    void applyPreCodecDrive(juce::dsp::AudioBlock<float> block, bool enabled) noexcept;
    void applyPostCodecDrive(juce::dsp::AudioBlock<float> block, filth::StageMask mask) noexcept;
    float processStftSample(ChannelStft& state, float inputSample, float amount,
                            const filth::CodecPass&, filth::StageMask mask) noexcept;
    void applyMp3SpectralArtifacts(const filth::SpectralSettings&, filth::StageMask mask) noexcept;

    std::atomic<float>* filthParam = nullptr;
    juce::SmoothedValue<float> filthSmoothed;
    std::atomic<filth::StageMask> debugStages { filth::allStages };
    std::atomic<float> debugAmount { 0.0f };
    std::atomic<float> debugCompressorAmount { 0.0f };
    std::array<std::atomic<float>, filth::codecPasses.size()> debugSpectralAmounts {};
    std::atomic<float> debugSampleRate { 48000.0f };

    juce::dsp::Compressor<float> compressor;
    juce::dsp::FirstOrderTPTFilter<float> dcBlocker;
    std::unique_ptr<juce::dsp::Oversampling<float>> preCodecOversampler;
    std::unique_ptr<juce::dsp::Oversampling<float>> postCodecOversampler;
    juce::AudioBuffer<float> dryDelay;
    juce::AudioBuffer<float> dryBlock;
    std::vector<float> knobValues;
    int dryWritePos = 0;

    juce::dsp::FFT fft { kFftOrder };
    std::array<float, kFftSize> sineWindow {};
    std::array<float, kFftSize * 2> fftWorkBuffer {};
    std::vector<CodecChain> stftChannels;

    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FilthAudioProcessor)
};
