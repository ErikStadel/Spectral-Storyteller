#pragma once

#include <juce_core/juce_core.h>

namespace fluid_spectra
{
struct Settings
{
    float drift = 0.3f;
    float bloom = 0.25f;
    float flow = 0.35f;
    float mix = 0.45f;
};

// Per-object, per-channel state for the evolving 2D field traversal.
struct State
{
    float xPosition = 0.0f;
    bool initialized = false;
};

void processBin(int bin,
                int nyquistBin,
                int channel,
                const Settings& settings,
                State& state,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm);
}
