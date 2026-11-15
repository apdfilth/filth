#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FilthResponse.h"
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
            [](float value, int) { return juce::String(juce::roundToInt(value * 100.0f)) + "%"; })
            .withValueFromStringFunction([](const juce::String& text)
            {
                const auto value = text.getFloatValue();
                const bool isPercent = text.trimEnd().endsWithChar('%') || value > 1.0f;
                return std::clamp(isPercent ? value * 0.01f : value, 0.0f, 1.0f);
            })));
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
    dcBlocker.setType(juce::dsp::FirstOrderTPTFilterType::highpass);
    dcBlocker.prepare(spec);
    dcBlocker.setCutoffFrequency(15.0f);

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
    dryDelay.setSize(numChannels, totalLatency);
    dryBlock.setSize(numChannels, static_cast<int>(spec.maximumBlockSize));
    knobValues.resize(spec.maximumBlockSize);
    reset();
    setLatencySamples(totalLatency);
}

void FilthAudioProcessor::releaseResources()
{
    reset();
}

void FilthAudioProcessor::reset()
{
    compressor.reset();
    dcBlocker.reset();
    if (oversampler != nullptr)
        oversampler->reset();

    dryDelay.clear();
    dryWritePos = 0;
    filthSmoothed.setCurrentAndTargetValue(filthParam->load());

    for (auto& ch : stftChannels)
        ch.clear();
}

void FilthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0 || oversampler == nullptr)
        return;

    filthSmoothed.setTargetValue(filthParam->load());
    const auto block = juce::dsp::AudioBlock<float>(buffer).getSubsetChannelBlock(
        0, static_cast<size_t>(totalNumOutputChannels));
    for (size_t offset = 0; offset < block.getNumSamples();)
    {
        const size_t count = std::min(knobValues.size(), block.getNumSamples() - offset);
        processAudio(block.getSubBlock(offset, count));
        offset += count;
    }
}

void FilthAudioProcessor::processAudio(juce::dsp::AudioBlock<float> block) noexcept
{
    const auto numSamples = block.getNumSamples();
    const auto channels = block.getNumChannels();
    const auto compression = filth::fadeIn(filthSmoothed.getCurrentValue(), 0.0f, 0.70f);

    // Keep the dry signal aligned to the oversampler and STFT, even during automation.
    for (size_t s = 0; s < numSamples; ++s)
    {
        knobValues[s] = filthSmoothed.getNextValue();
        for (size_t ch = 0; ch < channels; ++ch)
        {
            auto* delay = dryDelay.getWritePointer(static_cast<int>(ch));
            dryBlock.getWritePointer(static_cast<int>(ch))[s] = delay[dryWritePos];
            delay[dryWritePos] = block.getChannelPointer(ch)[s];
        }
        if (++dryWritePos == dryDelay.getNumSamples())
            dryWritePos = 0;
    }

    compressor.setThreshold(-30.0f * compression);
    compressor.setRatio(1.0f + 15.0f * compression);
    compressor.process(juce::dsp::ProcessContextReplacing<float>(block));

    // Each stage blends into the output of the previous stage at 4x the sample rate.
    auto upsampledBlock = oversampler->processSamplesUp(block);
    const size_t oversamplingFactor = oversampler->getOversamplingFactor();
    for (size_t s = 0; s < numSamples; ++s)
    {
        const filth::DriveStages stages(knobValues[s]);
        const float overdriveGain = 1.0f + 10.0f * stages.overdrive;
        const float clippingGain = 1.0f + 3.5f * stages.clipping;
        const float foldingGain = 1.0f + 4.0f * stages.folding;

        for (size_t ch = 0; ch < channels; ++ch)
        {
            float* samples = upsampledBlock.getChannelPointer(ch) + s * oversamplingFactor;
            for (size_t os = 0; os < oversamplingFactor; ++os)
            {
                float x = samples[os];
                if (stages.overdrive > 0.0f)
                    x = std::lerp(x, std::tanh(x * overdriveGain), stages.overdrive);
                if (stages.clipping > 0.0f)
                    x = std::lerp(x, std::clamp(x * clippingGain, -0.57f, 0.87f), stages.clipping);
                if (stages.folding > 0.0f)
                {
                    const float driven = x * foldingGain;
                    const float wrapped = driven - 4.0f * std::floor((driven + 1.0f) * 0.25f);
                    x = std::lerp(x, 1.0f - std::abs(wrapped - 1.0f), stages.folding);
                }
                samples[os] = x;
            }
        }
    }
    oversampler->processSamplesDown(block);
    dcBlocker.process(juce::dsp::ProcessContextReplacing<float>(block));

    for (size_t ch = 0; ch < channels; ++ch)
    {
        float* data = block.getChannelPointer(ch);
        const float* dry = dryBlock.getReadPointer(static_cast<int>(ch));
        for (size_t s = 0; s < numSamples; ++s)
        {
            const float amount = knobValues[s];
            const float midSweepTrim = filth::fadeIn(amount, 0.0f, 0.50f)
                                     * (1.0f - filth::fadeIn(amount, 0.50f, 0.80f));
            float wet = processStftSample(stftChannels[ch], data[s], amount) * (1.0f - 0.35f * midSweepTrim);
            const float peak = std::abs(wet);
            if (peak > 0.9f)
                wet = std::copysign(0.9f + 0.1f * std::tanh((peak - 0.9f) * 10.0f), wet);

            const float mix = filth::fadeIn(amount, 0.0f, 0.18f);
            data[s] = std::lerp(dry[s], wet, mix);
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

        const float codec = filth::fadeIn(filth, 0.18f, 1.0f);
        if (codec > 0.0f)
        {
            const int cutoff = applyMp3SpectralArtifacts(codec);
            const float corruption = std::sqrt(filth::fadeIn(filth, 0.50f, 1.0f));
            if (corruption > 0.0f)
                corruptSpectrum(corruption, cutoff, state.frame);
        }
        ++state.frame;

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

int FilthAudioProcessor::applyMp3SpectralArtifacts(float intensity) noexcept
{
    const float mp3       = std::pow(intensity, 1.25f);
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
    return cutoffBin;
}

void FilthAudioProcessor::corruptSpectrum(float amount, int cutoffBin, uint32_t frame) noexcept
{
    // Linked frame and bin patterns keep correlated stereo inputs correlated.
    uint32_t pattern = (frame + 1u) * 0x9e3779b9u;
    const int band = std::min(kHopSize - 1, cutoffBin + std::max(24, cutoffBin / 2));
    const int shift = 7 + static_cast<int>((pattern >> 24) % 19u);
    const float dropout = (pattern >> 28) < 3u ? 0.24f : 1.0f;
    corruptedSpectrum.fill(0.0f);

    for (int bin = 1; bin <= std::min(cutoffBin, kHopSize - 1); ++bin)
    {
        pattern ^= pattern << 13;
        pattern ^= pattern >> 17;
        pattern ^= pattern << 5;
        float re = fftWorkBuffer[static_cast<size_t>(2 * bin)];
        float im = fftWorkBuffer[static_cast<size_t>(2 * bin + 1)];
        if ((pattern & 1u) != 0u)
            std::swap(re, im);
        if ((pattern & 2u) != 0u)
            re = -re;
        if ((pattern & 4u) != 0u)
            im = -im;

        const int shifted = 1 + (bin - 1 + shift) % band;
        const int scattered = 1 + (shifted - 1 + band / 2
                                    + static_cast<int>((pattern >> 8) % 17u)) % band;
        corruptedSpectrum[static_cast<size_t>(2 * shifted)] += re * dropout;
        corruptedSpectrum[static_cast<size_t>(2 * shifted + 1)] += im * dropout;
        corruptedSpectrum[static_cast<size_t>(2 * scattered)] -= im * 0.55f * dropout;
        corruptedSpectrum[static_cast<size_t>(2 * scattered + 1)] += re * 0.55f * dropout;
    }

    // Compensate for the power lost when crossfading between decorrelated spectra.
    const float intact = 1.0f - amount;
    const float gain = 1.0f / std::hypot(intact, amount);
    for (size_t i = 0; i <= static_cast<size_t>(2 * kHopSize + 1); ++i)
        fftWorkBuffer[i] = gain * std::lerp(fftWorkBuffer[i], corruptedSpectrum[i], amount);
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
