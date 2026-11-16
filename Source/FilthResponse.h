#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace filth
{
enum class Stage
{
    compressor, overdrive, lowPass, masking, quantization, mp3Pass2, mp3Pass3,
    wavefolder1, wavefolder2, dcBlocker, trim, ceiling, mix, count
};

using StageMask = std::uint32_t;
constexpr StageMask stageBit(Stage stage) { return 1u << static_cast<unsigned>(stage); }
constexpr StageMask allStages = stageBit(Stage::count) - 1u;
constexpr bool stageEnabled(StageMask mask, Stage stage) { return (mask & stageBit(stage)) != 0u; }
constexpr StageMask mp3Components = stageBit(Stage::lowPass) | stageBit(Stage::masking) | stageBit(Stage::quantization);

inline constexpr const char* stageNames[] {
    "Compressor", "Soft Overdrive", "MP3 Low-Pass", "MP3 Masking",
    "MP3 Quantization", "MP3 Pass 2", "MP3 Pass 3", "Wavefolder 1", "Wavefolder 2",
    "DC Blocker", "Output Trim", "Soft Ceiling", "Dry/Wet Mix"
};
static_assert(sizeof(stageNames) / sizeof(*stageNames) == static_cast<unsigned>(Stage::count));

inline constexpr int fftSize = 1024;
inline constexpr float attackMs = 3.0f;
inline constexpr float releaseMs = 60.0f;
inline constexpr float dcCutoffHz = 15.0f;
inline constexpr float ceilingKnee = 0.9f;

inline float fadeIn(float amount, float start, float end) noexcept
{
    const float t = std::clamp((amount - start) / (end - start), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct DriveStages
{
    explicit DriveStages(float amount) noexcept
        : overdrive(fadeIn(amount, 0.0f, 0.50f)),
          fold1(fadeIn(amount, 0.22f, 0.67f)),
          fold2(fadeIn(amount, 0.42f, 0.86f))
    {
    }

    float overdrive;
    float fold1;
    float fold2;

    float overdriveGain() const { return 1.0f + 10.0f * overdrive; }
    float fold1Gain() const     { return 1.0f + 2.75f * fold1; }
    float fold2Gain() const     { return 1.0f + 4.0f * fold2; }
};

struct Compression
{
    explicit Compression(float amount) : intensity(fadeIn(amount, 0.0f, 0.70f)) {}
    float threshold() const { return -30.0f * intensity; }
    float ratio() const     { return 1.0f + 15.0f * intensity; }
    float intensity;
};

struct CodecPass
{
    float start;
    float minimumCutoff;
    float maxBandGate;
    float maxBinGate;
    float maxQuantStep;
};

inline constexpr std::array codecPasses {
    CodecPass { 0.18f, 3600.0f, 0.20f, 0.62f, 0.48f },
    CodecPass { 0.50f, 2900.0f, 0.25f, 0.69f, 0.60f },
    CodecPass { 0.75f, 2300.0f, 0.30f, 0.76f, 0.72f }
};

struct SpectralSettings
{
    SpectralSettings(float amount, const CodecPass& pass)
        : strength(std::pow(fadeIn(amount, pass.start, 1.0f), 1.25f)), profile(pass)
    {}

    int cutoffBin(float sampleRate) const
    {
        const float nyquist = sampleRate * 0.5f;
        const float minimum = std::min(profile.minimumCutoff, nyquist * 0.8f);
        const float cutoff = nyquist * std::pow(minimum / nyquist, strength);
        return std::clamp(static_cast<int>((cutoff / nyquist) * (fftSize / 2)), 1, fftSize / 2);
    }

    float bandGate() const { return profile.maxBandGate * strength; }
    float binGate() const  { return profile.maxBinGate * strength; }
    float quantStep() const { return profile.maxQuantStep * strength * strength; }

    float strength;
    const CodecPass& profile;
};

inline float outputGain(float amount)
{
    const float trim = fadeIn(amount, 0.0f, 0.50f) * (1.0f - fadeIn(amount, 0.50f, 0.80f));
    return 1.0f - 0.35f * trim;
}
inline float wetMix(float amount) { return fadeIn(amount, 0.0f, 0.18f); }
}
