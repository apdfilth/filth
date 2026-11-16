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
    debugSampleRate.store(static_cast<float>(sampleRate), std::memory_order_relaxed);
    const int numChannels = std::max(1, getTotalNumOutputChannels());

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(std::max(1, samplesPerBlock));
    spec.numChannels      = static_cast<juce::uint32>(numChannels);

    compressor.prepare(spec);
    compressor.setAttack(filth::attackMs);
    compressor.setRelease(filth::releaseMs);
    dcBlocker.setType(juce::dsp::FirstOrderTPTFilterType::highpass);
    dcBlocker.prepare(spec);
    dcBlocker.setCutoffFrequency(filth::dcCutoffHz);

    // Oversample each side of the host-rate STFT with integer latency for delay compensation.
    const auto makeOversampler = [&spec]
    {
        auto oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
            spec.numChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing(spec.maximumBlockSize);
        return oversampler;
    };
    preCodecOversampler = makeOversampler();
    postCodecOversampler = makeOversampler();

    stftChannels.assign(static_cast<size_t>(numChannels), CodecChain {});
    filthSmoothed.reset(sampleRate, 0.02);
    filthSmoothed.setCurrentAndTargetValue(filthParam->load());

    const int totalLatency = juce::roundToInt(preCodecOversampler->getLatencyInSamples()
                                             + postCodecOversampler->getLatencyInSamples())
                             + kFftSize * static_cast<int>(filth::codecPasses.size());
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
    if (preCodecOversampler != nullptr)
        preCodecOversampler->reset();
    if (postCodecOversampler != nullptr)
        postCodecOversampler->reset();

    dryDelay.clear();
    dryWritePos = 0;
    filthSmoothed.setCurrentAndTargetValue(filthParam->load());
    debugAmount.store(filthSmoothed.getCurrentValue(), std::memory_order_relaxed);
    debugCompressorAmount.store(filthSmoothed.getCurrentValue(), std::memory_order_relaxed);
    for (auto& amount : debugSpectralAmounts)
        amount.store(filthSmoothed.getCurrentValue(), std::memory_order_relaxed);

    for (auto& chain : stftChannels)
        for (auto& pass : chain)
        {
            pass.clear();
            pass.frameAmount = filthSmoothed.getCurrentValue();
        }
}

FilthAudioProcessor::DebugValues FilthAudioProcessor::getDebugValues() const noexcept
{
    DebugValues values {
        filthParam->load(std::memory_order_relaxed),
        debugAmount.load(std::memory_order_relaxed),
        debugCompressorAmount.load(std::memory_order_relaxed),
        {},
        debugSampleRate.load(std::memory_order_relaxed)
    };
    for (size_t i = 0; i < values.spectralAmounts.size(); ++i)
        values.spectralAmounts[i] = debugSpectralAmounts[i].load(std::memory_order_relaxed);
    return values;
}

double FilthAudioProcessor::getTailLengthSeconds() const
{
    return 0.05 + static_cast<double>(filth::codecPasses.size() * kFftSize)
                  / static_cast<double>(debugSampleRate.load(std::memory_order_relaxed));
}

void FilthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0 || postCodecOversampler == nullptr)
        return;

    filthSmoothed.setTargetValue(filthParam->load());
    const auto mask = debugStages.load(std::memory_order_relaxed);
    const auto block = juce::dsp::AudioBlock<float>(buffer).getSubsetChannelBlock(
        0, static_cast<size_t>(totalNumOutputChannels));
    for (size_t offset = 0; offset < block.getNumSamples();)
    {
        const size_t count = std::min(knobValues.size(), block.getNumSamples() - offset);
        processAudio(block.getSubBlock(offset, count), mask);
        offset += count;
    }
}

void FilthAudioProcessor::processAudio(juce::dsp::AudioBlock<float> block, filth::StageMask mask) noexcept
{
    const auto numSamples = block.getNumSamples();
    const auto channels = block.getNumChannels();
    const auto compressorAmount = filthSmoothed.getCurrentValue();
    const filth::Compression compression(compressorAmount);

    // Keep the dry signal aligned to both oversamplers and the codec chain during automation.
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

    if (stageEnabled(mask, filth::Stage::compressor))
    {
        compressor.setThreshold(compression.threshold());
        compressor.setRatio(compression.ratio());
        compressor.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    applyPreCodecDrive(block, stageEnabled(mask, filth::Stage::overdrive));
    const std::array<filth::StageMask, filth::codecPasses.size()> codecMasks {
        mask & filth::mp3Components,
        stageEnabled(mask, filth::Stage::mp3Pass2) ? filth::mp3Components : 0u,
        stageEnabled(mask, filth::Stage::mp3Pass3) ? filth::mp3Components : 0u
    };
    for (size_t ch = 0; ch < channels; ++ch)
    {
        auto* data = block.getChannelPointer(ch);
        auto& chain = stftChannels[ch];
        for (size_t i = 0; i < chain.size(); ++i)
            for (size_t s = 0; s < numSamples; ++s)
                data[s] = processStftSample(chain[i], data[s], knobValues[s], filth::codecPasses[i], codecMasks[i]);
    }
    applyPostCodecDrive(block, mask);
    if (stageEnabled(mask, filth::Stage::dcBlocker))
        dcBlocker.process(juce::dsp::ProcessContextReplacing<float>(block));

    const bool trim = stageEnabled(mask, filth::Stage::trim);
    const bool ceiling = stageEnabled(mask, filth::Stage::ceiling);
    const bool mix = stageEnabled(mask, filth::Stage::mix);

    for (size_t ch = 0; ch < channels; ++ch)
    {
        float* data = block.getChannelPointer(ch);
        const float* dry = dryBlock.getReadPointer(static_cast<int>(ch));
        for (size_t s = 0; s < numSamples; ++s)
        {
            const float amount = knobValues[s];
            float wet = trim ? data[s] * filth::outputGain(amount) : data[s];
            const float peak = std::abs(wet);
            if (ceiling && peak > filth::ceilingKnee)
                wet = std::copysign(filth::ceilingKnee + 0.1f * std::tanh((peak - filth::ceilingKnee) * 10.0f), wet);

            data[s] = mask == 0 ? dry[s] : std::lerp(dry[s], wet, mix ? filth::wetMix(amount) : 1.0f);
        }
    }

    const auto& firstChannel = stftChannels.front();
    debugAmount.store(knobValues[numSamples - 1], std::memory_order_relaxed);
    debugCompressorAmount.store(compressorAmount, std::memory_order_relaxed);
    for (size_t i = 0; i < firstChannel.size(); ++i)
        debugSpectralAmounts[i].store(firstChannel[i].frameAmount, std::memory_order_relaxed);
}

void FilthAudioProcessor::applyPreCodecDrive(juce::dsp::AudioBlock<float> block, bool enabled) noexcept
{
    auto upsampled = preCodecOversampler->processSamplesUp(block);
    const auto factor = preCodecOversampler->getOversamplingFactor();
    for (size_t s = 0; enabled && s < block.getNumSamples(); ++s)
    {
        const filth::DriveStages stages(knobValues[s]);
        if (stages.overdrive == 0.0f)
            continue;

        const float gain = stages.overdriveGain();
        for (size_t ch = 0; ch < upsampled.getNumChannels(); ++ch)
        {
            auto* samples = upsampled.getChannelPointer(ch) + s * factor;
            for (size_t os = 0; os < factor; ++os)
                samples[os] = std::lerp(samples[os], std::tanh(samples[os] * gain), stages.overdrive);
        }
    }
    preCodecOversampler->processSamplesDown(block);
}

void FilthAudioProcessor::applyPostCodecDrive(juce::dsp::AudioBlock<float> block, filth::StageMask mask) noexcept
{
    auto upsampled = postCodecOversampler->processSamplesUp(block);
    const auto factor = postCodecOversampler->getOversamplingFactor();
    const bool firstEnabled = stageEnabled(mask, filth::Stage::wavefolder1);
    const bool secondEnabled = stageEnabled(mask, filth::Stage::wavefolder2);
    const auto fold = [](float x, float gain, float blend)
    {
        const float driven = x * gain;
        const float wrapped = driven - 4.0f * std::floor((driven + 1.0f) * 0.25f);
        return std::lerp(x, 1.0f - std::abs(wrapped - 1.0f), blend);
    };

    for (size_t s = 0; (firstEnabled || secondEnabled) && s < block.getNumSamples(); ++s)
    {
        const filth::DriveStages stages(knobValues[s]);
        const bool applyFirst = firstEnabled && stages.fold1 > 0.0f;
        const bool applySecond = secondEnabled && stages.fold2 > 0.0f;
        if (!applyFirst && !applySecond)
            continue;

        for (size_t ch = 0; ch < upsampled.getNumChannels(); ++ch)
        {
            auto* samples = upsampled.getChannelPointer(ch) + s * factor;
            for (size_t os = 0; os < factor; ++os)
            {
                float x = samples[os];
                if (applyFirst)
                    x = fold(x, stages.fold1Gain(), stages.fold1);
                if (applySecond)
                    x = fold(x, stages.fold2Gain(), stages.fold2);
                samples[os] = x;
            }
        }
    }
    postCodecOversampler->processSamplesDown(block);
}

float FilthAudioProcessor::processStftSample(ChannelStft& state, float inputSample, float amount,
                                            const filth::CodecPass& pass, filth::StageMask mask) noexcept
{
    const auto idx = static_cast<size_t>(state.pos);
    const float outSample = state.readyOutput[idx];
    state.inputFifo[static_cast<size_t>(kHopSize) + idx] = inputSample;

    if (++state.pos >= kHopSize)
    {
        state.pos = 0;

        const filth::SpectralSettings settings(amount, pass);
        state.frameAmount = amount;
        for (size_t i = 0; i < static_cast<size_t>(kFftSize); ++i)
            fftWorkBuffer[i] = state.inputFifo[i] * sineWindow[i];

        if (mask != 0u && settings.strength > 0.0f)
        {
            std::fill(fftWorkBuffer.begin() + kFftSize, fftWorkBuffer.end(), 0.0f);
            fft.performRealOnlyForwardTransform(fftWorkBuffer.data(), true);
            applyMp3SpectralArtifacts(settings, mask);
            fft.performRealOnlyInverseTransform(fftWorkBuffer.data());
        }

        for (size_t i = 0; i < static_cast<size_t>(kFftSize); ++i)
            state.outputAccum[i] += fftWorkBuffer[i] * sineWindow[i];

        std::copy_n(state.outputAccum.begin(), kHopSize, state.readyOutput.begin());
        std::copy_n(state.outputAccum.begin() + kHopSize, kHopSize, state.outputAccum.begin());
        std::fill_n(state.outputAccum.begin() + kHopSize, kHopSize, 0.0f);

        std::copy_n(state.inputFifo.begin() + kHopSize, kHopSize, state.inputFifo.begin());
    }

    return outSample;
}

void FilthAudioProcessor::applyMp3SpectralArtifacts(const filth::SpectralSettings& settings,
                                                 filth::StageMask mask) noexcept
{
    const int cutoffBin = stageEnabled(mask, filth::Stage::lowPass)
                            ? settings.cutoffBin(static_cast<float>(currentSampleRate)) : kHopSize;

    // Brickwall high-frequency rolloff (emulates low-bitrate MP3 sfb21 cutoff)
    for (int k = cutoffBin + 1; k <= kHopSize; ++k)
    {
        fftWorkBuffer[static_cast<size_t>(2 * k)]     = 0.0f;
        fftWorkBuffer[static_cast<size_t>(2 * k + 1)] = 0.0f;
    }

    const bool masking = stageEnabled(mask, filth::Stage::masking);
    const bool quantize = stageEnabled(mask, filth::Stage::quantization);
    float globalPeak = 0.0f;
    for (int k = 1; masking && k <= cutoffBin; ++k)
    {
        const float re = fftWorkBuffer[static_cast<size_t>(2 * k)];
        const float im = fftWorkBuffer[static_cast<size_t>(2 * k + 1)];
        globalPeak = std::max(globalPeak, std::sqrt(re * re + im * im));
    }

    // Critical-band psychoacoustic masking + coarse spectral quantization ("swirl" & pre-echo)
    for (int bStart = 1; (masking || quantize) && bStart <= cutoffBin;)
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

        if (masking && bandPeak < globalPeak * settings.bandGate())
        {
            for (int k = bStart; k <= bEnd; ++k)
            {
                fftWorkBuffer[static_cast<size_t>(2 * k)]     = 0.0f;
                fftWorkBuffer[static_cast<size_t>(2 * k + 1)] = 0.0f;
            }
        }
        else
        {
            const float binGate   = bandPeak * settings.binGate();
            const float quantStep = bandPeak * settings.quantStep();

            for (int k = bStart; k <= bEnd; ++k)
            {
                float& re = fftWorkBuffer[static_cast<size_t>(2 * k)];
                float& im = fftWorkBuffer[static_cast<size_t>(2 * k + 1)];
                const float mag = std::sqrt(re * re + im * im);

                if (masking && mag < binGate)
                {
                    re = 0.0f;
                    im = 0.0f;
                }
                else if (quantize && quantStep > 1.0e-6f)
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
