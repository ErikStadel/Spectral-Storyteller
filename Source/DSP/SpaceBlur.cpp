#include "SpaceBlur.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace space_blur
{
namespace
{
inline float readFrac(const std::vector<float>& buffer, int lineFrames, int writePos, float delayFrames, int coeff)
{
    delayFrames = juce::jlimit(0.25f, static_cast<float>(lineFrames - 2), delayFrames);
    float readPos = static_cast<float>(writePos) - delayFrames;
    while (readPos < 0.0f)
        readPos += static_cast<float>(lineFrames);

    const int i0 = static_cast<int>(std::floor(readPos)) % lineFrames;
    const int i1 = (i0 + 1) % lineFrames;
    const float frac = juce::jlimit(0.0f, 1.0f, readPos - std::floor(readPos));

    const float a = buffer[static_cast<size_t>(i0 * coeffCount + coeff)];
    const float b = buffer[static_cast<size_t>(i1 * coeffCount + coeff)];
    return a + (b - a) * frac;
}

inline void writeCurrent(std::vector<float>& buffer, int lineFrames, int writePos, int coeff, float value)
{
    buffer[static_cast<size_t>(writePos * coeffCount + coeff)] = value;
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline float readSpectralFrac(const std::vector<float>& buffer,
                              int lineFrames,
                              int writePos,
                              float delayFrames,
                              int coeff,
                              int radius,
                              float blur)
{
    const auto readCoeff = [&](int c)
    {
        return readFrac(buffer, lineFrames, writePos, delayFrames, c);
    };

    if (radius <= 0 || blur <= 0.001f)
        return readCoeff(coeff);

    const float sigma = 0.85f + 4.0f * blur;
    const float neighbourGain = blur * blur;
    float sum = readCoeff(coeff);
    float weightSum = 1.0f;

    for (int step = 1; step <= radius; ++step)
    {
        const float distance = static_cast<float>(step);
        const float weight = neighbourGain * std::exp(-(distance * distance) / (2.0f * sigma * sigma));
        const int lower = coeff - 2 * step;
        const int upper = coeff + 2 * step;

        if (lower >= 0)
        {
            sum += readCoeff(lower) * weight;
            weightSum += weight;
        }

        if (upper < coeffCount)
        {
            sum += readCoeff(upper) * weight;
            weightSum += weight;
        }
    }

    return sum / weightSum;
}
}

void ensureState(State& state)
{
    if (state.initialized)
        return;

    for (auto& apf : state.apfBuffers)
    {
        apf.assign(static_cast<size_t>(maxFrames * coeffCount), 0.0f);
    }

    state.tankL.assign(static_cast<size_t>(maxFrames * coeffCount), 0.0f);
    state.tankR.assign(static_cast<size_t>(maxFrames * coeffCount), 0.0f);
    state.apfWritePos.fill(0);
    state.tankWritePosL = 0;
    state.tankWritePosR = 0;
    state.dampL.fill(0.0f);
    state.dampR.fill(0.0f);
    state.initialized = true;
}

void beginFrame(State& state)
{
    ensureState(state);

    for (int i = 0; i < 4; ++i)
    {
        auto& apf = state.apfBuffers[static_cast<size_t>(i)];
        const int wp = state.apfWritePos[static_cast<size_t>(i)];
        std::fill_n(apf.begin() + static_cast<size_t>(wp * coeffCount), coeffCount, 0.0f);
    }

    std::fill_n(state.tankL.begin() + static_cast<size_t>(state.tankWritePosL * coeffCount), coeffCount, 0.0f);
    std::fill_n(state.tankR.begin() + static_cast<size_t>(state.tankWritePosR * coeffCount), coeffCount, 0.0f);
}

void processBin(State& state,
                int channel,
                int bin,
                int hopSize,
                double sampleRate,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm)
{
    ensureState(state);

    const float hop = static_cast<float>(juce::jmax(1, hopSize));
    const float frameRate = static_cast<float>(juce::jmax(1.0, sampleRate)) / hop;

    const float size = juce::jlimit(0.0f, 1.0f, settings.size);
    const float roomCurve = size * size;
    const float blur = juce::jlimit(0.0f, 1.0f, settings.blur);

    const float delayLSeconds = lerp(0.018f, 1.18f, roomCurve);
    const float delayRSeconds = lerp(0.027f, 0.91f, roomCurve);

    const float delayLFrames = juce::jlimit(1.0f,
                                            static_cast<float>(maxFrames - 2),
                                            delayLSeconds * frameRate);
    const float delayRFrames = juce::jlimit(1.0f,
                                            static_cast<float>(maxFrames - 2),
                                            delayRSeconds * frameRate);

    const float decay = juce::jlimit(0.0f, 1.0f, settings.decay);
    const float feedbackGain = std::pow(decay, 0.55f) * 0.985f;

    const float mix = juce::jlimit(0.0f, 1.0f, settings.mix);
    const float dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
    const float wetGain = std::sin(mix * juce::MathConstants<float>::halfPi) * (0.95f + 0.55f * decay + 0.25f * size);
    const float binPosition = ObjectDatabase::NUM_BINS > 1
                                ? static_cast<float>(bin) / static_cast<float>(ObjectDatabase::NUM_BINS - 1)
                                : 0.0f;
    const float highDamping = juce::jlimit(0.42f, 1.0f, 1.0f - blur * 0.42f * std::sqrt(binPosition));
    const int spectralRadius = blur < 0.04f ? 0 : juce::jlimit(1, 12, 1 + static_cast<int>(std::round(blur * blur * 11.0f)));
    const float earlyComb = (1.0f - size) * (1.0f - 0.80f * blur);
    const float tankInputGain = 0.70f + 0.55f * blur + 0.25f * size;
    const float diffusionLowpass = juce::jlimit(0.04f, 0.75f, 0.45f - 0.31f * blur + 0.10f * (1.0f - size));

    auto processCoeff = [&](float x, int coeff)
    {
        const float dry = x;
        float early = 0.0f;
        float earlySeed = x;

        for (int i = 0; i < 4; ++i)
        {
            static constexpr std::array<float, 4> earlyFrameSeeds = { 0.75f, 1.25f, 2.10f, 3.40f };
            static constexpr std::array<float, 4> earlyWeights = { 0.48f, -0.34f, 0.26f, -0.18f };
            const float earlyDelayFrames = earlyFrameSeeds[static_cast<size_t>(i)] * (1.0f + 13.0f * roomCurve);
            auto& line = state.apfBuffers[static_cast<size_t>(i)];
            const int wp = state.apfWritePos[static_cast<size_t>(i)];
            const float delayed = readSpectralFrac(line, maxFrames, wp, earlyDelayFrames, coeff, spectralRadius / 2, blur * 0.65f);
            early += delayed * earlyWeights[static_cast<size_t>(i)];
            writeCurrent(line, maxFrames, wp, coeff, earlySeed + delayed * earlyComb * 0.38f);
            earlySeed = earlySeed * 0.58f + delayed * earlyComb;
        }

        const float tankOutL = readSpectralFrac(state.tankL, maxFrames, state.tankWritePosL, delayLFrames, coeff, spectralRadius, blur);
        const float tankOutR = readSpectralFrac(state.tankR, maxFrames, state.tankWritePosR, delayRFrames, coeff, spectralRadius, blur);

        const float tankExcite = (dry + early * lerp(1.15f, 0.45f, size)) * tankInputGain;
        const float inTankL = tankExcite + feedbackGain * highDamping * tankOutR;
        const float inTankR = tankExcite + feedbackGain * highDamping * tankOutL;

        float& dL = state.dampL[static_cast<size_t>(coeff)];
        float& dR = state.dampR[static_cast<size_t>(coeff)];
        dL = diffusionLowpass * inTankL + (1.0f - diffusionLowpass) * dL;
        dR = diffusionLowpass * inTankR + (1.0f - diffusionLowpass) * dR;

        writeCurrent(state.tankL, maxFrames, state.tankWritePosL, coeff, dL);
        writeCurrent(state.tankR, maxFrames, state.tankWritePosR, coeff, dR);

        const float tapA = readSpectralFrac(state.tankL, maxFrames, state.tankWritePosL, delayLFrames * 0.37f + 1.0f, coeff, spectralRadius, blur);
        const float tapB = readSpectralFrac(state.tankR, maxFrames, state.tankWritePosR, delayRFrames * 0.53f + 2.0f, coeff, spectralRadius, blur);
        const float tapC = readSpectralFrac(state.tankL, maxFrames, state.tankWritePosL, delayLFrames * 0.71f + 3.0f, coeff, spectralRadius, blur);
        const float tapD = readSpectralFrac(state.tankR, maxFrames, state.tankWritePosR, delayRFrames * 0.29f + 4.0f, coeff, spectralRadius, blur);

        const float earlyLevel = lerp(0.78f, 0.20f, size) * lerp(1.0f, 0.38f, blur);
        const float tailLevel = lerp(0.55f, 1.12f, size) * lerp(0.82f, 1.18f, blur);
        const float wet = (channel == 0)
                            ? early * earlyLevel + tailLevel * (0.34f * tankOutL + 0.28f * tapA + 0.22f * tapB + 0.16f * tapC)
                            : early * earlyLevel + tailLevel * (0.34f * tankOutR + 0.28f * tapD + 0.22f * tapC + 0.16f * tapB);
        return dry * dryGain + wet * wetGain;
    };

    outRe = processCoeff(inRe, 2 * bin);
    outIm = processCoeff(inIm, 2 * bin + 1);
}

void endFrame(State& state)
{
    ensureState(state);
    for (int i = 0; i < 4; ++i)
        state.apfWritePos[static_cast<size_t>(i)] = (state.apfWritePos[static_cast<size_t>(i)] + 1) % maxFrames;

    state.tankWritePosL = (state.tankWritePosL + 1) % maxFrames;
    state.tankWritePosR = (state.tankWritePosR + 1) % maxFrames;
}
}
