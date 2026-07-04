#pragma once

#include "ObjectDatabase.h"

namespace echo_bleed
{
constexpr int maxDelayFrames = 256;

struct Settings
{
    float timeSeconds = 0.18f;
    float feedback = 0.30f;
    float bleed = 0.30f;
    float mix = 0.30f;
};

struct State
{
    std::vector<std::array<float, ObjectDatabase::NUM_BINS * 2>> historyFrames;
    std::array<float, ObjectDatabase::NUM_BINS * 2> lowpassState{};
    std::array<float, ObjectDatabase::NUM_BINS * 2> highpassState{};
    std::array<float, ObjectDatabase::NUM_BINS * 2> highpassInput{};
    int writeIndex = 0;
};

void ensureState(State& state);
void beginFrame(State& state);
void processBin(State& state,
                int bin,
                int nyquistBin,
                float hopSeconds,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm);
void endFrame(State& state);
}
