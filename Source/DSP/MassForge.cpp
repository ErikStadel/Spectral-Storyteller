#include "MassForge.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace mass_forge
{
FrameParams computeFrameParams(const Settings& settings,
                               State& state,
                               const std::array<bool, ObjectDatabase::NUM_BINS>& activeMask,
                               const std::array<float, ObjectDatabase::NUM_BINS>& magnitudes,
                               double sampleRate,
                               int hopSize)
{
    FrameParams params;
    params.mix = juce::jlimit(0.0f, 1.0f, settings.mix);
    params.forge = juce::jlimit(0.0f, 1.0f, settings.forge);

    float objectEnergy = 0.0f;
    int objectWeight = 0;
    float objectPeak = 0.0f;
    for (int bin = 0; bin < ObjectDatabase::NUM_BINS; ++bin)
    {
        if (!activeMask[static_cast<size_t>(bin)])
            continue;

        const float mag = magnitudes[static_cast<size_t>(bin)];
        objectEnergy += mag * mag;
        objectPeak = juce::jmax(objectPeak, mag);
        ++objectWeight;
    }

    const float objectRms = objectWeight > 0
                                ? std::sqrt(objectEnergy / static_cast<float>(objectWeight))
                                : 0.0f;

    // Crest-normalized detector keeps 0 dB threshold near "mostly no compression" in practice.
    const float detectorLin = objectRms / juce::jmax(1.0e-6f, objectPeak);
    const float inputDb = juce::Decibels::gainToDecibels(detectorLin, -120.0f);

    const float r = juce::jlimit(0.0f, 1.0f, settings.response);
    const float tauAttMs = 0.1f * std::pow(150.0f / 0.1f, r);
    const float tauRelMs = 5.0f * std::pow(2000.0f / 5.0f, r);

    const float fs = static_cast<float>(juce::jmax(1.0, sampleRate));
    const float alphaAttSample = 1.0f - std::exp(-1.0f / juce::jmax(1.0e-6f, fs * (tauAttMs * 0.001f)));
    const float alphaRelSample = 1.0f - std::exp(-1.0f / juce::jmax(1.0e-6f, fs * (tauRelMs * 0.001f)));

    const int hop = juce::jmax(1, hopSize);
    const float alphaAtt = 1.0f - std::pow(1.0f - alphaAttSample, static_cast<float>(hop));
    const float alphaRel = 1.0f - std::pow(1.0f - alphaRelSample, static_cast<float>(hop));

    const float alpha = (inputDb > state.envDb) ? alphaAtt : alphaRel;
    state.envDb += alpha * (inputDb - state.envDb);

    const float ratio = 1.0f + 49.0f * params.forge * params.forge;
    const float overshoot = state.envDb - settings.thresholdDb;
    const float gainDb = (overshoot > 0.0f) ? overshoot * ((1.0f / ratio) - 1.0f) : 0.0f;
    const float driveDb = params.forge * params.forge * params.forge * 24.0f;
    const float makeupDbRaw = ((0.72f * (-settings.thresholdDb)) * (1.0f - (1.0f / ratio)))
                            - (driveDb * 0.22f)
                            + (params.forge * 2.5f);
    const float makeupDb = juce::jlimit(-18.0f, 12.0f, makeupDbRaw);

    params.gainLinear = juce::Decibels::decibelsToGain(gainDb);
    params.driveLinear = juce::Decibels::decibelsToGain(driveDb);
    params.makeupLinear = juce::Decibels::decibelsToGain(makeupDb);
    return params;
}

float processSample(float inSample, const FrameParams& params)
{
    // 1. Kompression
    const float compressed = inSample * params.gainLinear;
    
    // 2. Makeup-Gain und Drive werden VOR der Sättigung angewendet!
    const float driven = compressed * params.driveLinear * params.makeupLinear;
    
    // 3. Sättigung (Tanh fungiert nun als finaler Limiter)
    const float offset = 0.1f * params.forge;
    const float saturated = std::tanh(driven + offset) - std::tanh(offset);
    
    // 'wet' ist jetzt direkt 'saturated', da das Makeup-Gain schon im Tanh gelandet ist.
    const float wet = saturated; 

    return (1.0f - params.mix) * compressed + params.mix * wet;
}
}
