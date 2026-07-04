#pragma once

#include "ObjectDatabase.h"

namespace heat_glow
{
struct Settings
{
    float drive = 0.0f;
    float glow = 0.5f;
    float heat = 0.0f;
    float mix = 0.5f;
};

void processBin(int bin,
                const Settings& settings,
                float inRe,
                float inIm,
                float& outRe,
                float& outIm);
}
