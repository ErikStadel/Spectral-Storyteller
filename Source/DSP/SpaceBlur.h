#pragma once

#include "ObjectDatabase.h"

namespace space_blur
{
constexpr int maxFrames = 256;
constexpr int coeffCount = ObjectDatabase::NUM_BINS * 2;

struct Settings
{
    float size = 0.5f;
    float decay = 0.55f;
    float blur = 0.5f;
    float mix = 0.3f;
};

struct State
{
    std::array<std::vector<float>, 4> apfBuffers;
    std::array<int, 4> apfWritePos{};
    std::vector<float> tankL;
    std::vector<float> tankR;
    int tankWritePosL = 0;
    int tankWritePosR = 0;
    std::array<float, coeffCount> dampL{};
    std::array<float, coeffCount> dampR{};
    bool initialized = false;
};

void ensureState(State& state);
void beginFrame(State& state);
void processBin(State& state,
                int channel,
                int bin,
                int hopSize,
                double sampleRate,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm);
void endFrame(State& state);
}
