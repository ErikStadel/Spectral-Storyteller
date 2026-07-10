#include "FluidSpectra.h"
#include <cmath>
#include <cstdint>

namespace fluid_spectra
{
namespace
{
inline float clamp01(float x)
{
    return juce::jlimit(0.0f, 1.0f, x);
}

inline float smoothstep(float t)
{
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

inline std::uint32_t hash2D(int x, int y)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(y) * 668265263u
                    + 0x9e3779b9u;
    h ^= (h >> 13);
    h *= 1274126177u;
    h ^= (h >> 16);
    return h;
}

inline float noise2D(float x, float y)
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float xf = x - std::floor(x);
    const float yf = y - std::floor(y);

    const float u = smoothstep(xf);
    const float v = smoothstep(yf);

    auto h = [](int ix, int iy)
    {
        return static_cast<float>(hash2D(ix, iy) & 0x7fffffffu)
             / static_cast<float>(0x7fffffffu);
    };

    const float n00 = h(xi, yi);
    const float n10 = h(xi + 1, yi);
    const float n01 = h(xi, yi + 1);
    const float n11 = h(xi + 1, yi + 1);

    const float x1 = n00 + u * (n10 - n00);
    const float x2 = n01 + u * (n11 - n01);
    return (x1 + v * (x2 - x1)) * 2.0f - 1.0f;
}
}

void processBin(int bin,
                int nyquistBin,
                int channel,
                const Settings& settings,
                State& state,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm)
{
    const float drift = clamp01(settings.drift);
    const float bloom = clamp01(settings.bloom);
    const float flow = clamp01(settings.flow);
    const float mix = clamp01(settings.mix);

    if (mix <= 1.0e-5f)
    {
        outRe = inRe;
        outIm = inIm;
        return;
    }

    if (!state.initialized)
    {
        state.initialized = true;
        state.xPosition = 0.0f;
    }

    // Advance once per STFT frame and channel (bin 0 arrives first in our loop).
    if (bin == 0)
    {
        const float speed = 0.00035f
                          + 0.00220f * drift
                          + 0.00080f * bloom
                          + 0.00050f * flow;
        state.xPosition += speed;
    }

    const float denom = static_cast<float>(juce::jmax(1, nyquistBin));
    const float binNorm = static_cast<float>(bin) / denom;
    const float highBandWeight = std::pow(binNorm, 0.75f);
    const float flowBand = flow * smoothstep(binNorm * 3.2f);

    const float yCoord = 2.0f * std::sqrt(binNorm) + 10.0f * binNorm;
    const float stereoSign = (channel == 0) ? -1.0f : 1.0f;
    const float xOffset = stereoSign * flowBand * (0.35f + 0.55f * binNorm);
    const float xCoord = state.xPosition + xOffset;

    const float nDrift = noise2D(xCoord * 0.67f, yCoord * 2.9f + 1.3f);
    const float nBloom = noise2D(xCoord * 1.19f + 7.1f, yCoord * 4.4f + 2.2f);
    const float nFlow = noise2D(xCoord * 0.83f - 5.4f, yCoord * 2.2f + 11.5f);

    const float inMag = std::sqrt(inRe * inRe + inIm * inIm);
    const float inPhase = std::atan2(inIm, inRe);

    // Drift: smooth breathing gain contour from the 2D field.
    const float driftDepth = 0.52f * drift;
    const float driftGain = std::pow(2.0f, driftDepth * nDrift * (0.60f + 0.40f * highBandWeight));

    // Bloom: tiny phase scattering that increases with frequency.
    const float bloomPhase = nBloom * bloom * (0.05f + 0.33f * highBandWeight);

    // Stereo Flow: opposite micro-delay and opposing spectral tilt.
    const float delaySamples = stereoSign * flowBand * nFlow * (0.08f + 0.42f * binNorm);
    const float fftSize = 2.0f * denom;
    const float linearPhase = -juce::MathConstants<float>::twoPi
                            * static_cast<float>(bin)
                            * delaySamples
                            / juce::jmax(1.0f, fftSize);

    const float tiltAmount = stereoSign * flowBand * nFlow * 0.42f;
    const float tiltGain = std::pow(2.0f, tiltAmount * (binNorm - 0.5f));

    const float wetMag = inMag * driftGain * tiltGain;
    const float wetPhase = inPhase + bloomPhase + linearPhase;
    const float wetRe = wetMag * std::cos(wetPhase);
    const float wetIm = wetMag * std::sin(wetPhase);

    outRe = inRe * (1.0f - mix) + wetRe * mix;
    outIm = inIm * (1.0f - mix) + wetIm * mix;
}
}
