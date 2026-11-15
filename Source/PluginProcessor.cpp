#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

FilthAudioProcessor::FilthAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    filthParam = apvts.getRawParameterValue("filth");

    // Princen-Bradley sine window (identical to MP3's MDCT window: sin^2 + cos^2 == 1 at 50% hop)
    for (int i = 0; i < kFftSize; ++i)
    {
        const float phase = juce::MathConstants<float>::pi * (static_cast<float>(i) + 0.5f) / static_cast<float>(kFftSize);
        sineWindow[static_cast<size_t>(i)] = std::sin(phase);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout FilthAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "filth", 1 },
        "Filth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })));
    return layout;
}

bool FilthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto outSet = layouts.getMainOutputChannelSet();
    if (outSet != juce::AudioChannelSet::mono() && outSet != juce::AudioChannelSet::stereo())
        return false;

    return outSet == layouts.getMainInputChannelSet();
}

void FilthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int numChannels = std::max(1, getTotalNumOutputChannels());

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(std::max(1, samplesPerBlock));
    spec.numChannels      = static_cast<juce::uint32>(numChannels);

    compressor.prepare(spec);
    compressor.setAttack(3.0f);
    compressor.setRelease(60.0f);

    // 4x (2^2) linear-phase FIR oversampling with integer latency for exact DAW PDC
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        static_cast<size_t>(numChannels),
        2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,
        true);
    oversampler->initProcessing(static_cast<size_t>(std::max(1, samplesPerBlock)));

    stftChannels.assign(static_cast<size_t>(numChannels), ChannelStft {});
    filthSmoothed.reset(sampleRate, 0.02);
    filthSmoothed.setCurrentAndTargetValue(filthParam->load());

    const int totalLatency = juce::roundToInt(oversampler->getLatencyInSamples()) + kFftSize;
    setLatencySamples(totalLatency);
}

void FilthAudioProcessor::releaseResources()
{
    if (oversampler != nullptr)
        oversampler->reset();
}

void FilthAudioProcessor::reset()
{
    compressor.reset();
    if (oversampler != nullptr)
        oversampler->reset();

    for (auto& ch : stftChannels)
        ch.clear();
}

void FilthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples             = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    if (numSamples == 0 || oversampler == nullptr)
        return;

    filthSmoothed.setTargetValue(filthParam->load());
    const float blockFilth = filthSmoothed.getCurrentValue();

    // 1. High-ratio compression (1:1 -> 20:1, threshold 0 dB -> -32 dB)
    compressor.setThreshold(-32.0f * blockFilth);
    compressor.setRatio(1.0f + 19.0f * blockFilth);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    compressor.process(context);

    // Smoothly advance knob across the block and apply compressor makeup gain before clipping
    float endFilth = blockFilth;
    for (int s = 0; s < numSamples; ++s)
    {
        endFilth = filthSmoothed.getNextValue();
        const float makeup = juce::Decibels::decibelsToGain(endFilth * 13.5f);
        for (int ch = 0; ch < totalNumOutputChannels; ++ch)
            buffer.getWritePointer(ch)[s] *= makeup;
    }

    // 2. 4x Oversampled Soft Clipper (normalized tanh: transparent at 0%, +21 dB drive at 100%)
    auto upsampledBlock = oversampler->processSamplesUp(block);
    const float drive   = 1.0e-3f + 11.5f * (endFilth * endFilth);
    const float invNorm = 1.0f / std::tanh(drive);

    const size_t osChannels = upsampledBlock.getNumChannels();
    const size_t osSamples  = upsampledBlock.getNumSamples();
    for (size_t ch = 0; ch < osChannels; ++ch)
    {
        float* data = upsampledBlock.getChannelPointer(ch);
        for (size_t s = 0; s < osSamples; ++s)
            data[s] = std::tanh(drive * data[s]) * invNorm;
    }
    oversampler->processSamplesDown(block);

    // 3. Spectral MP3 Artifact Stage + Post-Gibbs Safety Ceiling (-0.9 dBFS -> 0 dBFS soft knee)
    const size_t activeCh = std::min(static_cast<size_t>(totalNumOutputChannels), stftChannels.size());
    for (size_t ch = 0; ch < activeCh; ++ch)
    {
        float* data = buffer.getWritePointer(static_cast<int>(ch));
        auto& stft  = stftChannels[ch];

        for (int s = 0; s < numSamples; ++s)
        {
            float y = processStftSample(stft, data[s], endFilth);

            const float absY = std::abs(y);
            if (endFilth > 0.001f && absY > 0.9f)
            {
                const float compressed = 0.9f + 0.1f * std::tanh((absY - 0.9f) / 0.1f);
                y = std::copysign(compressed, y);
            }

            data[s] = y;
        }
    }
}

float FilthAudioProcessor::processStftSample(ChannelStft& state, float inputSample, float filth) noexcept
{
    const auto idx = static_cast<size_t>(state.pos);
    const float outSample = state.readyOutput[idx];
    state.inputFifo[static_cast<size_t>(kHopSize) + idx] = inputSample;

    if (++state.pos >= kHopSize)
    {
        state.pos = 0;

        fftWorkBuffer.fill(0.0f);
        for (size_t i = 0; i < static_cast<size_t>(kFftSize); ++i)
            fftWorkBuffer[i] = state.inputFifo[i] * sineWindow[i];

        fft.performRealOnlyForwardTransform(fftWorkBuffer.data(), true);

        if (filth > 0.0005f)
            applyMp3SpectralArtifacts(filth);

        fft.performRealOnlyInverseTransform(fftWorkBuffer.data());

        for (size_t i = 0; i < static_cast<size_t>(kFftSize); ++i)
            state.outputAccum[i] += fftWorkBuffer[i] * sineWindow[i];

        std::copy_n(state.outputAccum.begin(), kHopSize, state.readyOutput.begin());
        std::copy_n(state.outputAccum.begin() + kHopSize, kHopSize, state.outputAccum.begin());
        std::fill_n(state.outputAccum.begin() + kHopSize, kHopSize, 0.0f);

        std::copy_n(state.inputFifo.begin() + kHopSize, kHopSize, state.inputFifo.begin());
    }

    return outSample;
}

void FilthAudioProcessor::applyMp3SpectralArtifacts(float filth) noexcept
{
    const float mp3       = std::pow(filth, 1.25f);
    const float nyquist   = static_cast<float>(currentSampleRate * 0.5);
    const float minCutoff = std::min(3600.0f, nyquist * 0.8f);
    const float cutoffHz  = nyquist * std::pow(minCutoff / nyquist, mp3);
    const int cutoffBin   = juce::jlimit(1, kHopSize, static_cast<int>((cutoffHz / nyquist) * static_cast<float>(kHopSize)));

    // Brickwall high-frequency rolloff (emulates low-bitrate MP3 sfb21 cutoff)
    for (int k = cutoffBin + 1; k <= kHopSize; ++k)
    {
        fftWorkBuffer[static_cast<size_t>(2 * k)]     = 0.0f;
        fftWorkBuffer[static_cast<size_t>(2 * k + 1)] = 0.0f;
    }

    float globalPeak = 0.0f;
    for (int k = 1; k <= cutoffBin; ++k)
    {
        const float re = fftWorkBuffer[static_cast<size_t>(2 * k)];
        const float im = fftWorkBuffer[static_cast<size_t>(2 * k + 1)];
        globalPeak = std::max(globalPeak, std::sqrt(re * re + im * im));
    }

    // Critical-band psychoacoustic masking + coarse spectral quantization ("swirl" & pre-echo)
    for (int bStart = 1; bStart <= cutoffBin;)
    {
        const int bWidth = 2 + bStart / 6;
        const int bEnd   = std::min(cutoffBin, bStart + bWidth - 1);

        float bandPeak = 0.0f;
        for (int k = bStart; k <= bEnd; ++k)
        {
            const float re = fftWorkBuffer[static_cast<size_t>(2 * k)];
            const float im = fftWorkBuffer[static_cast<size_t>(2 * k + 1)];
            bandPeak = std::max(bandPeak, std::sqrt(re * re + im * im));
        }

        if (bandPeak < globalPeak * (0.20f * mp3))
        {
            for (int k = bStart; k <= bEnd; ++k)
            {
                fftWorkBuffer[static_cast<size_t>(2 * k)]     = 0.0f;
                fftWorkBuffer[static_cast<size_t>(2 * k + 1)] = 0.0f;
            }
        }
        else
        {
            const float binGate   = bandPeak * (0.62f * mp3);
            const float quantStep = bandPeak * (0.48f * mp3 * mp3);

            for (int k = bStart; k <= bEnd; ++k)
            {
                float& re = fftWorkBuffer[static_cast<size_t>(2 * k)];
                float& im = fftWorkBuffer[static_cast<size_t>(2 * k + 1)];
                const float mag = std::sqrt(re * re + im * im);

                if (mag < binGate)
                {
                    re = 0.0f;
                    im = 0.0f;
                }
                else if (quantStep > 1.0e-6f)
                {
                    re = std::round(re / quantStep) * quantStep;
                    im = std::round(im / quantStep) * quantStep;
                }
            }
        }

        bStart = bEnd + 1;
    }

    // Keep DC and Nyquist strictly real-valued
    fftWorkBuffer[1]                                    = 0.0f;
    fftWorkBuffer[static_cast<size_t>(2 * kHopSize + 1)] = 0.0f;
}

void FilthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void FilthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* FilthAudioProcessor::createEditor()
{
    return new FilthAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FilthAudioProcessor();
}
