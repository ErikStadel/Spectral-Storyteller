#include "EchoBleed.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace echo_bleed
{
void ensureState(State& state)
{
    if (!state.historyFrames.empty())
        return;

    state.historyFrames.resize(maxDelayFrames);
    for (auto& frame : state.historyFrames)
        frame.fill(0.0f);
    state.lowpassState.fill(0.0f);
    state.highpassState.fill(0.0f);
    state.highpassInput.fill(0.0f);
    state.writeIndex = 0;
}

void beginFrame(State& state)
{
    ensureState(state);
    state.historyFrames[static_cast<size_t>(state.writeIndex)].fill(0.0f);
}

void processBin(State& state,
                int bin,
                int nyquistBin,
                float hopSeconds,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm)
{
    const float maxDelaySeconds = juce::jmax(0.02f, hopSeconds * static_cast<float>(maxDelayFrames - 2));
    const float delaySeconds = juce::jlimit(0.01f, maxDelaySeconds, settings.timeSeconds);
    const float delayFramesFloat = juce::jlimit(1.0f,
                                                static_cast<float>(maxDelayFrames - 2),
                                                delaySeconds / juce::jmax(1.0e-4f, hopSeconds));
    float readPos = static_cast<float>(state.writeIndex) - delayFramesFloat;
    while (readPos < 0.0f)
        readPos += static_cast<float>(maxDelayFrames);

    const int readIndex0 = static_cast<int>(std::floor(readPos)) % maxDelayFrames;
    const int readIndex1 = (readIndex0 + 1) % maxDelayFrames;
    const float frac = juce::jlimit(0.0f, 1.0f, readPos - std::floor(readPos));

    const int reIdx = 2 * bin;
    const int imIdx = reIdx + 1;

    const float delayedRe0 = state.historyFrames[static_cast<size_t>(readIndex0)][static_cast<size_t>(reIdx)];
    const float delayedRe1 = state.historyFrames[static_cast<size_t>(readIndex1)][static_cast<size_t>(reIdx)];
    const float delayedIm0 = state.historyFrames[static_cast<size_t>(readIndex0)][static_cast<size_t>(imIdx)];
    const float delayedIm1 = state.historyFrames[static_cast<size_t>(readIndex1)][static_cast<size_t>(imIdx)];
    float delayedRe = delayedRe0 + (delayedRe1 - delayedRe0) * frac;
    float delayedIm = delayedIm0 + (delayedIm1 - delayedIm0) * frac;

    const float bleed = juce::jlimit(0.0f, 1.0f, settings.bleed);
    const float cutoffLP = 20000.0f * std::pow(800.0f / 20000.0f, bleed);
    const float cutoffHP = 20.0f * std::pow(200.0f / 20.0f, bleed);
    const float dt = juce::jmax(1.0e-5f, hopSeconds);
    const float alphaLP = juce::jlimit(0.0f, 1.0f, 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffLP * dt));
    const float rcHP = 1.0f / (2.0f * juce::MathConstants<float>::pi * juce::jmax(1.0f, cutoffHP));
    const float alphaHP = juce::jlimit(0.0f, 1.0f, rcHP / (rcHP + dt));

    auto processLoopSample = [&](float x, int stateIndex)
    {
        float& lp = state.lowpassState[static_cast<size_t>(stateIndex)];
        lp += alphaLP * (x - lp);

        float& hp = state.highpassState[static_cast<size_t>(stateIndex)];
        float& hpInPrev = state.highpassInput[static_cast<size_t>(stateIndex)];
        hp = alphaHP * (hp + lp - hpInPrev);
        hpInPrev = lp;

        const float loopDrive = 1.0f + (bleed * 4.0f);
        const float xSat = std::tanh(hp * loopDrive) / loopDrive;
        return xSat * (1.0f + (bleed * 0.35f));
    };

    const float loopRe = processLoopSample(delayedRe, reIdx);
    const float loopIm = processLoopSample(delayedIm, imIdx);

    const float feedback = juce::jlimit(0.0f, 0.997f, settings.feedback);
    state.historyFrames[static_cast<size_t>(state.writeIndex)][static_cast<size_t>(reIdx)] = inRe + loopRe * feedback;
    state.historyFrames[static_cast<size_t>(state.writeIndex)][static_cast<size_t>(imIdx)] = inIm + loopIm * feedback;

    const float mix = juce::jlimit(0.0f, 1.0f, settings.mix);
    const float dryGain = std::cos(mix * juce::MathConstants<float>::halfPi);
    const float wetGain = std::sin(mix * juce::MathConstants<float>::halfPi) * (1.0f + 0.4f * bleed);
    outRe = (inRe * dryGain) + (loopRe * wetGain);
    outIm = (inIm * dryGain) + (loopIm * wetGain);
}

void endFrame(State& state)
{
    state.writeIndex = (state.writeIndex + 1) % maxDelayFrames;
}
}
