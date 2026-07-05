#pragma once

#include "ObjectDatabase.h"

namespace grit_edge
{
struct Settings
{
    float grit = 0.0f;
    float edge = 0.5f;
    float asymmetry = 0.5f;
    float mix = 0.5f;
};

void processBin(int bin,
                double sampleRate,
                int fftSize,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm);
}
