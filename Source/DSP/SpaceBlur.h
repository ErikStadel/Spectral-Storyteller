#pragma once
#include "ObjectDatabase.h"

namespace space_blur
{

struct Settings
{
    float shape = 0.5f; // <0.5 = Plate, >0.5 = Open Digital Hall
    float decay = 0.55f;
    float blur = 0.5f; // Diffusion amount
    float mix = 0.3f;
};

struct State
{
    // Input Diffusion (4 APFs in series)
    std::array<std::vector<float>, 4> inAPFLines;
    std::array<int, 4> inAPFWrite{};

    // 4x4 FDN Tank
    std::array<std::vector<float>, 4> tankLines;
    std::array<int, 4> tankWrite{};
    std::array<float, 4> tankDamp{};

    // LFO phases for modulation
    std::array<float, 4> lfoPhases{};

    bool initialized = false;
    double cachedSampleRate = 0.0;
};

void ensureState(State& state, double sampleRate);

void processBlock(const Settings& settings,
                  State& state,
                  double sampleRate,
                  int channel,
                  float* buffer,
                  int numSamples);

} // namespace space_blur