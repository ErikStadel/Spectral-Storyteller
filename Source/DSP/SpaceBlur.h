#pragma once

#include "ObjectDatabase.h"

namespace space_blur
{
struct Settings
{
    float size  = 0.5f;
    float decay = 0.55f;
    float blur  = 0.5f;  // 0=spring/metallic, 1=hall/diffuse
    float mix   = 0.3f;
};

// Per-object, per-channel time-domain reverb state.
// 4 parallel feedback comb filters  (spring→hall via delay times + damping)
// 2 series input  APF diffusers      (sparse at low Blur, dense at high Blur)
// 2 series output APF diffusers      (idem)
// LFO modulation on combs 0+2        (spring “boing” at low Blur)
struct State
{
    // Pre-delay
    std::vector<float> preDelayLine;
    int   preWrite      = 0;

    // 4 parallel feedback comb filters with LP damping in the loop
    std::array<std::vector<float>, 4> combLines;
    std::array<int,   4> combWrite{};
    std::array<float, 4> combDamp{};   // per-comb LP filter state

    // 2 input diffusion APFs  (Schroeder all-pass, run in series before combs)
    std::array<std::vector<float>, 2> inAPFLines;
    std::array<int, 2> inAPFWrite{};

    // 2 output diffusion APFs (run in series after comb sum)
    std::array<std::vector<float>, 2> outAPFLines;
    std::array<int, 2> outAPFWrite{};

    float  lfoPhase       = 0.0f;
    bool   initialized    = false;
    double cachedSampleRate = 0.0;
};

// Allocate / resize buffers. No-op if already correctly initialised.
void ensureState(State& state, double sampleRate);

// Process one per-object ISTFT synthesis frame (time-domain, in/out).
//   channel — 0 or 1; used to offset delay times for stereo width.
void processBlock(const Settings& settings,
                  State&          state,
                  double          sampleRate,
                  int             channel,
                  float*          buffer,
                  int             numSamples);
}
