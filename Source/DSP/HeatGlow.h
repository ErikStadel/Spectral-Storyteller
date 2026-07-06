#pragma once

#include "ObjectDatabase.h"

namespace heat_glow
{
struct Settings
{
    float drive = 0.0f;
    float glow  = 0.5f;   // 0 = warm/round, 1 = harsh/bright
    float heat  = 0.0f;   // 0 = digital, 1 = tube
    float mix   = 0.5f;
};

// Per-object time-domain state. One instance per object per channel.
struct State
{
    float glowZ1 = 0.0f;  // glow post-emphasis 1-pole filter memory
    float dcX1   = 0.0f;  // DC blocker: input delay
    float dcY1   = 0.0f;  // DC blocker: output delay
};

// Time-domain saturation on a per-object audio frame produced by the per-object
// ISTFT. Heat selects digital/tube character, Drive sets intensity, Glow shapes
// the post-saturation tone, Mix blends wet/dry.
void processBlock(const Settings& settings,
                  State& state,
                  float* buffer,
                  int numSamples);
}
