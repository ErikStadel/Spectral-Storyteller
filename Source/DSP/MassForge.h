#pragma once

#include "ObjectDatabase.h"

namespace mass_forge
{
struct Settings
{
    float thresholdDb = -18.0f;
    float forge = 0.0f;
    float response = 0.5f;
    float mix = 0.0f;
};

struct State
{
    float envDb = -120.0f;
};

struct FrameParams
{
    float gainLinear = 1.0f;
    float driveLinear = 1.0f;
    float makeupLinear = 1.0f;
    float mix = 0.0f;
    float forge = 0.0f;
};

FrameParams computeFrameParams(const Settings& settings,
                               State& state,
                               const std::array<bool, ObjectDatabase::NUM_BINS>& activeMask,
                               const std::array<float, ObjectDatabase::NUM_BINS>& magnitudes,
                               double sampleRate,
                               int hopSize);

float processSample(float inSample, const FrameParams& params);
}
