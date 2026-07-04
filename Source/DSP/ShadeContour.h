#pragma once

#include <juce_core/juce_core.h>

namespace shade_contour
{
struct Settings
{
    float lowCutHz = 20.0f;
    float highCutHz = 20000.0f;
};

float computeBinGain(int bin, double sampleRate, int fftSize, const Settings& settings);
}
