#pragma once
#include "ObjectDatabase.h"

namespace space_blur
{

struct Settings
{
    float size = 0.5f;
    float decay = 0.55f;
    float blur = 0.5f; // 0=spring/metallic, 0.5=plate, 1=hall/diffuse
    float mix = 0.3f;
};

// Per-object, per-channel time-domain reverb state.
struct State
{
    // Pre-delay (wird auch für Early Reflections genutzt)
    std::vector<float> preDelayLine;
    int preWrite = 0;

    // Dispersion Network (3 APFs für den Spring "Drip")
    std::array<std::vector<float>, 3> dispLines;
    std::array<int, 3> dispWrite{};

    // 8 parallel feedback comb filters (Upgrade von 4 auf 8 gegen "Boxiness")
    std::array<std::vector<float>, 8> combLines;
    std::array<int, 8> combWrite{};
    std::array<float, 8> combDamp{}; 

    // 4 output diffusion APFs (für den weichen Plate/Hall Wash)
    std::array<std::vector<float>, 4> outAPFLines;
    std::array<int, 4> outAPFWrite{};

    // 2 LFOs für Modulation (Shimmer & Boing)
    float lfoPhase1 = 0.0f;
    float lfoPhase2 = 0.0f;

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