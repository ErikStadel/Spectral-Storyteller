#include "ShadeContour.h"
#include <cmath>

namespace shade_contour
{
float computeBinGain(int bin, double sampleRate, int fftSize, const Settings& settings)
{
    const float freq = static_cast<float>(bin) * static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const float lowHz = juce::jmax(20.0f, settings.lowCutHz);
    const float highHz = juce::jmax(lowHz + 40.0f, settings.highCutHz);

    const float lowRatio = juce::jmax(1.0e-3f, freq / lowHz);
    const float highRatio = juce::jmax(1.0e-3f, freq / highHz);
    const float highPass = 1.0f / std::sqrt(1.0f + std::pow(1.0f / lowRatio, 4.0f));
    const float lowPass = 1.0f / std::sqrt(1.0f + std::pow(highRatio, 4.0f));
    return juce::jlimit(0.0f, 1.0f, highPass * lowPass);
}
}
