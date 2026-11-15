#pragma once

#include <algorithm>

namespace filth
{
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
};
}
