#pragma once

#include "ObjectDatabase.h"

namespace stasis_cloud
{
struct Settings
{
    float freeze = 0.0f;
    float blur = 0.5f;
    float size = 0.5f;
    float mix = 0.5f;
};

struct State
{
    bool initialized = false;
    bool freezeWasActive = false;
    float captureBlend = 0.0f;
    float capturedSize = 0.5f;
    std::array<float, ObjectDatabase::NUM_BINS> frozenMagnitudes{};
    std::array<float, ObjectDatabase::NUM_BINS> smoothedMagnitudes{};
    std::array<float, ObjectDatabase::NUM_BINS> phaseAccumulator{};
    std::array<float, ObjectDatabase::NUM_BINS> phaseVelocity{}; // organic shimmer/detune ONLY - not the bin's base rotation
};

// hopSize (samples between successive spectral frames) is now required. It is what
// lets the phase accumulator rotate in sync with the analysis/synthesis grid; without
// it the freeze cannot stay phase-coherent between frames, which is what caused the
// "machine gun" artifact regardless of Size/Blur.
void captureSnapshot(State& state,
                     const std::array<float, ObjectDatabase::NUM_BINS>& liveMagnitudes,
                     const float* fftData,
                     int fftSize,
                     int hopSize,
                     float size);

void processBin(int bin,
                int fftSize,
                int hopSize,
                double sampleRate,
                const Settings& settings,
                const std::array<float, ObjectDatabase::NUM_BINS>& liveMagnitudes,
                float inRe,
                float inIm,
                State& state,
                float& outRe,
                float& outIm);
}
