#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace filth
{
enum class Stage
{
    compressor, overdrive, lowPass, masking, quantization, corruption,
    clipper, wavefolder, dcBlocker, trim, ceiling, mix, count
};

using StageMask = std::uint32_t;
constexpr StageMask stageBit(Stage stage) { return 1u << static_cast<unsigned>(stage); }
constexpr StageMask allStages = stageBit(Stage::count) - 1u;
constexpr bool stageEnabled(StageMask mask, Stage stage) { return (mask & stageBit(stage)) != 0u; }

inline constexpr const char* stageNames[] {
    "Compressor", "Soft Overdrive", "MP3 Low-Pass", "MP3 Masking",
    "MP3 Quantization", "Spectral Corruption", "Asymmetric Clipper", "Wavefolder",
    "DC Blocker", "Output Trim", "Soft Ceiling", "Dry/Wet Mix"
};
static_assert(sizeof(stageNames) / sizeof(*stageNames) == static_cast<unsigned>(Stage::count));

inline constexpr int fftSize = 1024;
inline constexpr float attackMs = 3.0f;
inline constexpr float releaseMs = 60.0f;
inline constexpr float clipLow = -0.57f;
inline constexpr float clipHigh = 0.87f;
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
          clipping(fadeIn(amount, 0.22f, 0.67f)),
          folding(fadeIn(amount, 0.42f, 0.86f))
    {
    }

    float overdrive;
    float clipping;
    float folding;

    float overdriveGain() const { return 1.0f + 10.0f * overdrive; }
    float clipGain() const      { return 1.0f + 3.5f * clipping; }
    float foldGain() const      { return 1.0f + 4.0f * folding; }
};

struct Compression
{
    explicit Compression(float amount) : intensity(fadeIn(amount, 0.0f, 0.70f)) {}
    float threshold() const { return -30.0f * intensity; }
    float ratio() const     { return 1.0f + 15.0f * intensity; }
    float intensity;
};

struct SpectralSettings
{
    explicit SpectralSettings(float amount)
        : strength(std::pow(fadeIn(amount, 0.18f, 1.0f), 1.25f)),
          corruption(std::sqrt(fadeIn(amount, 0.50f, 1.0f)))
    {}

    int cutoffBin(float sampleRate) const
    {
        const float nyquist = sampleRate * 0.5f;
        const float minimum = std::min(3600.0f, nyquist * 0.8f);
        const float cutoff = nyquist * std::pow(minimum / nyquist, strength);
        return std::clamp(static_cast<int>((cutoff / nyquist) * (fftSize / 2)), 1, fftSize / 2);
    }

    float bandGate() const { return 0.20f * strength; }
    float binGate() const  { return 0.62f * strength; }
    float quantStep() const { return 0.48f * strength * strength; }

    float strength;
    float corruption;
};

inline std::uint32_t framePattern(std::uint32_t frame) { return (frame + 1u) * 0x9e3779b9u; }
inline int binShift(std::uint32_t pattern) { return 7 + static_cast<int>((pattern >> 24) % 19u); }
inline float dropoutGain(std::uint32_t pattern) { return (pattern >> 28) < 3u ? 0.24f : 1.0f; }
inline float outputGain(float amount)
{
    const float trim = fadeIn(amount, 0.0f, 0.50f) * (1.0f - fadeIn(amount, 0.50f, 0.80f));
    return 1.0f - 0.35f * trim;
}
inline float wetMix(float amount) { return fadeIn(amount, 0.0f, 0.18f); }
}
